/* Texturas substitutas (mods): ver texrepl.h. Indice por hash dos dados; PNGs decodificados por varias threads de fundo (WIC) e guardados
 * num cache em RAM (voltar a uma cena nao rele os PNGs). */
#include <atomic>
#include "texrepl.h"
#include "xxh.h"
#include "imgio.h"
#include "bc7.h"
#include "mods.h"
#include "util.h"
#include <windows.h>
#include <winioctl.h>
#include <atomic>
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
struct Job { uint32_t hash; std::wstring path; int ow, oh; int kind = 0; std::vector<std::wstring> cands; uint32_t target = 0xffffffffu; std::vector<uint32_t> src; int sw = 0, sh = 0, scale = 1; std::shared_ptr<TexReplImg> img; };
/* kind 0 = carregar PNG, 1 = ampliar (Scale2x), 2 = variante por cor, 3 = pre-carga; na fila de fundo (g_bgq): 4 = comprimir em BC7 a imagem img ja
 * lida (vai para o cache em disco e troca a RGBA do cache em RAM), 5 = comprimir o PNG path se ainda nao estiver no cache em disco (o pacote inteiro) */
static std::unordered_map<uint32_t, Res> g_up;
static std::unordered_map<uint32_t, Res> g_res;
static std::deque<Job> g_q;
static CRITICAL_SECTION g_cs; static HANDLE g_ev; static bool g_ready, g_ready_up, g_ready_any;   /* g_ready: ha indice; g_ready_up/any: thread e secao critica criadas */
static uint32_t g_gen;   /* troca de pacote (texrepl_reload): leitura que comecou antes dela e terminou depois e descartada */

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
    Rec &r = g_recent[key]; r.hash0 = hash0; r.path = path; r.bytes = (uint64_t)img.bytes(); r.last = GetTickCount64(); g_recent_dirty = true;
}
static std::unordered_map<uint32_t, RamEnt> g_ram; static uint64_t g_ram_bytes, g_ram_hits;
static std::deque<uint32_t> g_pre_ready;   /* pre-carregadas prontas na RAM, na ordem da lista (mais recentes primeiro), para subir para a GPU */
/* RAM cache of the decoded substitutes: 1024 MB, but at most 1/8 of the installed RAM (4 GB machines get 512 MB) */
static uint64_t ram_budget_default(void) {
    MEMORYSTATUSEX ms = {sizeof ms}; GlobalMemoryStatusEx(&ms);
    return std::max<uint64_t>(256, std::min<uint64_t>(1024, ms.ullTotalPhys >> 23));
}
static const uint64_t g_ram_budget = (uint64_t)(getenv("TF_TEX_RAM_MB") ? atoll(getenv("TF_TEX_RAM_MB")) : (long long)ram_budget_default()) << 20;
static void ram_put(uint32_t hash, const std::shared_ptr<TexReplImg> &img, const std::wstring &path, uint64_t last) {   /* sob g_cs; last = 0: pre-carga (sai primeiro) */
    uint64_t b = (uint64_t)img->bytes(); if (!g_ram_budget || b > g_ram_budget / 4) return;
    auto it = g_ram.find(hash); if (it != g_ram.end()) { g_ram_bytes -= (uint64_t)it->second.img->bytes(); g_ram.erase(it); }
    while (g_ram_bytes + b > g_ram_budget && !g_ram.empty()) {
        auto old = g_ram.begin(); for (auto i = g_ram.begin(); i != g_ram.end(); ++i) if (i->second.last < old->second.last) old = i;
        g_ram_bytes -= (uint64_t)old->second.img->bytes(); g_ram.erase(old);
    }
    g_ram[hash] = {img, last, path}; g_ram_bytes += b;
}
/* cache em disco (pasta texcache ao lado do .exe; TF_TEX_CACHE_DIR troca): as texturas ja comprimidas em BC7 com todos os mipmaps (bc7.h). Decodificar
 * um PNG grande e gerar os mipmaps custa dezenas de ms (o tranco na primeira vez que uma textura aparece); ler o BC7 (1/4 do RGBA) custa ~1 ms e a GPU
 * so copia. Chave = caminho dentro de "textures" + tamanho + data do PNG (mudou o PNG = outra entrada; o pacote pode mudar de pasta).
 * A compressao e feita em segundo plano (fila g_bgq, prioridade minima): a textura usada agora sai em RGBA e a versao BC7 fica para as proximas
 * vezes; e, aos poucos, o pacote inteiro (TF_TEX_BC7_FUNDO=0 desliga essa parte). TF_TEX_BC7=0 desliga o BC7 (volta ao RGBA de antes).
 * Limite TF_TEX_DISK_MB (padrao 8192; 0 desliga o cache); ao iniciar, apaga as entradas mais antigas se passar do limite (e os .rgba do formato antigo). */
static const uint64_t g_disk_budget = (uint64_t)(getenv("TF_TEX_DISK_MB") ? atoll(getenv("TF_TEX_DISK_MB")) : 8192ll) << 20;
static bool g_bc_on = g_disk_budget && !(getenv("TF_TEX_BC7") && atoi(getenv("TF_TEX_BC7")) == 0);
static std::wstring g_cdir = getenv("TF_TEX_CACHE_DIR") ? to_wide(getenv("TF_TEX_CACHE_DIR")) : std::wstring(L"texcache");
static volatile LONG64 g_disk_bytes;   /* tamanho atual do cache em disco (aproximado; disk_trim mede ao iniciar) */
struct DiskHdr { uint32_t magic, w, h, levels, sig, pad[3]; };
static const uint32_t DISK_MAGIC = 0x37425854u;   /* "TXB7" */
static bool disk_key(const std::wstring &png, std::wstring &out) {
    WIN32_FILE_ATTRIBUTE_DATA fa; if (!GetFileAttributesExW(png.c_str(), GetFileExInfoStandard, &fa)) return false;
    std::wstring low = png; for (auto &ch : low) ch = ch == L'/' ? L'\\' : (wchar_t)towlower(ch);
    const size_t k0 = low.rfind(L"\\textures\\"); const std::wstring rel = k0 == std::wstring::npos ? low : low.substr(k0 + 10);
    std::string k = to_utf8(rel) + "|" + std::to_string(((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow) + "|" + std::to_string(((uint64_t)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime);
    wchar_t n[40]; swprintf(n, 40, L"/%016llx.bc7", (unsigned long long)xxh64(k.data(), k.size(), 0x7E8C4Cull)); out = g_cdir + n; return true;
}
static bool disk_read(const std::wstring &cf, TexReplImg &img) {
    FILE *f = _wfopen(cf.c_str(), L"rb"); if (!f) return false;
    DiskHdr hd; int lv = 0;
    bool ok = fread(&hd, sizeof hd, 1, f) == 1 && hd.magic == DISK_MAGIC && hd.w > 0 && hd.h > 0 && (uint64_t)hd.w * hd.h <= (1u << 26);
    if (ok) { const size_t n = bc7_chain_bytes((int)hd.w, (int)hd.h, &lv); ok = (int)hd.levels == lv; if (ok) { img.bc.resize(n); ok = fread(img.bc.data(), 1, n, f) == n; } }
    fclose(f);
    if (!ok) { img.bc.clear(); DeleteFileW(cf.c_str()); return false; }   /* truncado/corrompido */
    img.w = (int)hd.w; img.h = (int)hd.h; img.levels = lv; img.sig = hd.sig; img.px.clear(); img.mips.clear(); return true;
}
static void disk_write(const std::wstring &cf, const TexReplImg &img) {
    CreateDirectoryW(g_cdir.c_str(), nullptr);
    std::wstring tmp = cf + L".tmp" + std::to_wstring(GetCurrentThreadId());
    if (FILE *f = _wfopen(tmp.c_str(), L"wb")) {
        DiskHdr hd = {DISK_MAGIC, (uint32_t)img.w, (uint32_t)img.h, (uint32_t)img.levels, img.sig, {0, 0, 0}};
        bool ok = fwrite(&hd, sizeof hd, 1, f) == 1 && fwrite(img.bc.data(), 1, img.bc.size(), f) == img.bc.size();
        fclose(f);
        if (ok && MoveFileExW(tmp.c_str(), cf.c_str(), MOVEFILE_REPLACE_EXISTING)) InterlockedAdd64(&g_disk_bytes, (LONG64)(sizeof hd + img.bc.size())); else DeleteFileW(tmp.c_str());
    }
}
static bool bc_fits(int w, int h) { return g_bc_on && !(w & 3) && !(h & 3) && (uint64_t)w * h >= 128 * 128; }   /* pequenas decodificam rapido: nao vale o disco */
static void build_mips(TexReplImg &img);
/* le a textura: BC7 do cache em disco, ou o PNG (RGBA + mipmaps). *todo = vale comprimir esta (ainda nao esta no cache) */
static bool load_tex(const std::wstring &png, TexReplImg &img, bool *todo) {
    *todo = false; std::wstring cf; const bool can = g_bc_on && disk_key(png, cf);
    if (can && disk_read(cf, img)) return true;
    if (!img_load(to_utf8(png).c_str(), &img.w, &img.h, img.px)) return false;
    img.sig = texrepl_color(img.px.data(), img.px.size());
    texrepl_make_mips(img);
    *todo = can && bc_fits(img.w, img.h);
    return true;
}
/* comprime src (RGBA) em BC7 com todos os niveis */
static std::shared_ptr<TexReplImg> compress(const TexReplImg &src) {
    TexReplImg tmp; const TexReplImg *s = &src;
    if (src.mips.empty()) { tmp.w = src.w; tmp.h = src.h; tmp.px = src.px; build_mips(tmp); s = &tmp; }
    auto o = std::make_shared<TexReplImg>(); o->w = src.w; o->h = src.h; o->sig = src.sig;
    o->bc.resize(bc7_chain_bytes(src.w, src.h, &o->levels));
    uint8_t *d = o->bc.data(); const uint32_t *p = s->px.data(); int w = src.w, h = src.h;
    for (int i = 0; i < o->levels; i++) {
        bc7_encode(p, w, h, d); d += bc7_level_bytes(w, h);
        p = i == 0 ? s->mips.data() : p + (size_t)w * h; w = std::max(1, w / 2); h = std::max(1, h / 2);
    }
    return o;
}
static void disk_trim(void) {   /* mantem o cache dentro do limite (apaga os mais antigos) */
    if (!g_disk_budget) return;
    struct F { std::wstring p; uint64_t t, sz; }; std::vector<F> v; uint64_t total = 0;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((g_cdir + L"/*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return;
    do { if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue; uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
         const std::wstring p = g_cdir + L"/" + fd.cFileName; const size_t nl = wcslen(fd.cFileName);
         if ((nl > 5 && _wcsicmp(fd.cFileName + nl - 5, L".rgba") == 0) || wcsstr(fd.cFileName, L".tmp")) { DeleteFileW(p.c_str()); continue; }   /* formato antigo (RGBA cru) / gravacao interrompida */
         total += sz; v.push_back({p, ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime, sz}); } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (total > g_disk_budget) {
        std::sort(v.begin(), v.end(), [](const F &a, const F &b) { return a.t < b.t; });
        for (auto &f : v) { if (total <= g_disk_budget * 9 / 10) break; if (DeleteFileW(f.p.c_str())) total -= f.sz; }
    }
    g_disk_bytes = (LONG64)total;
}
static bool choose(Job &j, std::shared_ptr<TexReplImg> &o, bool *todo) {   /* j.path = PNG escolhido */
    std::vector<std::shared_ptr<TexReplImg>> imgs(j.cands.size()); std::vector<char> td(j.cands.size(), 0); int best = 0; long long bd = -1;
    for (size_t i = 0; i < j.cands.size(); i++) {
        uint32_t sig; EnterCriticalSection(&g_sigcs); auto it = g_sig.find(j.cands[i]); bool have = it != g_sig.end(); if (have) sig = it->second; LeaveCriticalSection(&g_sigcs);
        if (!have) { imgs[i] = std::make_shared<TexReplImg>(); bool t = false; if (!load_tex(j.cands[i], *imgs[i], &t)) { imgs[i].reset(); EnterCriticalSection(&g_sigcs); g_sig[j.cands[i]] = 0xffffffffu; LeaveCriticalSection(&g_sigcs); continue; } td[i] = t; sig = imgs[i]->sig; EnterCriticalSection(&g_sigcs); g_sig[j.cands[i]] = sig; LeaveCriticalSection(&g_sigcs); }
        if (sig == 0xffffffffu || j.target == 0xffffffffu) { if (bd < 0 && i == 0) best = 0; continue; }
        /* palette variants differ in hue, while the HD PNGs are brighter than the PSP texture: compare the colour ratios first, brightness only as a small tie-breaker */
        const double sr = sig & 255, sg = (sig >> 8) & 255, sb = (sig >> 16) & 255, tr = j.target & 255, tg = (j.target >> 8) & 255, tb = (j.target >> 16) & 255;
        const double ss = sr + sg + sb + 1.0, ts = tr + tg + tb + 1.0;
        const double cr = sr / ss - tr / ts, cg = sg / ss - tg / ts, cb = sb / ss - tb / ts;
        const double dl = (ss - ts) / 765.0;
        long long d = (long long)((cr * cr + cg * cg + cb * cb) * 1e6 + dl * dl * 1e3); if (bd < 0 || d < bd) { bd = d; best = (int)i; }
    }
    if (!imgs[best]) { imgs[best] = std::make_shared<TexReplImg>(); bool t = false; if (!load_tex(j.cands[best], *imgs[best], &t)) return false; td[best] = t; }
    o = imgs[best]; *todo = td[best] != 0; j.path = j.cands[best]; return true;
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
/* fila de fundo (compressao BC7): so anda quando a fila principal esta vazia, em no maximo metade das threads, com prioridade minima */
static std::deque<Job> g_bgq; static int g_bg_run, g_bg_max = 1; static uint64_t g_bc_done, g_bc_ms, g_sync_reads;   /* g_sync_reads: BC7 lidas direto no pedido (texrepl_get) */
static bool g_bg_full;
static uint64_t g_bg_from;   /* a fila de fundo so anda depois desta hora (ms): os primeiros 30 s (abertura, titulo, carregamentos) ficam livres */   /* cache em disco chegou a 90% do limite: para de comprimir o pacote inteiro */
static void queue_compress(uint32_t hash, const std::wstring &path, const std::shared_ptr<TexReplImg> &img) {   /* sob g_cs */
    Job c; c.kind = 4; c.hash = hash; c.path = path; c.img = img; g_bgq.push_front(std::move(c));
}
/* the drive holding the cache is nearly full (< 4 GB free): stop adding files (checked at most every 20 s) */
static bool disk_low(void) {
    static uint64_t last; static bool low; const uint64_t now = GetTickCount64();
    if (now - last > 20000) { last = now; ULARGE_INTEGER fr; low = GetDiskFreeSpaceExW(nullptr, &fr, nullptr, nullptr) && (fr.QuadPart >> 30) < 4; }
    return low;
}
static void run_bg(Job &j, uint32_t gen) {
    std::wstring cf; if (!g_bc_on || !disk_key(j.path, cf)) return;
    if (disk_low()) { static bool told; if (!told) { told = true; fprintf(stderr, "[texturas] less than 4 GB free on the drive: not adding more BC7 files to the cache\n"); } return; }
    const uint64_t t0 = GetTickCount64();
    std::shared_ptr<TexReplImg> src = j.img;
    if (j.kind == 5) {
        if (GetFileAttributesW(cf.c_str()) != INVALID_FILE_ATTRIBUTES) return;   /* ja comprimida */
        src = std::make_shared<TexReplImg>();
        if (!img_load(to_utf8(j.path).c_str(), &src->w, &src->h, src->px) || !bc_fits(src->w, src->h)) return;
        src->sig = texrepl_color(src->px.data(), src->px.size());
    }
    auto bc = compress(*src);
    disk_write(cf, *bc);
    EnterCriticalSection(&g_cs);
    g_bc_done++; g_bc_ms += GetTickCount64() - t0;
    if (j.kind == 4 && gen == g_gen) {   /* a RGBA no cache em RAM vira a BC7 (1/4 da memoria) */
        auto it = g_ram.find(j.hash);
        if (it != g_ram.end() && it->second.img == j.img) { g_ram_bytes -= (uint64_t)it->second.img->bytes(); it->second.img = bc; g_ram_bytes += (uint64_t)bc->bytes(); }
    }
    if ((uint64_t)g_disk_bytes > g_disk_budget / 10 * 9 && !g_bg_full) { g_bg_full = true; for (auto it = g_bgq.begin(); it != g_bgq.end();) if (it->kind == 5) it = g_bgq.erase(it); else ++it;
        fprintf(stderr, "[texturas] cache em disco cheio (%llu MB): parei de comprimir o pacote em segundo plano\n", (unsigned long long)(g_disk_bytes >> 20)); }
    LeaveCriticalSection(&g_cs);
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
            if (g_q.empty()) {
                if (g_bgq.empty() || g_bg_run >= g_bg_max || now < g_bg_from) { LeaveCriticalSection(&g_cs); break; }
                j = std::move(g_bgq.front()); g_bgq.pop_front(); g_bg_run++; const uint32_t gen = g_gen;
                if (!g_bgq.empty() && g_bg_run < g_bg_max) SetEvent(g_ev);
                LeaveCriticalSection(&g_cs);
                SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);   /* CPU, disco e memoria em prioridade de fundo */
                run_bg(j, gen);
                SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END); SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                EnterCriticalSection(&g_cs); g_bg_run--; LeaveCriticalSection(&g_cs);
                continue;
            }
            auto prio = [&](const Job &q) -> uint64_t { if (q.kind == 3) return 0; auto &m = q.kind == 1 ? g_up : g_res; auto r = m.find(q.hash); return r == m.end() ? 0 : r->second.ask + 1; };
            auto best = g_q.begin(); uint64_t bp = prio(*best);
            for (auto it = g_q.begin(); it != g_q.end(); ++it) { uint64_t p = prio(*it); if (p > bp) { best = it; bp = p; } }
            j = std::move(*best); g_q.erase(best); if (!g_q.empty() || !g_bgq.empty()) SetEvent(g_ev);   /* ainda ha trabalho: acorda outra thread */
            const uint32_t gen = g_gen;
            if (j.kind == 3 && (g_ram.count(j.hash) || g_res.count(j.hash))) { LeaveCriticalSection(&g_cs); continue; }   /* ja carregada/pedida de verdade */
            LeaveCriticalSection(&g_cs);
            auto img = std::make_shared<TexReplImg>(); bool ok, todo = false;
            if (j.kind == 3) {
                if (load_tex(j.path, *img, &todo)) { EnterCriticalSection(&g_cs); if (gen == g_gen && !g_ram.count(j.hash)) { ram_put(j.hash, img, j.path, 0); g_prefetched++; if (!g_amb.count(j.hash)) g_pre_ready.push_back(j.hash); if (todo) queue_compress(j.hash, j.path, img); } LeaveCriticalSection(&g_cs); }
                continue;
            }
            if (j.kind == 1) { ok = scale2x(j, *img); EnterCriticalSection(&g_cs); if (gen != g_gen) { LeaveCriticalSection(&g_cs); continue; } Res &r = g_up[j.hash]; r.state = ok ? S_READY : S_FAILED; r.ask = GetTickCount64(); if (ok) r.img = img; LeaveCriticalSection(&g_cs); continue; }
            uint64_t t0 = GetTickCount64();
            ok = j.kind == 2 ? choose(j, img, &todo) : load_tex(j.path, *img, &todo);
            uint64_t t1 = GetTickCount64();
            { static const bool tr = getenv("TF_TEX_TRACE") != nullptr; if (tr && ok) fprintf(stderr, "[texrepl] hash %08x kind %d (%zu candidatos, alvo %08x) -> %ls%s\n", j.hash, j.kind, j.cands.size(), j.target, j.path.c_str(), img->bc.empty() ? "" : " (BC7)"); }   /* qual PNG foi usado */
            if (t1 - t0 > 2000) fprintf(stderr, "[texturas] leitura lenta (%llu ms): %ls\n", (unsigned long long)(t1 - t0), j.path.c_str());
            EnterCriticalSection(&g_cs);   /* contadores e resultado sob a trava (varias threads de leitura) */
            if (gen != g_gen) { LeaveCriticalSection(&g_cs); continue; }   /* PNG do pacote anterior */
            g_progress = t1;
            if (!ok) { g_failed++; if (g_failed <= 20) fprintf(stderr, "[texturas] falha ao ler %ls\n", j.path.c_str()); } else g_loaded++;
            Res &r = g_res[j.hash]; r.state = ok ? S_READY : S_FAILED; r.ask = GetTickCount64(); if (ok) { r.img = img; r.path = j.path; ram_put(j.hash, img, j.path, r.ask); if (todo) queue_compress(j.hash, j.path, img); }
            g_decode_ms += t1 - t0;
            LeaveCriticalSection(&g_cs);
        }
    }
    return 0;
}
bool texrepl_take_preupload(uint32_t *hash, std::shared_ptr<TexReplImg> &img) {
    if (!g_ready_any) return false;
    EnterCriticalSection(&g_cs);
    bool ok = false;
    while (!g_pre_ready.empty() && !ok) {
        const uint32_t h = g_pre_ready.front(); g_pre_ready.pop_front();
        auto c = g_ram.find(h); if (c == g_ram.end()) continue;   /* saiu do cache em RAM */
        *hash = h; img = c->second.img; ok = true;
    }
    LeaveCriticalSection(&g_cs);
    return ok;
}
void texrepl_no_bc(void) {
    if (!g_bc_on) return;
    g_bc_on = false; fprintf(stderr, "[texturas] a GPU recusou textura BC7: voltando a ler os PNGs\n");
    if (!g_ready_any) return;
    EnterCriticalSection(&g_cs);
    g_bgq.clear();
    for (auto it = g_ram.begin(); it != g_ram.end();) { if (!it->second.img->bc.empty()) { g_ram_bytes -= (uint64_t)it->second.img->bytes(); it = g_ram.erase(it); } else ++it; }
    for (auto it = g_res.begin(); it != g_res.end();) { if (it->second.state == S_READY && !it->second.img->bc.empty()) it = g_res.erase(it); else ++it; }
    LeaveCriticalSection(&g_cs);
}
void texrepl_make_mips(TexReplImg &img) {   /* A/B: TF_TEX_MIPS_GPU = mipmaps gerados na GPU, como antes */
    static const bool off = getenv("TF_TEX_MIPS_GPU") != nullptr;
    if (off) { img.mips.clear(); return; }
    build_mips(img);
}
static void build_mips(TexReplImg &img) {   /* mesmos tamanhos do Vulkan (max(1, n/2) por nivel, ate 1x1); bordas impares repetem o ultimo texel */
    img.mips.clear(); if (img.w < 2 && img.h < 2) return;
    size_t total = 0; for (int w = img.w, h = img.h; w > 1 || h > 1;) { w = std::max(1, w / 2); h = std::max(1, h / 2); total += (size_t)w * h; }
    img.mips.resize(total);
    const uint32_t *src = img.px.data(); int sw = img.w, sh = img.h; uint32_t *dst = img.mips.data();
    while (sw > 1 || sh > 1) {
        const int dw = std::max(1, sw / 2), dh = std::max(1, sh / 2);
        for (int y = 0; y < dh; y++) {
            const uint32_t *r0 = src + (size_t)std::min(2 * y, sh - 1) * sw, *r1 = src + (size_t)std::min(2 * y + 1, sh - 1) * sw;
            for (int x = 0; x < dw; x++) {
                const int x0 = std::min(2 * x, sw - 1), x1 = std::min(2 * x + 1, sw - 1);
                const uint32_t a = r0[x0], b = r0[x1], c = r1[x0], d = r1[x1]; uint32_t o = 0;
                for (int k = 0; k < 32; k += 8) o |= ((((a >> k) & 255) + ((b >> k) & 255) + ((c >> k) & 255) + ((d >> k) & 255) + 2) >> 2) << k;
                dst[(size_t)y * dw + x] = o;
            }
        }
        src = dst; dst += (size_t)dw * dh; sw = dw; sh = dh;
    }
}
/* Cold-open warm-up. The first open of each cache file costs ~5 ms (real-time antivirus scan / metadata), reading it only ~0.3 ms; a re-open is ~0.25 ms and
 * parallel first opens are ~5x faster. A new scene asks for ~24 textures one by one on the game thread (~130 ms: an audio gap). So a few background threads
 * open every cached .bc7 once after the start (low priority, I/O included); by the first duel the opens are warm. */
static bool cache_on_hdd(void) {   /* the drive of the working folder has a seek penalty (spinning disk): parallel opens would only make the heads thrash */
    wchar_t cwd[MAX_PATH]; if (!GetCurrentDirectoryW(MAX_PATH, cwd) || cwd[1] != L':') return false;
    wchar_t vol[8] = {L'\\', L'\\', L'.', L'\\', cwd[0], L':', 0};
    HANDLE h = CreateFileW(vol, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr); if (h == INVALID_HANDLE_VALUE) return false;
    STORAGE_PROPERTY_QUERY q = {}; q.PropertyId = StorageDeviceSeekPenaltyProperty; q.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR d = {}; DWORD got = 0;
    const bool ok = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, &d, sizeof d, &got, nullptr) && got >= sizeof d;
    CloseHandle(h); return ok && d.IncursSeekPenalty;
}
static DWORD WINAPI warm_cache(LPVOID) {
    Sleep(4000);
    std::vector<std::wstring> files; WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_cdir + L"/*.bc7").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do files.push_back(g_cdir + L"/" + fd.cFileName); while (FindNextFileW(h, &fd));
    FindClose(h);
    static std::atomic<size_t> next; static std::atomic<int> live; const uint64_t t0 = GetTickCount64();
    const int nt = cache_on_hdd() ? 1 : 3; live = nt;
    for (int i = 0; i < nt; i++) CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        auto *fl = (std::vector<std::wstring> *)p;
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        for (size_t k; (k = next++) < fl->size();) { HANDLE f = CreateFileW((*fl)[k].c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); if (f != INVALID_HANDLE_VALUE) CloseHandle(f); }
        live--; return 0; }, &files, 0, nullptr);
    while (live > 0) Sleep(200);
    fprintf(stderr, "[texturas] cache BC7 aquecido: %zu arquivo(s) abertos em %llu ms (%d thread(s) de fundo)%c", files.size(), (unsigned long long)(GetTickCount64() - t0), nt, 10);
    return 0;
}
static int g_nthreads;
static void start_loaders(void) {   /* varias threads de leitura (nucleos - 2, de 2 a 8; TF_TEX_THREADS) */
    if (g_ready_up) return;
    InitializeCriticalSection(&g_cs); InitializeCriticalSection(&g_sigcs); g_ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    SYSTEM_INFO si; GetSystemInfo(&si); int n = std::max(2, std::min(8, (int)si.dwNumberOfProcessors - 2));
    if (const char *e = getenv("TF_TEX_THREADS")) n = std::max(1, std::min(16, atoi(e)));
    for (int i = 0; i < n; i++) { HANDLE t = CreateThread(nullptr, 0, loader, nullptr, 0, nullptr); if (t) { SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(t); g_nthreads++; } }
    g_bg_max = std::max(1, std::min(2, g_nthreads / 2));
    g_bg_from = GetTickCount64() + 30000;
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD { Sleep(30500); SetEvent(g_ev); return 0; }, nullptr, 0, nullptr);   /* acorda a fila de fundo quando a espera acaba */   /* 2 no maximo: com 4 a 1a execucao (pacote inteiro) atrasava o jogo (sessao 192436: quadros chegando em 11-20 ms em vez de ~5) */
    if (g_bc_on && !getenv("TF_TEX_NO_WARM")) CreateThread(nullptr, 0, warm_cache, nullptr, 0, nullptr);
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
/* o pacote inteiro na fila de fundo: cada PNG que ainda nao esta no cache em disco vira BC7 aos poucos (TF_TEX_BC7_FUNDO=0 desliga) */
static void queue_pack(void) {
    const char *e = getenv("TF_TEX_BC7_FUNDO"); if (!g_bc_on || (e && atoi(e) == 0)) return;
    std::unordered_set<std::wstring> seen; size_t n = 0;
    EnterCriticalSection(&g_cs);
    for (auto &kv : g_idx) for (auto &en : kv.second) if (seen.insert(en.path).second) { Job j; j.kind = 5; j.path = en.path; g_bgq.push_back(std::move(j)); n++; }
    if (n) SetEvent(g_ev);
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[texturas] %zu PNG(s) do pacote na fila de compressao BC7 em segundo plano (os que ja estao no cache passam direto)\n", n);
}
static void build_index(void) {   /* indice das pastas de texturas dos mods ligados */
    std::vector<std::wstring> dirs; mods_texture_dirs(dirs);
    for (auto &d : dirs) {
        size_t n = 0; std::wstring ini = d + L"\\textures.ini";
        if (GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES) add_dir_ini(d, ini, &n); else scan_pngs(d, &n);
        /* card style (Graphics > Card style): 1 = anime version, an optional textures_anime.ini whose rows replace the TCG ones */
        if ((getenv("TF_CARD_STYLE") ? atoi(getenv("TF_CARD_STYLE")) : (int)GetPrivateProfileIntW(L"graficos", L"estilo_cartas", 0, app_ini_path().c_str())) == 1) { std::wstring an = d + L"\\textures_anime.ini"; if (GetFileAttributesW(an.c_str()) != INVALID_FILE_ATTRIBUTES) add_dir_ini(d, an, &n); }
        fprintf(stderr, "[texturas] %ls: %zu substituicao(oes)\n", d.c_str(), n);
    }
    for (auto &kv : g_idx) { for (auto &e : kv.second) if (e.path != kv.second[0].path) { g_amb.insert(kv.first); break; } }
    if (!g_idx.empty()) fprintf(stderr, "[texturas] %zu textura(s) com variantes por paleta (escolha pela cor)\n", g_amb.size());
}
void texrepl_reload(void) {
    if (g_ready_any) {   /* o que foi lido, pedido ou esta sendo lido do pacote anterior deixa de valer */
        EnterCriticalSection(&g_cs);
        g_gen++; g_q.clear(); g_bgq.clear(); g_res.clear(); g_up.clear(); g_ram.clear(); g_ram_bytes = 0; g_recent.clear(); g_recent_dirty = true;
        LeaveCriticalSection(&g_cs);
        EnterCriticalSection(&g_sigcs); g_sig.clear(); LeaveCriticalSection(&g_sigcs);
    }
    g_ready = false; g_idx.clear(); g_amb.clear();
    build_index();
    if (g_idx.empty()) return;
    start_loaders();
    static bool saving; if (!saving) { saving = true; atexit(texrepl_save_recent); }
    g_ready = true; queue_pack();
    fprintf(stderr, "[texturas] pacote trocado: %zu textura(s) com substituicao no indice\n", g_idx.size());
}
void texrepl_init(void) {
    build_index();
    if (g_idx.empty()) return;
    disk_trim();
    start_loaders();
    g_ready = true; atexit(texrepl_save_recent);
    queue_pack();
    /* pre-carregamento: texturas usadas nas ultimas execucoes (TF_TEX_PREFETCH=0 desliga) */
    const char *pf = getenv("TF_TEX_PREFETCH");
    if (g_ram_budget && !(pf && atoi(pf) == 0)) if (FILE *f = fopen("texturas_recentes.txt", "r")) {
        char line[2048]; size_t n = 0; uint64_t bytes = 0;
        EnterCriticalSection(&g_cs);
        while (fgets(line, sizeof line, f)) {
            unsigned key = 0, h0 = 0; unsigned long long b = 0; char path[1800] = "";
            if (sscanf(line, "%x %x %llu %1799[^\r\n]", &key, &h0, &b, path) != 4) continue;
            auto it = g_idx.find(h0); if (it == g_idx.end()) continue;
            if (g_amb.count(h0)) continue;   /* palette variants are re-chosen by colour each run: a stale record must not pin a wrong frame */
            /* casa pelo trecho depois de "textures\" (o pacote pode estar em outra pasta: replays usam gen/mods_teste, a lista do jogador tem mods\...) */
            auto tail = [](const std::wstring &p) { size_t k = p.rfind(L"\\textures\\"); if (k == std::wstring::npos) k = p.rfind(L"/textures/"); return k == std::wstring::npos ? p : p.substr(k + 10); };
            std::wstring wp = to_wide(path); const std::wstring wt = tail(wp); bool known = false;
            for (auto &e : it->second) if (e.path == wp || tail(e.path) == wt) { known = true; wp = e.path; break; }
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
void texrepl_bc_stats(uint64_t *done, uint64_t *ms, size_t *pending, uint64_t *disk_mb, uint64_t *sync) {
    *sync = g_sync_reads;
    *done = g_bc_done; *ms = g_bc_ms; *disk_mb = (uint64_t)g_disk_bytes >> 20; *pending = 0;
    if (!g_ready_any) return; EnterCriticalSection(&g_cs); *pending = g_bgq.size(); LeaveCriticalSection(&g_cs);
}
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
    /* ja comprimida no cache em disco: le aqui mesmo (~1 ms), sem passar pela fila das threads. O jogo pede as texturas de uma cena uma de cada
     * vez e espera cada uma (repl_get); pela fila, cada pedido custava a troca de thread + a espera em passos de 1 ms (~13 ms por textura na tela de titulo) */
    if (r == g_res.end() && !amb && g_bc_on) {
        const uint32_t gen = g_gen; const std::wstring path = pick->path;
        LeaveCriticalSection(&g_cs);
        auto img = std::make_shared<TexReplImg>(); std::wstring cf;
        const uint64_t rt0 = GetTickCount64();
        const bool ok = disk_key(path, cf) && disk_read(cf, *img);
        { const uint64_t rms = GetTickCount64() - rt0; static int told; if (rms > 10 && told < 100) { told++; fprintf(stderr, "[texturas] leitura direta lenta (%llu ms, %zu KB): %ls%c", (unsigned long long)rms, img->bc.size() >> 10, path.c_str(), 10); } }
        EnterCriticalSection(&g_cs);
        if (ok && gen == g_gen) { ram_put(hash, img, path, now); note_recent(hash, hash0, path, *img); out = img; g_loaded++; g_sync_reads++; LeaveCriticalSection(&g_cs); return 2; }
        r = g_res.find(hash);
    }
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
