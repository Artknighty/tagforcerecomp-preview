/* Controle remoto do jogo (TF_CTL=<pasta>): o agente joga em ciclo fechado.
 * O runtime para no inicio de cada quadro e espera comandos em <pasta>/cmd.txt; responde em <pasta>/resp.txt.
 * Protocolo e cliente: tools/tf.py (docs em as notas do projeto, secao 4). */
#pragma once
#include <stdint.h>
extern volatile bool g_ctl_texdump_arm;   /* comando texdump: o proximo quadro despeja texturas e PRIMs (hle_ge.cpp) */
extern volatile bool g_ctl_nodraw;   /* pula a rasterizacao (hle_ge.cpp) */
bool ctl_active(void);
/* chamado no inicio de cada quadro; devolve true se o cliente pediu para sair */
bool ctl_frame(uint64_t frame);
uint32_t ctl_buttons(void);
/* captura pedida para o quadro em andamento (caminho do .bmp) ou nullptr */
const char *ctl_shot_path(void);
