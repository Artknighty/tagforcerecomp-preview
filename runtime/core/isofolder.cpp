/* Disco em pasta: a mesma imagem de disco da ISO, mas com os arquivos do jogo SOLTOS numa pasta (jogo/), criada uma unica vez por iso_install
 * (install.cpp). O jogo abre arquivos por posicao no disco (setor + tamanho) e ate le setores brutos (diretorios, tamanhos), entao a pasta
 * reproduz a imagem exatamente:
 *   _disco.tsv    indice: "#setores N", e uma linha por diretorio/arquivo: D|F <lba> <tamanho> <caminho relativo> (escrito por ultimo = instalacao completa)
 *   _setores.bin  os setores que NAO pertencem a nenhum arquivo e nao sao zeros (area do sistema, descritores, diretorios): "TFSE", n, n x (lba, qtd), dados
 *   <caminho>     cada arquivo do disco, com o nome original
 * Nada disso vai para o git (jogo/ e dados do usuario). */
#include "hle.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <mutex>
#include "util.h"

namespace {
struct Ext { uint32_t lba, nsect, size; std::wstring path; };
struct Entry { uint32_t lba, size; bool dir; std::string path; };
std::vector<Ext> g_ext;                          /* arquivos, ordenados por lba */
std::map<std::string, Entry> g_ent;              /* caminho em minusculas ("" = raiz) -> entrada */
std::map<uint32_t, std::vector<DirEnt>> g_dirs;  /* lba do diretorio -> filhos */
struct Run { uint32_t lba, count; size_t off; };
std::vector<Run> g_runs; std::vector<uint8_t> g_meta;
std::wstring g_dir; uint64_t g_sectors;
bool g_on;
std::mutex g_mu;
struct Open { size_t ext; HANDLE h; uint64_t used; };
Open g_open[8]; uint64_t g_tick;

std::string lower(std::string s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; }
std::string norm(const std::string &p) {          /* "PSP_GAME\\USRDIR/x" -> "psp_game/usrdir/x" */
    std::string o; size_t i = 0;
    while (i < p.size()) {
        while (i < p.size() && (p[i] == '/' || p[i] == '\\')) i++;
        size_t e = i; while (e < p.size() && p[e] != '/' && p[e] != '\\') e++;
        if (e > i) { if (!o.empty()) o += '/'; o += p.substr(i, e - i); }
        i = e;
    }
    return lower(o);
}
HANDLE open_ext(size_t ei) {
    for (auto &o : g_open) if (o.h && o.ext == ei) { o.used = ++g_tick; return o.h; }
    Open *v = &g_open[0]; for (auto &o : g_open) { if (!o.h) { v = &o; break; } if (o.used < v->used) v = &o; }
    if (v->h) { CloseHandle(v->h); v->h = nullptr; }
    HANDLE h = CreateFileW((g_dir + g_ext[ei].path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    v->ext = ei; v->h = h; v->used = ++g_tick; return h;
}
/* RAM cache of the game files (same idea as the ISO cache in iso.cpp): the duel start reads ~8 files in one frame and, with the disk cache cold,
 * each read took 5-7 ms (35-40 ms frames at every duel start, session 20261023_224203). A background thread (low I/O priority) reads every file
 * once; fold_read serves the files that are done from memory. TF_NO_ISO_CACHE=1 turns it off. */
uint8_t *g_fc; std::vector<uint64_t> g_fc_off; std::vector<uint8_t> g_fc_ok; volatile LONG g_fc_stop; HANDLE g_fc_thr;
struct FcJob { std::vector<std::wstring> paths; std::vector<uint64_t> sizes, offs; };
DWORD WINAPI fc_thread(void *p) {
    FcJob *j = (FcJob *)p;
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    for (size_t i = 0; i < j->paths.size() && !g_fc_stop; i++) {
        HANDLE h = CreateFileW(j->paths[i].c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        uint64_t got = 0; bool ok = true;
        while (got < j->sizes[i] && !g_fc_stop) {
            DWORD n = (DWORD)std::min<uint64_t>(j->sizes[i] - got, 4u << 20), r = 0;
            if (!ReadFile(h, g_fc + j->offs[i] + got, n, &r, nullptr) || r != n) { ok = false; break; }
            got += r;
        }
        CloseHandle(h);
        if (ok && got == j->sizes[i]) __atomic_store_n(&g_fc_ok[i], (uint8_t)1, __ATOMIC_RELEASE);
    }
    delete j; return 0;
}
void fc_stop(void) {
    if (g_fc_thr) { InterlockedExchange(&g_fc_stop, 1); WaitForSingleObject(g_fc_thr, 10000); CloseHandle(g_fc_thr); g_fc_thr = nullptr; }
    if (g_fc) { VirtualFree(g_fc, 0, MEM_RELEASE); g_fc = nullptr; }
    g_fc_ok.clear(); g_fc_off.clear(); g_fc_stop = 0;
}
void fc_start(void) {
    if (getenv("TF_NO_ISO_CACHE") || g_ext.empty()) return;
    uint64_t total = 0; FcJob *j = new FcJob;
    for (auto &x : g_ext) { j->paths.push_back(g_dir + x.path); j->sizes.push_back(x.size); j->offs.push_back(total); total += (x.size + 4095) & ~4095ull; }
    MEMORYSTATUSEX ms = {sizeof ms}; GlobalMemoryStatusEx(&ms);
    if (!total || total > (3ull << 30) || ms.ullAvailPhys < total + (1536ull << 20)) { delete j; return; }   /* needs the data plus 1.5 GB of free RAM to spare */
    g_fc = (uint8_t *)VirtualAlloc(nullptr, (SIZE_T)total, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_fc) { delete j; return; }
    g_fc_off = j->offs; g_fc_ok.assign(g_ext.size(), 0); g_fc_stop = 0;
    g_fc_thr = CreateThread(nullptr, 0, fc_thread, j, 0, nullptr);
    fprintf(stderr, "[iso] arquivos do jogo (%llu MB) sendo lidos para a memoria em segundo plano%c", (unsigned long long)(total >> 20), 10);
}
}

bool iso_folder_mode(void);
void fold_close(void) {
    fc_stop();
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto &o : g_open) { if (o.h) CloseHandle(o.h); o = Open{0, nullptr, 0}; }
    g_ext.clear(); g_ent.clear(); g_dirs.clear(); g_runs.clear(); g_meta.clear(); g_on = false;
}
bool fold_active(void) { return g_on; }
bool fold_open(const wchar_t *dir) {
    fold_close();
    g_dir = dir; if (!g_dir.empty() && g_dir.back() != L'\\' && g_dir.back() != L'/') g_dir += L'\\';
    FILE *f = _wfopen((g_dir + L"_disco.tsv").c_str(), L"rb"); if (!f) return false;
    std::string txt; { char b[65536]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) txt.append(b, n); } fclose(f);
    if (txt.compare(0, 17, "#tagforce-disco 1") != 0) return false;
    size_t pos = 0;
    while (pos < txt.size()) {
        size_t e = txt.find('\n', pos); if (e == std::string::npos) e = txt.size();
        std::string line = txt.substr(pos, e - pos); pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, 9, "#setores ") == 0) { g_sectors = strtoull(line.c_str() + 9, nullptr, 10); continue; }
        if (line.size() < 5 || (line[0] != 'D' && line[0] != 'F') || line[1] != ' ') continue;
        char *p; uint32_t lba = (uint32_t)strtoul(line.c_str() + 2, &p, 10); uint32_t size = (uint32_t)strtoul(p, &p, 10);
        if (*p == ' ') p++; std::string path = p;
        Entry en{lba, size, line[0] == 'D', path}; g_ent[norm(path)] = en;
        if (line[0] == 'F') { Ext x{lba, (size + 2047) / 2048, size, to_wide(path)}; for (auto &c : x.path) if (c == L'/') c = L'\\'; if (std::none_of(g_ext.begin(), g_ext.end(), [&](const Ext &o) { return o.lba == lba; })) g_ext.push_back(x); }
    }
    std::sort(g_ext.begin(), g_ext.end(), [](const Ext &a, const Ext &b) { return a.lba < b.lba; });
    /* filhos de cada diretorio (para iso_list_dir) */
    for (auto &kv : g_ent) {
        const Entry &en = kv.second; if (kv.first.empty()) continue;
        size_t sl = kv.first.rfind('/'); std::string parent = sl == std::string::npos ? "" : kv.first.substr(0, sl);
        auto pi = g_ent.find(parent); if (pi == g_ent.end()) continue;
        size_t ps = en.path.find_last_of("/\\"); std::string name = ps == std::string::npos ? en.path : en.path.substr(ps + 1);
        g_dirs[pi->second.lba].push_back({name, en.dir, en.size, en.lba});
    }
    for (auto &kv : g_dirs) {   /* "." e ".." primeiro, como no disco */
        auto self = g_ent.begin(); for (auto it = g_ent.begin(); it != g_ent.end(); ++it) if (it->second.dir && it->second.lba == kv.first) { self = it; break; }
        kv.second.insert(kv.second.begin(), {{"..", true, self->second.size, self->second.lba}, {".", true, self->second.size, self->second.lba}});
        std::swap(kv.second[0], kv.second[1]);
    }
    /* setores fora dos arquivos */
    f = _wfopen((g_dir + L"_setores.bin").c_str(), L"rb");
    if (f) {
        uint32_t hdr[2];
        if (fread(hdr, 4, 2, f) == 2 && hdr[0] == 0x45534654u /* "TFSE" */) {
            std::vector<uint32_t> r(hdr[1] * 2); if (hdr[1] && fread(r.data(), 4, r.size(), f) != r.size()) { fclose(f); return false; }
            size_t off = 0; for (uint32_t i = 0; i < hdr[1]; i++) { g_runs.push_back({r[i * 2], r[i * 2 + 1], off}); off += (size_t)r[i * 2 + 1] * 2048; }
            g_meta.resize(off); if (off && fread(g_meta.data(), 1, off, f) != off) { fclose(f); return false; }
        }
        fclose(f);
    }
    g_on = !g_ent.empty() && g_ent.count("");
    if (g_on) fc_start();
    return g_on;
}
int fold_read(uint32_t lba, uint32_t off, void *dst, uint32_t n) {
    if (!g_on) return -1;
    std::lock_guard<std::mutex> lk(g_mu);
    uint64_t P = (uint64_t)lba * 2048 + off; uint8_t *d = (uint8_t *)dst; uint32_t done = 0;
    if (g_sectors && P >= g_sectors * 2048) return 0;
    if (g_sectors && P + n > g_sectors * 2048) n = (uint32_t)(g_sectors * 2048 - P);
    while (done < n) {
        uint64_t s = (P + done) / 2048, so = (P + done) % 2048;
        uint32_t chunk;
        auto it = std::upper_bound(g_ext.begin(), g_ext.end(), (uint32_t)s, [](uint32_t v, const Ext &x) { return v < x.lba; });
        if (it != g_ext.begin() && (it - 1)->lba + (it - 1)->nsect > s) {   /* dentro de um arquivo */
            const Ext &x = *(it - 1); uint64_t fo = (P + done) - (uint64_t)x.lba * 2048, avail = (uint64_t)x.nsect * 2048 - fo;
            chunk = (uint32_t)std::min<uint64_t>(n - done, avail);
            memset(d + done, 0, chunk);
            if (fo < x.size) {
                uint32_t rd = (uint32_t)std::min<uint64_t>(chunk, x.size - fo);
                const size_t ei = (size_t)(&x - g_ext.data());
                if (g_fc && ei < g_fc_ok.size() && __atomic_load_n(&g_fc_ok[ei], __ATOMIC_ACQUIRE)) { memcpy(d + done, g_fc + g_fc_off[ei] + fo, rd); done += chunk; continue; }
                HANDLE h = open_ext(ei);
                if (!h) return -1;
                LARGE_INTEGER li; li.QuadPart = (LONGLONG)fo; DWORD got = 0;
                if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN) || !ReadFile(h, d + done, rd, &got, nullptr)) return -1;
            }
        } else {   /* fora dos arquivos: setores gravados em _setores.bin ou zeros */
            uint64_t next = it != g_ext.end() ? it->lba : (g_sectors ? g_sectors : s + 1);   /* ate o proximo arquivo */
            chunk = (uint32_t)std::min<uint64_t>(n - done, (next - s) * 2048 - so);
            memset(d + done, 0, chunk);
            for (auto &r : g_runs) {
                uint64_t a = std::max<uint64_t>(s, r.lba), b = std::min<uint64_t>(s + (chunk + so + 2047) / 2048, (uint64_t)r.lba + r.count);
                if (a >= b) continue;
                for (uint64_t q = a; q < b; q++) {   /* copia a fatia deste setor que cai em [P+done, P+done+chunk) */
                    uint64_t sb = q * 2048, lo = std::max<uint64_t>(sb, P + done), hi = std::min<uint64_t>(sb + 2048, P + done + chunk);
                    if (lo < hi) memcpy(d + done + (lo - (P + done)), g_meta.data() + r.off + (q - r.lba) * 2048 + (lo - sb), (size_t)(hi - lo));
                }
            }
        }
        done += chunk;
    }
    return (int)done;
}
int64_t fold_find(const std::string &path, uint32_t *lba, uint32_t *size, bool *isdir) {
    auto it = g_ent.find(norm(path)); if (it == g_ent.end()) return -1;
    *lba = it->second.lba; *size = it->second.size; *isdir = it->second.dir; return 0;
}
std::vector<DirEnt> fold_list(uint32_t lba, uint32_t size) {
    (void)size; auto it = g_dirs.find(lba); return it != g_dirs.end() ? it->second : std::vector<DirEnt>();
}
