/* Interface escura das janelas nativas (versao de PC): caixa de mensagem propria no lugar da MessageBox do Windows (que nao
 * tem modo escuro e saia no tema classico) e ajudantes para as outras janelas (barra de titulo, botoes, cores). Uma paleta so
 * para tudo da recomp (menus, caixas, ajustes, tela de abertura e os paineis dos mods no jogo): azul-marinho com dourado. */
#pragma once
#include <windows.h>

namespace dark {
const COLORREF BG = RGB(17, 21, 36),        /* fundo das janelas e dos menus */
               CARD = RGB(25, 31, 52),      /* areas elevadas (rodape, cartoes) */
               LINE = RGB(50, 60, 94),      /* linhas e bordas */
               TXT = RGB(236, 240, 250), DIM = RGB(142, 154, 188),
               BTN = RGB(38, 46, 76), BTN_HOT = RGB(52, 62, 100),
               ACC = RGB(222, 182, 92), ACC_HOT = RGB(242, 206, 124), ACC_TXT = RGB(30, 24, 10),   /* destaque dourado (texto escuro por cima) */
               OFF = RGB(32, 38, 60), OFF_TXT = RGB(96, 106, 138),
               BAR = RGB(12, 15, 27),       /* barra de titulo e barra de menu */
               HOT = RGB(40, 52, 92),       /* item de menu sob o mouse */
               ICON = RGB(150, 170, 222);   /* icones dos menus */
HBRUSH brush(COLORREF c);                                     /* pincel em cache (nao apague) */
void title_bar(HWND h);                                       /* barra de titulo escura (DWM), na cor BAR no Windows 11 */
void theme_control(HWND h);                                   /* controle comum (edit, barra de rolagem) no tema escuro do Windows */
bool gdiplus(void);                                           /* GDI+ iniciado (uma vez, qualquer thread): formas com antialias */
/* retangulo arredondado com antialias (GDI+; sem ele, RoundRect do GDI); edge == fill = sem borda */
void round_rect(HDC dc, RECT r, COLORREF fill, COLORREF edge, int rad);
void accent_line(HDC dc, int x0, int x1, int y, int h);       /* filete dourado que some nas pontas (assinatura visual das janelas) */
/* botao BS_OWNERDRAW: fundo arredondado; primary = dourado (botao padrao); scale = pixels por 96 dpi * 1000;
 * around = cor atras do botao (cantos) */
void draw_button(const DRAWITEMSTRUCT *d, bool primary, HFONT font, int scale = 1000, COLORREF around = BG);
}

/* Caixa de mensagem escura e modal (mesmos flags da MessageBox: MB_OK, MB_OKCANCEL, MB_YESNO, MB_ICON*, MB_DEFBUTTON2) e
 * mesmos retornos (IDOK, IDCANCEL, IDYES, IDNO). Enter/Espaco = botao com foco, Esc = Nao/Cancelar, setas/Tab trocam. */
int tf_msgbox(HWND owner, const wchar_t *text, const wchar_t *title, UINT flags);

/* Janela escura modal com controle deslizante inteiro de vmin a vmax (slider_dialog.cpp): arrastar com o mouse, setas, Home/End, PgUp/PgDn, roda.
 * Botoes Padrao (volta a def), Cancelar e OK. unit = sufixo mostrado (x, % ...). Devolve true se OK (e grava em *value). */
bool tf_slider(HWND owner, const wchar_t *title, const wchar_t *text, const wchar_t *unit, int vmin, int vmax, int def, int *value);
