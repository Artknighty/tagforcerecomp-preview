/* Duel Result: loading, the title capsule, the question window and the player's input on the reward pages. */
#include "result_internal.h"
#include <stdio.h>
#include <string.h>

namespace duel_mgr {

namespace {
constexpr uint32_t kUiScreenWidth = 0x1e0, kUiScreenHeight = 0x110;
constexpr uint32_t kButtonL = 0x100, kButtonLeft = 0x80, kButtonR = 0x200, kButtonRight = 0x20, kButtonUp = 0x10, kButtonDown = 0x40;
constexpr uint32_t kSoundPageSlide = 0x6212, kSoundConfirm = 0x6213, kSoundQuestion = 0x621a;
constexpr int16_t kQuestionBothPlayersAnswer = 1, kQuestionAnswer = 2, kQuestionWait = 3;   /* CreateQuestionWindow kinds (names are by use) */
constexpr uint32_t kFileDevice = 3;   /* the device number the game's file calls take for the disc data */
constexpr uint32_t kMessageNo = 0x104, kMessageWaiting = 0x103, kMessageHost = 0x102, kMessageGuest = 0x101;   /* text table indexes */
}  // namespace

uint32_t DuelResult::LoadArchive() {
    ResultScreen &s = screen();
    if (s.archive != 0) return s.archive;
    if (s.flags17c & 2) return 0;   /* already requested */
    GuestStack frame(x_, 0x120);
    char letters[8] = {};
    memcpy(letters, x_.mem(moduleAddress(kArchiveLetters)), 6);
    s.flags17c = (s.flags17c & 0xfd) | 2;
    const uint32_t language = ehsys::GetLanguage(x_);
    const uint32_t name = frame.addr() + 32;   /* "duel/result/result_<letter>.ehp" */
    x_.callImport("sprintf", name, moduleAddress(kArchiveNameFormat), (uint32_t)(int32_t)(int8_t)letters[language]);
    const uint32_t size = ehsys::FileSize(x_, kFileDevice, name);
    s.archive = ehsys::Alloc(x_, s.heap, size);
    ehsys::ReadFileAsync(x_, kFileDevice, name, s.archive, size, moduleAddress(fn::OnArchiveLoaded), s.archive);
    return 0;
}

bool DuelResult::OnArchiveLoaded(uint32_t archive, int32_t result) {
    if (result > 0) ehsys::RegisterArchive(x_, 0, archive);
    ResultScreen &s = screen();
    s.flags17c &= 0xfd;
    return result > 0;
}

void DuelResult::SetTitleBarState(uint16_t state) {
    const uint32_t sprites = GetResource(kTitleBarSprites);
    if (sprites == 0) return;
    uint32_t first = 0, last = 0;   /* the part of the capsule's animation that belongs to the state */
    switch (state) {
    case 3: first = 0x60; last = 0x65; break;
    case 2: first = 0x10; last = 0x60; break;
    case 1: first = 0; last = 0x10; break;
    default: state = 0; break;
    }
    ehsys::SetSpriteFrame(x_, sprites, first, last + 1);
    ResultScreen &s = screen();
    s.titleState = (s.titleState & 0xcf) | (uint8_t)((state & 3) << 4);
}

bool DuelResult::SpriteAnimationDone(uint32_t sprites) {
    GuestStack out(x_, 16);
    ehsys::GetSpritePlayback(x_, sprites, out.addr(), out.addr() + 4);
    const int32_t current = x_.at<int32_t>(out.addr() + 4);
    const int32_t frames = (int32_t)ehsys::GetSpriteFrameCount(x_, sprites);
    return !(current < 1 || frames < current - 1);
}

bool DuelResult::TitleBarAnimationDone() {
    const uint32_t sprites = GetResource(kTitleBarSprites);
    return sprites != 0 && SpriteAnimationDone(sprites);
}

bool DuelResult::IsBannerFinished() {
    const int16_t banner = screen().banner;
    if (banner < 0) return true;
    const uint32_t sprites = GetResource((uint16_t)banner);
    return sprites != 0 && SpriteAnimationDone(sprites);
}

void DuelResult::StartBanner(int16_t kind) {
    ResultScreen &s = screen();
    if (s.banner < 0) return;
    const uint32_t sprites = GetResource((uint16_t)s.banner);
    if (sprites == 0) return;
    uint32_t first = 0, last = 0;   /* the part of the banner's animation that belongs to the kind */
    if (kind == 3) {
        first = 0x46; last = 0x4a;
        s.flags = (s.flags & 0xfe) | 1;
    } else if (kind == 2) {
        first = 0x32; last = 0x32;
        s.flags = (s.flags & 0xfe) | 1;
    } else if (kind != 1) {
        s.flags &= 0xfe;
    } else {
        if (s.banner == kDrawBanner) last = 0x14;
        else if (s.banner == kLoseBanner) last = 0x1b;
        else if (s.banner == kWinBanner) last = 0x31;
        s.flags = (s.flags & 0xfe) | 1;
    }
    ehsys::SetSpriteFrame(x_, sprites, first, last + 1);
}

void DuelResult::MeasureEntryWidths() {
    ehsys::SetFontSize(x_, 0xe, 0xe);
    const int32_t lineHeight = ehsys::GetLineHeight(x_);
    ResultScreen &s = screen();
    /* per list: which table says what each entry is called (its halfword >> 1 is the string index) and the text style to measure in */
    struct { int list; uint32_t table; uint32_t style; } kLists[3] = {{1, 0x08f145ac, 0x5d80}, {2, 0x08f1455c, 0x5080}, {3, 0x08f14654, 0x5f80}};
    for (const auto &l : kLists) {
        EntryList &list = s.lists[l.list];
        for (int i = 0; i < list.count; i++) {
            const uint16_t entry = x_.at<uint16_t>(list.items + i * 2u);
            const uint16_t label = x_.at<uint16_t>(moduleAddress(l.table) + entry * 2u);
            const int32_t width = ehsys::MeasureText(x_, l.style, Text(label >> 1));
            x_.at<int16_t>(list.widths + i * 2u) = (int16_t)((width + 0x40) / lineHeight);
        }
    }
}

void DuelResult::CreateQuestionWindow(int16_t kind) {
    ResultScreen &s = screen();
    libtitle::Window &w = s.window;
    memset(&w, 0, sizeof w);
    w.styleKind = 1;
    w.frameKind = 0;
    w.unknown100 = 0xf0;
    w.unknown96 = 1;
    w.colour = 0xffffffff;
    w.unknown136 = 1;
    w.margin[0] = 10;
    w.margin[2] = 8;
    w.margin[1] = 10;
    w.margin[3] = 0;
    w.textSize = 0xc;
    w.textColour = 0xff000000;
    if (kind == kQuestionWait) {
        w.text = Text(kMessageNo);
        w.flags3 = (w.flags3 & 0xf0) | 1;
        w.unknown212 = 1;
        w.flags = (w.flags & 0xdf) | 0x20;
    } else if (kind == kQuestionAnswer) {
        w.flags = (w.flags & 0xfe) | 1;
        w.text = Text(kMessageWaiting);
        w.margin[3] = 8;
    } else if (kind == kQuestionBothPlayersAnswer) {
        w.flags3 = (w.flags3 & 0xf0) | 3;
        w.flags = (w.flags & 0xfb) | 4;
        const uint8_t mode = settings().networkMode;
        if (mode == kNetworkHost) {
            w.text = Text(kMessageHost);
            w.selectedButton = (w.selectedButton & 0xf0) | 3;
        } else if (mode == kNetworkGuest) {
            w.text = Text(kMessageGuest);
            w.selectedButton = (w.selectedButton & 0xf0) | 2;
        }
    }
    w.animationA = 2;
    w.animationB = 2;
    w.heap = screen().heap;
    w.flags = (w.flags & 0xfd) | 2;
    ehsys::LayoutWindow(x_, x_.at<uint32_t>(kResultScreenPointer) + offsetof(ResultScreen, window));
    /* centred on the screen (the division rounds toward zero) */
    int32_t room = (int32_t)kUiScreenWidth - (int16_t)w.width;
    w.x = (uint16_t)(room < 0 ? (0x1e1 - (int16_t)w.width) >> 1 : room >> 1);
    room = (int32_t)kUiScreenHeight - (int16_t)w.height;
    w.y = (uint16_t)(room < 0 ? (0x111 - (int16_t)w.height) >> 1 : room >> 1);
    s.windowState = (s.windowState & 0xfc) | ((uint8_t)kind & 3);
}

void DuelResult::HandlePageInput() {
    ResultScreen &s = screen();
    const ehsys::PadState &pad = x_.at<ehsys::PadState>(ehsys::GetInput(x_));
    const uint32_t page = s.page & 0xf;
    const uint32_t buttonMask = ehsys::GetButtonMask(x_, 0);
    if ((pad.pressed & buttonMask & 0xffff) != 0) {   /* the confirm button: leave the pages */
        bool askQuestion = false;
        if (settings().resultMode != kResultModeWithOutcome) {
            const uint8_t mode = settings().networkMode;
            if (mode == kNetworkHost) {
                askQuestion = true;
            } else if (mode == kNetworkGuest) {
                if (request().unknown20 != 1) askQuestion = true;
            }
        }
        if (askQuestion) {
            CreateQuestionWindow(kQuestionBothPlayersAnswer);
            ehsys::PlaySound(x_, kSoundQuestion);
        } else {
            ehsys::PlaySound(x_, kSoundConfirm);
        }
        s.phase = kPhaseAnswer;
        s.step = 0;
        return;
    }
    const uint32_t held = pad.pressedOrRepeating;
    /* L / R (or left / right) change the page: the next page, going around, that has something to show; page 0 is always there */
    auto changePage = [&](bool forward) {
        for (uint32_t i = 1; i < 4; i++) {
            const uint32_t other = (forward ? page + i : page + 4 - i) & 3;
            if (other == 0) { s.page &= 0xf0; break; }
            if (s.rewards[other - 1] > 0) { s.page = (s.page & 0xf0) | (uint8_t)(other & 0xf); break; }
        }
        if (page != (s.page & 0xfu)) {   /* start the slide: direction in bits 5-6, the page we leave in bits 1-4 */
            s.phase = kPhaseSliding;
            s.flags = forward ? (uint8_t)((s.flags & 0x9f) | 0x20) : (uint8_t)(s.flags & 0x9f);
            s.flags = (s.flags & 0xe1) | (uint8_t)(page << 1);
            s.slide = 0;
            SetTitleBarState(3);
            ehsys::PlaySound(x_, kSoundPageSlide);
        }
    };
    if (held & (kButtonL | kButtonLeft)) { changePage(false); return; }
    if (held & (kButtonR | kButtonRight)) { changePage(true); return; }
    if (page == 0) return;
    EntryList &list = s.lists[page];
    if (held & kButtonUp) {
        if (list.scroll != 0) list.scroll--;
    } else if (held & kButtonDown) {   /* scroll while the entries from the top still fill more than ten lines */
        int32_t lines = 0;
        int32_t position = list.scroll;
        for (; position < (int32_t)list.count; position++) {
            lines += x_.at<uint16_t>(list.widths + position * 2u);
            if (lines > 10) { position--; break; }
        }
        if (position < (int32_t)list.count - 1) list.scroll++;
    }
}

void DuelResult::UpdateSlide() {
    ResultScreen &s = screen();
    if (TitleBarAnimationDone()) SetTitleBarState(0);
    const uint16_t before = s.slide++;
    if (before >= 0x10) {   /* the slide is over: the capsule re-opens on the new page's tab, then browsing goes on */
        SetTitleBarState(1);
        s.titleState = (s.titleState & 0xf0) | (s.page & 0xf);
        s.phase = kPhaseTitleBarOpening;
        s.slide = 0;
    }
}

int16_t DuelResult::GetDuelOutcome() {
    const StartRequest &r = request();
    const int16_t *halfwords = (const int16_t *)&r.playerCount;   /* the stored outcome is read as halfword number playerCount of this run */
    if (r.kind == 8) return halfwords[(uint16_t)r.playerCount];   /* a recorded duel: the outcome is stored */
    if (r.playerCount == 3) {   /* three results: wins minus losses */
        int32_t score = 0;
        for (int i = 0; i < 3; i++) {
            if (r.results[i] == 1) score++;
            else if (r.results[i] == 2) score--;
        }
        return score > 0 ? 1 : (score < 0 ? 2 : 3);
    }
    if (r.playerCount == 2) {
        if (r.results[0] == 1 && r.results[1] == 1) return 1;
        if (r.results[0] == 2) return r.results[1] == 2 ? 2 : 0;
    }
    return 0;
}

void DuelResult::SendNetworkStep(uint8_t kind, uint8_t argument) {
    GuestStack message(x_, 0x100);
    memset(x_.mem(message.addr()), 0, 0x100);
    x_.at<uint8_t>(message.addr()) = 8;
    x_.at<uint8_t>(message.addr() + 1) = kind;
    x_.at<uint8_t>(message.addr() + 2) = argument;
    ehsys::SendNetworkMessage(x_, message.addr());
}

uint32_t DuelResult::Update() {
    ResultScreen &s = screen();
    const uint32_t windowAddress = x_.at<uint32_t>(kResultScreenPointer) + offsetof(ResultScreen, window);
    if ((s.windowState & 3) != 0) {   /* the question window is open: it takes the frame */
        const int32_t answer = ehsys::UpdateWindow(x_, windowAddress);
        const uint8_t state = s.windowState & 3;
        if (state == 3) {   /* waiting for the other player */
            if (answer != -1) {
                s.windowState &= 0xfb;
                s.windowState &= 0xfc;
                s.flags17c &= 0xfe;
                ehsys::ClearWindow(x_, windowAddress);
            }
            return 0;
        }
        if (state == 2) {
            if (ehsys::GetNetworkPlayerAnswers(x_) < (int32_t)s.networkPlayerTarget) {
                ehsys::ClearWindow(x_, windowAddress);
                CreateQuestionWindow(kQuestionWait);
            } else if (ehsys::ReceiveNetworkMessage(x_, 8, 2, 0xff)) {
                s.windowState = (s.windowState & 0xfb) | 4;
                ehsys::ClearWindow(x_, windowAddress);
                s.windowState &= 0xfc;
            }
            return 0;
        }
        if (answer == -1) {   /* state 1, still undecided */
            if (ehsys::GetNetworkScene(x_) && ehsys::GetNetworkPlayerAnswers(x_) < (int32_t)s.networkPlayerTarget) CreateQuestionWindow(kQuestionWait);
        } else {
            s.windowState &= 0xfc;
            ehsys::ClearWindow(x_, windowAddress);
            if (answer != 2) return 0;
            s.windowState = (s.windowState & 0xfb) | 4;
            if (ehsys::GetNetworkScene(x_)) {
                if ((int32_t)s.networkPlayerTarget == ehsys::GetNetworkPlayerAnswers(x_)) {
                    CreateQuestionWindow(kQuestionAnswer);
                    SendNetworkStep(2, 0);
                } else {
                    CreateQuestionWindow(kQuestionWait);
                }
            }
        }
        return 0;
    }

    switch (s.phase) {
    case kPhaseWaitForArchive:
        if ((s.flags17c & 2) == 0) {   /* the archive has arrived */
            ehsys::SetScreenOverlay(x_, false);
            MeasureEntryWidths();
            ehsys::FadeFrom(x_, 1, 0xff000000, 0x10);
            SetTitleBarState(0);
            s.phase = kPhaseFadeIn;
        }
        break;
    case kPhaseFadeIn:
        if (ehsys::IsFadeDone(x_)) {
            SetTitleBarState(1);
            s.phase = kPhaseTitleBarOpening;
        }
        break;
    case kPhaseTitleBarOpening:
        if (TitleBarAnimationDone()) {
            SetTitleBarState(2);
            s.phase = kPhasePages;
        }
        break;
    case kPhasePages:
        HandlePageInput();
        break;
    case kPhaseSliding:
        UpdateSlide();
        break;
    case kPhaseAnswer:
        if (s.step == 3) {   /* what comes next: the outcome banner (in duels that have an outcome) or the end */
            if (settings().resultMode == kResultModeWithOutcome) {
                if (GetDuelOutcome() == 0) {
                    s.titleState = (s.titleState & 0xf0) | 4;
                } else {
                    const int16_t outcome = GetDuelOutcome();
                    if (outcome == 3) s.banner = kDrawBanner;
                    else if (outcome == 2) s.banner = kLoseBanner;
                    else if (outcome == 1) s.banner = kWinBanner;
                    s.titleState = (s.titleState & 0xf0) | 5;
                }
                ehsys::FadeFrom(x_, 1, 0xff000000, 0x10);
                s.phase = kPhaseBannerIn;
            } else {
                s.phase = kPhaseClosed;
            }
            s.step = 0;
        } else if (s.step == 2) {
            if (ehsys::IsFadeDone(x_)) s.step++;
        } else if (s.step == 1) {
            if (TitleBarAnimationDone()) {
                SetTitleBarState(0);
                ehsys::FadeTo(x_, 1, 0xff000000, 0x10);
                if (settings().resultMode != kResultModeWithOutcome) ehsys::SetAttractTimer(x_, 600);
                s.step++;
            }
        } else if (s.step == 0) {
            SetTitleBarState(3);
            s.step++;
        }
        break;
    case kPhaseBannerIn:
        if (s.step == 3) {
            s.phase = kPhaseWaitButton;
            s.step = 0;
        } else if (s.step == 2) {
            if (IsBannerFinished()) {
                StartBanner(2);
                s.step++;
            }
        } else if (s.step == 1) {
            if (TitleBarAnimationDone()) {
                SetTitleBarState(2);
                if (s.banner < 0) {
                    s.step = 3;
                } else {
                    StartBanner(1);
                    s.step++;
                }
            }
        } else if (s.step == 0 && ehsys::IsFadeDone(x_)) {
            SetTitleBarState(1);
            s.step++;
        }
        break;
    case kPhaseWaitButton: {
        const ehsys::PadState &pad = x_.at<ehsys::PadState>(ehsys::GetInput(x_));
        if ((pad.pressed & ehsys::GetButtonMask(x_, 0) & 0xffff) != 0) {
            const int16_t outcome = GetDuelOutcome();
            bool askQuestion = false;
            if (outcome != 0) {
                const uint8_t mode = settings().networkMode;
                if (mode == kNetworkHost) askQuestion = true;
                else if (mode == kNetworkGuest && outcome != 1) askQuestion = true;
            }
            if (askQuestion) {
                CreateQuestionWindow(kQuestionBothPlayersAnswer);
                ehsys::PlaySound(x_, kSoundQuestion);
            } else {
                ehsys::PlaySound(x_, kSoundConfirm);
            }
            s.phase = kPhaseBannerOut;
            s.step = 0;
        }
        break;
    }
    case kPhaseBannerOut:
        if (s.step != 2) {
            if (s.step != 1) {
                if (s.step != 0) return 0;
                SetTitleBarState(3);
                StartBanner(3);
                s.step++;
                return 0;
            }
            if (IsBannerFinished()) {
                StartBanner(0);
                s.step++;
            }
        }
        if (TitleBarAnimationDone()) {
            SetTitleBarState(0);
            StartBanner(0);
            ehsys::FadeTo(x_, 1, 0xff000000, 0x10);
            ehsys::SetAttractTimer(x_, 600);
            s.phase = kPhaseFadeOut;
            s.step = 0;
        }
        break;
    case kPhaseFadeOut:
        if (ehsys::IsFadeDone(x_)) s.phase = kPhaseClosed;
        break;
    case kPhaseClosed: {
        const uint32_t result = (s.windowState & 4) ? 2 : 1;   /* 2: the question was answered */
        if (ehsys::GetNetworkScene(x_) && (s.flags17c & 1) != 0) ehsys::SetNetworkFlag(x_, 1);
        Free();
        return result;
    }
    default:
        return 0;
    }
    return 0;
}

void DuelResult::Free() {
    ResultScreen &s = screen();
    const uint32_t heap = s.heap;
    for (int i = 0; i < 4; i++) {
        if (s.lists[i].items != 0) ehsys::Free(x_, heap, s.lists[i].items);
        if (s.lists[i].widths != 0) ehsys::Free(x_, heap, s.lists[i].widths);
    }
    if (s.archive != 0) ehsys::Free(x_, heap, s.archive);
    ehsys::Free(x_, heap, x_.at<uint32_t>(kResultScreenPointer));
    x_.at<uint32_t>(kResultScreenPointer) = 0;
}

}  // namespace duel_mgr
