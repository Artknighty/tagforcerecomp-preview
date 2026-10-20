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
#include <mmsystem.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <map>
#include <algorithm>

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

static void log_sysinfo(void);
std::wstring diag_session_prefix(void) { return g_dir + g_session; }   /* <pasta dos logs>/<sessao> (capturas de diagnostico: runtime/gpu/capturas.inc) */
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
    log_sysinfo();
    if (getenv("TF_DIAG_SELFTEST")) { FILE *tf = fopen("gen/abertura/diag_selftest.txt", "w"); if (tf) { fprintf(tf, "rc=%d logf=%p tee=%p console=%p fileno=%d\n", rc0, (void *)g_logf, (void *)g_tee, (void *)g_console, _fileno(stderr)); fclose(tf); } }
}
/* Machine summary at the top of every log (CPU, RAM, Windows build, power source and plan, timer resolution, audio output devices): audio/performance reports
 * depend on all of these and the player rarely knows to mention them. */
static void log_sysinfo(void) {
    char cpu[128] = "?"; DWORD sz = sizeof cpu;
    RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu, &sz);
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof ms; GlobalMemoryStatusEx(&ms);
    unsigned major = 0, minor = 0, build = 0;
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (RtlGetVersionFn rgv = nt ? (RtlGetVersionFn)GetProcAddress(nt, "RtlGetVersion") : nullptr) { OSVERSIONINFOW v = {}; v.dwOSVersionInfoSize = sizeof v; if (!rgv(&v)) { major = v.dwMajorVersion; minor = v.dwMinorVersion; build = v.dwBuildNumber; } }
    fprintf(stderr, "[sys] CPU: %s, %u logical processors; RAM %.1f GB; Windows %u.%u build %u\n", cpu, (unsigned)si.dwNumberOfProcessors, ms.ullTotalPhys / 1073741824.0, major, minor, build);
    SYSTEM_POWER_STATUS ps = {};
    if (GetSystemPowerStatus(&ps)) fprintf(stderr, "[sys] power: %s, battery %s%u%%\n", ps.ACLineStatus == 1 ? "plugged in" : ps.ACLineStatus == 0 ? "ON BATTERY" : "unknown", ps.BatteryFlag == 128 ? "none " : "", ps.BatteryLifePercent == 255 ? 0u : (unsigned)ps.BatteryLifePercent);
    char plan[64] = "?"; sz = sizeof plan;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes", "ActivePowerScheme", RRF_RT_REG_SZ, nullptr, plan, &sz) == ERROR_SUCCESS) {
        const char *nm = !_stricmp(plan, "381b4222-f694-41f0-9685-ff5bb260df2e") ? " (Balanced)" : !_stricmp(plan, "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") ? " (High performance)" : !_stricmp(plan, "a1841308-3541-4fab-bc81-f71556f20b4a") ? " (Power saver)" : "";
        fprintf(stderr, "[sys] power plan: %s%s\n", plan, nm);
    }
    typedef LONG (NTAPI *QueryTimerResFn)(ULONG *, ULONG *, ULONG *);
    if (QueryTimerResFn qtr = nt ? (QueryTimerResFn)GetProcAddress(nt, "NtQueryTimerResolution") : nullptr) { ULONG mn = 0, mx = 0, cur = 0; if (!qtr(&mn, &mx, &cur)) fprintf(stderr, "[sys] timer resolution: current %.2f ms (best %.2f, coarsest %.2f)\n", cur / 10000.0, mx / 10000.0, mn / 10000.0); }
    const UINT nd = waveOutGetNumDevs();
    for (UINT i = 0; i < nd; i++) { WAVEOUTCAPSA c = {}; if (waveOutGetDevCapsA(i, &c, sizeof c) == MMSYSERR_NOERROR) fprintf(stderr, "[sys] audio out %u: %s\n", i, c.szPname); }
    fprintf(stderr, "[sys] audio out devices: %u (the game uses the system default)\n", nd);
}
/* Resource monitor, once per second (only with diag.txt / TF_LAT_DEBUG): CPU use of the process and of its busiest threads, memory, page faults,
 * free system RAM and the real CPU clock. Answers "is this CPU-, RAM- or clock-bound" from a player's log. */
static uint64_t ft64(const FILETIME &f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }
static DWORD WINAPI monitor_thread(void *) {
    typedef BOOL (WINAPI *GetPmi)(HANDLE, PROCESS_MEMORY_COUNTERS_EX *, DWORD);
    typedef LONG (WINAPI *CallNtPower)(int, void *, ULONG, void *, ULONG);
    struct PowerInfo { ULONG number, maxMhz, curMhz, limitMhz, maxIdle, curIdle; };
    GetPmi gpmi = (GetPmi)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo");
    HMODULE pp = LoadLibraryW(L"powrprof.dll");
    CallNtPower cnp = pp ? (CallNtPower)GetProcAddress(pp, "CallNtPowerInformation") : nullptr;
    SYSTEM_INFO si; GetSystemInfo(&si); const int ncpu = (int)si.dwNumberOfProcessors;
    std::vector<PowerInfo> pi(ncpu);
    std::map<DWORD, uint64_t> prev; uint64_t prevProc = 0, prevWall = 0; DWORD prevFaults = 0;
    const DWORD pid = GetCurrentProcessId();
    for (;;) {
        Sleep(1000);
        FILETIME wall, cr, ex, kr, us; GetSystemTimeAsFileTime(&wall); GetProcessTimes(GetCurrentProcess(), &cr, &ex, &kr, &us);
        const uint64_t w = ft64(wall), proc = ft64(kr) + ft64(us);
        std::vector<std::pair<uint64_t, DWORD>> top; std::map<DWORD, uint64_t> cur;
        if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = {}; te.dwSize = sizeof te;
            for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                if (te.th32OwnerProcessID != pid) continue;
                if (HANDLE th = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID)) {
                    FILETIME a, b, k, u; if (GetThreadTimes(th, &a, &b, &k, &u)) { const uint64_t t = ft64(k) + ft64(u); cur[te.th32ThreadID] = t; if (prev.count(te.th32ThreadID)) top.push_back({t - prev[te.th32ThreadID], te.th32ThreadID}); }
                    CloseHandle(th);
                }
            }
            CloseHandle(snap);
        }
        std::sort(top.rbegin(), top.rend());
        PROCESS_MEMORY_COUNTERS_EX mc = {}; mc.cb = sizeof mc; if (gpmi) gpmi(GetCurrentProcess(), &mc, sizeof mc);
        MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof ms; GlobalMemoryStatusEx(&ms);
        ULONG curMhz = 0, limMhz = 0, maxMhz = 0;
        if (cnp && cnp(11, nullptr, 0, pi.data(), (ULONG)(sizeof(PowerInfo) * ncpu)) == 0) for (const PowerInfo &p : pi) { curMhz = std::max(curMhz, p.curMhz); limMhz = p.limitMhz; maxMhz = p.maxMhz; }
        if (prevWall) {
            const double dw = (double)(w - prevWall);
            char line[400]; int n = snprintf(line, sizeof line, "[mon] CPU %.0f%% of one core (%d logical) | busiest threads:", (proc - prevProc) * 100.0 / dw, ncpu);
            for (size_t i = 0; i < top.size() && i < 4; i++) n += snprintf(line + n, sizeof line - n, " %lu=%.0f%%", (unsigned long)top[i].second, top[i].first * 100.0 / dw);
            fprintf(stderr, "%s | mem %.0f MB (private %.0f), %lu page faults/s | free RAM %.0f MB | CPU clock max-core %lu MHz (limit %lu, rated %lu)\n", line, mc.WorkingSetSize / 1048576.0,
                    mc.PrivateUsage / 1048576.0, (unsigned long)(mc.PageFaultCount - prevFaults), ms.ullAvailPhys / 1048576.0, (unsigned long)curMhz, (unsigned long)limMhz, (unsigned long)maxMhz);
        }
        prev = cur; prevProc = proc; prevWall = w; prevFaults = mc.PageFaultCount;
    }
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
    if (tf_lat_debug()) CreateThread(nullptr, 0, monitor_thread, nullptr, 0, nullptr);
}
void diag_record_buttons(uint64_t frame, uint32_t mask) {
    if (!g_inputs) return;
    fprintf(g_inputs, "%llu:%x,", (unsigned long long)frame, mask); fflush(g_inputs);
}
