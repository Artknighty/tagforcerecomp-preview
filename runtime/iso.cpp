/* Leitor minimo de ISO9660 (a ISO e fornecida pelo usuario; nada e distribuido). */
#include "hle.h"
#include <stdio.h>
#include <windows.h>
#include <vector>
#include <algorithm>
#include <ctype.h>

static FILE *g_iso; static uint32_t g_root_lba, g_root_size;
/* disco em pasta (isofolder.cpp): mesma interface, arquivos soltos */
bool fold_open(const wchar_t *dir); void fold_close(void); bool fold_active(void);
int fold_read(uint32_t lba, uint32_t off, void *dst, uint32_t n); int64_t fold_find(const std::string &path, uint32_t *lba, uint32_t *size, bool *isdir);
std::vector<DirEnt> fold_list(uint32_t lba, uint32_t size);
bool iso_open_folder(const wchar_t *dir) { return fold_open(dir); }
bool iso_folder_mode(void) { return fold_active(); }
void iso_close(void) { fold_close(); if (g_iso) { fclose(g_iso); g_iso = nullptr; } }

/* Cache da ISO inteira na RAM (~800 MB), preenchido por uma thread em segundo plano com prioridade de E/S baixa.
 * Motivo: com a ISO num HD mecanico, o disco para de girar quando fica ocioso e a proxima leitura (ex.: ao carregar o
 * duelo) prendia o jogo ~2,3 s. Enquanto um trecho nao foi carregado, a leitura vai ao arquivo como antes.
 * TF_NO_ISO_CACHE=1 desliga. */
static const uint64_t CHUNK = 4u << 20;
static uint8_t *g_cache; static uint64_t g_isize; static std::vector<uint8_t> g_chunk_ok;
static DWORD WINAPI iso_cache_thread(void *p) {
    HANDLE h = CreateFileW((const wchar_t *)p, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    free(p);
    if (h == INVALID_HANDLE_VALUE) return 0;
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);   /* E/S de baixa prioridade: o jogo tem preferencia */
    DWORD t0 = GetTickCount();
    for (size_t i = 0; i < g_chunk_ok.size(); i++) {
        uint64_t pos = i * CHUNK, n = std::min(CHUNK, g_isize - pos); DWORD got = 0;
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)pos; SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
        if (!ReadFile(h, g_cache + pos, (DWORD)n, &got, nullptr) || got != n) { CloseHandle(h); return 0; }
        __atomic_store_n(&g_chunk_ok[i], (uint8_t)1, __ATOMIC_RELEASE);
    }
    CloseHandle(h);
    fprintf(stderr, "[iso] ISO carregada na memoria (%llu MB em %.1f s)\n", (unsigned long long)(g_isize >> 20), (GetTickCount() - t0) / 1000.0);
    return 0;
}
static void iso_cache_start(const wchar_t *wp) {
    if (getenv("TF_NO_ISO_CACHE")) return;
    _fseeki64(g_iso, 0, SEEK_END); g_isize = (uint64_t)_ftelli64(g_iso);
    if (!g_isize || g_isize > (4ull << 30)) return;
    g_cache = (uint8_t *)VirtualAlloc(nullptr, (SIZE_T)g_isize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_cache) { fprintf(stderr, "[iso] sem memoria para o cache da ISO; lendo do disco\n"); return; }
    g_chunk_ok.assign((size_t)((g_isize + CHUNK - 1) / CHUNK), 0);
    CreateThread(nullptr, 0, iso_cache_thread, _wcsdup(wp), 0, nullptr);
}
static bool iso_cached(uint64_t pos, uint64_t n) {
    if (!g_cache || !n || pos + n > g_isize) return false;
    for (uint64_t i = pos / CHUNK; i <= (pos + n - 1) / CHUNK; i++) if (!__atomic_load_n(&g_chunk_ok[i], __ATOMIC_ACQUIRE)) return false;
    return true;
}

bool iso_open(const char *path, bool cache) {   /* caminho em UTF-8 (aceita acentos no Windows) */
    wchar_t wp[1024];
    bool wide = MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 1024) != 0;
    g_iso = wide ? _wfopen(wp, L"rb") : fopen(path, "rb");
    if (!g_iso) return false;
    uint8_t pvd[2048]; fseek(g_iso, 16 * 2048, SEEK_SET);
    if (fread(pvd, 1, 2048, g_iso) != 2048 || memcmp(pvd + 1, "CD001", 5) != 0) { fclose(g_iso); g_iso = nullptr; return false; }
    g_root_lba = *(uint32_t *)(pvd + 156 + 2); g_root_size = *(uint32_t *)(pvd + 156 + 10);
    if (wide && cache) iso_cache_start(wp);
    return true;
}
int iso_read_raw(uint32_t lba, uint32_t off, void *dst, uint32_t n);
int iso_read(uint32_t lba, uint32_t off, void *dst, uint32_t n) { return fold_active() ? fold_read(lba, off, dst, n) : iso_read_raw(lba, off, dst, n); }
uint64_t iso_image_size(void) { if (!g_iso) return 0; _fseeki64(g_iso, 0, SEEK_END); return (uint64_t)_ftelli64(g_iso); }
int iso_read_raw(uint32_t lba, uint32_t off, void *dst, uint32_t n) {   /* sempre da ISO (ignora a pasta) */
    if (!g_iso) return -1;
    uint64_t pos = (uint64_t)lba * 2048 + off;
    if (iso_cached(pos, n)) { memcpy(dst, g_cache + pos, n); return (int)n; }
    _fseeki64(g_iso, (int64_t)pos, SEEK_SET);
    return (int)fread(dst, 1, n, g_iso);
}
static bool eq_nocase(const char *a, size_t na, const std::string &b) {
    if (na != b.size()) return false;
    for (size_t i = 0; i < na; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}
/* caminho relativo a raiz do disco ("PSP_GAME/USRDIR/x"); devolve 0 se achou */
int64_t iso_find(const std::string &path, uint32_t *lba, uint32_t *size, bool *isdir) {
    if (fold_active()) return fold_find(path, lba, size, isdir);
    uint32_t cl = g_root_lba, cs = g_root_size; bool dir = true;
    size_t pos = 0;
    while (pos < path.size()) {
        while (pos < path.size() && (path[pos] == '/' || path[pos] == 0x5c)) pos++;
        if (pos >= path.size()) break;
        size_t e = pos; while (e < path.size() && path[e] != '/' && path[e] != 0x5c) e++;
        std::string comp = path.substr(pos, e - pos); pos = e;
        if (!dir) return -1;
        std::vector<uint8_t> d(cs); iso_read(cl, 0, d.data(), cs);
        bool found = false;
        for (uint32_t o = 0; o < cs;) {
            uint8_t len = d[o];
            if (!len) { o = (o + 2048) & ~2047u; continue; }
            if (len < 34 || o + len > cs || o + 33u + d[o + 32] > cs) break;   /* registro fora do diretorio (ISO corrompida) */
            uint8_t nl = d[o + 32]; const char *nm = (const char *)&d[o + 33];
            size_t n = nl; for (size_t i = 0; i < nl; i++) if (nm[i] == ';') { n = i; break; }
            if (eq_nocase(nm, n, comp) && !(nl == 1 && nm[0] < 2)) {
                cl = *(uint32_t *)&d[o + 2]; cs = *(uint32_t *)&d[o + 10]; dir = (d[o + 25] & 2) != 0; found = true; break;
            }
            o += len;
        }
        if (!found) return -1;
    }
    *lba = cl; *size = cs; *isdir = dir; return 0;
}

#include <string>
std::vector<DirEnt> iso_list_dir(uint32_t lba, uint32_t size) {
    if (fold_active()) return fold_list(lba, size);
    std::vector<DirEnt> out; std::vector<uint8_t> d(size); iso_read(lba, 0, d.data(), size);
    for (uint32_t o = 0; o < size;) {
        uint8_t len = d[o];
        if (!len) { o = (o + 2048) & ~2047u; continue; }
        if (len < 34 || o + len > size || o + 33u + d[o + 32] > size) break;   /* registro fora do diretorio (ISO corrompida) */
        uint8_t nl = d[o + 32]; std::string nm((const char *)&d[o + 33], nl);
        size_t sc = nm.find(';'); if (sc != std::string::npos) nm.resize(sc);
        if (nl == 1 && d[o + 33] == 0) nm = "."; else if (nl == 1 && d[o + 33] == 1) nm = "..";
        out.push_back({nm, (d[o + 25] & 2) != 0, *(uint32_t *)&d[o + 10], *(uint32_t *)&d[o + 2]});
        o += len;
    }
    return out;
}

#include <stdlib.h>
bool parse_lbn_path(const std::string &rest, uint32_t *lba, uint32_t *size) {
    size_t a = rest.find("sce_lbn0x"), b = rest.find("_size0x");
    if (a == std::string::npos || b == std::string::npos) return false;
    *lba = (uint32_t)strtoul(rest.c_str() + a + 9, nullptr, 16); *size = (uint32_t)strtoul(rest.c_str() + b + 7, nullptr, 16); return true;
}
