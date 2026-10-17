/* Duel Result: how the screen starts (the rewards are computed here) and how the duel ends. */
#include "result_screen.h"
#include <string.h>

namespace duel_mgr {

namespace {
constexpr uint32_t kGetDuelHeap = 0x6D200A3F;        /* (guess) NID of the duel drawing module's function that returns the heap the duel allocates from */
constexpr uint32_t kReleaseDuelDrawing = 0x34213DF0; /* (guess) NID of the duel drawing module's function that releases its drawing */

/* how many entries each reward list can hold (the allocation is two halfwords per entry: the items and their widths) */
constexpr uint32_t kBonusEntries = kBonusCount, kHighScoreEntries = kHighScoreCount, kChallengeEntries = kChallengeCount;
constexpr int32_t kHighScoreBonus = 0x14, kChallengeBonus = 0x1e;
}  // namespace

void DuelResult::Start() {
    const uint32_t heap = x_.callNid(kGetDuelHeap);
    const uint32_t block = ehsys::Alloc(x_, heap, sizeof(ResultScreen));
    x_.at<uint32_t>(kResultScreenPointer) = block;
    memset(x_.mem(block), 0, sizeof(ResultScreen));
    ResultScreen &s = screen();
    s.heap = heap;
    s.lists[1].items = ehsys::Alloc(x_, heap, kBonusEntries * 2);
    s.lists[1].widths = ehsys::Alloc(x_, heap, kBonusEntries * 2);
    s.lists[2].items = ehsys::Alloc(x_, heap, kHighScoreEntries * 2);
    s.lists[2].widths = ehsys::Alloc(x_, heap, kHighScoreEntries * 2);
    s.lists[3].items = ehsys::Alloc(x_, heap, kChallengeEntries * 2);
    s.lists[3].widths = ehsys::Alloc(x_, heap, kChallengeEntries * 2);
    s.stats = ehsys::GetStartRequest(x_) + offsetof(StartRequest, statistics);

    /* an entry counts when its table says it can be earned (bit 0 of its halfword) and the duel's statistics say it was */
    auto earnable = [&](uint32_t table, uint32_t index) { return (x_.at<uint16_t>(moduleAddress(table) + index * 2u) & 1) != 0; };
    int32_t n = 0;
    for (uint32_t i = 0; i < kBonusEntries; i++) {
        const int32_t points = statistics().bonusPoints[i];   /* the DP each bonus earned */
        if (earnable(kEntryNames[0], i) && points != 0) {
            s.rewards[0] += points;
            x_.at<uint16_t>(s.lists[1].items + n * 2u) = (uint16_t)i;
            n++;
        }
    }
    s.lists[1].count = (uint16_t)n;
    for (uint32_t i = 0; i < kHighScoreEntries; i++) {
        if (earnable(kEntryNames[1], i) && statistics().highScores[i] != 0) s.rewards[1] += kHighScoreBonus;
    }
    n = 0;
    for (uint32_t i = 0; i < kChallengeEntries; i++) {
        if (earnable(kEntryNames[2], i) && statistics().challengeDone[i] == 1) {
            s.rewards[2] += kChallengeBonus;
            x_.at<uint16_t>(s.lists[3].items + n * 2u) = (uint16_t)i;
            n++;
        }
    }
    s.lists[3].count = (uint16_t)n;

    /* the bonus is scaled down by how many of the save's entries are in state 1 or 3 */
    int32_t scaleIndex = 0;
    for (int32_t i = 0; i < (int32_t)x_.callLocal(fn::GetSaveEntryCount - kModuleBase); i++) {
        const int16_t state = settings().entryStates[i];
        if (state == 3 || state == 1) scaleIndex++;
    }
    uint16_t scale[4];
    for (int i = 0; i < 4; i++) scale[i] = x_.at<uint16_t>(moduleAddress(kDuelOutcomeScale) + i * 2u);
    SaveBlock &save = x_.at<SaveBlock>(ehsys::GetSaveBlock(x_));
    int32_t total = 0;
    for (int i = 0; i < kRewardCount; i++) total += s.rewards[i];
    int32_t scaled = total;
    if (scaleIndex != 0) scaled = (total * 4) / (int32_t)scale[scaleIndex];
    if (total < 0) total = 0;
    if (save.bestBonus < scaled) {   /* a new best: +20 DP and the save remembers it */
        total += 0x14;
        s.rewards[1] += 0x14;
        scaled = total;
        if (scaleIndex != 0) scaled = (total * 4) / (int32_t)scale[scaleIndex];
        statistics().highScores[2] = scaled;   /* the stored copy of the scaled bonus */
        save.bestBonus = scaled;
        save.changed = 1;
    }
    x_.callLocal(fn::AddPlayerDP - kModuleBase, (uint32_t)scaled);
    s.rewards[3] = -(total - scaled);   /* the part the scaling took away is shown as a negative row */

    n = 0;
    for (uint32_t i = 0; i < kHighScoreEntries; i++) {
        if (statistics().highScores[i] != 0) x_.at<uint16_t>(s.lists[2].items + n++ * 2u) = (uint16_t)i;
    }
    s.lists[2].count = (uint16_t)n;
    LoadArchive();
    s.banner = -1;
    ehsys::SetUiFlag4(x_, true);
    ehsys::SetUiFlag2(x_, true);
    if (ehsys::GetLanguage(x_) == 0) {
        ehsys::SetUiFlag1(x_, true);
        ehsys::SetUiFlag8(x_, false);
    } else {
        ehsys::SetUiFlag1(x_, false);
        ehsys::SetUiFlag8(x_, true);
    }
    if (ehsys::GetNetworkScene(x_)) {
        s.networkPlayerTarget = (uint16_t)ehsys::GetNetworkPlayerTarget(x_);
        ehsys::SetNetworkFlag(x_, 0);
        SendNetworkStep(1, 0);
        s.flags17c = (s.flags17c & 0xfe) | 1;
    }
}

void DuelResult::Finish() {
    SaveState &save = x_.at<SaveState>(x_.callLocal(fn::GetSaveState - kModuleBase));
    if (x_.at<uint32_t>(kDuelModuleLoaded[3]) != 0) x_.callLocal(fn::UnloadModule - kModuleBase, 10);
    if (x_.at<uint32_t>(kDuelModuleLoaded[2]) != 0) x_.callNid(ehsys::nid::UnloadSoundBanks);
    if (x_.at<uint32_t>(kDuelModuleLoaded[1]) != 0) x_.callLocal(fn::UnloadModule - kModuleBase, 8);
    if (x_.at<uint32_t>(kDuelModuleLoaded[0]) != 0) {
        x_.callNid(kReleaseDuelDrawing);
        x_.callLocal(fn::UnloadModule - kModuleBase, 9);
    }
    if (ehsys::GetNetworkScene(x_)) ehsys::LeaveNetworkScene(x_);
    if (settings().duelKind == kDuelKindRestoresSetup) memcpy(save.duelSetup, x_.mem(kDuelSettingsBackup), sizeof save.duelSetup);
    ehsys::SetSceneMode(x_, 0);
    ehsys::ReleaseSceneResources(x_);
}

}  // namespace duel_mgr
