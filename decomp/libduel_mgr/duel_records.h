/* Records that other modules own and the Duel Result screen reads or updates. Only the fields the screen uses are named; the rest is
 * padding. Offsets were checked against the generated code (tools/decomp/decomp_acessos.py) and are asserted below. */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace duel_mgr {

constexpr int kBonusCount = 83;        /* duel bonuses a duel can earn */
constexpr int kHighScoreCount = 40;    /* high scores a duel can set */
constexpr int kChallengeCount = 118;   /* challenges a duel can complete */

/* What the duel recorded about itself (inside StartRequest). */
struct DuelStatistics {
    int32_t highScores[kHighScoreCount];    /* +0x000 the record of each high score; 0 = not reached */
    int32_t bonusPoints[kBonusCount];       /* +0x0a0 the DP each duel bonus earned; 0 = not earned */
    uint8_t challengeDone[kChallengeCount]; /* +0x1ec 1 = the challenge was completed */
};
static_assert(offsetof(DuelStatistics, bonusPoints) == 0xa0, "DuelStatistics layout");
static_assert(offsetof(DuelStatistics, challengeDone) == 0x1ec, "DuelStatistics layout");

/* ehsys::GetStartRequest: the record describing the duel that was started and how it went. */
struct StartRequest {
    uint8_t unknown00[0x20];
    int32_t unknown20;
    int32_t kind;                  /* +0x24 8 = a recorded duel (the outcome is stored, see GetDuelOutcome) */
    uint8_t unknown28[0xc];
    uint16_t playerCount;          /* +0x34 how many players; in a recorded duel, the index of the stored outcome */
    int16_t results[3];            /* +0x36 per player: 1 = won, 2 = lost */
    DuelStatistics statistics;     /* +0x3c */
};
static_assert(offsetof(StartRequest, kind) == 0x24, "StartRequest layout");
static_assert(offsetof(StartRequest, results) == 0x36, "StartRequest layout");
static_assert(offsetof(StartRequest, statistics) == 0x3c, "StartRequest layout");

/* ehsys::GetSettingsBlock: the settings in memory. */
struct SettingsBlock {
    uint8_t unknown000[0x108];
    int16_t entryStates[8];        /* +0x108 per save entry: 1 or 3 count toward the bonus scaling */
    uint8_t duelKind;              /* +0x118 */
    uint8_t resultMode;            /* +0x119 2 = the result screen shows the outcome banner */
    uint8_t teamMode;              /* +0x11a 2 = four duelists (two per side) on the outcome screen */
    uint8_t unknown11b[2];
    uint8_t networkMode;          /* +0x11d 1 = guest, 2 = host of a network duel */
};
static_assert(offsetof(SettingsBlock, entryStates) == 0x108, "SettingsBlock layout");
static_assert(offsetof(SettingsBlock, duelKind) == 0x118, "SettingsBlock layout");
static_assert(offsetof(SettingsBlock, networkMode) == 0x11d, "SettingsBlock layout");
static_assert(offsetof(SettingsBlock, teamMode) == 0x11a, "SettingsBlock layout");
constexpr uint8_t kResultModeWithOutcome = 2;
constexpr uint8_t kTeamModeFour = 2;
constexpr uint32_t kPlayerRecordBase = 0x130, kPlayerRecordSize = 0x18c;   /* in the settings block: one record per duelist, the name first */
constexpr uint8_t kDuelKindRestoresSetup = 5;   /* the duel kind whose setup the exit copies back into the save */
constexpr uint8_t kNetworkGuest = 1, kNetworkHost = 2;

/* ehsys::GetSaveBlock: the part of the save the screen updates. */
struct SaveBlock {
    uint8_t unknown00[8];
    int32_t bestBonus;             /* +0x08 the best scaled bonus so far */
    uint8_t unknown0c[0x96];
    uint8_t changed;               /* +0xa2 set when the save has something new to write */
};
static_assert(offsetof(SaveBlock, bestBonus) == 0x08, "SaveBlock layout");
static_assert(offsetof(SaveBlock, changed) == 0xa2, "SaveBlock layout");

/* The module's own thunk save_estado: the save's state block. */
struct SaveState {
    uint8_t unknown00[0x60];
    uint8_t duelSetup[8];          /* +0x60 */
};
static_assert(offsetof(SaveState, duelSetup) == 0x60, "SaveState layout");

}  // namespace duel_mgr
