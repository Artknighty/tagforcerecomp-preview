/* HLE: sceUtility (dialogos). Maquina de estados minima; o savedata grava/le em ./ms0/PSP/SAVEDATA/<jogo><save>/<arquivo>. */
#include "hle.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <windows.h>
#include <stdlib.h>

struct Dialog { int status = 0; uint32_t params = 0; };
static Dialog g_save, g_msg, g_osk, g_net;

static std::string save_dir(uint32_t p) {
    std::string d = std::string("ms0/PSP/SAVEDATA/") + mem_cstr(p + 0x3c) + mem_cstr(p + 0x4c);
    return d;
}
static void mkdirs(const std::string &path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) { cur += path[i]; if (path[i] == '/') CreateDirectoryA(cur.c_str(), nullptr); }
    CreateDirectoryA(cur.c_str(), nullptr);
}
static void savedata_run(uint32_t p) {
    uint32_t mode = rd32(p + 0x30); int32_t result = 0;
    std::string dir = save_dir(p), file = dir + "/" + mem_cstr(p + 0x64);
    uint32_t buf = rd32(p + 0x74), bufsz = rd32(p + 0x78), datasz = rd32(p + 0x7c);
    switch (mode) {
    case 0: case 2: case 4: {   /* (auto)load */
        FILE *f = fopen(file.c_str(), "rb");
        if (!f) { result = (int32_t)0x80110306; break; }
        size_t n = fread(mem_ptr(buf), 1, bufsz, f); fclose(f); wr32(p + 0x7c, (uint32_t)n); break; }
    case 1: case 3: case 5: {   /* (auto)save */
        /* grava num .tmp e troca no fim: se o jogo fechar/travar no meio, o save anterior continua inteiro */
        mkdirs(dir); std::string tmp = file + ".tmp"; FILE *f = fopen(tmp.c_str(), "wb");
        if (!f) { result = (int32_t)0x80110307; break; }
        size_t want = datasz ? datasz : bufsz;
        bool ok = fwrite(mem_ptr(buf), 1, want, f) == want; ok = (fclose(f) == 0) && ok;
        if (!ok || !MoveFileExA(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileA(tmp.c_str()); result = (int32_t)0x80110307; }
        break; }
    case 7: case 9: case 10: RemoveDirectoryA(dir.c_str()); break;
    default: break;   /* SIZES, LIST, FILES...: sem dados por enquanto */
    }
    wr32(p + 0x1c, (uint32_t)result);
    extern void display_notify(const char *msg);
    if (mode == 1 || mode == 3 || mode == 5) display_notify(result == 0 ? "jogo salvo" : "ERRO ao salvar");
    else if ((mode == 2 || mode == 4) && result == 0) display_notify("save carregado");
    fprintf(stderr, "[savedata] modo %u %s -> %08x\n", mode, mode >= 7 ? dir.c_str() : file.c_str(), (uint32_t)result);
}
static void step(Dialog &d, void (*run)(uint32_t)) {
    if (d.status == 1) d.status = 2;
    else if (d.status == 2) { if (run && d.params) run(d.params); d.status = 3; }
}

HLE(sceUtilitySavedataInitStart) { g_save.params = ARG(0); g_save.status = 1; RET(0); }
HLE(sceUtilitySavedataGetStatus) { RET(g_save.status); if (g_save.status == 1) g_save.status = 2; }
HLE(sceUtilitySavedataUpdate) { step(g_save, savedata_run); RET(0); }
HLE(sceUtilitySavedataShutdownStart) { g_save.status = 0; RET(0); }

HLE(sceUtilityMsgDialogInitStart) { g_msg.params = ARG(0); g_msg.status = 1; RET(0); }
HLE(sceUtilityMsgDialogGetStatus) { RET(g_msg.status); if (g_msg.status == 1) g_msg.status = 2; }
HLE(sceUtilityMsgDialogUpdate) { step(g_msg, nullptr); RET(0); }
HLE(sceUtilityMsgDialogShutdownStart) { g_msg.status = 0; RET(0); }

/* ---- OSK: em vez do teclado virtual do PSP, uma caixa de texto nativa (runtime/text_input.cpp) ----
 * SceUtilityOskParams: base (0x30 bytes, result em +0x1c), +0x30 fieldCount, +0x34 fields.
 * SceUtilityOskData (campo): +0x1c desc, +0x20 intext, +0x24 outtextlength, +0x28 outtext, +0x2c result, +0x30 outtextlimit
 * (textos em UTF-16). result do campo: 0 = inalterado, 1 = cancelado, 2 = alterado.
 * AUTO_TEXT="..." responde sem abrir janela (testes automatizados). */
void text_input_begin(const std::wstring &desc, const std::wstring &initial, int maxlen);
int text_input_poll(std::wstring *out);
static std::wstring rd_u16(uint32_t a, size_t max = 512) {
    std::wstring s; if (!a) return s;
    for (size_t i = 0; i < max; i++) { uint16_t ch = *(uint16_t *)mem_ptr(a + i * 2); if (!ch) break; s += (wchar_t)ch; }
    return s;
}
static uint32_t osk_field() { return rd32(g_osk.params + 0x34); }
static int osk_limit(uint32_t f) {
    int lim = (int)rd32(f + 0x30), len = (int)rd32(f + 0x24) - 1;
    if (lim <= 0 || (len > 0 && len < lim)) lim = len;
    return lim > 0 ? lim : 0;
}
static void osk_finish(bool ok, const std::wstring &txt) {
    uint32_t f = osk_field(), out = rd32(f + 0x28);
    std::wstring in = rd_u16(rd32(f + 0x20)), s = ok ? txt : in;
    int lim = osk_limit(f); if (lim && (int)s.size() > lim) s.resize(lim);
    if (out) { for (size_t i = 0; i < s.size(); i++) *(uint16_t *)mem_ptr(out + i * 2) = (uint16_t)s[i]; *(uint16_t *)mem_ptr(out + s.size() * 2) = 0; }
    wr32(f + 0x2c, !ok ? 1u : (s == in ? 0u : 2u));
    wr32(g_osk.params + 0x1c, ok ? 0u : 1u);
    fprintf(stderr, "[osk] %s: '%ls'\n", ok ? "confirmado" : "cancelado", s.c_str());
    g_osk.status = 3;
}
HLE(sceUtilityOskInitStart) {
    g_osk.params = ARG(0); g_osk.status = 1;
    uint32_t f = osk_field();
    std::wstring desc = rd_u16(rd32(f + 0x1c)), in = rd_u16(rd32(f + 0x20));
    fprintf(stderr, "[osk] campos=%u limite=%d desc='%ls' inicial='%ls'\n", rd32(g_osk.params + 0x30), osk_limit(f), desc.c_str(), in.c_str());
    if (!getenv("AUTO_TEXT")) text_input_begin(desc, in, osk_limit(f));
    RET(0);
}
HLE(sceUtilityOskGetStatus) {
    if (g_osk.status == 2) {
        if (const char *at = getenv("AUTO_TEXT")) {   /* lista "a|b|c": um item por OSK, na ordem */
            static int n = 0; int k = 0; std::wstring w;
            for (const char *q = at; *q; q++) { if (*q == '|') { k++; continue; } if (k == n) w += (wchar_t)(uint8_t)*q; }
            n++; osk_finish(true, w);
        }
        else { std::wstring t; int st = text_input_poll(&t); if (st >= 2) osk_finish(st == 2, t); }
    }
    RET(g_osk.status); if (g_osk.status == 1) g_osk.status = 2;
}
HLE(sceUtilityOskUpdate) { RET(0); }
HLE(sceUtilityOskShutdownStart) { g_osk.status = 0; RET(0); }

HLE(sceUtilityNetconfInitStart) { g_net.params = ARG(0); g_net.status = 1; RET(0); }
HLE(sceUtilityNetconfGetStatus) { RET(g_net.status); if (g_net.status == 1) g_net.status = 2; }
HLE(sceUtilityNetconfUpdate) { step(g_net, nullptr); RET(0); }
HLE(sceUtilityNetconfShutdownStart) { g_net.status = 0; RET(0); }
