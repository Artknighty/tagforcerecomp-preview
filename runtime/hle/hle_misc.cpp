/* HLE: bibliotecas simples (stdio, utils, umd, ctrl...). Vai crescendo conforme o jogo pede. */
#include "hle.h"
#include <stdio.h>
#include <string.h>

HLE(sceKernelPrintf) { fprintf(stderr, "[PSP printf] %s\n", mem_cstr(ARG(0))); RET(0); }
HLE(sceKernelStdin) { RET(0); }
HLE(sceKernelStdout) { RET(1); }
HLE(sceKernelStderr) { RET(2); }

/* ---- InterruptManager: sub-interrupcoes (vblank) ficam no kernel.cpp ---- */

/* ---- UtilsForUser ---- */
HLE(sceKernelLibcTime) { uint32_t t = 1700000000u + (uint32_t)(k_now_us() / 1000000); if (ARG(0)) wr32(ARG(0), t); RET(t); }
HLE(sceKernelLibcClock) { RET((uint32_t)k_now_us()); }
HLE(sceKernelLibcGettimeofday) { if (ARG(0)) { wr32(ARG(0), 1700000000u + (uint32_t)(k_now_us() / 1000000)); wr32(ARG(0) + 4, (uint32_t)(k_now_us() % 1000000)); } RET(0); }
HLE(sceKernelDcacheWritebackAll) { RET(0); }
HLE(sceKernelDcacheWritebackInvalidateAll) { RET(0); }
HLE(sceKernelGetGPI) { RET(0); }

/* ---- UMD ---- */
HLE(sceUmdActivate) { RET(0); }
HLE(sceUmdWaitDriveStat) { RET(0); }
HLE(sceUmdWaitDriveStatCB) { RET(0); }
HLE(sceUmdCancelWaitDriveStat) { RET(0); }
HLE(sceUmdGetDriveStat) { RET(0x32); }   /* PRESENT | INITED | READY */

/* ---- sceCtrl ---- */
uint32_t display_buttons(void);
uint32_t display_analog(void);
HLE(sceCtrlSetSamplingMode) { RET(0); }
static void ctrl_read(CPU *c) {
    uint32_t p = ARG(0), n = ARG(1) ? ARG(1) : 1;
    for (uint32_t i = 0; i < n; i++) { uint32_t q = p + i * 16; memset(mem_ptr(q), 0, 16); wr32(q, (uint32_t)k_now_us()); wr32(q + 4, display_buttons()); const uint32_t an = display_analog(); mem_ptr(q)[8] = an & 255; mem_ptr(q)[9] = (an >> 8) & 255; }
    RET(n);
}
HLE(sceCtrlReadBufferPositive) { ctrl_read(c); }
HLE(sceCtrlPeekBufferPositive) { ctrl_read(c); }

/* ---- sceUtility: modulos de sistema (carregar/descarregar so registram) ---- */
HLE(sceUtilityLoadAvModule) { RET(0); }
HLE(sceUtilityUnloadAvModule) { RET(0); }
HLE(sceUtilityLoadNetModule) { RET(0); }
HLE(sceUtilityUnloadNetModule) { RET(0); }
HLE(sceUtilityLoadUsbModule) { RET(0); }
HLE(sceUtilityUnloadUsbModule) { RET(0); }
HLE(sceUtilityGetSystemParamString) {   /* nome do jogador; sempre termina em zero dentro do tamanho pedido */
    uint32_t n = ARG(2) ? ARG(2) : 7;
    if (ARG(1) && n) { char *d = (char *)mem_ptr(ARG(1)); strncpy(d, "Player", n); d[n - 1] = 0; }
    RET(0);
}

/* ---- sceOpenPSID: ID de 16 bytes do console (fixo, para o save ser estavel entre execucoes) ---- */
HLE(sceOpenPSIDGetOpenPSID) {
    static const uint8_t id[16] = {0x10, 0x02, 0x00, 0x00, 0x54, 0x46, 0x52, 0x43, 0x4f, 0x4d, 0x50, 0x00, 0x00, 0x00, 0x00, 0x01};
    if (ARG(0)) memcpy(mem_ptr(ARG(0)), id, 16);
    RET(0);
}

/* ---- sceDmac: copia de memoria por DMA (o player de filmes copia o quadro decodificado para a VRAM) ---- */
void ge_vram_written(uint32_t addr, uint32_t size);
HLE(sceDmacMemcpy) { if (ARG(2)) { memmove(mem_ptr(ARG(0)), mem_ptr(ARG(1)), ARG(2)); ge_vram_written(ARG(0), ARG(2)); } RET(0); }
HLE(sceDmacTryMemcpy) { if (ARG(2)) { memmove(mem_ptr(ARG(0)), mem_ptr(ARG(1)), ARG(2)); ge_vram_written(ARG(0), ARG(2)); } RET(0); }
