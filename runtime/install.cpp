/* Instalacao unica: extrai a ISO do usuario para a pasta jogo/ (ver isofolder.cpp) e verifica a copia. A ISO so e necessaria nesta etapa;
 * depois o jogo roda so da pasta. Nada e distribuido: a pasta e feita no PC do usuario com a propria copia do jogo. */
#include "hle.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <map>

typedef void (*InstProgress)(double frac, const wchar_t *msg, void *ctx);
int iso_read_raw(uint32_t lba, uint32_t off, void *dst, uint32_t n);
uint64_t iso_image_size(void);
bool fold_open(const wchar_t *dir); int fold_read(uint32_t lba, uint32_t off, void *dst, uint32_t n);

namespace {
struct Item { std::string path; uint32_t lba, size; bool dir; };
void walk(const std::string &path, uint32_t lba, uint32_t size, std::vector<Item> &out, int depth) {
    if (depth > 32) return;
    for (const DirEnt &e : iso_list_dir(lba, size)) {
        if (e.name == "." || e.name == "..") continue;
        std::string p = path.empty() ? e.name : path + "/" + e.name;
        out.push_back({p, e.lba, e.size, e.dir});
        if (e.dir) walk(p, e.lba, e.size, out, depth + 1);
    }
}
bool all_zero(const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) if (p[i]) return false; return true; }
}

bool iso_install(const wchar_t *iso, const wchar_t *dest, InstProgress cb, void *ctx, std::wstring *err) {
    auto fail = [&](const std::wstring &m) { if (err) *err = m; iso_close(); return false; };
    iso_close();
    if (!iso_open(to_utf8(iso).c_str(), false)) return fail(L"Could not open this file as a UMD ISO.");
    uint32_t rl, rs; bool rd;
    if (iso_find("", &rl, &rs, &rd) != 0) return fail(L"ISO has no root directory.");
    uint32_t pl, ps; bool pd;
    if (iso_find("PSP_GAME/SYSDIR/BOOT.BIN", &pl, &ps, &pd) != 0 && iso_find("PSP_GAME/USRDIR", &pl, &ps, &pd) != 0) return fail(L"This ISO does not look like a PSP game.");
    std::vector<Item> items; walk("", rl, rs, items, 0);
    uint64_t total = 0; for (auto &i : items) if (!i.dir) total += i.size;
    const uint64_t isize = iso_image_size(), nsect = isize / 2048;
    namespace fs = std::filesystem; std::error_code ec;
    fs::path root(dest); fs::create_directories(root, ec);
    if (ec) return fail(L"Could not create the destination folder.");
    fs::remove(root / L"_disco.tsv", ec);   /* reinstalacao: sem o indice antigo, uma copia interrompida nao parece completa */
    { auto fsp = fs::space(root, ec); if (!ec && fsp.available < total + (64u << 20)) return fail(L"Not enough disk space to install."); }
    std::map<uint32_t, std::string> by_lba; std::vector<std::pair<uint32_t, uint32_t>> covered;   /* lba -> primeiro caminho; (lba, setores) */
    std::vector<uint8_t> buf(4u << 20); uint64_t done = 0; int lastpct = -1;
    for (auto &it : items) {
        std::wstring wp = to_wide(it.path); for (auto &c : wp) if (c == L'/') c = L'\\';
        if (it.dir) { fs::create_directories(root / wp, ec); continue; }
        if (by_lba.count(it.lba)) { continue; }   /* mesmo arquivo com dois nomes: so grava uma copia */
        by_lba[it.lba] = it.path; covered.push_back({it.lba, (it.size + 2047) / 2048});
        FILE *f = _wfopen((root / wp).c_str(), L"wb"); if (!f) return fail(L"Could not write: " + wp);
        for (uint64_t off = 0; off < it.size;) {
            uint32_t n = (uint32_t)std::min<uint64_t>(buf.size(), it.size - off);
            int r = iso_read_raw(it.lba, (uint32_t)off, buf.data(), n);
            if (r != (int)n || fwrite(buf.data(), 1, n, f) != n) { fclose(f); return fail(L"Copy error: " + wp); }
            off += n; done += n;
            int pct = total ? (int)(done * 90 / total) : 0;
            if (cb && pct != lastpct) { lastpct = pct; cb(pct / 100.0, wp.c_str(), ctx); }
        }
        fclose(f);
    }
    /* setores fora dos arquivos que nao sao zeros (sistema, descritores, diretorios) */
    std::sort(covered.begin(), covered.end());
    struct R { uint32_t lba, count; }; std::vector<R> runs; std::vector<uint8_t> meta;
    uint64_t cur = 0; size_t ci = 0;
    auto scan_gap = [&](uint64_t a, uint64_t b) {
        for (uint64_t s = a; s < b;) {
            uint32_t cnt = (uint32_t)std::min<uint64_t>(b - s, 512); std::vector<uint8_t> t((size_t)cnt * 2048);
            if (iso_read_raw((uint32_t)s, 0, t.data(), cnt * 2048) != (int)(cnt * 2048)) { s += cnt; continue; }
            for (uint32_t q = 0; q < cnt; q++) {
                if (all_zero(&t[(size_t)q * 2048], 2048)) continue;
                if (!runs.empty() && runs.back().lba + runs.back().count == s + q) runs.back().count++; else runs.push_back({(uint32_t)(s + q), 1});
                meta.insert(meta.end(), t.begin() + (size_t)q * 2048, t.begin() + (size_t)(q + 1) * 2048);
            }
            s += cnt;
        }
    };
    for (; ci < covered.size(); ci++) {
        if (covered[ci].first > cur) scan_gap(cur, covered[ci].first);
        cur = std::max<uint64_t>(cur, (uint64_t)covered[ci].first + covered[ci].second);
        if (cb) cb(0.90 + 0.08 * (double)cur / (double)std::max<uint64_t>(nsect, 1), L"disc sectors", ctx);
    }
    if (cur < nsect) scan_gap(cur, nsect);
    { FILE *f = _wfopen((root / L"_setores.bin").c_str(), L"wb"); if (!f) return fail(L"Could not write the index.");
      uint32_t hdr[2] = {0x45534654u, (uint32_t)runs.size()}; fwrite(hdr, 4, 2, f);
      for (auto &r : runs) { uint32_t v[2] = {r.lba, r.count}; fwrite(v, 4, 2, f); }
      if (!meta.empty()) fwrite(meta.data(), 1, meta.size(), f);
      fclose(f); }
    std::string tsv = "#tagforce-disco 1\n#setores " + std::to_string(nsect) + "\nD " + std::to_string(rl) + " " + std::to_string(rs) + " \n";
    for (auto &it : items) tsv += std::string(it.dir ? "D " : "F ") + std::to_string(it.lba) + " " + std::to_string(it.size) + " " + it.path + "\n";
    { fs::path tmp = root / L"_disco.tsv.tmp"; FILE *f = _wfopen(tmp.c_str(), L"wb"); if (!f) return fail(L"Could not write the index.");
      fwrite(tsv.data(), 1, tsv.size(), f); fclose(f);
      fs::rename(tmp, root / L"_disco.tsv", ec); if (ec) { fs::remove(root / L"_disco.tsv", ec); fs::rename(tmp, root / L"_disco.tsv", ec); } }   /* o indice por ultimo = instalacao completa */
    iso_close();
    if (cb) cb(1.0, L"done", ctx);
    return true;
}

/* confere TODOS os setores: a pasta tem que dar os mesmos bytes que a ISO */
bool iso_verify(const wchar_t *iso, const wchar_t *dest, InstProgress cb, void *ctx, std::wstring *err) {
    iso_close();
    if (!iso_open(to_utf8(iso).c_str(), false)) { if (err) *err = L"ISO did not open"; return false; }
    if (!fold_open(dest)) { if (err) *err = L"folder did not open"; iso_close(); return false; }
    const uint64_t nsect = iso_image_size() / 2048; std::vector<uint8_t> a(1u << 20), b(1u << 20); int lastpct = -1;
    for (uint64_t s = 0; s < nsect;) {
        uint32_t cnt = (uint32_t)std::min<uint64_t>(nsect - s, 512);
        int ra = iso_read_raw((uint32_t)s, 0, a.data(), cnt * 2048), rb = fold_read((uint32_t)s, 0, b.data(), cnt * 2048);
        if (ra != (int)(cnt * 2048) || rb != (int)(cnt * 2048) || memcmp(a.data(), b.data(), (size_t)cnt * 2048)) {
            uint64_t bad = s; for (uint32_t q = 0; q < cnt; q++) if (memcmp(&a[(size_t)q * 2048], &b[(size_t)q * 2048], 2048)) { bad = s + q; break; }
            if (err) *err = L"difference at sector " + std::to_wstring(bad) + L" (reads " + std::to_wstring(ra) + L"/" + std::to_wstring(rb) + L")";
            iso_close(); return false;
        }
        s += cnt; int pct = (int)(s * 100 / nsect); if (cb && pct != lastpct) { lastpct = pct; cb(pct / 100.0, L"verifying", ctx); }
    }
    iso_close(); return true;
}
