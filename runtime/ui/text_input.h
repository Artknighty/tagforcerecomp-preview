/* Text entry for the game's name box (replaces the PSP on-screen keyboard, sceUtilityOsk). Plain C++ on purpose: no windows.h, so the decomp
 * (decomp/libtitle/name_box.cpp) can include it.
 *
 * Two ways to show the box:
 *  - in the game (preferred): the game draws the box itself from text_input_live() and the keys come from the game window
 *    (display.cpp passes WM_KEYDOWN / WM_CHAR to text_input_key). The decomp asks for it with text_input_use_game_ui(true) before opening the keyboard;
 *  - in a small window over the game (text_input.cpp): used when nothing in the game draws the box (a version without the swaps). */
#pragma once
#include <string>

void text_input_begin(const std::wstring &desc, const std::wstring &initial, int maxlen);
void text_input_begin_multiline(const std::wstring &desc, const std::wstring &initial, int maxlen);   /* big window, Enter = new line, Ctrl+Enter confirms */
int text_input_poll(std::wstring *out);   /* 0 = nothing open, 1 = typing, 2 = confirmed, 3 = cancelled (on 2/3 *out gets the text) */
bool text_input_busy(void);               /* open, or the result was not collected yet */

void text_input_use_game_ui(bool on);     /* the next box is drawn by the game: no window */
bool text_input_in_game(void);            /* a box drawn by the game is open (the keyboard no longer moves the game's cursor) */
/* The text being typed and whether the caret is lit this instant; false when no in-game box is open. */
bool text_input_live(std::wstring *text, bool *caretLit, std::wstring *description, int *maxLength);
void text_input_finish(bool confirmed);   /* the pad's confirm / cancel buttons end the in-game box too */
/* WM_KEYDOWN (0x100) / WM_CHAR (0x102) from the game window; true = used by the box. */
bool text_input_key(unsigned message, unsigned long long wparam);
