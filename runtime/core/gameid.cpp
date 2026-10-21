#include "gameid.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <vector>

static std::string g_built = TF_GAME_ID;
std::string game_id_built(void) { return g_built; }
void game_id_set_built(const std::string &id) { g_built = id; }

static std::string id_from_umd(const char *buf, size_t n) {   /* 'ULUS-10136|AA86E435ABE8C363|0001|G ...' */
    std::string s(buf, strnlen(buf, n)); size_t p = s.find('|'); if (p == std::string::npos) return "";
    s.resize(p); if (s.size() != 10 || s[4] != '-') return ""; return s;
}
std::string game_id_dir(const std::wstring &dir) {
    FILE *f = _wfopen((dir + L"\\UMD_DATA.BIN").c_str(), L"rb"); if (!f) return "";
    char b[64] = {}; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); return id_from_umd(b, n);
}
std::string game_id_iso(const std::wstring &iso) {   /* leitor minimo de ISO9660: acha UMD_DATA.BIN na raiz */
    FILE *f = _wfopen(iso.c_str(), L"rb"); if (!f) return "";
    std::string id; uint8_t pvd[2048];
    auto rd = [&](uint64_t lba, void *dst, size_t n) { return _fseeki64(f, (long long)(lba * 2048), SEEK_SET) == 0 && fread(dst, 1, n, f) == n; };
    if (rd(16, pvd, sizeof pvd) && !memcmp(pvd + 1, "CD001", 5)) {
        uint32_t rl = *(uint32_t *)(pvd + 156 + 2), rs = *(uint32_t *)(pvd + 156 + 10);
        if (rs > 0 && rs <= (1u << 20)) {
            std::vector<uint8_t> d(rs);
            if (rd(rl, d.data(), rs)) {
                for (size_t i = 0; i < rs;) {
                    uint8_t n = d[i];
                    if (!n) { i = (i / 2048 + 1) * 2048; continue; }
                    if (i + 33 + d[i + 32] > rs) break;
                    uint8_t nl = d[i + 32]; std::string nome((const char *)&d[i + 33], nl);
                    if (nome.compare(0, 12, "UMD_DATA.BIN") == 0) {
                        char b[64] = {}; if (rd(*(uint32_t *)&d[i + 2], b, sizeof b - 1)) id = id_from_umd(b, sizeof b); break;
                    }
                    i += n;
                }
            }
        }
    }
    fclose(f); return id;
}
static std::wstring compact(const std::string &id) { std::wstring w; for (char c : id) if (c != '-') w += (wchar_t)c; return w; }
std::wstring game_data_dir_name(const std::string &id) { return id.empty() || id == "ULES-00600" ? L"jogo" : L"jogo_" + compact(id); }
