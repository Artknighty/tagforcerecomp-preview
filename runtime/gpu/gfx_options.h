/* The graphics options as a list of choices (name + current choice), for the in-game Graphics screen (decomp/libtitle/options_screen.cpp).
 * Plain C++ on purpose: no windows.h, so the decomp can include it. Setting a choice applies it, saves tagforce_recomp.ini and updates
 * the window's Graphics menu. */
#pragma once

enum GfxOption {
    GFXOPT_RENDERER,     /* Software / Direct3D 11 / Vulkan */
    GFXOPT_RESOLUTION,   /* internal resolution: 1x 2x 3x 4x 6x 8x */
    GFXOPT_MSAA,         /* anti-aliasing: Off 2x 4x 8x */
    GFXOPT_TEXTURE_PACK, /* texture package: Original (mod TexturasPC) / Enhanced (mod EnhancedTextures); applied right away */
    GFXOPT_FRAME_RATE,   /* 30 FPS / 60 FPS / 120 FPS (interpolated) */
    GFXOPT_SYNC,         /* presentation: Default / VSync / Lowest latency */
    GFXOPT_FILTER,       /* scaling filter of the final image */
    GFXOPT_ASPECT,       /* aspect ratio of the final image */
    GFXOPT_COUNT
};

const char *gfx_option_label(int option);                      /* "Renderer", "Internal resolution", ... */
const char *gfx_option_help(int option);                       /* one line (about 60 characters) for the help panel */
int gfx_option_choice_count(int option);
const char *gfx_option_choice_name(int option, int choice);    /* "Vulkan", "4x", "On", ... */
int gfx_option_get(int option);                                /* index of the current choice */
void gfx_option_set(int option, int choice);                   /* applies it (the renderer restarts the GPU) */

/* True when the PC-style menus are on: the texture package in use carries the new button sheet (marker file menu_estilo_pc.txt in its folder;
 * TF_MODERN_MENUS=0/1 overrides). The decomp's list drawing then uses the slanted, glowing buttons with icons. */
bool gfx_modern_menus();
