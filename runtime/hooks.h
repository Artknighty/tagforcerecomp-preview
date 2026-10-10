/* Ganchos de codigo para mods: interceptam funcoes do jogo (recompiladas) pelo endereco.
 *   PspHook: chamado na ENTRADA da funcao, com os registradores do PSP em c->r[] (argumentos em r[4..7], resultado em r[2]).
 *   Devolve true = a funcao foi tratada (o corpo original NAO roda; o gancho deve ter posto o resultado em r[2]);
 *   false = segue o corpo original (gancho so observou/preparou). Para "embrulhar", o gancho chama hook_call_original() e depois
 *   devolve true. Valem so para a entrada principal da funcao. Ver docs/MODS.md. */
#pragma once
#include <stdint.h>
#include "psp_rt.h"
typedef bool (*PspHook)(CPU *c);
/* varios ganchos por endereco formam uma cadeia (ordem de instalacao); o primeiro que devolver true encerra. owner: identifica quem
 * instalou (plugins), ver g_hook_owner */
void hook_install(uint32_t addr, PspHook fn, void *owner = nullptr);
void hook_install_c(uint32_t addr, int (*fn)(CPU *), void *owner);   /* ganchos de plugins (ABI C: 1 = tratou) */
void hook_remove(uint32_t addr);                  /* tira todos os ganchos do endereco */
void hook_remove_owner(void *owner);              /* tira os ganchos de um dono (plugin que falhou no init) */
/* roda o corpo original. Dentro de um gancho deste endereco, os ganchos seguintes da cadeia rodam antes do corpo */
bool hook_call_original(CPU *c, uint32_t addr);
extern void *g_hook_owner;                        /* dono do gancho rodando agora */
