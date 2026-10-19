#include "duelrecord.h"
#include "records.h"
#include "../common/ehsys.h"
#include <string.h>
#include <stdio.h>

namespace {
/* offsets of the module's own functions that the game calls through guest addresses */
constexpr uint32_t kOffsetTextureSlotCompare = 0x604;
constexpr uint32_t kOffsetDatabaseLoadedCallback = 0x574;
constexpr uint32_t kPoolSize = 0x200000;
constexpr uint32_t kTextTableTexture = 0x1020;   /* key of the string-table entry in the texture slots */
constexpr uint32_t kStyleWithValue = 0x5280, kStylePlain = 0x6600;
constexpr uint32_t kSaveNewFlagsHighScore = 0xa0, kSaveStatusBonus = 0xcc, kSaveStatusExtra = 0x120;
/* PSP pad bits */
constexpr uint32_t kButtonUp = 0x10, kButtonRight = 0x20, kButtonDown = 0x40, kButtonLeft = 0x80, kButtonLTrigger = 0x100, kButtonRTrigger = 0x200;
constexpr uint32_t kButtonCircle = 0x2000, kButtonCross = 0x4000, kButtonSquare = 0x8000;
/* sound effect ids */
constexpr uint32_t kSoundScroll = 0x6212, kSoundCancel = 0x6214, kSoundTab = 0x621b;
}  // namespace

void DuelRecord::Init(uint32_t heap) {
    memset(x_.mem(x_.addr(kRankingStateOffset)), 0, 0x34);
    if (heap == 0) {   /* no heap given: take a private 2 MB pool from the kernel */
        state().poolId = (int32_t)x_.callImport("sceKernelCreateFpl", x_.addr(kPoolNameOffset), 2, 0, kPoolSize, 1, 0);
        x_.callImport("sceKernelAllocateFpl", state().poolId, x_.addr(kRankingStateOffset + offsetof(RankingState, poolMem)), 0);
        heap = ehsys::InitHeap(x_, state().poolMem, kPoolSize);
    }
    state().heap = heap;
    x_.callImport("sceKernelChangeCurrentThreadAttr", 0, 0x4000);
    InitTextureSlots();
    ehsys::SetUiFlag4(x_, true);
    ehsys::SetUiFlag2(x_, true);
    const bool firstLanguage = ehsys::GetLanguage(x_) == 0;
    ehsys::SetUiFlag1(x_, firstLanguage);
    ehsys::SetUiFlag8(x_, !firstLanguage);
}

bool DuelRecord::Update() {
    RankingState &s = state();
    switch (s.phase) {
    case 0:   /* load the database, build the rows, start the fade in */
        if (LoadStep()) {
            BuildRows();
            s.phase = 1;
            ehsys::FadeFrom(x_, 1, 0xff000000, 16);
            ehsys::SetScreenOverlay(x_, false);
        }
        return false;
    case 1:   /* fade in */
        if (ehsys::IsFadeDone(x_)) s.phase = 2;
        return false;
    case 2:   /* run: input and drawing */
        HandleInput();
        return false;
    case 3:   /* fade out */
        if (ehsys::IsFadeDone(x_)) s.phase = 4;
        return false;
    case 4:
        return Unload();
    default:
        return false;
    }
}

/* Space for `count` rows (16 bytes each, zeroed) from the screen's heap; returns its guest address. */
uint32_t DuelRecord::NewRows(uint32_t count) {
    const uint32_t buffer = ehsys::Alloc(x_, state().heap, count * sizeof(RankingRow));
    memset(x_.mem(buffer), 0, count * sizeof(RankingRow));
    return buffer;
}

/* Height of a row in text lines: the width of its label (in 1/64 pixel) plus one unit, divided by the line height. */
uint8_t DuelRecord::RowHeight(uint32_t style, uint16_t tab, uint16_t item, int32_t lineHeight) {
    const uint32_t texts = GetTexture(kTextTableTexture);
    const uint16_t label = LabelIndex((const uint16_t *)x_.mem(x_.addr(kLabelTables[tab].offset)), kLabelTables[tab].labels, item);
    const int32_t width = ehsys::MeasureText(x_, style, ehsys::GetText(x_, texts, label));
    return lineHeight ? (uint8_t)(((width + 0x40) / lineHeight) & 0x3f) : 0;
}

void DuelRecord::BuildRows() {
    RankingState &s = state();
    s.saveData = ehsys::GetSaveBlock(x_);
    uint8_t *save = x_.mem(s.saveData);
    ehsys::SetFontSize(x_, 14, 14);
    const int32_t lineHeight = ehsys::GetLineHeight(x_);
    int32_t shown = 0, total = 0;   /* rows built / items that exist: the completion percentage */

    /* High Score: every item that exists, with the value kept in the save and a "new" mark */
    {
        const uint32_t buffer = NewRows(kLabelTables[kTabHighScore].items);
        RankingRow *rows = (RankingRow *)x_.mem(buffer);
        const uint8_t *table = x_.mem(x_.addr(kLabelTables[kTabHighScore].offset));
        uint16_t count = 0, order = 0;
        for (uint32_t i = 0; i < kLabelTables[kTabHighScore].items; i++) {
            if (!(table[i * 2] & 1)) continue;
            RankingRow &r = rows[count];
            r.item = (uint16_t)i;
            r.order = order;
            total++;
            r.flags = (r.flags & 0xfe) | (save[kSaveNewFlagsHighScore + i] & 1);
            if (r.flags & 1) save[kSaveNewFlagsHighScore + i] = 0;
            order++;
            r.flags = (r.flags & 0xfd) | 2;
            r.value = *(uint32_t *)(save + i * 4);
            r.heightBits = (r.heightBits & 0xc0) | RowHeight(kStyleWithValue, kTabHighScore, (uint16_t)i, lineHeight);
            count++;
            shown++;
        }
        s.rows[kTabHighScore] = buffer;
        s.rowCount[kTabHighScore] = count;
    }

    /* Extra tab: only the items the player has unlocked (status 1 = just unlocked, 2 = seen), no value */
    BuildStatusRows(kTabExtra, kSaveStatusExtra, kStylePlain, lineHeight, shown, total);

    /* Bonus: unlocked items; the first three also show a number (cards owned, player level, rows of the extra tab) */
    BuildStatusRows(kTabBonus, kSaveStatusBonus, kStylePlain, lineHeight, shown, total);

    s.tabInfo = (s.tabInfo & 0x0f) | 0x30;
    if (!ehsys::SaveFlag(x_, 7)) s.tabInfo = (uint8_t)((s.tabInfo & 0x0f) | ((((s.tabInfo >> 4) + 0xff) & 0xf) << 4));   /* the extra tab stays hidden until flag 7 */
    s.completionPercent = total ? (uint8_t)(shown * 100 / total) : 0;
}

/* Rows of a tab whose items come from a status byte per item in the save (0 = locked). */
void DuelRecord::BuildStatusRows(uint32_t tab, uint32_t saveOffset, uint32_t plainStyle, int32_t lineHeight, int32_t &shown, int32_t &total) {
    RankingState &s = state();
    uint8_t *save = x_.mem(s.saveData);
    const uint32_t buffer = NewRows(kLabelTables[tab].items);
    RankingRow *rows = (RankingRow *)x_.mem(buffer);
    const uint8_t *table = x_.mem(x_.addr(kLabelTables[tab].offset));
    uint16_t count = 0, order = 0;
    for (uint32_t i = 0; i < kLabelTables[tab].items; i++) {
        if (!(table[i * 2] & 1)) continue;
        RankingRow &r = rows[count];
        r.order = order;
        total++;
        const uint8_t status = save[saveOffset + i];
        order++;
        if (status == 0) continue;
        r.item = (uint16_t)i;
        r.flags = (r.flags & 0xfe) | (status == 1);
        if (r.flags & 1) save[saveOffset + i] = 2;   /* the "new" mark is shown once */
        r.flags &= 0xfd;
        r.value = 0;
        if (tab == kTabBonus) {
            if (i == 2) { r.value = s.rowCount[kTabExtra]; r.flags |= 2; }
            else if (i == 1) { r.value = ehsys::PlayerLevel(x_); r.flags |= 2; }
            else if (i == 0) {
                for (uint32_t card = 2; card < 152; card++)
                    if (ehsys::CardState(x_, card) == 3) r.value++;
                r.flags |= 2;
            }
        }
        r.heightBits = (r.heightBits & 0xc0) | RowHeight((r.flags & 2) ? kStyleWithValue : plainStyle, (uint16_t)tab, (uint16_t)i, lineHeight);
        count++;
        shown++;
    }
    s.rows[tab] = buffer;
    s.rowCount[tab] = count;
}

void DuelRecord::HandleInput() {
    RankingState &s = state();
    const uint32_t input = ehsys::GetInput(x_);
    const uint32_t pressed = x_.at<uint32_t>(input + 4), repeated = x_.at<uint32_t>(input + 12);
    if (pressed & (kButtonCircle | kButtonCross)) {   /* leave: fade out, the next Update steps move on to unloading */
        ehsys::FadeTo(x_, 1, 0xff000000, 16);
        s.phase = 3;
        ehsys::PlaySound(x_, kSoundCancel);
        return;
    }
    const uint32_t tab = s.tabInfo & 0xf, tabCount = s.tabInfo >> 4;
    if (pressed & (kButtonLeft | kButtonLTrigger)) {   /* previous tab */
        s.tabInfo = (uint8_t)((s.tabInfo & 0xf0) | ((tabCount ? (tab + tabCount - 1) % tabCount : 0) & 0xf));
        ehsys::PlaySound(x_, kSoundTab);
        return;
    }
    if (pressed & (kButtonRight | kButtonRTrigger | kButtonSquare)) {   /* next tab */
        s.tabInfo = (uint8_t)((s.tabInfo & 0xf0) | (tabCount ? (tab + 1) % tabCount : 0));
        ehsys::PlaySound(x_, kSoundTab);
        return;
    }
    uint16_t &scroll = s.scroll[tab];
    if (repeated & kButtonUp) {
        if (scroll > 0) {
            scroll--;
            ehsys::PlaySound(x_, kSoundScroll);
        } else if ((pressed & kButtonUp) && MaxScroll(tab) > 0) {   /* a fresh press at the top wraps to the bottom */
            scroll = (uint16_t)MaxScroll(tab);
            ehsys::PlaySound(x_, kSoundScroll);
        }
    } else if (repeated & kButtonDown) {
        const int32_t max = MaxScroll(tab);
        if (scroll < max) {
            scroll++;
            ehsys::PlaySound(x_, kSoundScroll);
        } else if ((pressed & kButtonDown) && max > 0) {   /* a fresh press at the bottom wraps to the top */
            scroll = 0;
            ehsys::PlaySound(x_, kSoundScroll);
        }
    }
}

int32_t DuelRecord::MaxScroll(uint32_t tab) {
    RankingState &s = state();
    const uint32_t count = s.rowCount[tab & 0xffff];
    return MaxScrollPosition(count ? (const RankingRow *)x_.mem(s.rows[tab & 0xffff]) : nullptr, count);
}

void DuelRecord::InitTextureSlots() {
    uint8_t &flags = state().loadFlags;
    flags = 0;
    flags = (flags & 0xfe) | 1;
    ehsys::Sort(x_, x_.addr(kTextureSlotsOffset), 3, sizeof(TextureSlot), x_.addr(kOffsetTextureSlotCompare));
}

bool DuelRecord::Unload() {
    RankingState &s = state();
    FreeDatabase();
    for (int i = 0; i < kTabCount; i++)
        if (s.rows[i] != 0) ehsys::Free(x_, s.heap, s.rows[i]);
    if (s.poolId > 0) {
        x_.callImport("sceKernelFreeFpl", s.poolId, s.poolMem);
        x_.callImport("sceKernelDeleteFpl", s.poolId);
    }
    return true;
}

void DuelRecord::FreeDatabase() {
    RankingState &s = state();
    if (s.ehp != 0) {
        ehsys::Free(x_, s.heap, s.ehp);
        s.ehp = 0;
    }
}

bool DuelRecord::LoadStep() {
    uint8_t &flags = state().loadFlags;
    if ((flags & 1) == 0) return false;
    if ((flags >> 1) == 0) {
        if (StartDatabaseLoad() != 0) flags = (uint8_t)((flags & 1) | (((flags >> 1) + 1) * 2));
        return false;
    }
    flags &= 1;
    return true;
}

uint32_t DuelRecord::StartDatabaseLoad() {
    RankingState &s = state();
    if (s.ehp != 0) return s.ehp;
    if (s.loadFlags & 1) {
        s.loadFlags &= 0xfe;
        const uint32_t path = x_.addr(kDatabasePathOffset);
        const uint32_t size = ehsys::FileSize(x_, 3, path);
        s.ehp = ehsys::Alloc(x_, s.heap, size);
        ehsys::ReadFileAsync(x_, 3, path, s.ehp, size, x_.addr(kOffsetDatabaseLoadedCallback), s.ehp);
    }
    return 0;
}

bool DuelRecord::OnDatabaseLoaded(uint32_t buffer, int32_t result) {
    uint8_t &flags = state().loadFlags;
    if (result > 0) {
        if (buffer != 0) ehsys::RegisterArchive(x_, 0, buffer);
        flags = (uint8_t)(((flags >> 1) + 1) * 2);
    }
    flags = (flags & 0xfe) | 1;
    return result > 0;
}

uint32_t DuelRecord::GetTexture(uint16_t key) {
    const int32_t index = FindEntryIndex(key, x_.mem(x_.addr(kTextureSlotsOffset)), 3, sizeof(TextureSlot));
    if (index < 0) return 0;
    TextureSlot &slot = slots()[index];
    if (slot.texture != 0) return slot.texture;
    const uint32_t archive = StartDatabaseLoad();
    if (archive == 0) return 0;
    char letters[6];
    memcpy(letters, x_.mem(x_.addr(kLanguageLettersOffset)), sizeof letters);
    const uint32_t language = ehsys::GetLanguage(x_);
    /* the file name is built on the guest stack, below the caller's frame */
    CPU *c = x_.c;
    c->r[29] -= 0x120;
    const uint32_t name = c->r[29] + 0x20;
    x_.callImport("sprintf", name, slot.nameFormat, (uint32_t)(int32_t)letters[language]);
    slot.texture = ehsys::FindTexture(x_, archive, name, 0);
    c->r[29] += 0x120;
    return slot.texture;
}
