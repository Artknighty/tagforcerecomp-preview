/* Janela Win32 que mostra o framebuffer do PSP (480x272, ampliado 2x). Chamada a cada vblank pelo agendador. */
#include "dark_ui.h"
#include "hle.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <vector>
#include "menu.h"
#include "gfx.h"
#include "util.h"
#include "gpu.h"
#include "interp.h"
#include "pacing.h"
#include "modbox.h"
#include "mouse_ui.h"

static HWND g_wnd; static bool g_ready;
static uint32_t g_pix[480 * 272];
static uint32_t g_fb_addr, g_fb_stride = 512, g_fb_fmt = 3;
static uint64_t g_frames;
static void latch_buttons(void);

void display_set_frame(uint32_t addr, uint32_t stride, uint32_t fmt) { g_fb_addr = addr; g_fb_stride = stride ? stride : 512; g_fb_fmt = fmt; }
uint64_t display_frames(void) { return g_frames; }
HWND display_hwnd(void) { return g_wnd; }
/* aviso curto no titulo da janela (ex.: "jogo salvo"): o PSP mostraria a tela de gravacao do sistema */
static uint64_t g_notify_until;
/* titulo da janela; execucoes de teste (scripts de tools/ definem TF_LOG_DIR) ficam marcadas, para nao serem confundidas com o jogo do
 * jogador (ex.: o teste de New Game abre sem save e mostra o seletor de idioma) */
#include "capturas.inc"
static const char *win_title(void) { static const char *t = getenv("TF_LOG_DIR") ? "Tag Force Recomp  [TESTE AUTOMATICO]" : "Tag Force Recomp"; return t; }
void display_notify(const char *msg) {
    if (!g_wnd) return;
    char t[200]; snprintf(t, sizeof t, "%s  -  %s", win_title(), msg); SetWindowTextA(g_wnd, t);
    g_notify_until = g_frames + 180;
}

/* Janela: redimensionavel (imagem mantem a proporcao 480x272, com faixas pretas), barra de menu (menu.cpp), tela cheia
 * sem bordas e sem menu com F11 ou Alt+Enter, Esc sai da tela cheia ou pergunta antes de fechar o jogo. */
static bool g_fullscreen; static WINDOWPLACEMENT g_wp = {sizeof(WINDOWPLACEMENT)};
static HMENU g_menu;
static bool g_show_fps = getenv("FPS_OVERLAY") != nullptr;   /* overlay de fps (F3 / menu Exibir) */
static void sync_menu(void);
static void toggle_fullscreen(void) {
    LONG st = GetWindowLongA(g_wnd, GWL_STYLE);
    if (!g_fullscreen) {
        MONITORINFO mi = {sizeof mi};
        if (GetWindowPlacement(g_wnd, &g_wp) && GetMonitorInfoA(MonitorFromWindow(g_wnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetMenu(g_wnd, nullptr);
            SetWindowLongA(g_wnd, GWL_STYLE, (st & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
            SetWindowPos(g_wnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g_fullscreen = true;
        }
    } else {
        SetWindowLongA(g_wnd, GWL_STYLE, (st & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetMenu(g_wnd, g_menu);
        SetWindowPlacement(g_wnd, &g_wp);
        SetWindowPos(g_wnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = false;
    }
    InvalidateRect(g_wnd, nullptr, TRUE);
    sync_menu();
}
static int current_scale(void) {   /* 1..4 se a area do jogo for exatamente 480x272 vezes n; 0 = tamanho livre */
    if (g_fullscreen) return 0;
    RECT cr; GetClientRect(g_wnd, &cr);
    for (int s = 1; s <= 4; s++) if (cr.right - cr.left == 480 * s && cr.bottom - cr.top == 272 * s) return s;
    return 0;
}
/* ajusta a janela para a area do jogo ter 480x272 vezes s (sai da tela cheia antes) */
void display_set_scale(int s) {
    if (g_fullscreen) toggle_fullscreen();
    if (IsZoomed(g_wnd)) ShowWindow(g_wnd, SW_RESTORE);
    RECT r = {0, 0, 480 * s, 272 * s}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, TRUE);
    SetWindowPos(g_wnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    /* se a barra de menu quebrou em duas linhas (janela estreita), a area encolheu: corrige pela diferenca real */
    RECT cr; GetClientRect(g_wnd, &cr);
    int dh = 272 * s - (cr.bottom - cr.top);
    if (dh) SetWindowPos(g_wnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top + dh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    sync_menu();
}
void display_toggle_fullscreen(void) { toggle_fullscreen(); }
static std::wstring g_iso_w, g_data_w;   /* ISO atual ou pasta instalada, para reiniciar */
void display_set_data(const wchar_t *dir) { g_data_w = dir; }
void display_set_iso(const char *utf8) {
    wchar_t w[1024]; if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 1024)) g_iso_w = w;
}
/* imagem que vai para a janela: o quadro nativo (g_pix) ou o quadro em resolucao interna maior (g_hi) */
static const uint32_t *g_src = g_pix; static int g_sw = 480, g_sh = 272; static std::vector<uint32_t> g_hi;
const uint32_t *ge_shadow_get(uint32_t addr, uint32_t fmt, int *w, int *h, uint32_t *stride);
void ge_set_res(int n);
int ge_gpu_frame(uint32_t addr, uint32_t fmt);
bool ge_gpu_skip_active(uint32_t addr);
const uint32_t *ge_gpu_small(void);
void ge_set_gpu(bool on);
static bool gpu_mode(void) { return g_gfx.renderer != GFX_RENDER_SOFTWARE && gpu_ready(); }
/* Janela-filha de apresentacao da GPU: cobre a area do jogo e e criada de novo a cada renderizador. Uma janela que ja recebeu uma swapchain
 * DXGI "flip" (Direct3D 11) fica marcada pelo Windows e o Vulkan da NVIDIA nao aparecia mais nela (trocar D3D11 -> Vulkan pelo menu
 * deixava a imagem congelada, mesmo soltando o dispositivo D3D11 inteiro). Desabilitada: nunca recebe foco (teclado/menu ficam na
 * janela principal). */
static HWND g_rwnd;
static uint64_t g_gpu_gen;   /* muda a cada backend de GPU iniciado (o overlay precisa ser reenviado) */
static void paint_frame(HDC dc);
static LRESULT CALLBACK render_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint_frame(dc); EndPaint(h, &ps); return 0; }
    if (m == WM_ERASEBKGND) return 1;
    return DefWindowProcW(h, m, w, l);
}
static void render_window_destroy(void) { if (g_rwnd) { DestroyWindow(g_rwnd); g_rwnd = nullptr; } }
static HWND render_window_create(void) {
    static bool reg;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = render_proc; wc.hInstance = hi; wc.lpszClassName = L"TagForceRender"; wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); RegisterClassW(&wc); reg = true; }
    render_window_destroy();
    RECT cr; GetClientRect(g_wnd, &cr);
    g_rwnd = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"TagForceRender", L"", WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_CLIPSIBLINGS,
                             0, 0, cr.right - cr.left, cr.bottom - cr.top, g_wnd, nullptr, hi, nullptr);
    return g_rwnd;
}
/* inicia o renderizador por GPU pedido; se falhar tenta o proximo (Vulkan -> Direct3D 11 -> software). Devolve o que ficou. */
static int start_gpu(int r) {
    for (; r != GFX_RENDER_SOFTWARE; r = (r == GFX_RENDER_VULKAN) ? GFX_RENDER_D3D11 : GFX_RENDER_SOFTWARE) {
        HWND rw = render_window_create();
        if (rw && gpu_select(r) && gpu_init(rw)) { ge_set_gpu(true); g_gpu_gen++; fprintf(stderr, "[gpu] renderizador: %s\n", gpu_backend_name()); return r; }
        fprintf(stderr, "[gpu] %s indisponivel\n", gpu_backend_name());
        gpu_release_swapchain();
    }
    render_window_destroy();   /* software: o GDI pinta direto na janela principal */
    return GFX_RENDER_SOFTWARE;
}
/* troca o renderizador em execucao (menu Graficos > Renderizacao); cai para o proximo disponivel se o pedido nao iniciar */
void display_set_renderer(int r) {
    ge_set_gpu(false); gpu_release_swapchain();   /* solta o backend atual (quadros sombra, texturas, swapchain) */
    int got = start_gpu(r);
    if (got != r) tf_msgbox(g_wnd, L"Could not start the chosen renderer; using another available one.", L"Tag Force Recomp", MB_ICONWARNING);
    g_gfx.renderer = got;
    gfx_save();
    InvalidateRect(g_wnd, nullptr, TRUE);
}
static void paint_frame(HDC dc) {   /* imagem 480x272 na janela: filtro/proporcao em gfx.cpp */
    if (gpu_mode()) { gpu_present_last(); return; }
    RECT cr; GetClientRect(g_wnd, &cr);
    gfx_paint(dc, g_src, g_sw, g_sh, cr.right - cr.left, cr.bottom - cr.top);
}
static void quit_process(void);
static void toggle_fps_overlay(void);
/* para explicar quadros lentos no log: tempo do laco de mensagens e da leitura do controle neste quadro, e se houve laco
 * modal (arrastar/redimensionar a janela, caixa "Sair?") desde o quadro anterior */
static double g_pump_ms, g_pad_ms; static bool g_modal_seen;
static bool g_keys_held_after_menu;   /* ao fechar um menu: ignora o teclado ate soltar as teclas (o Enter que escolheu o item) */
void display_ask_exit(void);
void display_ask_reset_text(const wchar_t *text);
static int g_wheel_pend;       /* entalhes da roda do mouse ainda por entregar ao jogo (+ cima, - baixo) */
bool display_pc_input(void);
static bool g_cursor_hidden;   /* cursor do mouse some apos 5 s parado sobre a area do jogo; qualquer movimento traz de volta */
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SETCURSOR && g_cursor_hidden && LOWORD(l) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
    { LRESULT dr; if (menu_dark_message(h, m, w, l, &dr)) return dr; }
    if (m == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint_frame(dc); EndPaint(h, &ps); return 0; }
    if (m == WM_PRINTCLIENT) { paint_frame((HDC)w); return 0; }
    if (m == WM_CLOSE || m == WM_DESTROY) { quit_process(); }
    const bool novo = !(l & (1 << 30));   /* tecla recem-apertada, nao a repeticao automatica de quem a segura (segurar F11 alternava a tela cheia sem parar; F7/F8 criavam varias marcas) */
    if (m == WM_KEYDOWN && w == VK_F3) { if (novo) toggle_fps_overlay(); return 0; }
    if (m == WM_KEYDOWN && w == VK_F7) { if (novo) marca_cpu(); return 0; }   /* "a CPU errou" */
    if (m == WM_KEYDOWN && w == VK_F9) { if (novo) cap_toggle(); return 0; }   /* liga/desliga as capturas de diagnostico */
    if (m == WM_KEYDOWN && w == VK_F8) { if (novo) marca_graf(); return 0; }   /* "tem algo errado na tela": quadro inteiro + contexto grafico + nota */
    if (m == WM_KEYDOWN && w == VK_F11) { if (novo) toggle_fullscreen(); return 0; }
    if (m == WM_KEYDOWN && novo) {   /* tecla de um liga/desliga dos mods (ex.: cheats) */
        extern int plugin_toggle_by_key(int vk); extern bool plugin_toggle_on(int i); extern std::wstring plugin_toggle_label(int i);
        const int i = plugin_toggle_by_key((int)w);
        if (i >= 0) {
            menu_sync_toggles();
            std::wstring t = plugin_toggle_label(i); const size_t tab = t.find(L'\t'); if (tab != std::wstring::npos) t.resize(tab);
            t.erase(std::remove(t.begin(), t.end(), L'&'), t.end());   /* '&' = atalho de teclado no menu, nao vai para o titulo */
            t += plugin_toggle_on(i) ? L": on" : L": off";
            char a[200]; if (WideCharToMultiByte(CP_ACP, 0, t.c_str(), -1, a, sizeof a, nullptr, nullptr) > 0) display_notify(a);
            return 0;
        }
    }
    if (m == WM_SYSKEYDOWN && w == VK_RETURN && (l & (1 << 29))) { if (novo) toggle_fullscreen(); return 0; }   /* Alt+Enter */
    /* enquanto a janela e arrastada/redimensionada (ou uma caixa de mensagem esta aberta) o Windows prende a thread num
     * laco modal e o jogo pausa: avisa o watchdog para nao registrar isso como travamento */
    extern volatile int g_diag_paused;
    if (m == WM_ENTERSIZEMOVE || m == WM_ENTERMENULOOP) { g_diag_paused++; g_modal_seen = true; return 0; }
    if (m == WM_EXITSIZEMOVE || m == WM_EXITMENULOOP) {
        if (g_diag_paused > 0) g_diag_paused--;
        if (m == WM_EXITMENULOOP) g_keys_held_after_menu = true;   /* Enter/setas usados no menu nao vao para o jogo */
        return 0;
    }
    if (m == WM_COMMAND && HIWORD(w) == 0) {   /* itens do menu (as caixas Controles/Sobre tambem pausam o jogo) */
        g_diag_paused++; g_modal_seen = true;
        bool done = menu_command(h, LOWORD(w));
        g_diag_paused--;
        if (done) return 0;
    }
    if (m == WM_SIZE) { sync_menu(); if (g_rwnd) MoveWindow(g_rwnd, 0, 0, LOWORD(l), HIWORD(l), FALSE); }   /* a janela de apresentacao da GPU acompanha a area do jogo */
    if (m == WM_KEYDOWN && w == VK_ESCAPE) {
        if (g_fullscreen) toggle_fullscreen(); else display_ask_exit();
        return 0;
    }
    if (m == WM_MOUSEWHEEL && display_pc_input()) {   /* roda = cima/baixo (pulsos em read_keys_pc) */
        static int acc; acc += GET_WHEEL_DELTA_WPARAM(w);
        while (acc >= WHEEL_DELTA) { acc -= WHEEL_DELTA; if (g_wheel_pend < 8) g_wheel_pend++; }
        while (acc <= -WHEEL_DELTA) { acc += WHEEL_DELTA; if (g_wheel_pend > -8) g_wheel_pend--; }
        return 0;
    }
    if (m == WM_ERASEBKGND) return 1;   /* o present pinta tudo (evita piscar) */
    return DefWindowProcA(h, m, w, l);
}
static bool confirm(const wchar_t *text) {   /* caixa Sim/Nao (o jogo fica pausado enquanto ela esta aberta) */
    extern volatile int g_diag_paused;
    g_diag_paused++; g_modal_seen = true;
    int r = tf_msgbox(g_wnd, text, L"Tag Force Recomp", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    g_diag_paused--;
    return r == IDYES;
}
void display_ask_exit(void) {
    if (confirm(L"Quit the game? Anything not saved inside the game will be lost.")) quit_process();
}
/* Reiniciar = abrir o proprio .exe de novo com a mesma ISO e fechar este processo (o estado do jogo emulado nao tem como
 * ser zerado com seguranca dentro do processo). A posicao/tamanho da janela, tela cheia e FPS passam por TF_WINDOW. */
void display_ask_reset(void) { display_ask_reset_text(L"Restart the game? Anything not saved inside the game will be lost."); }
void display_ask_reset_text(const wchar_t *text) {
    if (!confirm(text)) return;
    WINDOWPLACEMENT wp = {sizeof wp};
    if (g_fullscreen) wp = g_wp; else GetWindowPlacement(g_wnd, &wp);
    RECT n = wp.rcNormalPosition; wchar_t geo[96];
    swprintf(geo, 96, L"%ld,%ld,%ld,%ld,%d,%d,%d", n.left, n.top, n.right - n.left, n.bottom - n.top, g_fullscreen ? 1 : 0,
             g_show_fps ? 1 : 0, wp.showCmd == SW_SHOWMAXIMIZED ? 1 : 0);
    SetEnvironmentVariableW(L"TF_WINDOW", geo);
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
    if (!g_iso_w.empty()) cmd += L" --iso \"" + g_iso_w + L"\"";
    else if (!g_data_w.empty()) cmd += L" --dados \"" + g_data_w + L"\"";
    STARTUPINFOW si = {sizeof si}; PROCESS_INFORMATION pi;
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        tf_msgbox(g_wnd, L"Could not restart the game.", L"Tag Force Recomp", MB_ICONERROR); return;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    fprintf(stderr, "[run] reiniciando (novo processo %lu)\n", pi.dwProcessId);
    quit_process();
}
void display_init(void) {
    WNDCLASSA wc = {}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "TagForceRecomp";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hIcon = (HICON)LoadImageA(wc.hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);   /* icone embutido (gen/app.ico), se houver */
    RegisterClassA(&wc);
    gfx_load(); ge_set_res(g_gfx.res);
    menu_dark_init();
    g_menu = menu_create();
    RECT r = {0, 0, 960, 544}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, TRUE);
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = r.right - r.left, h = r.bottom - r.top, fs = 0, fps = 0, mx = 0;
    if (const char *tw = getenv("TF_WINDOW")) {   /* vindo de "Reiniciar jogo": mesma janela de antes */
        int a, b, c, d; if (sscanf(tw, "%d,%d,%d,%d,%d,%d,%d", &a, &b, &c, &d, &fs, &fps, &mx) >= 4 && c > 100 && d > 100) { x = a; y = b; w = c; h = d; }
        SetEnvironmentVariableW(L"TF_WINDOW", nullptr);
    }
    g_wnd = CreateWindowExA(getenv("TF_SEGUNDO_PLANO") ? (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) : 0,   /* testes automaticos: janela que nunca ganha o foco (antes ela era ativada mesmo escondida e minimizava o jogo que o usuario estivesse jogando) */
                          "TagForceRecomp", win_title(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                          x, y, w, h, nullptr, g_menu, wc.hInstance, nullptr);
    if (!g_wnd) fprintf(stderr, "[display] CreateWindow falhou (erro %lu)\n", GetLastError());
    if (g_wnd) {
        menu_dark_window(g_wnd);
        ShowWindow(g_wnd, getenv("TF_SEGUNDO_PLANO") ? SW_HIDE : mx ? SW_SHOWMAXIMIZED : SW_SHOW);   /* testes automaticos: janela escondida, sem roubar o foco (capturas vem do framebuffer) */
        if (fps) g_show_fps = true;
        if (fs) toggle_fullscreen();
        sync_menu();
        if (g_gfx.renderer != GFX_RENDER_SOFTWARE) g_gfx.renderer = start_gpu(g_gfx.renderer);   /* renderizador por GPU (padrao); sem GPU cai no software */
    }
    g_ready = g_wnd != nullptr;
}
static uint32_t to8888(uint32_t p, uint32_t fmt) {   /* -> 0x00RRGGBB */
    uint32_t r, g, b;
    switch (fmt) {
    case 0: r = (p & 31) * 255 / 31; g = ((p >> 5) & 63) * 255 / 63; b = ((p >> 11) & 31) * 255 / 31; break;
    case 1: r = (p & 31) * 255 / 31; g = ((p >> 5) & 31) * 255 / 31; b = ((p >> 10) & 31) * 255 / 31; break;
    case 2: r = (p & 15) * 17; g = ((p >> 4) & 15) * 17; b = ((p >> 8) & 15) * 17; break;
    default: r = p & 255; g = (p >> 8) & 255; b = (p >> 16) & 255; break;
    }
    return (r << 16) | (g << 8) | b;
}
extern uint64_t g_fb_writes, g_prims;
void k_dump_threads(void);
/* Desempenho: tempo de TRABALHO por quadro (intervalo entre apresentacoes menos o Sleep do limitador).
 * - Log (sempre): a cada 600 quadros, e ao sair (janela parcial + resumo da sessao), linhas "[perf]" no console, que o
 *   diag copia para gen/logs/sessao_*.log.
 * - Overlay (F3, ou FPS_OVERLAY=1): fps real (quadros apresentados por segundo real) e trabalho medio por quadro.
 * O jogo pode renderizar a 30 fps sobre um vblank de 60 (quadros pesado/leve alternados), entao "quadro > 16,7 ms" engana:
 * simula-se o atraso acumulado contra um orcamento de 16,68 ms por quadro; "atrasados" = quadros em que o atraso passou
 * de um vblank inteiro (o que o jogador sente como lentidao). */
static double g_ov_fps, g_ov_ms;                                   /* valores mostrados no overlay */
static float g_pw[600]; static int g_pn;                           /* janela atual */
static double g_lag, g_wsum, g_wmax, g_wmaxlag; static int g_wlate;
static uint64_t g_tn, g_tlate; static double g_tsum, g_tmaxlag;    /* sessao inteira */
void raster_stats_print();
uint32_t audio_out_realigns(void);
static void perf_report(bool final) {
    { const uint32_t ra = audio_out_realigns(); if (ra) fprintf(stderr, "[perf] audio: %u falta(s) de amostras (canal atrasado) neste trecho\n", ra); }
    if (g_pn >= (final ? 30 : 600)) {
        std::sort(g_pw, g_pw + g_pn);
        fprintf(stderr, "[perf] quadros %llu-%llu: trabalho medio %.1f ms (%.0f fps possiveis), p99 %.1f ms, pior %.1f ms, atraso max %.0f ms, atrasados: %d (%.1f%%)\n",
                (unsigned long long)(g_frames - g_pn + 1), (unsigned long long)g_frames, g_wsum / g_pn, 1000.0 * g_pn / g_wsum,
                (double)g_pw[g_pn * 99 / 100], g_wmax, g_wmaxlag, g_wlate, 100.0 * g_wlate / g_pn);
    }
    g_pn = 0; g_wsum = g_wmax = g_wmaxlag = 0; g_wlate = 0;
    if (final && g_tn)
        fprintf(stderr, "[perf] sessao: %llu quadros, trabalho medio %.1f ms (%.0f fps possiveis), atraso max %.0f ms, atrasados: %llu (%.1f%%)\n",
                (unsigned long long)g_tn, g_tsum / g_tn, 1000.0 * g_tn / g_tsum, g_tmaxlag, (unsigned long long)g_tlate, 100.0 * g_tlate / g_tn);
    if (final) raster_stats_print();
    fflush(stderr);
}
static void frame_stats(void) {
    static LARGE_INTEGER freq, last, ov0; static bool init; static int ovn; static double ovsum;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!init) { QueryPerformanceFrequency(&freq); init = true; last = ov0 = now; pace_take_slept_ms(); return; }
    double ms = (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)freq.QuadPart - pace_take_slept_ms();
    last = now;
    if (ms < 0) ms = 0;
    if (g_modal_seen && g_pump_ms > 100) {   /* menu/caixa aberta ou janela arrastada: pausa pedida pelo jogador, nao lentidao */
        fprintf(stderr, "[perf] jogo pausado %.0f ms no quadro %llu (menu, caixa ou janela arrastada)\n", g_pump_ms, (unsigned long long)g_frames);
        ms = std::max(0.0, ms - g_pump_ms);
    }
    if (g_pausa_ms > 0) {   /* caixa de nota do F7/F8 (capturas.inc): o jogo esperou o jogador escrever, nao e lentidao */
        fprintf(stderr, "[perf] jogo pausado %.0f ms no quadro %llu (nota da marca F7/F8)%c", g_pausa_ms, (unsigned long long)g_frames, 10);
        ms = std::max(0.0, ms - g_pausa_ms); g_pausa_ms = 0;
    }
    if (ms > 100)   /* travadas visiveis: onde foi o tempo (o resto e emulacao + desenho) */
        fprintf(stderr, "[perf] quadro lento %llu: %.0f ms (mensagens da janela %.0f ms, controle %.0f ms%s%s)\n", (unsigned long long)g_frames, ms,
                g_pump_ms, g_pad_ms, g_modal_seen ? ", janela arrastada/redimensionada ou caixa aberta" : "", GetForegroundWindow() != g_wnd ? ", janela sem foco" : "");
    g_modal_seen = false;
    { extern void ge_res_feedback(double); ge_res_feedback(ms); }
    g_pw[g_pn++] = (float)ms; g_wsum += ms; if (ms > g_wmax) g_wmax = ms;
    g_lag = std::min(250.0, std::max(0.0, g_lag + ms - 16.68));   /* como o limitador: desiste apos 250 ms de atraso */
    pace_work(ms);   /* o mesmo atraso acumulado decide a suspensao dos intermediarios (pacing.cpp) */
    if (g_lag > g_wmaxlag) g_wmaxlag = g_lag;
    if (g_lag > g_tmaxlag) g_tmaxlag = g_lag;
    if (g_lag > 16.68) { g_wlate++; g_tlate++; }
    g_tn++; g_tsum += ms;
    ovn++; ovsum += ms;
    if (ovn >= 30) {   /* overlay: atualiza a cada 30 quadros */
        double el = (double)(now.QuadPart - ov0.QuadPart) / (double)freq.QuadPart;
        if (el > 0) g_ov_fps = ovn / el;
        g_ov_ms = ovsum / ovn; ovn = 0; ovsum = 0; ov0 = now;
    }
    if (g_pn >= 600) perf_report(false);
}
/* Profiler por amostragem (PROFILE=arquivo): uma thread suspende a thread do jogo a cada ~1 ms e guarda o RIP; ao sair grava
 * "<rip hex> <contagem>" em <arquivo>. Resolva com tools/prof.py (nm/addr2line; o runtime e compilado com -g). */
#include <map>
static HANDLE g_prof_thread; static std::map<uint64_t, uint32_t> g_prof; static volatile bool g_prof_run;
static DWORD WINAPI prof_main(LPVOID) {
    while (g_prof_run) {
        Sleep(1);
        CONTEXT cx = {}; cx.ContextFlags = CONTEXT_CONTROL;
        if (SuspendThread(g_prof_thread) == (DWORD)-1) continue;
        if (GetThreadContext(g_prof_thread, &cx)) g_prof[cx.Rip]++;
        ResumeThread(g_prof_thread);
    }
    return 0;
}
static void profiler_start(void) {
    static bool done; if (done) return;
    static const char *from = getenv("PROFILE_FROM");   /* PROFILE_FROM=n: so amostra a partir do quadro n */
    if (from && g_frames < (uint64_t)atoll(from)) return;
    done = true;
    if (!getenv("PROFILE")) return;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_prof_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    g_prof_run = true; CreateThread(nullptr, 0, prof_main, nullptr, 0, nullptr);
}
static void profiler_dump(void) {
    const char *fn = getenv("PROFILE"); if (!fn || !g_prof_run) return;
    g_prof_run = false; Sleep(5);
    /* o exe carrega com ASLR: grava enderecos rebaseados para o ImageBase do PE, que e o que nm/addr2line usam */
    uint8_t *base = (uint8_t *)GetModuleHandleA(nullptr);
    const uint64_t ib = 0x140000000ull;   /* ImageBase do PE em disco (o cabecalho em memoria ja vem atualizado pelo loader) */
    if (FILE *f = fopen(fn, "w")) { for (auto &kv : g_prof) fprintf(f, "%llx %u\n", (unsigned long long)(kv.first - (uint64_t)base + ib), kv.second); fclose(f); }
}
void diag_flush(void);
void texrepl_save_recent(void);
/* toda saida passa por aqui (janela fechada, Esc, EXIT_FRAME, o jogo pedindo sceKernelExitGame): ExitProcess nao roda os atexit da CRT */
static void quit_process(void) { perf_report(true); profiler_dump(); seq_esperar_gravacao(); texrepl_save_recent(); diag_flush(); ExitProcess(0); }
void display_quit(void) { quit_process(); }
/* fonte 5x7 minima (digitos, '.', ' ', f p s m) para o overlay */
static const uint8_t *glyph(char ch) {
    static const uint8_t G[][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2},
        {31,16,30,1,1,17,14}, {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12},
        {0,0,0,0,0,6,6}, {0,0,0,0,0,0,0}, {6,9,8,28,8,8,8}, {0,30,17,17,30,16,16}, {0,15,16,14,1,30,0}, {0,26,21,21,21,21,21}};
    if (ch >= '0' && ch <= '9') return G[ch - '0'];
    switch (ch) { case '.': return G[10]; case 'f': return G[12]; case 'p': return G[13]; case 's': return G[14]; case 'm': return G[15]; default: return G[11]; }
}
static uint32_t *g_ov_buf = g_pix; static int g_ov_w = 480, g_ov_h = 272, g_ov_n = 1;   /* alvo do overlay: quadro nativo ou em resolucao interna (escala n) */
static void draw_text(int x0, int y0, const char *t, int sc) {
    sc *= g_ov_n; x0 *= g_ov_n; y0 *= g_ov_n;
    for (; *t; t++, x0 += 6 * sc) {
        const uint8_t *g = glyph(*t);
        for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (g[r] & (16 >> c))
            for (int dy = 0; dy < sc; dy++) for (int dx = 0; dx < sc; dx++) {
                int x = x0 + c * sc + dx, y = y0 + r * sc + dy;
                if (x >= 0 && x < g_ov_w && y >= 0 && y < g_ov_h) g_ov_buf[y * g_ov_w + x] = 0x00ffffff;
            }
    }
}
static void draw_fps_overlay(void) {
    char a[24], b[24];
    snprintf(a, sizeof a, "%.1f fps", g_ov_fps); snprintf(b, sizeof b, "%.1f ms", g_ov_ms);
    int w = 8 * 12 + 6, h = 2 * 16 + 4, x0 = 2, y0 = 272 - h - 2, n = g_ov_n;
    for (int y = y0 * n; y < (y0 + h) * n; y++) for (int x = x0 * n; x < (x0 + w) * n; x++) g_ov_buf[y * g_ov_w + x] = (g_ov_buf[y * g_ov_w + x] >> 2) & 0x003f3f3f;   /* fundo escurecido */
    draw_text(x0 + 3, y0 + 3, a, 2); draw_text(x0 + 3, y0 + 19, b, 2);
}
static void toggle_fps_overlay(void) { g_show_fps = !g_show_fps; sync_menu(); }
void display_toggle_fps(void) { toggle_fps_overlay(); }
static void sync_menu(void) { menu_sync(g_fullscreen, g_show_fps, current_scale()); }
void audio_out_skip(bool on);
static int g_loading_run;   /* quadros seguidos com tela de carregamento */
const uint32_t *mpeg_hd_present(int *w, int *h, uint32_t *ver);
/* Marcas do jogador (F7/F8; capturas.inc): primeiro a caixa de nota, que pausa o jogo; grava so se confirmada. Chamado logo depois do laco de
 * mensagens, antes de qualquer decisao do quadro: o que o menu da janela fizer enquanto a caixa esta aberta (trocar o renderizador, a resolucao...)
 * acontece num ponto tao seguro quanto o proprio laco de mensagens (antes a caixa abria no meio do display_present, com o quadro da GPU ja escolhido). */
void ge_marca_texturas(int n);
static void marcas_processar(void) {
    { static const char *mq = getenv("TF_MARCA_QUADRO"); if (mq && quadro_na_lista(mq, g_frames)) marca_cpu(); }    /* testes: marca nestes quadros (como F7) */
    { static const char *mg = getenv("TF_MARCAG_QUADRO"); if (mg && quadro_na_lista(mg, g_frames)) marca_graf(); }  /* testes: F8 nestes quadros */
    if (!g_marcag_pedida && !g_marca_pedida) return;
    if (text_input_busy()) return;   /* a caixa de texto do jogo (nome do jogador) esta aberta: a marca espera ela fechar */
    const unsigned long long q = g_frames;
    if (g_marcag_pedida) {
        g_marcag_pedida = false; std::string nota;
        if (!marca_nota_modal(true, &nota)) { fprintf(stderr, "[marca-graf] descartada no quadro %llu (Esc ou nota vazia)\n", q); display_notify("Graphics mark discarded"); }
        else {
            const int n = ++g_marcag_n;
            fprintf(stderr, "[marca-graf] #%d quadro %llu (marcado pelo jogador: problema grafico)\n", n, q);
            cap_marcar(); g_burst_n = 2; g_burst_next = q + 10;   /* este quadro inteiro e mais 2, 10 e 20 quadros depois (piscadas) */
            char motivo[24]; snprintf(motivo, sizeof motivo, "F8 #%d", n); seq_gravar(motivo);   /* caixa-preta: os ~1,5 s apresentados antes do F8 */
            fprintf(stderr, "[marca-graf] #%d config: renderizador=%d resolucao=%dx msaa=%d texturas_ampliadas=%d tela=%dx%d quadro=%llu\n", n, (int)g_gfx.renderer, (int)g_gfx.res, (int)g_gfx.msaa, (int)g_gfx.texscale, g_sw, g_sh, q);
            {   /* saida: janela e o monitor onde ela esta */
                RECT cr = {}; GetClientRect(g_wnd, &cr); DEVMODEW dm = {}; dm.dmSize = sizeof dm; MONITORINFOEXW mi = {}; mi.cbSize = sizeof mi;
                if (!GetMonitorInfoW(MonitorFromWindow(g_wnd, MONITOR_DEFAULTTOPRIMARY), &mi) || !EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm);
                fprintf(stderr, "[marca-graf] #%d saida: janela %ldx%ld (imagem interna %dx%d), filtro=%d proporcao=%d escala_inteira=%d sync=%d fps60=%d, monitor %lux%lu a %lu Hz, %s\n", n, cr.right, cr.bottom,
                        g_sw, g_sh, (int)g_gfx.filter, (int)g_gfx.aspect, (int)g_gfx.integer, (int)g_gfx.sync, (int)g_gfx.fps60, dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency, pace_mid_suspended() ? "60 FPS SUSPENSO agora" : "60 FPS ativo");
            }
            ge_marca_texturas(n);
            if (!nota.empty()) marca_nota_log(n, nota, "marca-graf");
            char t[64]; snprintf(t, sizeof t, "Graphics issue #%d saved", n); display_notify(t);
        }
    }
    if (g_marca_pedida) {
        g_marca_pedida = false; std::string nota;
        if (!marca_nota_modal(false, &nota)) { fprintf(stderr, "[marca-cpu] descartada no quadro %llu (Esc ou nota vazia)\n", q); display_notify("CPU mark discarded"); }
        else {
            const int n = ++g_marca_n;
            fprintf(stderr, "[marca-cpu] #%d quadro %llu (marcado pelo jogador: erro da CPU)\n", n, q);
            cap_marcar();
            plugins_marca(n);   /* os plugins despejam o estado do duelo (CPUvsCPU: tabuleiro; IAAprimorada: ultimas decisoes) */
            if (!nota.empty()) marca_nota_log(n, nota, "marca-cpu");
            char t[64]; snprintf(t, sizeof t, "CPU mistake #%d saved", n); display_notify(t);
        }
    }
}
void display_present(void) {
    static const bool ge_stats = getenv("GE_STATS") != nullptr, no_pace = getenv("NO_PACE") != nullptr, load_dbg = getenv("TF_LOAD_DEBUG") != nullptr;
    if (ge_stats && (g_frames % 60) == 0) fprintf(stderr, "[stats] frame %llu prims=%llu fbwrites=%llu fb=%08x fmt=%u stride=%u", (unsigned long long)g_frames, (unsigned long long)g_prims, (unsigned long long)g_fb_writes, g_fb_addr, g_fb_fmt, g_fb_stride), fputc(10, stderr);
    if (!g_ready) return;
    LARGE_INTEGER q0, q1, q2, qf; QueryPerformanceCounter(&q0);
    /* a saida da chamada (a hora em que o jogo e o audio voltam a rodar) e regulada por pace_exit */
    bool call_timed = false;
    struct ExitMark { bool &timed; ~ExitMark() { pace_exit(timed); } } exit_mark{call_timed};
    MSG msg; while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    {   /* esconde o cursor depois de 5 s sem mexer o mouse (so com a janela do jogo ativa e o cursor sobre a area do jogo) */
        static POINT last; static ULONGLONG moved = GetTickCount64(); POINT pt; GetCursorPos(&pt);
        if (pt.x != last.x || pt.y != last.y) { last = pt; moved = GetTickCount64(); g_cursor_hidden = false; }
        else if (!g_cursor_hidden && GetTickCount64() - moved >= 5000 && GetForegroundWindow() == g_wnd) {
            RECT cr; GetClientRect(g_wnd, &cr); POINT cp = pt; ScreenToClient(g_wnd, &cp);
            if (PtInRect(&cr, cp)) { g_cursor_hidden = true; SetCursor(nullptr); }
        }
    }
    QueryPerformanceCounter(&q1);
    g_frames++;
    { extern volatile uint64_t g_diag_frames; g_diag_frames = g_frames; }
    if (g_notify_until && g_frames >= g_notify_until) { g_notify_until = 0; SetWindowTextA(g_wnd, win_title()); }
    { static const char *ef = getenv("EXIT_FRAME"); if (ef && g_frames >= (uint64_t)atoll(ef)) quit_process(); }   /* testes: sai no quadro n */
    { static const bool f60 = getenv("TF_FPS60") != nullptr;   /* teste: motor do jogo em 60 FPS (g_fps_alvo em 08903908; 30 = limitador de 2 vblanks) */
      if (f60 && *(uint32_t *)mem_ptr(0x08903908) == 30) *(uint32_t *)mem_ptr(0x08903908) = 60; }
    QueryPerformanceCounter(&q2); QueryPerformanceFrequency(&qf);
    g_pump_ms = (q1.QuadPart - q0.QuadPart) * 1000.0 / qf.QuadPart; g_pad_ms = (q2.QuadPart - q1.QuadPart) * 1000.0 / qf.QuadPart;
    { static const char *md = getenv("MEM_DUMP");   /* MEM_DUMP="q1,q2": grava a RAM do PSP (0x08800000, 24 MB) em gen/mem_<q>.bin (achar variaveis comparando dois quadros) */
      if (md && quadro_na_lista(md, g_frames)) { char fn[64]; snprintf(fn, sizeof fn, "gen/mem_%llu.bin", (unsigned long long)g_frames);
          if (FILE *f = fopen(fn, "wb")) { fwrite(mem_ptr(0x08800000), 1, 0x1800000, f); fclose(f); } } }
    { static const char *pk = getenv("TF_POKE");   /* TF_POKE="quadro:endereco_hex:valor,...": escreve um byte na memoria do PSP (experimentos de achar variaveis) */
      if (pk) for (const char *q = pk; *q;) { char *e; unsigned long long fr = strtoull(q, &e, 10); unsigned long ad = strtoul(e + 1, &e, 16), v = strtoul(e + 1, &e, 10);
          if (fr == g_frames) *(uint8_t *)mem_ptr((uint32_t)ad) = (uint8_t)v; q = *e == ',' ? e + 1 : e; if (e == q && *q) break; } }
    { static const char *df = getenv("DUMP_FRAME"); if (df && g_frames == (uint64_t)atoi(df)) k_dump_threads(); }
    profiler_start();
    frame_stats();
    marcas_processar();   /* F7/F8: caixa de nota (pausa o jogo) e, se confirmada, a marca; a pausa sai das estatisticas do quadro seguinte (g_pausa_ms) */
    static bool ff_off = getenv("TF_NO_LOADING_FF") != nullptr;
    audio_out_skip(g_loading_run >= 12 && !ff_off);
    /* quadro real pendente do modo 'apresentar antes' (ver abaixo): sai 1/60 s depois do intermediario */
    bool real_flushed = false;
    { int fb; int64_t due, start;
      if (pace_real_take(&fb, &due, &start) && gpu_mode() && fb >= 0) {
          pace_wait_until(due); gpu_present_fb(fb); real_flushed = true;
          pace_stat(PACE_STAT_REAL, pace_ms(pace_now() - start)); } }
    const bool gpu = gpu_mode(); int gfb = -1;
    if (gpu && g_fb_addr) gfb = ge_gpu_frame(g_fb_addr, g_fb_fmt);   /* renderizador por GPU: id do quadro sombra na GPU, ou -1 (quadro nativo) */
    /* Latencia do 60 FPS: o quadro novo ja esta pronto (o jogo termina em poucos ms) mas era segurado ate o vblank (~13 ms de espera) e o real
     * ainda um vblank depois. Com quadro novo + intermediario, o intermediario sai logo que a "fase" do jogo permite (ultima saida + phi, o tempo de
     * trabalho tipico) e o real 1/60 s depois; a grade de vblanks so avanca. O teclado e lido DEPOIS da espera (antes o jogo via o controle de ~13 ms atras). */
    const bool pace_on = !no_pace && !(g_loading_run >= 12 && !ff_off);
    {   /* sincronia com o monitor (pacing.cpp): taxa do monitor onde a janela esta (a cada ~1 s; ela pode mudar de monitor) */
        static int hz; static uint64_t next;
        if (g_frames >= next) {
            next = g_frames + 60; hz = 0;
            MONITORINFOEXW mi = {}; mi.cbSize = sizeof mi; DEVMODEW dm = {}; dm.dmSize = sizeof dm;
            if (GetMonitorInfoW(MonitorFromWindow(g_wnd, MONITOR_DEFAULTTOPRIMARY), &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) hz = (int)dm.dmDisplayFrequency;
        }
        pace_monitor(hz, gpu_mode() && pace_on && g_gfx.sync != 2);   /* menor latencia (IMMEDIATE) fica sem: a fase so moveria o rasgo para o meio da tela */
    }
    static const bool no_early = getenv("TF_NO_EARLY") != nullptr;   /* A/B: sem apresentar antes da grade de vblanks */
    const bool mid_ok = pace_mid_allowed();
    { static uint32_t sn; const uint32_t n = pace_susp_n(); if (n != sn) { sn = n; fprintf(stderr, "[perf] 60 FPS suspenso no quadro %llu (saidas atrasadas em mais da metade dos quadros; %u vez(es) na sessao): ~2,5 s a 30 quadros reais por segundo%c", (unsigned long long)g_frames, n, 10); } }
    const bool early = !no_early && pace_on && pace_have_exit() && gfb >= 0 && interp_mid_ready(gfb, g_frames, mid_ok);
    const int64_t work_start = pace_last_exit(); int64_t mid_target = 0;
    if (early) { mid_target = pace_mid_target(q0.QuadPart); pace_grid(false); }   /* a espera fica para logo antes de apresentar */
    else if (pace_on) pace_grid(true);
    call_timed = pace_on;
    latch_buttons();
    if (!g_fb_addr) return;
    /* sem raster nativo o quadro nativo da VRAM esta invalido (e protegido): a deteccao da tela de carregamento usa a reducao da GPU */
    const bool nonative = gpu && ge_gpu_skip_active(g_fb_addr);
    if (!nonative) for (int y = 0; y < 272; y++) for (int x = 0; x < 480; x++) {
        uint32_t off = g_fb_addr + (y * g_fb_stride + x) * (g_fb_fmt == 3 ? 4 : 2);
        uint32_t p = g_fb_fmt == 3 ? *(uint32_t *)mem_ptr(off) : *(uint16_t *)mem_ptr(off);
        g_pix[y * 480 + x] = to8888(p, g_fb_fmt);
    }
    /* Tela de carregamento do jogo (fundo preto + Ojama amarelo e disco no canto inferior direito; no PSP indicava leitura do
     * UMD, que nao existe aqui): apos 12 quadros assim, o icone e apagado e o limitador de quadros desligado ate acabar. */
    {
        int outside = 0, inside = 0; const uint32_t *small = nonative ? ge_gpu_small() : nullptr;
        if (!nonative || small) for (int y = 0; y < 272; y += 2) for (int x = 0; x < 480; x += 2) {
            uint32_t p = nonative ? small[y * 480 + x] : g_pix[y * 480 + x];
            uint32_t r = nonative ? (p & 255) : ((p >> 16) & 255), g = (p >> 8) & 255, b = nonative ? ((p >> 16) & 255) : (p & 255);
            if (r > 24 || g > 24 || b > 24) { if (x >= 360 && y >= 150) inside++; else outside++; }
        }
        if (load_dbg && outside + inside < 4000 && (outside + inside) > 0) fprintf(stderr, "[load] quadro %llu fora %d dentro %d\n", (unsigned long long)g_frames, outside, inside);
        if (nonative && !small) g_loading_run = 0;
        else if (outside == 0 && inside < 1500) g_loading_run++; else g_loading_run = 0;
        if (g_loading_run >= 12 && !nonative && !ff_off) memset(g_pix, 0, sizeof g_pix);
    }
    g_src = g_pix; g_sw = 480; g_sh = 272;
    if (!gpu) { int hw, hh; uint32_t hs;   /* resolucao interna maior: usa o quadro sombra do GE se ele combina com o nativo */
      if (const uint32_t *hi = ge_shadow_get(g_fb_addr, g_fb_fmt, &hw, &hh, &hs)) {
          g_hi.resize((size_t)hw * hh);
          for (int y = 0; y < hh; y++) { const uint32_t *r = hi + (size_t)y * hs; uint32_t *o = &g_hi[(size_t)y * hw]; for (int x = 0; x < hw; x++) { uint32_t p = r[x]; o[x] = ((p & 255) << 16) | (p & 0xff00) | ((p >> 16) & 255); } }
          g_src = g_hi.data(); g_sw = hw; g_sh = hh;
      } }
    if (gpu) {   /* overlay num quadro 480x272 proprio (caixa translucida + texto), composto pela GPU */
        /* a imagem do overlay (1920x1088, 8 MB) so vai para a GPU quando muda: o texto do fps muda a cada 30 quadros e a caixa dos mods
         * quando o texto dela muda (antes era reenviada a cada quadro enquanto aparecia) */
        static uint32_t ov[480 * 272], ovr[480 * 272]; static std::vector<uint32_t> ov4, comb; static uint64_t sent_key, sent_gen = ~0ull, fps_built = ~0ull;
        const uint32_t *mb = modbox_pixels(g_frames), *src = nullptr; uint64_t key = 0, fkey = 0;
        if (g_show_fps) {
            fkey = (1ull << 63) | ((uint64_t)(uint32_t)(g_ov_fps * 10.0) << 24) | (uint32_t)(g_ov_ms * 10.0);
            if (fkey != fps_built || ov4.empty()) {
                fps_built = fkey;
                memset(ov, 0, sizeof ov); g_ov_buf = ov; g_ov_w = 480; g_ov_h = 272; g_ov_n = 1; draw_fps_overlay();
                for (int y = 0; y < 272; y++) for (int x = 0; x < 480; x++) {
                    uint32_t p = ov[y * 480 + x]; bool box = x >= 2 && x < 2 + 102 && y >= 272 - 36 - 2 && y < 272 - 2;
                    ovr[y * 480 + x] = p ? (((p & 255) << 16) | (p & 0xff00) | ((p >> 16) & 255) | 0xff000000u) : box ? 0xc0000000u : 0u;
                }
                ov4.resize((size_t)GPU_OVW * GPU_OVH);   /* o overlay da GPU e 4x: amplia o quadro de fps */
                for (int y = 0; y < GPU_OVH; y++) for (int x = 0; x < GPU_OVW; x++) ov4[(size_t)y * GPU_OVW + x] = ovr[(y >> 2) * 480 + (x >> 2)];
            }
        }
        if (mb && g_show_fps) {   /* caixa/paineis dos mods + fps: o fps por cima */
            key = (1ull << 61) ^ (modbox_version() << 32) ^ fkey;
            if (key != sent_key) { comb.assign(mb, mb + (size_t)GPU_OVW * GPU_OVH); for (size_t i = 0; i < comb.size(); i++) if (ov4[i] >> 24) comb[i] = ov4[i]; }
            src = comb.data();
        } else if (mb) { key = (1ull << 62) | modbox_version(); src = mb; }
        else if (g_show_fps) { key = fkey; src = ov4.data(); }
        if (key != sent_key || g_gpu_gen != sent_gen) { gpu_set_overlay(src); sent_key = key; sent_gen = g_gpu_gen; }   /* backend novo: reenvia */
    } else {
        g_ov_buf = g_hi.empty() || g_src == g_pix ? g_pix : g_hi.data(); g_ov_w = g_sw; g_ov_h = g_sh; g_ov_n = g_sw / 480;
        if (g_show_fps) draw_fps_overlay();
    }
    static const char *snap = getenv("SNAP_FRAME");
    if (g_burst_n && g_frames >= g_burst_next) { g_burst_n--; g_burst_next = g_frames + 10; g_cap_marca = true; }   /* F8: mais 2 quadros, para pegar piscadas */
    seq_quadro();   /* TF_SEQ_FRAME (replays): grava a caixa-preta dos quadros apresentados */
    const char *cap_tag = cap_tag_devida(g_frames);   /* modo de diagnostico grafico: nullptr = nada a gravar neste quadro */
    const bool snap_want = snap && quadro_na_lista(snap, g_frames);
    const bool want = snap_want || cap_tag;
    if (want) {   /* depuracao: SNAP_FRAME="n1,n2,..." grava gen/snap_<n>.bmp */
        int hw2, hh2; const uint32_t *hd2 = gpu ? mpeg_hd_present(&hw2, &hh2, nullptr) : nullptr;
        if (hd2) { g_src = hd2; g_sw = hw2; g_sh = hh2; }   /* video substituto HD em andamento: grava o quadro HD */
        else if (gpu && gfb >= 0) {   /* a imagem esta na GPU: le de volta (ABGR -> 0x00RRGGBB) */
            std::vector<uint32_t> tmp; int hw, hh;
            if (gpu_fb_readback(gfb, tmp, &hw, &hh)) {
                g_hi.resize(tmp.size());
                for (size_t i = 0; i < tmp.size(); i++) { uint32_t p = tmp[i]; g_hi[i] = ((p & 255) << 16) | (p & 0xff00) | ((p >> 16) & 255); }
                g_src = g_hi.data(); g_sw = hw; g_sh = hh;
            }
        }
        if (const uint32_t *mb = gpu ? modbox_pixels(g_frames) : nullptr) {   /* a captura vem antes do overlay da GPU: compoe a caixa dos mods */
            if (g_src != g_hi.data()) { g_hi.assign(g_src, g_src + (size_t)g_sw * g_sh); g_src = g_hi.data(); }
            for (int y = 0; y < g_sh; y++) for (int x = 0; x < g_sw; x++) {
                uint32_t o = mb[(size_t)(y * GPU_OVH / g_sh) * GPU_OVW + (x * GPU_OVW / g_sw)], a = o >> 24; if (!a) continue;
                uint32_t &d = g_hi[(size_t)y * g_sw + x], s[3] = {o & 255, (o >> 8) & 255, (o >> 16) & 255}, r = 0;   /* s = R, G, B */
                for (int k = 0; k < 3; k++) { uint32_t dv = (d >> (16 - 8 * k)) & 255; r |= ((s[k] * a + dv * (255 - a)) / 255) << (16 - 8 * k); }
                d = r;
            }
        }
        if (cap_tag) cap_salvar(cap_tag, g_src, g_sw, g_sh);
        if (snap_want) {
        char fn[64]; snprintf(fn, sizeof fn, "gen/snap_%llu.bmp", (unsigned long long)g_frames);
        const bool saved = write_bmp24(fn, g_sw, g_sh, [&](int y, uint8_t *row) {
            const uint32_t *r = g_src + (size_t)y * g_sw;
            for (int x = 0; x < g_sw; x++) { row[x * 3] = (uint8_t)r[x]; row[x * 3 + 1] = (uint8_t)(r[x] >> 8); row[x * 3 + 2] = (uint8_t)(r[x] >> 16); }
        });
        if (saved) fprintf(stderr, "[snap] quadro %llu gravado\n", (unsigned long long)g_frames);
        /* VRAM_DUMP="endereco,stride,w,h" (hex,dec,dec,dec; 8888): grava gen/vram_<n>.bmp (ex.: alvo de render-to-texture) */
        if (const char *vd = getenv("VRAM_DUMP")) {
            unsigned long a = 0, st = 0, w = 0, h = 0;
            if (sscanf(vd, "%lx,%lu,%lu,%lu", &a, &st, &w, &h) == 4 && w && h && w <= 1024 && h <= 1024) {
                char fn2[64]; snprintf(fn2, sizeof fn2, "gen/vram_%llu.bmp", (unsigned long long)g_frames);
                if (FILE *f2 = fopen(fn2, "wb")) {
                    uint32_t sz = 54 + w * h * 4; uint8_t fh[14] = {'B', 'M'}; memcpy(fh + 2, &sz, 4); fh[10] = 54;
                    int32_t ih[10] = {40, (int32_t)w, -(int32_t)h, 0x00200001, 0, (int32_t)(w * h * 4), 0, 0, 0, 0};
                    fwrite(fh, 1, 14, f2); fwrite(ih, 4, 10, f2);
                    for (unsigned long y = 0; y < h; y++) for (unsigned long x = 0; x < w; x++) {
                        uint32_t p = *(uint32_t *)mem_ptr((uint32_t)(a + (y * st + x) * 4));
                        uint8_t px[4] = {(uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p, (uint8_t)(p >> 24)};
                        fwrite(px, 1, 4, f2);
                    }
                    fclose(f2);
                }
            }
        }
        }   /* fim de snap_want */
    }
    if (gpu) { int hw, hh; uint32_t hv; static uint32_t hlast; static bool hdon;
      if (const uint32_t *hd = mpeg_hd_present(&hw, &hh, &hv)) {   /* filme com video substituto HD (mod): so reenvia a imagem (8 MB) quando o quadro muda */
          if (hv != hlast || !hdon) { gpu_present_pixels(hd, hw, hh); hlast = hv; hdon = true; } else gpu_present_last();
          return; }
      hdon = false;
      if (gfb >= 0) {
          if (early) pace_wait_until(mid_target);
          const bool skip_repeat = real_flushed && !interp_has_new(gfb);   /* o real ja saiu no inicio desta chamada e nao ha quadro novo: nao repete (era um segundo passe de escala 4K + espera da GPU) */
          const int64_t t0 = pace_now();
          const bool shown = interp_present(gfb, g_frames, mid_ok && !(g_loading_run >= 12 && !ff_off), g_gfx.res);
          pace_stat(PACE_STAT_PRESENT, pace_ms(pace_now() - t0));
          if (shown && early) pace_real_arm(gfb, work_start);   /* o real sai 1/60 s depois, no inicio da proxima chamada (a grade de saidas do jogo continua regular: o audio depende dela) */
          else if (!shown && !skip_repeat) gpu_present_fb(gfb);
          if (early) latch_buttons();   /* entrada o mais fresca possivel para quando o jogo voltar a rodar */
      } else gpu_present_pixels(g_pix, 480, 272);
      return; }
    HDC dc = GetDC(g_wnd); paint_frame(dc); ReleaseDC(g_wnd, dc);
}

/* Teclado -> botoes do PSP. Setas = direcional, Z = X(cross), X = O(circle), A = quadrado, S = triangulo, Q/W = L/R, Enter = Start, Shift = Select.
 * O teclado e lido UMA vez por quadro (display_present) e o valor fica travado durante o quadro: assim a gravacao de
 * entradas (diag) reproduz exatamente a sessao. */
void diag_record_buttons(uint64_t frame, uint32_t mask);
static uint32_t g_latched;
/* Controle XInput (Xbox e compativeis), carregado dinamicamente: A = X(cross), B = O, X = quadrado, Y = triangulo,
 * LB/RB = L/R, Start/Back = START/SELECT, direcional ou analogico esquerdo = direcional do PSP. */
struct XPad { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct XState { DWORD packet; XPad pad; };
typedef DWORD (WINAPI *XGetState)(DWORD, XState *);
static uint32_t read_pad(void) {
    static XGetState get; static bool tried;
    if (!tried) {
        tried = true;
        const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
        for (const char *d : dlls) if (HMODULE m = LoadLibraryA(d)) { get = (XGetState)(void *)GetProcAddress(m, "XInputGetState"); if (get) break; }
    }
    if (!get) return 0;
    /* XInputGetState numa porta sem controle e lenta (ate ~1 ms): portas vazias so sao consultadas de novo a cada ~2 s */
    static uint64_t retry[4];
    uint32_t b = 0;
    for (DWORD i = 0; i < 4; i++) {
        if (g_frames < retry[i]) continue;
        XState s = {}; if (get(i, &s) != 0) { retry[i] = g_frames + 120; continue; }
        WORD x = s.pad.buttons;
        { static bool l3r3; const bool ambos = (x & 0x00C0) == 0x00C0; if (ambos && !l3r3) marca_cpu(); l3r3 = ambos; }   /* L3 + R3 = "a CPU errou" */
        if (x & 0x0001) b |= 0x10;   /* cima */
        if (x & 0x0002) b |= 0x40;   /* baixo */
        if (x & 0x0004) b |= 0x80;   /* esquerda */
        if (x & 0x0008) b |= 0x20;   /* direita */
        if (x & 0x0010) b |= 0x8;    /* Start */
        if (x & 0x0020) b |= 0x1;    /* Back = Select */
        if (x & 0x0100) b |= 0x100;  /* LB = L */
        if (x & 0x0200) b |= 0x200;  /* RB = R */
        if (x & 0x1000) b |= 0x4000; /* A = X(cross) */
        if (x & 0x2000) b |= 0x2000; /* B = O */
        if (x & 0x4000) b |= 0x8000; /* X = quadrado */
        if (x & 0x8000) b |= 0x1000; /* Y = triangulo */
        const int dz = 16000;
        if (s.pad.ly > dz) b |= 0x10;
        if (s.pad.ly < -dz) b |= 0x40;
        if (s.pad.lx < -dz) b |= 0x80;
        if (s.pad.lx > dz) b |= 0x20;
        if (s.pad.lt > 100) b |= 0x100;
        if (s.pad.rt > 100) b |= 0x200;
    }
    return b;
}
static uint32_t read_keys(void);
static uint32_t read_keyboard(void) {
    if (!g_ready || GetForegroundWindow() != g_wnd) return 0;
    uint32_t keys = read_keys();
    if (g_keys_held_after_menu) { if (keys) keys = 0; else g_keys_held_after_menu = false; }
    return keys | read_pad();
}
/* Esquema de PC (menu Exibir > Teclado e mouse de PC; ligado por padrao): WASD = direcional; Espaco/E... ver read_keys_pc. A roda do mouse vira
 * cima/baixo em pulsos de 2 quadros (uma entalhe = um passo na lista). */
static int g_wheel_ph, g_wheel_dir;
static int pc_input_cfg = -1;
bool display_pc_input(void) {
    if (pc_input_cfg < 0) pc_input_cfg = GetPrivateProfileIntW(L"controles", L"teclado_mouse_pc", 1, app_ini_path().c_str()) != 0;
    return pc_input_cfg != 0;
}
void display_set_pc_input(bool on) {
    pc_input_cfg = on; WritePrivateProfileStringW(L"controles", L"teclado_mouse_pc", on ? L"1" : L"0", app_ini_path().c_str());
}
/* Mouse nos menus (mouse_ui.cpp): EXPERIMENTAL, desligado por padrao */
static int mouse_menus_cfg = -1;
bool display_mouse_menus(void) {
    if (mouse_menus_cfg < 0) mouse_menus_cfg = (GetPrivateProfileIntW(L"controles", L"mouse_nos_menus", 0, app_ini_path().c_str()) != 0 || getenv("TF_MOUSE_AT") != nullptr) && display_pc_input();
    return mouse_menus_cfg != 0;
}
void display_set_mouse_menus(bool on) {
    mouse_menus_cfg = on; WritePrivateProfileStringW(L"controles", L"mouse_nos_menus", on ? L"1" : L"0", app_ini_path().c_str());
}
static uint32_t wheel_bits(void) {
    if (g_wheel_ph == 0 && g_wheel_pend) { g_wheel_dir = g_wheel_pend > 0 ? 1 : -1; g_wheel_pend -= g_wheel_dir; g_wheel_ph = 4; }
    uint32_t b = 0;
    if (g_wheel_ph > 2) b = g_wheel_dir > 0 ? 0x10 : 0x40;
    if (g_wheel_ph) g_wheel_ph--;
    return b;
}
/* Teclado + mouse de PC: WASD/setas = direcional; Espaco, E ou clique esquerdo = X(cross); Backspace, C ou clique direito = O(circle);
 * F = quadrado; R ou clique do meio = triangulo; Q = L, Tab = R (ou botoes laterais do mouse); Enter = START; Shift = correr (O segurado); Ctrl = SELECT.
 * Z e X continuam valendo como X(cross) e O(circle). */
static uint32_t read_keys_pc(void) {
    auto k = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    uint32_t b = 0;
    if (k(VK_UP) || k('W')) b |= 0x10;
    if (k(VK_RIGHT) || k('D')) b |= 0x20;
    if (k(VK_DOWN) || k('S')) b |= 0x40;
    if (k(VK_LEFT) || k('A')) b |= 0x80;
    if (k(VK_SPACE) || k('E') || k('Z')) b |= 0x4000;
    if (k(VK_BACK) || k('X') || k('C')) b |= 0x2000;
    if (k('F')) b |= 0x8000;
    if (k('R')) b |= 0x1000;
    if (k('Q')) b |= 0x100;
    if (k(VK_TAB)) b |= 0x200;
    if (k(VK_RETURN)) b |= 0x8;
    if (k(VK_SHIFT)) b |= 0x2000;   /* Shift = correr (no campo o O/circle segurado) */
    if (k(VK_CONTROL)) b |= 0x1;
    POINT p; RECT cr; GetCursorPos(&p); GetClientRect(g_wnd, &cr);
    const bool in_client = ScreenToClient(g_wnd, &p) && PtInRect(&cr, p);   /* cliques so contam sobre a area do jogo (a barra de menu e as bordas ficam do Windows) */
    bool eat_cross = false; static bool l_prev;
    {   /* mouse nos menus (mouse_ui.cpp): leva o mouse a coordenadas 480x272 e deixa o modulo apertar direcional/X ate a barra sob o mouse */
        int ix, iy, iw, ih, mx = 0, my = 0; bool inside = false;
        if (in_client) { gfx_dest_rect(cr.right, cr.bottom, &ix, &iy, &iw, &ih); if (iw > 0 && ih > 0 && p.x >= ix && p.x < ix + iw && p.y >= iy && p.y < iy + ih) { inside = true; mx = (p.x - ix) * 480 / iw; my = (p.y - iy) * 272 / ih; } }
        const bool l_down = in_client && k(VK_LBUTTON);
        if (display_mouse_menus()) b |= mui_update(g_frames, inside, mx, my, l_down && !l_prev, l_down, b & 0xf0, &eat_cross);
        l_prev = l_down;
    }
    if (in_client) {
        if (k(VK_LBUTTON) && !eat_cross) b |= 0x4000;
        if (k(VK_RBUTTON)) b |= 0x2000;
        if (k(VK_MBUTTON)) b |= 0x1000;
        if (k(VK_XBUTTON1)) b |= 0x100;
        if (k(VK_XBUTTON2)) b |= 0x200;
    }
    return b | wheel_bits();
}
static uint32_t read_keys(void) {
    if (display_pc_input()) return read_keys_pc();
    auto k = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    uint32_t b = 0;
    if (k(VK_UP)) b |= 0x10;
    if (k(VK_RIGHT)) b |= 0x20;
    if (k(VK_DOWN)) b |= 0x40;
    if (k(VK_LEFT)) b |= 0x80;
    if (k('Z')) b |= 0x4000;
    if (k('X')) b |= 0x2000;
    if (k('A')) b |= 0x8000;
    if (k('S')) b |= 0x1000;
    if (k('Q')) b |= 0x100;
    if (k('W')) b |= 0x200;
    if (k(VK_RETURN)) b |= 0x8;
    if (k(VK_SHIFT)) b |= 0x1;
    return b;
}
/* AUTO_PRESS="quadro:mascara,..." (hex) ou AUTO_PRESS_FILE=<arquivo .inputs gravado>: roteiro no lugar do teclado */
static std::vector<std::pair<uint64_t, uint32_t>> g_script; static bool g_scripted;
static void load_script(void) {
    static bool done; if (done) return; done = true;
    std::string txt;
    if (const char *f = getenv("AUTO_PRESS_FILE")) { if (FILE *fp = fopen(f, "rb")) { int ch; while ((ch = fgetc(fp)) != EOF) txt += (char)ch; fclose(fp); } }
    else if (const char *a = getenv("AUTO_PRESS")) txt = a;
    else return;
    g_scripted = true;
    const char *q = txt.c_str();
    while (*q) {
        char *e; unsigned long long fr = strtoull(q, &e, 10); if (*e != ':') break;
        unsigned long m = strtoul(e + 1, &e, 16);
        g_script.push_back({fr, (uint32_t)m});
        while (*e == ',' || *e == ' ' || *e == 10 || *e == 13) e++;
        q = e;
    }
}
static void latch_buttons(void) {   /* chamado a cada quadro */
    load_script();
    if (g_scripted) {
        static size_t i; static uint32_t smask; while (i < g_script.size() && g_script[i].first <= g_frames) smask = g_script[i++].second;
        g_latched = smask;
        /* TF_MOUSE_AT="quadro:x:y:botao,...": mouse simulado (x,y em 480x272; botao 1 = esquerdo apertado) para testar o mouse nos menus sem tocar no do jogador */
        static const char *ma = getenv("TF_MOUSE_AT"); static int mx = -1, my = -1, ml; static const char *mp = ma; static bool prev;
        if (ma) {
            while (mp && *mp) { char *e; unsigned long long q = strtoull(mp, &e, 10); if (q > g_frames || *e != ':') break;
                mx = (int)strtol(e + 1, &e, 10); my = (int)strtol(e + 1, &e, 10); ml = (int)strtol(e + 1, &e, 10); mp = *e == ',' ? e + 1 : e; }
            bool eat; uint32_t held; const uint32_t scr = smask;
            held = mui_update(g_frames, mx >= 0, mx, my, ml && !prev, ml != 0, scr & 0xf0, &eat); prev = ml != 0;
            g_latched = scr | held | (ml && !eat ? 0x4000 : 0);
        }
        return;
    }
    uint32_t b = read_keyboard();
    if (b != g_latched) { g_latched = b; diag_record_buttons(g_frames, b); }
}
uint32_t display_buttons(void) { return g_latched; }
