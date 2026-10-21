/* Executavel do runtime: escolhe a ISO (--iso ou janela inicial), carrega o BOOT e roda o jogo. */
#include "dark_ui.h"
#include "hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shellapi.h>
#include <string>
#include "util.h"
#include "gameid.h"

void display_init(void);
void display_set_iso(const char *utf8);
void display_set_data(const wchar_t *dir);
void mods_init(void);
void texrepl_init(void);
void ffmpeg_preload(void);
void plugins_load(void);
void decomp_init(void);
std::string launcher_pick_iso(std::wstring *data);   /* ISO (jogar direto) ou, em *data, a pasta instalada */
typedef void (*InstProgress)(double frac, const wchar_t *msg, void *ctx);
bool iso_install(const wchar_t *iso, const wchar_t *dest, InstProgress cb, void *ctx, std::wstring *err);
bool iso_verify(const wchar_t *iso, const wchar_t *dest, InstProgress cb, void *ctx, std::wstring *err);
static void cli_progress(double f, const wchar_t *m, void *) { static int last = -1; int p = (int)(f * 100); if (p != last) { last = p; fprintf(stderr, "[instalar] %3d%% %ls\n", p, m); } }


void diag_init(void);
void diag_start_game(void);
void diag_flush(void);

/* Um jogo por pasta de saves: dois processos com o MESMO ms0/ se sobrescreveriam (o ultimo a salvar vence). O mutex dura o processo todo; o
 * "Reiniciar jogo" abre o novo processo antes de o antigo sair, entao ele espera ate 10 s. Testes automaticos (TF_KEEP_CWD / TF_LOG_DIR) usam
 * o ms0 de teste e rodam em paralelo de proposito: ficam de fora. */
static bool acquire_save_lock() {
    if (getenv("TF_KEEP_CWD") || getenv("TF_LOG_DIR")) return true;
    wchar_t cwd[MAX_PATH] = L""; GetCurrentDirectoryW(MAX_PATH, cwd);
    uint32_t h = 2166136261u; for (const wchar_t *p = cwd; *p; p++) { h ^= (uint32_t)towlower(*p); h *= 16777619u; }
    wchar_t name[64]; swprintf(name, 64, L"Local\\TagForceRecomp_%08x", h);
    HANDLE m = CreateMutexW(nullptr, FALSE, name);
    if (!m) return true;   /* sem mutex nao bloqueia o jogo */
    DWORD r = WaitForSingleObject(m, 10000);   /* WAIT_ABANDONED = o processo anterior caiu: assumimos */
    if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) return true;   /* o handle fica aberto ate o fim do processo */
    tf_msgbox(nullptr, L"The game is already running with this save folder. Close it first (two copies would overwrite each other's saves).", L"Tag Force Recomp", MB_ICONWARNING);
    return false;
}

int main() {
    /* executavel de janela (sem console atras do jogo); se foi aberto de um terminal sem redirecionar a saida, usa o console dele */
    if (!GetStdHandle(STD_ERROR_HANDLE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        HANDLE h = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) { SetStdHandle(STD_ERROR_HANDLE, h); SetStdHandle(STD_OUTPUT_HANDLE, h); }
    }
    /* saves (ms0/) sempre ao lado do .exe, qualquer que seja a pasta de onde foi aberto. Os testes automaticos usam
     * TF_KEEP_CWD=1 para ficar na pasta atual (ms0/ de teste na raiz do projeto) e nunca tocar no save do jogador. */
    if (!getenv("TF_KEEP_CWD")) { std::wstring d = exe_dir(); if (!d.empty()) SetCurrentDirectoryW(d.c_str()); }
    diag_init();
    std::string iso;
    int argc; wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);   /* argv em UTF-16: caminhos com acento */
    std::wstring data, cli_iso, cli_dest; int cli = 0;   /* cli: 1 = --install, 2 = --verify (the Portuguese spellings --instalar/--verificar/--dados still work) */
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--iso") && i + 1 < argc) iso = to_utf8(argv[++i]);
        else if ((!wcscmp(argv[i], L"--data") || !wcscmp(argv[i], L"--dados")) && i + 1 < argc) data = argv[++i];
        else if ((!wcscmp(argv[i], L"--install") || !wcscmp(argv[i], L"--verify") || !wcscmp(argv[i], L"--instalar") || !wcscmp(argv[i], L"--verificar")) && i + 1 < argc) { cli = argv[i][2] == L'i' ? 1 : 2; cli_iso = argv[++i]; if (i + 1 < argc && wcsncmp(argv[i + 1], L"--", 2)) cli_dest = argv[++i]; }
    }
    LocalFree(argv);
    if (const wchar_t *e = _wgetenv(L"TF_DATA")) if (*e && data.empty()) data = e;   /* testes: usa a pasta instalada */
    if (cli) {   /* sem janela: instala/verifica a pasta jogo/ e sai */
        if (cli_dest.empty()) { std::string gid = game_id_iso(cli_iso); cli_dest = exe_dir() + game_data_dir_name(gid.empty() ? game_id_built() : gid); }
        std::wstring err; bool ok = cli == 1 ? iso_install(cli_iso.c_str(), cli_dest.c_str(), cli_progress, nullptr, &err) : iso_verify(cli_iso.c_str(), cli_dest.c_str(), cli_progress, nullptr, &err);
        fprintf(stderr, "[instalar] %s%s%ls\n", ok ? "OK" : "FALHOU", err.empty() ? "" : ": ", err.c_str());
        diag_flush(); return ok ? 0 : 1;
    }
    if (!data.empty() && iso_open_folder(data.c_str())) { fprintf(stderr, "[run] jogo instalado em %ls (sem ISO)\n", data.c_str()); goto started; }
    for (;;) {
        if (iso.empty()) {
            std::wstring chosen; iso = launcher_pick_iso(&chosen);
            if (iso.empty() && chosen.empty()) { diag_flush(); return 0; }   /* usuario saiu */
            if (!chosen.empty()) { if (iso_open_folder(chosen.c_str())) { data = chosen; fprintf(stderr, "[run] jogo instalado em %ls (sem ISO)\n", chosen.c_str()); goto started; } tf_msgbox(nullptr, L"The jogo/ folder is incomplete: install again from the ISO.", L"Tag Force Recomp", MB_ICONERROR); continue; }
        }
        if (iso_open(iso.c_str())) break;
        fprintf(stderr, "nao consegui abrir a ISO: %s\n", iso.c_str());
        tf_msgbox(nullptr, L"Could not open this file as a UMD ISO.", L"Tag Force Recomp", MB_ICONERROR);
        iso.clear();
    }
    fprintf(stderr, "[run] ISO: %s\n", iso.c_str());
started:
    {   /* the game version is whatever was installed; its code is translated on first run (prepare.cpp) */
        if (data.empty()) data = exe_dir() + game_data_dir_name(game_id_iso(to_wide(iso)));
        std::string id = game_id_dir(data);
        if (id.empty() || !iso_open_folder(data.c_str())) { tf_msgbox(nullptr, L"The game is not installed yet. Start the program without arguments and pick your ISO.", L"Tag Force Recomp", MB_ICONERROR); diag_flush(); return 1; }
        game_id_set_built(id);
        if (!code_ready(id)) { std::wstring err; if (!code_prepare(id, data, [](double, const wchar_t *m, void *) { fprintf(stderr, "[prepare] %ls\n", m); }, nullptr, &err)) { tf_msgbox(nullptr, err.c_str(), L"Tag Force Recomp", MB_ICONERROR); diag_flush(); return 1; } }
    }
    if (!acquire_save_lock()) { diag_flush(); return 1; }
    diag_start_game();
    if (!jit_init()) { tf_msgbox(nullptr, L"Could not compile the translated game code (see the log in the logs folder). Delete the gen folder and start again; if it keeps failing, please report it.", L"Tag Force Recomp", MB_ICONERROR); diag_flush(); return 1; }
    ffmpeg_preload(); host_create(); k_init(); mods_init(); texrepl_init(); plugins_load(); decomp_init(); display_init(); display_set_iso(iso.c_str()); if (!data.empty()) display_set_data(data.c_str());
    bool ok; module_load("SYSDIR/BOOT.BIN", &ok);
    const GenModule *boot = nullptr;
    { unsigned nm; const GenModule *gm = gen_modules(&nm); for (unsigned i = 0; i < nm; i++) if (!strcmp(gm[i].name, "BOOT")) boot = &gm[i]; }
    if (!ok || !boot) { fprintf(stderr, "[run] SYSDIR/BOOT.BIN nao carregou\n"); tf_msgbox(nullptr, L"SYSDIR/BOOT.BIN not found: is this ISO of Yu-Gi-Oh! GX Tag Force?", L"Tag Force Recomp", MB_ICONERROR); diag_flush(); return 1; }
    k_start_root(boot->entry, boot->gp, 0, 0);
    int rc = k_run();
    fprintf(stderr, "[run] todas as threads terminaram (rc=%d)\n", rc);
    diag_flush();
    return rc;
}
