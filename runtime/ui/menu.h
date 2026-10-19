/* Barra de menu nativa (menu.cpp). */
#pragma once
#include <windows.h>
HMENU menu_create(void);
void menu_sync(bool fullscreen, bool fps, int scale);   /* scale 1..4, 0 = tamanho livre */
bool menu_command(HWND h, int id);                      /* true se o ID era do menu */
void menu_sync_toggles(void);                           /* marcas dos liga/desliga dos mods (depois de trocar por tecla) */

/* modo escuro (janela, menus e barra de menu) */
void menu_dark_init(void);                 /* antes de criar janelas e menus */
void menu_dark_window(HWND h);
bool menu_dark_message(HWND h, UINT m, WPARAM w, LPARAM l, LRESULT *res);   /* true = tratada (res) */
