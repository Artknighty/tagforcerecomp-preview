/* Save states (Cheats > Save state / Load state, and the TF_CTL commands "save N" / "load N"). */
#pragma once
#include <stdint.h>
#define SAVESTATE_SLOTS 4
void savestate_request(int slot, bool load);   /* asks for it; done between two thread slices, a few microseconds later (safe from any thread) */
bool savestate_has(int slot);
const char *savestate_last_result(void);       /* "saved 1", "loaded 1" or the reason it failed (for the TF_CTL reply) */
