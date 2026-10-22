/* Compile-time guard for the plugin ABI (run by tools/test/sdk_check.sh). Every field of TfModApi keeps the offset it had in the release that
 * introduced it: the API only grows at the end, so a plugin built against an older header keeps working on a newer runtime. If one of these
 * asserts fails, a field was moved, removed or resized: restore it (add new fields at the END and raise TFMOD_API_VERSION). When a new field
 * is added legitimately, append its line here. Offsets are for the 64-bit build (the runtime and the plugins are 64-bit). */
#include "tfmod.h"
#include <stddef.h>

static_assert(sizeof(void *) == 8, "plugins and runtime are 64-bit");
static_assert(offsetof(TfModApi, version) == 0, "v1");
static_assert(offsetof(TfModApi, mod_dir) == 8, "v1");
static_assert(offsetof(TfModApi, mem) == 16, "v1");
static_assert(offsetof(TfModApi, reg) == 24, "v1");
static_assert(offsetof(TfModApi, set_reg) == 32, "v1");
static_assert(offsetof(TfModApi, hook) == 40, "v1");
static_assert(offsetof(TfModApi, call_original) == 48, "v1");
static_assert(offsetof(TfModApi, call) == 56, "v1");
static_assert(offsetof(TfModApi, buttons) == 64, "v1");
static_assert(offsetof(TfModApi, frame) == 72, "v1");
static_assert(offsetof(TfModApi, module_loaded) == 80, "v1");
static_assert(offsetof(TfModApi, message_box) == 88, "v1");
static_assert(offsetof(TfModApi, log) == 96, "v1");
static_assert(offsetof(TfModApi, guest_alloc) == 104, "v2");
static_assert(offsetof(TfModApi, add_slider) == 112, "v3");
static_assert(offsetof(TfModApi, add_toggle) == 120, "v4");
static_assert(offsetof(TfModApi, panel) == 128, "v4");
static_assert(offsetof(TfModApi, use_menu) == 136, "v4");
static_assert(offsetof(TfModApi, sprite) == 144, "v5");
static_assert(offsetof(TfModApi, quad_near) == 152, "v5");
static_assert(offsetof(TfModApi, set_speed) == 160, "v6");
static_assert(sizeof(TfModApi) == 168, "TfModApi grew: append the new field's assert above");
static_assert(TFMOD_API_VERSION >= 6, "the API version never goes down");
