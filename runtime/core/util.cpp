/* Utilitarios do runtime (ver util.h). */
#include "util.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

std::wstring exe_dir(void) {
    wchar_t p[MAX_PATH]; DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p, n); size_t k = s.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"" : s.substr(0, k + 1);
}
std::wstring app_ini_path(void) {   /* TF_INI=arquivo: os testes usam um .ini proprio (nunca leem nem gravam as configuracoes do jogador) */
    if (const wchar_t *e = _wgetenv(L"TF_INI")) if (*e) return e;
    return exe_dir() + L"tagforce_recomp.ini";
}
std::string to_utf8(const std::wstring &w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring to_wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

bool quadro_na_lista(const char *lista, uint64_t quadro) {
    for (const char *q = lista; q && *q;) {
        char *e; const unsigned long long fr = strtoull(q, &e, 10); if (e == q) break;
        if (fr == quadro) return true;
        q = *e == ',' ? e + 1 : e;
    }
    return false;
}

bool write_bmp24(const char *fn, int w, int h, const std::function<void(int, uint8_t *)> &fill) {
    FILE *f = fopen(fn, "wb"); if (!f) return false;
    const uint32_t stride = ((uint32_t)w * 3 + 3) & ~3u;   /* linhas do BMP em multiplos de 4 bytes (larguras de hoje ja sao: 480*n) */
    uint32_t sz = 54 + stride * (uint32_t)h; uint8_t fh[14] = {'B', 'M'}; memcpy(fh + 2, &sz, 4); fh[10] = 54;
    int32_t ih[10] = {40, w, -h, 0x00180001, 0, (int32_t)(stride * (uint32_t)h), 0, 0, 0, 0};
    fwrite(fh, 1, 14, f); fwrite(ih, 4, 10, f);
    std::vector<uint8_t> row(stride, 0);   /* uma linha por fwrite (a 8x sao 8 milhoes de pixels) */
    for (int y = 0; y < h; y++) { fill(y, row.data()); fwrite(row.data(), 1, row.size(), f); }
    fclose(f); return true;
}
