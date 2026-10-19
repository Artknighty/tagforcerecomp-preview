/* Controle remoto por arquivos (TF_CTL=<pasta>). Comandos (uma linha em cmd.txt, "id comando args"):
 *   step N          roda N quadros e para
 *   nodraw 0|1      liga/desliga a rasterizacao (1 = rapido, capturas pretas)
 *   hold MASK       mantem esses botoes (hex, mascaras do AUTO_PRESS) ate o proximo hold
 *   shot ARQ.bmp    roda 1 quadro e grava a imagem
 *   peek ADDR LEN   le LEN bytes da RAM do PSP (ADDR hex) e responde em hex
 *   poke ADDR HEX   escreve bytes na RAM do PSP
 *   ping            responde o quadro atual
 *   quit            encerra o jogo
 * Resposta (resp.txt): "id ok <quadro> [dados]" ou "id erro <motivo>". */
#include "ctl.h"
#include "hle.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static const char *dir(void) { static const char *d = getenv("TF_CTL"); return d && *d ? d : nullptr; }
static uint32_t g_mask; static int64_t g_run; static std::string g_pend_id, g_shot; static bool g_shot_pending;

volatile bool g_ctl_texdump_arm = false;
volatile bool g_ctl_nodraw;
bool ctl_active(void) { return dir() != nullptr; }
uint32_t ctl_buttons(void) { return g_mask; }
const char *ctl_shot_path(void) { return g_shot_pending ? g_shot.c_str() : nullptr; }

static void reply(const std::string &id, uint64_t frame, const std::string &txt) {
    std::string tmp = std::string(dir()) + "/resp.tmp", fin = std::string(dir()) + "/resp.txt";
    if (FILE *f = fopen(tmp.c_str(), "wb")) {
        fprintf(f, "%s ok %llu%s%s", id.c_str(), (unsigned long long)frame, txt.empty() ? "" : " ", txt.c_str());
        fclose(f);
        MoveFileExA(tmp.c_str(), fin.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}
static void reply_err(const std::string &id, const char *why) {
    std::string tmp = std::string(dir()) + "/resp.tmp", fin = std::string(dir()) + "/resp.txt";
    if (FILE *f = fopen(tmp.c_str(), "wb")) { fprintf(f, "%s erro %s", id.c_str(), why); fclose(f); MoveFileExA(tmp.c_str(), fin.c_str(), MOVEFILE_REPLACE_EXISTING); }
}

bool ctl_frame(uint64_t frame) {
    if (!dir()) return false;
    if (g_shot_pending) g_shot_pending = false;   /* o quadro anterior gravou a captura */
    if (g_run > 0) { g_run--; if (g_run > 0) return false; }
    if (!g_pend_id.empty()) { reply(g_pend_id, frame, ""); g_pend_id.clear(); }
    std::string cmdf = std::string(dir()) + "/cmd.txt";
    for (;;) {
        FILE *f = fopen(cmdf.c_str(), "rb");
        if (!f) { Sleep(2); continue; }
        char line[4096]; size_t n = fread(line, 1, sizeof line - 1, f); fclose(f); line[n] = 0;
        DeleteFileA(cmdf.c_str());
        char id[64] = "", cmd[32] = ""; int off = 0;
        if (sscanf(line, "%63s %31s%n", id, cmd, &off) < 2) continue;
        const char *args = line + off;
        if (!strcmp(cmd, "ping")) { reply(id, frame, ""); continue; }
        if (!strcmp(cmd, "quit")) { reply(id, frame, ""); return true; }
        if (!strcmp(cmd, "nodraw")) { g_ctl_nodraw = atoi(args) != 0; reply(id, frame, ""); continue; }
        if (!strcmp(cmd, "texdump")) { g_ctl_texdump_arm = true; fprintf(stderr, "[ctl] texdump armado no quadro %llu\n", (unsigned long long)frame); reply(id, frame, ""); continue; }   /* proximo quadro desenhado: despeja texturas (gen/tex_*.bmp) e lista os PRIMs ([prim]) */
        if (!strcmp(cmd, "hold")) { g_mask = (uint32_t)strtoul(args, nullptr, 16); reply(id, frame, ""); continue; }
        if (!strcmp(cmd, "step")) { long long k = atoll(args); if (k < 1) k = 1; g_run = k; g_pend_id = id; return false; }
        if (!strcmp(cmd, "shot")) {
            while (*args == ' ') args++;
            std::string p = args; while (!p.empty() && (p.back() == '\n' || p.back() == '\r' || p.back() == ' ')) p.pop_back();
            g_shot = p; g_shot_pending = true; g_run = 1; g_pend_id = id; return false;
        }
        if (!strcmp(cmd, "peek")) {
            unsigned long a = 0, l = 0; if (sscanf(args, "%lx %lu", &a, &l) != 2 || l > 65536) { reply_err(id, "uso: peek ADDR LEN"); continue; }
            std::string hex; hex.reserve(l * 2); const uint8_t *p = (const uint8_t *)mem_ptr((uint32_t)a);
            for (unsigned long i = 0; i < l; i++) { char b[3]; snprintf(b, sizeof b, "%02x", p[i]); hex += b; }
            reply(id, frame, hex); continue;
        }
        if (!strcmp(cmd, "poke")) {
            unsigned long a = 0; char hex[8192] = ""; if (sscanf(args, "%lx %8191s", &a, hex) != 2) { reply_err(id, "uso: poke ADDR HEX"); continue; }
            uint8_t *p = (uint8_t *)mem_ptr((uint32_t)a);
            for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) { char b[3] = {hex[i], hex[i + 1], 0}; *p++ = (uint8_t)strtoul(b, nullptr, 16); }
            reply(id, frame, ""); continue;
        }
        reply_err(id, "comando desconhecido");
    }
}
