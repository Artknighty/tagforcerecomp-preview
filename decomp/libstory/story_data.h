/* libstory: the story mode ("modstory"). It runs the days of the story: a calendar of 15-minute steps, the 150 characters' state (where each one is and
 * which event they are in), the field (the map) and the places' scripts, and it loads the modules of each place (field, shop, deck...). The module image
 * loads at 0x08EFCA00 (the same base as libtitle, at other times); its data has fixed addresses, listed here as absolute guest addresses. */
#pragma once
#include "../swap.h"
#include <stdint.h>

namespace story {

constexpr uint32_t kModuleBase = 0x08EFCA00;
constexpr uint32_t offsetOf(uint32_t guestAddress) { return guestAddress - kModuleBase; }
inline uint32_t guest(SwapCtx &x, uint32_t address) { return x.addr(offsetOf(address)); }                 /* the address of a module datum in this run */
template <class T> T &datum(SwapCtx &x, uint32_t address) { return x.at<T>(guest(x, address)); }

constexpr int32_t kCharacterCount = 150;     /* the characters with ids 2 .. 151 */

/* ---- the calendar (FUN_08f03e0c binds it to the save data) ---- */

/* The story's date and time. The story runs in steps of 15 minutes, 73 a day from 05:45. The date record lives in the save data. */
struct Date {
    int32_t ticks;           /* +0x00 days * 73 + tick */
    int32_t month;           /* +0x04 1..8: the story starts in May */
    int32_t dayOfMonth;      /* +0x08 */
    int32_t tick;            /* +0x0C 1..73: the step inside the day */
    int32_t calendarMonth;   /* +0x10 5..12 */
    int32_t hour;            /* +0x14 */
    int32_t minute;          /* +0x18 45, 0, 15 or 30 */
    int32_t weekday;         /* +0x1C 0 = Sunday */
    int32_t daysLeft;        /* +0x20 days until the tournament: 92 minus the day of the year, never below 0 */
};
static_assert(sizeof(Date) == 0x24, "Date layout");

constexpr int32_t kTicksPerDay = 73;
constexpr uint32_t kDatePointer = 0x08F21B44;       /* -> Date */
constexpr uint32_t kLastDayPointer = 0x08F21B48;    /* -> int: the day of the year seen last */
constexpr uint32_t kFlagsPointer = 0x08F21B40;      /* -> the story's flags (a bit set at +8) */
constexpr uint32_t kHolidayTable = 0x08F15BE0;      /* 16-byte entries: month, day (0 = any), unknown, value at +8 */

/* ---- the characters ---- */

constexpr uint32_t kCharacterStatesPointer = 0x08F16420;   /* -> CharacterState[150] (in the save data) */
constexpr uint32_t kProgressBitsPointer = 0x08F1641C;      /* -> the progress flag bits; the 12 bytes per character at +0x20 list their image events */
constexpr uint32_t kCharacterScripts = 0x08F16424;         /* CharacterScript[150] */

/* One character's state in the save data (0x98 bytes). */
struct CharacterState {
    uint16_t eventId;            /* +0x00 the event the character is in */
    uint8_t unknown02[10];
    uint32_t field0C;            /* +0x0C copied from the schedule record */
    uint32_t field10;            /* +0x10 */
    uint8_t unknown14;
    uint8_t field15;             /* +0x15 */
    uint8_t field16;             /* +0x16 */
    uint8_t field17;             /* +0x17 */
    uint8_t place;               /* +0x18 the character's place on the map */
    uint8_t unknown19[7];
    uint32_t scheduleIndex;      /* +0x20 which schedule (script) the character follows */
    uint32_t eventValue;         /* +0x24 found by FindEventValues */
    uint32_t eventKind;          /* +0x28 */
    uint32_t imageEvent;         /* +0x2C the event whose picture is shown */
    uint16_t record[17];         /* +0x30 .. +0x50: halfwords copied from the schedule record (see kRecordCopies) */
    uint16_t alternativeEventId; /* +0x52 */
    uint8_t unknown54[0x34];
    uint32_t seenMask;           /* +0x88 bit n-1: something seen (FUN_08efcf70) */
    uint8_t flags;               /* +0x8C bit 0: hidden, bit 2: changed */
    uint8_t unknown8D[3];
    uint32_t dayStamp;           /* +0x90 the day of the year the record was loaded on */
    uint8_t unknown94[4];
};
static_assert(sizeof(CharacterState) == 0x98, "CharacterState layout");

/* One character's script tables (0x134 bytes at kCharacterScripts); the tables are guest addresses into the field data. */
struct CharacterScript {
    uint32_t eventTableA;        /* +0x00 records of 6 halfwords: a date range (4) and two values */
    uint32_t eventTableB;        /* +0x04 entries of 3 halfwords: the place the character must be at, two values */
    uint32_t eventTableC;        /* +0x08 entries of 3 halfwords: a "seen" number, two values */
    uint32_t eventTableD;        /* +0x0C entries of 3 halfwords: a story flag, two values */
    uint32_t table10;            /* +0x10 */
    uint32_t imageRules;         /* +0x14 records of 8 halfwords: a date range, three conditions, the event whose picture to show */
    uint32_t table18;            /* +0x18 */
    uint32_t table1C;            /* +0x1C */
    uint32_t schedule;           /* +0x20 records of 25 halfwords: the schedule index, a tick, and the values copied into the state */
    uint8_t unknown24[0x10];
    uint16_t name[0x40];         /* +0x34 the character's name for the current event (UTF-16, 0x80 bytes) */
    uint16_t imageName[0x40];    /* +0xB4 the name of the picture event */
};
static_assert(sizeof(CharacterScript) == 0x134, "CharacterScript layout");

/* ---- memory and files ---- */

constexpr uint32_t kStoryHeaps = 0x091C1D0C;          /* three heaps (0x240000, 0x40000 and 0x20000 bytes) the story's data is read into */
constexpr uint32_t kArchiveBuffer = 0x091C1D18, kArchiveSize = 0x091C1D1C, kArchiveDone = 0x091C1D20;   /* the archive being read */
constexpr uint32_t kScriptArchive = 0x091C1D5C;       /* the opened "brain" archive: the places' tables */
constexpr uint32_t kTextArchive = 0x091C1D60;         /* the same for the language's texts */
constexpr uint32_t kLanguageIndex = 0x091C1D64;       /* the language as the story's file names count it */
constexpr uint32_t kLanguageSuffixes = 0x08F16200;    /* char *[6]: the letters added to the text file names */

}  // namespace story
