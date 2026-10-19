/* libfield: the functions that only clear a block of the module's data when the field starts. Each block belongs to one part of the field screen. */
#include "../common/swap.h"
#include "field_data.h"
#include "field_functions.h"
#include <string.h>

#define MODULE "libfield"

namespace {
using namespace field;

#define RESET_BLOCK(Name, function, block, bytes) \
    void swap_##Name(SwapCtx &x) { memset(x.mem(guest(x, block)), 0, bytes); } \
    SwapReg reg_##Name({"libfield." #Name, MODULE, offsetOf(function), swap_##Name});

RESET_BLOCK(ResetFieldState, 0x091CD4F8, kFieldState, 0x18)
RESET_BLOCK(ResetBeacons, 0x091CF8CC, kBeacons - 0x10, 0x610)
RESET_BLOCK(ResetMenuState, 0x091D21B8, 0x0999338C, 0x280)      /* the windows / state of the field menus */
RESET_BLOCK(ResetHud, 0x091D2EE8, 0x0999360C, 0x200)             /* the calendar, clock and number display */
RESET_BLOCK(ResetDialog, 0x091D806C, 0x0999398C, 0x154)          /* the text window the story opens over the map */
RESET_BLOCK(ResetCameraShake, 0x091D9578, 0x09993B40, 0x80)
RESET_BLOCK(ResetWorldMap, 0x091D9E50, 0x09993BC0, 0x314)        /* the menu of the 14 places */
RESET_BLOCK(ResetFollower, 0x091DC8E4, 0x09993EE0, 0xB0)
RESET_BLOCK(ResetEffects, 0x091DD5C4, 0x09993FE0, 0x28C0)        /* the list of drawn effects */
RESET_BLOCK(ResetPanel, 0x091E7100, 0x09996FD0, 0x1A0)
RESET_BLOCK(ResetPanelData, 0x091E7DFC, 0x09997170, 0xC00)
RESET_BLOCK(ResetBanner, 0x091E8700, 0x09997D70, 0x98)           /* the faded title text */
RESET_BLOCK(ResetScreenFade, 0x091EB63C, 0x099982C0, 0x18)

/* ---- FUN_091c5820: clears the shadow offset block and the panel data ---- */
void swap_ResetShadowOffset(SwapCtx &x) {
    memset(x.mem(guest(x, 0x092061C0)), 0, 0x20);
    x.callLocal(fn::ResetPanelData);
}
SwapReg reg_ResetShadowOffset({"libfield.ResetShadowOffset", MODULE, offsetOf(0x091C5820), swap_ResetShadowOffset});

/* ---- FUN_091d0d5c: clears the arguments block and the parts of the screen that have their own reset ---- */
void swap_ResetScreens(SwapCtx &x) {
    memset(x.mem(guest(x, kFieldArguments) - 0x48), 0, 0xB8);
    x.callLocal(fn::ResetHud);
    x.callLocal(fn::ResetPanel);
    x.callLocal(fn::ResetPlayer);
    x.callLocal(fn::ResetFollower);
    x.callLocal(fn::ResetMenuState);
}
SwapReg reg_ResetScreens({"libfield.ResetScreens", MODULE, offsetOf(0x091D0D5C), swap_ResetScreens});

}  // namespace
