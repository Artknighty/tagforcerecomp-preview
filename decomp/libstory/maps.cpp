/* libstory: small lookup functions that turn one code into another. */
#include "../swap.h"
#include "story_data.h"

#define MODULE "libstory"

namespace {
using namespace story;

constexpr uint32_t kAfterFirstBreak = offsetOf(0x08F040B4);

/* historia_objeto_id_por_estado (FUN_08f0a108)(state index): the object id of a character: the indexes 0..149 are the ids 2..151, anything else 0 */
void swap_ObjectIdOfCharacter(SwapCtx &x) { const uint32_t state = x.arg(0); x.ret(state <= 0x95 ? state + 2 : 0); }
SwapReg reg_ObjectIdOfCharacter({"libstory.ObjectIdOfCharacter", MODULE, offsetOf(0x08F0A108), swap_ObjectIdOfCharacter, true});

/* FUN_08f0abec(place): the number the story's text files use for a place; two places change after the first break, unknown places are 30 */
void swap_PlaceTextNumber(SwapCtx &x) {
    static const uint8_t kNumbers[0x1C] = {0x1E, 1, 2, 3, 4, 5, 0, 0xD, 0xE, 0xF, 0x10, 0x11, 0x12, 0, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1D, 0x1E, 0x1F, 0x20, 0x24, 0x27};
    const uint32_t place = x.arg(0);
    if (place == 6) { x.ret(x.callLocal(kAfterFirstBreak) == 0 ? 0xC : 0x25); return; }
    if (place == 0xD) { x.ret(x.callLocal(kAfterFirstBreak) == 0 ? 0x13 : 0x26); return; }
    x.ret(place >= 1 && place <= 0x1B ? kNumbers[place] : 0x1E);
}
SwapReg reg_PlaceTextNumber({"libstory.PlaceTextNumber", MODULE, offsetOf(0x08F0ABEC), swap_PlaceTextNumber});

/* FUN_08f0ae50(code): 8 -> 1, 9 -> 3, 10 -> 5, 11 -> 7, anything else 0 */
void swap_OddNumberOfCode(SwapCtx &x) {
    const uint32_t code = x.arg(0);
    x.ret(code == 0xB ? 7 : code == 10 ? 5 : code == 9 ? 3 : code == 8 ? 1 : 0);
}
SwapReg reg_OddNumberOfCode({"libstory.OddNumberOfCode", MODULE, offsetOf(0x08F0AE50), swap_OddNumberOfCode, true});

/* FUN_08f0adf4(language): the language number 0..5 as the story's file names use it (the original leaves r2 alone for others) */
void swap_LanguageIndex(SwapCtx &x) { const uint32_t language = x.arg(0); if (language < 6) x.ret(language); }
SwapReg reg_LanguageIndex({"libstory.LanguageIndex", MODULE, offsetOf(0x08F0ADF4), swap_LanguageIndex});

}  // namespace
