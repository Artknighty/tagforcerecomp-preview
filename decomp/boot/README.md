# BOOT (ehsys, the engine library)

`BOOT` is the game's resident module: the engine every other Module calls (`libehsys_*`, the `NID`s in [ehsys.h](../ehsys.h)). It has 2291 functions;
only the ones below are decompiled so far. Method and rules: [docs/DECOMP.md](../../docs/DECOMP.md).

## Window drawing ([window.cpp](window.cpp), [window_content.cpp](window_content.cpp), [window.h](window.h), [boot.h](boot.h))

A window block (`WindowBlock`, 0x120 bytes; `libtitle::Window` is the same memory seen from the title code) holds up to 16 window items and the
set's own style. `DrawWindowContent` (`EF9B5D06`) is what a screen calls each frame to draw the set.

| Function | Swap | What it does |
|---|---|---|
| `FUN_08848a14` (`EF9B5D06`) | `BOOT.DrawWindowContent` | the whole set: the frames of its windows, then the content: the grid of cells (background, selection highlight, per-cell callback, overlay), the scroll arrows and the dialog buttons; fades everything by rewriting the alpha of the colour commands while the set opens or closes |
| `FUN_088418b8` (`14BFFB01`) | `BOOT.DrawWindows` | lays out and draws the frame of every window item, lowest priority first |
| `FUN_08842808` | `BOOT.DrawWindowItem` | one window: the frame (a mesh of 16-bit sprites bound to the block's frame sheet, optional overlay mesh), the body text and the title text |
| `FUN_08841b8c` (`8AC7E9E7`) | `BOOT.WindowTextBottom` | the y the cells start at: just below the window's body text |
| `FUN_08843984`, `FUN_0884a1fc` | `BOOT.FindWindowItem`, `BOOT.WindowAnimationStep` | find the item of a style; how far a window's open / close animation is |
| `FUN_08849f44` (`646197AD`), `FUN_08849e40` (`40D6B29D`), `FUN_08849fb8` | `BOOT.DrawCellBackground`, `BOOT.DrawSelectedCell`, `BOOT.DrawBoxSprites` | a cell's blue background, the orange pulsing highlight of the selected cell, and the nine-piece box both use |
| `FUN_0884a4a0` (`91E5BB91`), `FUN_08849b0c` (`B7DFD65E`) | `BOOT.DrawScrollArrow`, `BOOT.DrawWindowButton` | a blinking scroll arrow; a dialog button with its label |
| `39ADDA9F` (`FUN_088493cc`) | `BOOT.UpdateWindow` ([window_update.cpp](window_update.cpp)) | one frame of a window set's input and state: opening, reading the pad (move the selection among cells or dialog buttons, confirm, cancel), the answer animation, closing; answers -1 nothing, -2 cancelled, else the chosen cell or button |
| `FUN_0884a2c8`, `FUN_0884a3d0`, `5B4DADF5`, `0190A327` | `BOOT.SelectPreviousCell`, `SelectNextCell`, `MoveSelectionBack`, `MoveSelectionOn` | the selection one cell back or on (wrapping or not, skipping cells that cannot be chosen) |
| `B0F320E6`, `FA53F08C`, `F60A4259`, `8171F765` | `BOOT.BeginClose`, `StartItemClosing`, `ItemSettled`, `ConfirmCancelMask` | start closing a window; whether its item has finished appearing or disappearing; the pad masks of confirm and cancel |
| `FUN_08843aa0` and the VFPU helpers `0881d3d8`, `0881d5c0`, `0881d334` | `BOOT.FadeColour` | the colour `factor` of the way between two colours, through floats (so a byte b comes back as b - 1: the engine's own rounding); the animations use it to halve the alpha. Checked against the generated code on 100000 random colours and factors: no difference |
| `FUN_0884368c` | `BOOT.PlaceRect` | the corners of a rectangle for one of seven anchors |
| `FUN_08841e9c` | `BOOT.LayoutWindowItem` ([window_layout.cpp](window_layout.cpp)) | one frame of an item's layout: the item's phase (new, opening, shown, closing, closed), the size worked out from the text when the style changed, the five-step open / close animation (texts hidden and half-faded, the window grown from or shrunk to half its height, a ghost overlay) and the frame mesh |
| `FUN_08842c68`, `FUN_088435b4`, `FUN_08843808`, `FUN_088439cc` | `BOOT.BuildFrameMesh`, `BOOT.FrameVertexCount`, `StyleDiffers` | the frame mesh: eight border pieces, the filler column in rows of eight pixels, the tail and the title strip, as sprites (two vertices each) in the display list's data; how many vertices it needs; whether a style changed |

Natives call each other directly, so only the entry points above show as "first call" in the log; `tools/decomp_compare_skip.sh BOOT ...` compares captures
with all of them on and off.

PC style: `DrawWindowItem` asks `pcStyleWindow` ([pc_style.h](../pc_style.h), implemented in `libtitle/panels.cpp`) to draw a glass panel over the
frame's rectangle instead of the frame's sprites; it declines (and the game's frame is drawn) unless the PC style is on and libtitle is on a screen
whose sheet has the pieces (the options / card list screens, which include Free Duel).

The window drawing, layout and input paths of BOOT are all decompiled. The text and GU wrappers in [ehsys.h](../ehsys.h) are named but still generated code.
Debt: `boot::WindowBlock` and `libtitle::Window` describe the same memory; the title code should move to the former.
