/* Release (generic) build: no game code is linked in. The modules are translated on the player's machine and compiled in memory (jit.cpp, prepare.cpp). */
#include "hle.h"
extern "C" const GenModule g_gen_modules[1] = {};
extern "C" const unsigned g_gen_nmodules = 0;
