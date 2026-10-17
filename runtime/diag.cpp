/* Diagnostico de sessoes jogadas pelo usuario (para reproduzir problemas depois):
 *  - tudo o que vai para stderr tambem vai para gen/logs/<sessao>.log (pipe + thread "tee");
 *  - as teclas por quadro vao para gen/logs/<sessao>.inputs (formato do AUTO_PRESS: "quadro:mascara,");
 *  - o save (ms0/) do inicio da sessao e copiado para gen/logs/<sessao>_ms0/ (a reproducao parte dele);
 *  - watchdog: se nenhum quadro for apresentado por 5 s, grava onde o jogo esta preso (funcoes, registradores, threads).
 * Reproduzir: copie <sessao>_ms0 para ./ms0 e rode com AUTO_PRESS_FILE=gen/logs/<sessao>.inputs e AUTO_TEXT="nome1|nome2". */
#include "hle.h"
#include "util.h"
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <string>
#include <filesystem>

static FILE *g_logf, *g_inputs;
static HANDLE g_console = INVALID_HANDLE_VALUE;
static std::wstring g_dir, g_session;
volatile uint64_t g_diag_frames;          /* atualizado pelo display a cada quadro */
volatile int g_diag_paused;                /* >0 enquanto uma janela nativa (texto) espera o usuario */

static DWORD WINAPI tee_thread(void *h) {
    char buf[4096]; DWORD n;
    while (ReadFile((HANDLE)h, buf, sizeof buf, &n, nullptr) && n) {
        DWORD w; if (g_console != INVALID_HANDLE_VALUE) WriteFile(g_console, buf, n, &w, nullptr);
        if (g_logf) { fwrite(buf, 1, n, g_logf); fflush(g_logf); }
    }
    return 0;
}
/* Ao sair: fecha a ponta de escrita do pipe e espera o tee gravar o que falta (antes o ExitProcess cortava a ultima linha
 * do log, ex.: "[perf] sessao: ... (448 f"). Espera no maximo 1 s. */
static HANDLE g_tee; static int g_efd = 2;
void diag_flush(void) {
    fflush(stderr);
    if (!g_tee) return;
    const HANDLE tee = g_tee; g_tee = nullptr;   /* uma vez so: fechar o descritor de novo (fim normal + erro fatal) cairia no handler de parametro invalido do CRT */
    _close(g_efd);
    WaitForSingleObject(tee, 1000);
    if (g_logf) fflush(g_logf);
}
void k_dump_threads(void);
CPU *k_current_cpu(void);
extern "C" { extern uint32_t g_trace[1024]; extern uint32_t g_trace_i; }
static DWORD WINAPI watchdog_thread(void *) {
    uint64_t last = g_diag_frames; DWORD since = GetTickCount(); bool reported = false;
    for (;;) {
        Sleep(500);
        uint64_t f = g_diag_frames;
        if (f != last || g_diag_paused) { last = f; since = GetTickCount(); reported = false; continue; }
        if (!reported && GetTickCount() - since > 5000) {
            reported = true;
            fprintf(stderr, "\n[watchdog] nenhum quadro apresentado ha 5 s (quadro %llu, tempo virtual %llu us). Ultimas funcoes:\n",
                    (unsigned long long)f, (unsigned long long)k_now_us());
            for (uint32_t i = 0; i < 64; i++) fprintf(stderr, " %08x%s", g_trace[(g_trace_i - 64 + i) & 1023], (i % 16) == 15 ? "\n" : "");
            if (CPU *c = k_current_cpu()) {
                fprintf(stderr, "cpu atual:");
                for (int i = 1; i < 32; i++) fprintf(stderr, "%s r%d=%08x", (i % 8) ? "" : "\n    ", i, c->r[i]);
                fputc(10, stderr);
            }
            k_dump_threads();
        }
    }
}

std::wstring diag_session_prefix(void) { return g_dir + g_session; }   /* <pasta dos logs>/<sessao> (capturas de diagnostico: runtime/capturas.inc) */
void diag_init(void) {
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t name[64]; swprintf(name, 64, L"sessao_%04u%02u%02u_%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    g_dir = exe_dir() + L"logs\\"; g_session = name;
    if (const wchar_t *d = _wgetenv(L"TF_LOG_DIR")) { g_dir = d; if (!g_dir.empty() && g_dir.back() != L'\\' && g_dir.back() != L'/') g_dir += L"\\"; }   /* testes: logs na pasta do teste (gen/logs fica so com as sessoes do usuario) */
    CreateDirectoryW(g_dir.c_str(), nullptr);
    g_logf = _wfopen((g_dir + g_session + L".log").c_str(), L"wb");
    /* stderr -> pipe -> (console + arquivo) */
    g_console = GetStdHandle(STD_ERROR_HANDLE);
    /* executavel de janela aberto por duplo clique: sem handles padrao o stderr do CRT fica invalido e nada chegava ao log (sessoes vazias) */
    if (!g_console || g_console == INVALID_HANDLE_VALUE) g_console = INVALID_HANDLE_VALUE;
    if (_fileno(stderr) < 0) freopen("NUL", "wb", stderr);   /* aplicativo de janela: o CRT deixa stderr/stdout sem descritor (fileno -2) */
    if (_fileno(stdout) < 0) freopen("NUL", "wb", stdout);
    HANDLE rd, wr;
    if (g_logf && CreatePipe(&rd, &wr, nullptr, 1 << 16)) {
        int fd = _open_osfhandle((intptr_t)wr, _O_WRONLY | _O_BINARY);
        g_efd = _fileno(stderr);   /* normalmente 2; num aplicativo de janela reaberto em NUL pode ser outro */
        if (fd >= 0 && _dup2(fd, g_efd) == 0) {
            setvbuf(stderr, nullptr, _IONBF, 0); g_tee = CreateThread(nullptr, 0, tee_thread, rd, 0, nullptr);
            if (fd != g_efd) _close(fd);   /* so o fd 2 fica com a ponta de escrita: fecha-lo encerra o tee (diag_flush) */
        }
    }
    int rc0 = fprintf(stderr, "[diag] sessao %ls (log, entradas e copia do save em %ls)\n", g_session.c_str(), g_dir.c_str());
    if (getenv("TF_DIAG_SELFTEST")) { FILE *tf = fopen("gen/abertura/diag_selftest.txt", "w"); if (tf) { fprintf(tf, "rc=%d logf=%p tee=%p console=%p fileno=%d\n", rc0, (void *)g_logf, (void *)g_tee, (void *)g_console, _fileno(stderr)); fclose(tf); } }
}
/* chamado depois que a ISO foi escolhida: comeca a gravar entradas e copia o save atual */
void diag_start_game(void) {
    if (g_session.empty()) return;
    g_inputs = _wfopen((g_dir + g_session + L".inputs").c_str(), L"wb");
    std::error_code ec;
    if (std::filesystem::exists(L"ms0", ec))
        std::filesystem::copy(L"ms0", g_dir + g_session + L"_ms0", std::filesystem::copy_options::recursive, ec);
    wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
    fprintf(stderr, "[diag] pasta de trabalho (ms0/ fica aqui): %ls\n", cwd);
    CreateThread(nullptr, 0, watchdog_thread, nullptr, 0, nullptr);
}
void diag_record_buttons(uint64_t frame, uint32_t mask) {
    if (!g_inputs) return;
    fprintf(g_inputs, "%llu:%x,", (unsigned long long)frame, mask); fflush(g_inputs);
}
