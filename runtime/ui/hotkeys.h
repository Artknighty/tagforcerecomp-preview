/* The F-keys of the game window, in ONE table (kHotkeys in display.cpp, next to the actions). Everything that needs to know about them asks here:
 * the window procedure runs them, the plugins avoid them, the View menu and Help > Controls show their names and descriptions.
 * To add a key: one line in that table (key, extra name, description, action); nothing else needs to change. */
#pragma once
#include <string>

bool hotkey_run(int vk, bool fresh);        /* the key is bound: runs its action when `fresh` (not an auto-repeat) and returns true */
bool hotkey_reserved(int vk);               /* the key is bound (plugins cannot use it for their toggles) */
const wchar_t *hotkey_key(int vk);          /* "F11", for the accelerator column of a menu item; L"" when not bound */
std::wstring hotkey_help_line(int vk);      /* "   F11 or Alt+Enter: fullscreen\n", a line of Help > Controls */
