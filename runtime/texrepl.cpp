/* Texturas substitutas (mods): ver texrepl.h. Indice por hash dos dados; PNGs decodificados por varias threads de fundo (WIC) e guardados
 * num cache em RAM (voltar a uma cena nao rele os PNGs). */
#include "texrepl.h"
#include "xxh.h"
#include "imgio.h"
#include "mods.h"
#include "util.h"
#include <windows.h>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <algorithm>
#include <string>
#include <stdio.h>
#include <stdlib.h>

struct Entry { std::wstring path; uint32_t low; };   /* low = 32 bits baixos da chave do PPSSPP (dim, xor hash da paleta nas texturas indexadas) */
static std::unordered_map<uint32_t, std::vector<Entry>> g_idx;
static std::unordered_set<uint32_t> g_amb;   /* hashes de dados com PNGs diferentes por paleta */
enum { S_PENDING = 1, S_READY = 2, S_FAILED = 3 };
struct Res { int state; std::shared_ptr<TexReplImg> img; uint64_t ask = 0; std::wstring path; };   /* path: PNG lido (o escolhido, nas variantes por cor) */   /* ask: ultima vez (ms) que alguem pediu / quando ficou pronta ou falhou */
static uint64_t g_loaded, g_failed, g_dropped, g_progress, g_decode_ms;   /* g_progress: ultima vez (ms) que a thread de leitura terminou um PNG (deteccao de travamento) */
struct Job { uint32_t hash; std::wstring path; int ow, oh; int kind = 0; std::vector<std::wstring> cands; uint32_t target = 0xffffffffu; std::vector<uint32_t> src; int sw = 0, sh = 0, scale = 1; };   /* kind 0 = carregar PNG, 1 = ampliar (Scale2x) */
static std::unordered_map<uint32_t, Res> g_up;
static std::unordered_map<uint32_t, Res> g_res;
static std::deque<Job> g_q;
static CRITICAL_SECTION g_cs; static HANDLE g_ev; static bool g_ready, g_ready_up, g_ready_any;   /* g_ready: ha indice; g_ready_up/any: thread e secao critica criadas */

static bool is_hex(const std::string &s, size_t a, size_t n) { if (a + n > s.size()) return false; for (size_t i = a; i < a + n; i++) if (!isxdigit((unsigned char)s[i])) return false; return true; }
static uint32_t hex32(const std::string &s, size_t a) { return (uint32_t)strtoul(s.substr(a, 8).c_str(), nullptr, 16); }

uint32_t texrepl_color(const uint32_t *px, size_t n) {
    uint64_t r = 0, g = 0, b = 0, c = 0; size_t step = std::max<size_t>(1, n / 65536);
    for (size_t i = 0; i < n; i += step) { uint32_t p = px[i]; if ((p >> 24) < 128) continue; r += p & 255; g += (p >> 8) & 255; b += (p >> 16) & 255; c++; }
    if (!c) return 0xffffffffu;
    return (uint32_t)(r / c) | ((uint32_t)(g / c) << 8) | ((uint32_t)(b / c) << 16);
}
static std::unordered_map<std::wstring, uint32_t> g_sig;   /* cor media de cada PNG candidato (threads de leitura, sob g_sigcs) */
static CRITICAL_SECTION g_sigcs;
/* cache em RAM das imagens decodificadas (por hash, ja com a variante de cor): a GPU solta as texturas que a cena deixou de usar; ao voltar,
 * a imagem sai daqui sem decodificar o PNG de novo. LRU por bytes, TF_TEX_RAM_MB (padrao 1024; 0 desliga) */
struct RamEnt { std::shared_ptr<TexReplImg> img; uint64_t last; std::wstring path; };
/* texturas usadas recentemente (gravadas em texturas_recentes.txt na pasta atual = ao lado do .exe): na proxima execucao as threads de leitura
 * as decodificam no cache em RAM enquanto o jogo esta no titulo/menus (pre-carregamento), entao a primeira visita a uma cena ja as encontra prontas */
struct Rec { uint32_t hash0; std::wstring path; uint64_t bytes, last; };
static std::unordered_map<uint32_t, Rec> g_recent; static bool g_recent_dirty; static uint64_t g_prefetched;
static void note_recent(uint32_t key, uint32_t hash0, const std::wstring &path, const TexReplImg &img) {   /* sob g_cs */
    if (path.empty()) return;
    Rec &r = g_recent[key]; r.hash0 = hash0; r.path = path; r.bytes = (uint64_t)img.px.size() * 4; r.last = GetTickCount64(); g_recent_dirty = true;
}
static std::unordered_map<uint32_t, RamEnt> g_ram; static uint64_t g_ram_bytes, g_ram_hits;
static const uint64_t g_ram_budget = (uint64_t)(getenv("TF_TEX_RAM_MB") ? atoll(getenv("TF_TEX_RAM_MB")) : 1024) << 20;
static void ram_put(uint32_t hash, const std::shared_ptr<TexReplImg> &img, const std::wstring &path, uint64_t last) {   /* sob g_cs; last = 0: pre-carga (sai primeiro) */
    uint64_t b = (uint64_t)img->px.size() * 4; if (!g_ram_budget || b > g_ram_budget / 4) return;
    auto it = g_ram.find(hash); if (it != g_ram.end()) { g_ram_bytes -= (uint64_t)it->second.img->px.size() * 4; g_ram.erase(it); }
    while (g_ram_bytes + b > g_ram_budget && !g_ram.empty()) {
        auto old = g_ram.begin(); for (auto i = g_ram.begin(); i != g_ram.end(); ++i) if (i->second.last < old->second.last) old = i;
        g_ram_bytes -= (uint64_t)old->second.img->px.size() * 4; g_ram.erase(old);
    }
    g_ram[hash] = {img, last, path}; g_ram_bytes += b;
}
static bool choose(Job &j, TexReplImg &o) {   /* j.path = PNG escolhido */
    std::vector<std::shared_ptr<TexReplImg>> imgs(j.cands.size()); int best = 0; long long bd = -1;
    for (size_t i = 0; i < j.cands.size(); i++) {
        uint32_t sig; EnterCriticalSection(&g_sigcs); auto it = g_sig.find(j.cands[i]); bool have = it != g_sig.end(); if (have) sig = it->second; LeaveCriticalSection(&g_sigcs);
        if (!have) { imgs[i] = std::make_shared<TexReplImg>(); if (!img_load(to_utf8(j.cands[i]).c_str(), &imgs[i]->w, &imgs[i]->h, imgs[i]->px)) { imgs[i].reset(); EnterCriticalSection(&g_sigcs); g_sig[j.cands[i]] = 0xffffffffu; LeaveCriticalSection(&g_sigcs); continue; } sig = texrepl_color(imgs[i]->px.data(), imgs[i]->px.size()); EnterCriticalSection(&g_sigcs); g_sig[j.cands[i]] = sig; LeaveCriticalSection(&g_sigcs); }
        if (sig == 0xffffffffu || j.target == 0xffffffffu) { if (bd < 0 && i == 0) best = 0; continue; }
        long long dr = (long long)(sig & 255) - (j.target & 255), dg = (long long)((sig >> 8) & 255) - ((j.target >> 8) & 255), db = (long long)((sig >> 16) & 255) - ((j.target >> 16) & 255);
        long long d = dr * dr + dg * dg + db * db; if (bd < 0 || d < bd) { bd = d; best = (int)i; }
    }
    if (!imgs[best]) { imgs[best] = std::make_shared<TexReplImg>(); if (!img_load(to_utf8(j.cands[best]).c_str(), &imgs[best]->w, &imgs[best]->h, imgs[best]->px)) return false; }
    o.w = imgs[best]->w; o.h = imgs[best]->h; o.px.swap(imgs[best]->px); j.path = j.cands[best]; return true;
}
/* Scale2x (EPX): cada pixel vira 2x2; os cantos tomam a cor do vizinho quando dois vizinhos adjacentes sao iguais entre si e diferentes dos outros.
 * Bom para bordas em degrau de UI/texto de 4 cores; 4x = aplicada duas vezes. */
static void epx(const std::vector<uint32_t> &in, int w, int h, std::vector<uint32_t> &out) {
    out.assign((size_t)w * h * 4, 0);
    auto at = [&](int x, int y) { x = x < 0 ? 0 : x >= w ? w - 1 : x; y = y < 0 ? 0 : y >= h ? h - 1 : y; return in[(size_t)y * w + x]; };
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        uint32_t P = at(x, y), A = at(x, y - 1), B = at(x + 1, y), C = at(x - 1, y), D = at(x, y + 1);
        uint32_t p1 = P, p2 = P, p3 = P, p4 = P;
        if (C == A && C != D && A != B) p1 = A;
        if (A == B && A != C && B != D) p2 = B;
        if (D == C && D != B && C != A) p3 = C;
        if (B == D && B != A && D != C) p4 = D;
        size_t o = (size_t)(2 * y) * (2 * w) + 2 * x;
        out[o] = p1; out[o + 1] = p2; out[o + 2 * (size_t)w] = p3; out[o + 2 * (size_t)w + 1] = p4;
    }
}
static bool scale2x(const Job &j, TexReplImg &o) {
    std::vector<uint32_t> a = j.src, b; int w = j.sw, h = j.sh;
    for (int s = j.scale; s > 1; s >>= 1) { epx(a, w, h, b); a.swap(b); w *= 2; h *= 2; }
    o.w = w; o.h = h; o.px.swap(a); return true;
}
static DWORD WINAPI loader(LPVOID) {
    for (;;) {
        WaitForSingleObject(g_ev, INFINITE);
        for (;;) {
            Job j; EnterCriticalSection(&g_cs);
            uint64_t now = GetTickCount64();
            /* descarta o que ninguem pede ha 3 s (a cena mudou) e libera prontas que ninguem consumiu ha 30 s; o resto sai do mais recente para o mais antigo */
            for (auto it = g_q.begin(); it != g_q.end();) {
                if (it->kind == 3) { ++it; continue; }   /* pre-carga: ninguem pede, nao expira */
                auto &m = it->kind == 1 ? g_up : g_res; auto r = m.find(it->hash);
                if (r == m.end() || now - r->second.ask > 3000) { if (r != m.end()) m.erase(r); g_dropped++; it = g_q.erase(it); } else ++it;
            }
            for (auto *m : {&g_res, &g_up}) for (auto it = m->begin(); it != m->end();) { if (it->second.state == S_READY && now - it->second.ask > 30000) it = m->erase(it); else ++it; }
            if (g_q.empty()) { LeaveCriticalSection(&g_cs); break; }
            auto prio = [&](const Job &q) -> uint64_t { if (q.kind == 3) return 0; auto &m = q.kind == 1 ? g_up : g_res; auto r = m.find(q.hash); return r == m.end() ? 0 : r->second.ask + 1; };
            auto best = g_q.begin(); uint64_t bp = prio(*best);
            for (auto it = g_q.begin(); it != g_q.end(); ++it) { uint64_t p = prio(*it); if (p > bp) { best = it; bp = p; } }
            j = std::move(*best); g_q.erase(best); if (!g_q.empty()) SetEvent(g_ev);   /* ainda ha trabalho: acorda outra thread */
            if (j.kind == 3 && (g_ram.count(j.hash) || g_res.count(j.hash))) { LeaveCriticalSection(&g_cs); continue; }   /* ja carregada/pedida de verdade */
            LeaveCriticalSection(&g_cs);
            auto img = std::make_shared<TexReplImg>(); bool ok;
            if (j.kind == 3) {
                if (img_load(to_utf8(j.path).c_str(), &img->w, &img->h, img->px)) { EnterCriticalSection(&g_cs); if (!g_ram.count(j.hash)) { ram_put(j.hash, img, j.path, 0); g_prefetched++; } LeaveCriticalSection(&g_cs); }
                continue;
            }
            if (j.kind == 1) { ok = scale2x(j, *img); EnterCriticalSection(&g_cs); Res &r = g_up[j.hash]; r.state = ok ? S_READY : S_FAILED; r.ask = GetTickCount64(); if (ok) r.img = img; LeaveCriticalSection(&g_cs); continue; }
            uint64_t t0 = GetTickCount64();
            ok = j.kind == 2 ? choose(j, *img) : img_load(to_utf8(j.path).c_str(), &img->w, &img->h, img->px);
            uint64_t t1 = GetTickCount64();
            { static const bool tr = getenv("TF_TEX_TRACE") != nullptr; if (tr && ok) fprintf(stderr, "[texrepl] hash %08x kind %d (%zu candidatos, alvo %08x) -> %ls\n", j.hash, j.kind, j.cands.size(), j.target, j.path.c_str()); }   /* qual PNG foi usado */
            if (t1 - t0 > 2000) fprintf(stderr, "[texturas] leitura lenta (%llu ms): %ls\n", (unsigned long long)(t1 - t0), j.path.c_str());
            EnterCriticalSection(&g_cs);   /* contadores e resultado sob a trava (varias threads de leitura) */
            g_progress = t1;
            if (!ok) { g_failed++; if (g_failed <= 20) fprintf(stderr, "[texturas] falha ao ler %ls\n", j.path.c_str()); } else g_loaded++;
            Res &r = g_res[j.hash]; r.state = ok ? S_READY : S_FAILED; r.ask = GetTickCount64(); if (ok) { r.img = img; r.path = j.path; ram_put(j.hash, img, j.path, r.ask); }
            g_decode_ms += t1 - t0;
            LeaveCriticalSection(&g_cs);
        }
    }
    return 0;
}
static int g_nthreads;
static void start_loaders(void) {   /* varias threads de leitura (nucleos - 2, de 2 a 8; TF_TEX_THREADS) */
    if (g_ready_up) return;
    InitializeCriticalSection(&g_cs); InitializeCriticalSection(&g_sigcs); g_ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    SYSTEM_INFO si; GetSystemInfo(&si); int n = std::max(2, std::min(8, (int)si.dwNumberOfProcessors - 2));
    if (const char *e = getenv("TF_TEX_THREADS")) n = std::max(1, std::min(16, atoi(e)));
    for (int i = 0; i < n; i++) { HANDLE t = CreateThread(nullptr, 0, loader, nullptr, 0, nullptr); if (t) { SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(t); g_nthreads++; } }
    g_ready_up = true; g_ready_any = true;
}
static void add_dir_ini(const std::wstring &dir, const std::wstring &ini, size_t *count) {
    FILE *f = _wfopen(ini.c_str(), L"rb"); if (!f) return;
    char line[2048]; bool inhashes = false;
    while (fgets(line, sizeof line, f)) {
        std::string l = line; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.size() >= 3 && (uint8_t)l[0] == 0xef && (uint8_t)l[1] == 0xbb && (uint8_t)l[2] == 0xbf) l.erase(0, 3);
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;
        if (l[0] == '[') { inhashes = l.rfind("[hashes]", 0) == 0; continue; }
        if (!inhashes || !is_hex(l, 0, 24)) continue;   /* so chaves de 24 hex (textura + paleta); "none = ..." e outras secoes sao ignoradas */
        size_t eq = l.find('=', 24); if (eq == std::string::npos) continue;
        std::string rel = l.substr(eq + 1); while (!rel.empty() && rel[0] == ' ') rel.erase(0, 1); while (!rel.empty() && rel.back() == ' ') rel.pop_back();
        if (rel.empty() || rel == "none") continue;
        for (auto &ch : rel) if (ch == '/') ch = '\\';
        Entry e; e.path = dir + L"\\" + to_wide(rel); e.low = hex32(l, 8);
        auto &v = g_idx[hex32(l, 16)];
        bool dup = false; for (auto &o : v) if (o.low == e.low) { o = e; dup = true; }   /* pasta posterior (mod de ordem maior) vence */
        if (!dup) v.push_back(e);
        ++*count;
    }
    fclose(f);
}
static void scan_pngs(const std::wstring &dir, size_t *count) {   /* sem textures.ini: <hash8>.png ou <chave24>.png em qualquer subpasta */
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { scan_pngs(dir + L"\\" + n, count); continue; }
        if (n.size() < 5 || _wcsicmp(n.c_str() + n.size() - 4, L".png") != 0) continue;
        std::string s = to_utf8(n.substr(0, n.size() - 4));
        Entry e; e.path = dir + L"\\" + n; e.low = 0xffffffffu; uint32_t key;
        if (s.size() == 8 && is_hex(s, 0, 8)) key = hex32(s, 0);
        else if (s.size() == 24 && is_hex(s, 0, 24)) { key = hex32(s, 16); e.low = hex32(s, 8); }
        else continue;
        g_idx[key].assign(1, e); ++*count;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
void texrepl_init(void) {
    std::vector<std::wstring> dirs; mods_texture_dirs(dirs);
    for (auto &d : dirs) {
        size_t n = 0; std::wstring ini = d + L"\\textures.ini";
        if (GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES) add_dir_ini(d, ini, &n); else scan_pngs(d, &n);
        fprintf(stderr, "[texturas] %ls: %zu substituicao(oes)\n", d.c_str(), n);
    }
    if (g_idx.empty()) return;
    for (auto &kv : g_idx) { for (auto &e : kv.second) if (e.path != kv.second[0].path) { g_amb.insert(kv.first); break; } }
    fprintf(stderr, "[texturas] %zu textura(s) com variantes por paleta (escolha pela cor)\n", g_amb.size());
    start_loaders();
    g_ready = true; atexit(texrepl_save_recent);
    /* pre-carregamento: texturas usadas nas ultimas execucoes (TF_TEX_PREFETCH=0 desliga) */
    const char *pf = getenv("TF_TEX_PREFETCH");
    if (g_ram_budget && !(pf && atoi(pf) == 0)) if (FILE *f = fopen("texturas_recentes.txt", "r")) {
        char line[2048]; size_t n = 0; uint64_t bytes = 0;
        EnterCriticalSection(&g_cs);
        while (fgets(line, sizeof line, f)) {
            unsigned key = 0, h0 = 0; unsigned long long b = 0; char path[1800] = "";
            if (sscanf(line, "%x %x %llu %1799[^\r\n]", &key, &h0, &b, path) != 4) continue;
            auto it = g_idx.find(h0); if (it == g_idx.end()) continue;
            std::wstring wp = to_wide(path); bool known = false; for (auto &e : it->second) if (e.path == wp) known = true;
            if (!known || bytes + b > g_ram_budget * 3 / 4) continue;   /* o pacote mudou / nao cabe */
            Job j; j.hash = key; j.path = wp; j.kind = 3; g_q.push_back(std::move(j)); bytes += b; n++;
            Rec &r = g_recent[key]; r.hash0 = h0; r.path = wp; r.bytes = b; r.last = 0;   /* continua na lista mesmo se nao for usada nesta execucao */
        }
        if (n) SetEvent(g_ev);
        LeaveCriticalSection(&g_cs); fclose(f);
        if (n) fprintf(stderr, "[texturas] pre-carregando %zu textura(s) usadas recentemente (%llu MB) no cache em RAM\n", n, (unsigned long long)(bytes >> 20));
    }
    fprintf(stderr, "[texturas] %zu textura(s) com substituicao no indice\n", g_idx.size());
}
bool texrepl_active(void) { return g_ready; }
bool texrepl_ambiguous(uint32_t hash) { return g_amb.count(hash) != 0; }
/* grava a lista de texturas usadas recentemente (as mais recentes primeiro, ate 3/4 do cache em RAM); chamada a cada ~10 s e ao sair */
void texrepl_save_recent(void) {
    if (!g_ready || !g_recent_dirty) return;
    std::vector<std::pair<uint32_t, Rec>> v;
    EnterCriticalSection(&g_cs); v.assign(g_recent.begin(), g_recent.end()); g_recent_dirty = false; LeaveCriticalSection(&g_cs);
    std::sort(v.begin(), v.end(), [](const std::pair<uint32_t, Rec> &a, const std::pair<uint32_t, Rec> &b) { return a.second.last > b.second.last; });
    FILE *f = fopen("texturas_recentes.txt.tmp", "w"); if (!f) return;
    uint64_t bytes = 0;
    for (auto &kv : v) {
        if (bytes + kv.second.bytes > g_ram_budget * 3 / 4) continue;
        bytes += kv.second.bytes;
        fprintf(f, "%08x %08x %llu %s\n", kv.first, kv.second.hash0, (unsigned long long)kv.second.bytes, to_utf8(kv.second.path).c_str());
    }
    fclose(f);
    MoveFileExW(L"texturas_recentes.txt.tmp", L"texturas_recentes.txt", MOVEFILE_REPLACE_EXISTING);
}
uint64_t texrepl_prefetched(void) { return g_prefetched; }
void texrepl_stats2(uint64_t *ram_mb, uint64_t *ram_hits, uint64_t *decode_ms, int *threads) { *ram_mb = g_ram_bytes >> 20; *ram_hits = g_ram_hits; *decode_ms = g_decode_ms; *threads = g_nthreads; }
void texrepl_stats(int *queued, int *ready, uint64_t *loaded, uint64_t *failed, uint64_t *dropped) {
    *queued = 0; *ready = 0; *loaded = g_loaded; *failed = g_failed; *dropped = g_dropped;
    if (!g_ready_any) return;
    EnterCriticalSection(&g_cs); *queued = (int)g_q.size(); for (auto &kv : g_res) if (kv.second.state == S_READY) ++*ready; LeaveCriticalSection(&g_cs);
}
uint32_t texrepl_hash(const void *data, size_t n) { return (uint32_t)xxh64(data, n, 0xBACD7814ull); }
int texrepl_get(uint32_t hash0, uint32_t dim, int ow, int oh, std::shared_ptr<TexReplImg> &out, uint32_t target) {
    uint32_t hash = hash0;
    if (!g_ready) return 0;
    auto it = g_idx.find(hash0); if (it == g_idx.end()) return 0;
    const Entry *pick = &it->second[0];
    bool amb = target != 0xffffffffu && g_amb.count(hash0);
    if (amb) { uint32_t q = (((target & 255) >> 4) << 8 | (((target >> 8) & 255) >> 4) << 4 | (((target >> 16) & 255) >> 4)) + 1; hash = hash0 ^ (q * 2654435761u); }   /* uma entrada por classe de cor */
    for (auto &e : it->second) if (e.low == dim) { pick = &e; break; }   /* textura sem paleta: a chave tem a dimensao; indexadas: a primeira (as paletas quase sempre levam ao mesmo PNG) */
    EnterCriticalSection(&g_cs);
    auto r = g_res.find(hash); int ret;
    uint64_t now = GetTickCount64();
    { static uint64_t warned; if (!g_q.empty() && g_progress && now - g_progress > 10000 && now - warned > 30000) { warned = now; fprintf(stderr, "[texturas] ATENCAO: a leitura de PNG esta parada ha %llu s (fila %zu)\n", (unsigned long long)((now - g_progress) / 1000), g_q.size()); } }
    if (r != g_res.end() && r->second.state == S_FAILED && now - r->second.ask > 10000) { g_res.erase(r); r = g_res.end(); }   /* tenta de novo depois de 10 s */
    if (r == g_res.end()) { auto c = g_ram.find(hash); if (c != g_ram.end()) { c->second.last = now; out = c->second.img; g_ram_hits++; note_recent(hash, hash0, c->second.path, *out); LeaveCriticalSection(&g_cs); return 2; } }
    if (r == g_res.end()) { Res &n = g_res[hash]; n.state = S_PENDING; n.ask = now; if (g_q.empty()) g_progress = now;   /* a contagem de "parada" recomeca quando a fila deixa de estar vazia */
    Job jb = {hash, pick->path, ow, oh}; if (amb) { jb.kind = 2; jb.target = target; for (auto &e : it->second) { bool dup = false; for (auto &c : jb.cands) if (c == e.path) dup = true; if (!dup) jb.cands.push_back(e.path); } } g_q.push_back(std::move(jb)); SetEvent(g_ev); ret = 1; }
    else if (r->second.state == S_READY) { out = r->second.img; note_recent(hash, hash0, r->second.path, *out); g_res.erase(r); ret = 2; }
    else if (r->second.state == S_PENDING) { r->second.ask = now; ret = 1; }
    else ret = 0;
    LeaveCriticalSection(&g_cs);
    return ret;
}
void texrepl_upscale_request(uint32_t hash, int w, int h, const uint32_t *rgba, int scale) {
    start_loaders();
    EnterCriticalSection(&g_cs);
    if (g_up.find(hash) == g_up.end()) {
        { Res &n = g_up[hash]; n.state = S_PENDING; n.ask = GetTickCount64(); }
        Job j; j.hash = hash; j.kind = 1; j.sw = w; j.sh = h; j.scale = scale; j.src.assign(rgba, rgba + (size_t)w * h); g_q.push_back(std::move(j)); SetEvent(g_ev);
    }
    LeaveCriticalSection(&g_cs);
}
int texrepl_upscale_take(uint32_t hash, std::shared_ptr<TexReplImg> &out) {
    if (!g_ready_any) return 0;
    EnterCriticalSection(&g_cs); int ret = 0;
    auto r = g_up.find(hash);
    if (r != g_up.end()) { if (r->second.state == S_READY) { out = r->second.img; g_up.erase(r); ret = 2; } else if (r->second.state == S_PENDING) { r->second.ask = GetTickCount64(); ret = 1; } }
    LeaveCriticalSection(&g_cs);
    return ret;
}
void texrepl_upscale_reset(void) { if (!g_ready_any) return; EnterCriticalSection(&g_cs); g_up.clear(); LeaveCriticalSection(&g_cs); }
/* ---- despejo */
static std::string g_dumpdir; static bool g_dumpinit; static std::unordered_set<uint32_t> g_dumped;
void texrepl_set_dump(bool on) {
    g_dumpinit = true;
    if (!on) { g_dumpdir.clear(); fprintf(stderr, "[texturas] despejo desligado\n"); return; }
    g_dumpdir = "texturas_despejo"; CreateDirectoryW(to_wide(g_dumpdir).c_str(), nullptr); g_dumped.clear();
    fprintf(stderr, "[texturas] despejando texturas em %s\\ (um PNG por textura, nome = hash)\n", g_dumpdir.c_str());
}
bool texrepl_dumping(void) {
    if (!g_dumpinit) { g_dumpinit = true; if (const char *d = getenv("TF_TEX_DUMP")) { g_dumpdir = d; CreateDirectoryW(to_wide(g_dumpdir).c_str(), nullptr); fprintf(stderr, "[texturas] despejando texturas em %s\n", d); } }
    return !g_dumpdir.empty();
}
void texrepl_dump(uint32_t hash, int w, int h, unsigned psm, uint32_t addr, const uint32_t *rgba, bool replaced) {
    if (!texrepl_dumping() || !g_dumped.insert(hash).second) return;
    char fn[600];
    if (!replaced) { snprintf(fn, sizeof fn, "%s/faltam", g_dumpdir.c_str()); CreateDirectoryW(to_wide(fn).c_str(), nullptr); }   /* so as que o pacote ainda NAO cobre */
    snprintf(fn, sizeof fn, replaced ? "%s/%08x.png" : "%s/faltam/%08x.png", g_dumpdir.c_str(), hash);
    if (GetFileAttributesW(to_wide(fn).c_str()) == INVALID_FILE_ATTRIBUTES) img_save_png(fn, w, h, rgba);
    snprintf(fn, sizeof fn, "%s/index.txt", g_dumpdir.c_str());
    if (FILE *f = fopen(fn, "a")) { fprintf(f, "%08x %dx%d psm=%u addr=%08x %s\n", hash, w, h, psm, addr, replaced ? "ok" : "FALTA"); fclose(f); }
}
