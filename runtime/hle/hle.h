/* API de HLE (implementacao em C++ das syscalls/bibliotecas do PSP). */
#pragma once
#include "psp_rt.h"
#include <stdint.h>
#include <string>

typedef struct { uint32_t stub, nid; const char *lib, *name; } ImportEntry;
typedef struct { uint32_t addr; void (*fn)(CPU *); } FnEntry;
typedef void (*HleFn)(CPU *);
typedef struct { uint32_t nid, addr; const char *lib; } ExportEntry;
typedef struct {
    const char *name, *iso_path; uint32_t base, gp, memsz, textsz, datasz, bsssz, entry;
    const FnEntry *fns; unsigned nfns; const ImportEntry *imps; unsigned nimps; const ExportEntry *exps; unsigned nexps;
} GenModule;
extern "C" const GenModule g_gen_modules[]; extern "C" const unsigned g_gen_nmodules;
bool jit_init(void);   /* TF_JIT=1: compila os gen/ *.c em memoria com o TinyCC em vez de usar os modulos ligados (jit.cpp) */
bool jit_generic(void);   /* release build: no game code is linked in; it is translated from the installed game on first run and compiled in memory */
std::wstring code_dir(const std::string &id);   /* where the translated C code of this game version lives (prepare.cpp) */
bool code_ready(const std::string &id);
bool code_prepare(const std::string &id, const std::wstring &data, void (*cb)(double, const wchar_t *, void *), void *ctx, std::wstring *err);
const GenModule *gen_modules(unsigned *n);   /* tabela de modulos em uso (JIT ou estatica) */

void hle_register(const char *name, HleFn fn);
struct HleReg { HleReg(const char *n, HleFn f) { hle_register(n, f); } };

/* HLE(sceFoo) { ... }  -- argumentos: ARG(0..7), retorno: RET(v) */
#define HLE(name) static void hle_##name(CPU *c); static HleReg reg_##name(#name, hle_##name); static void hle_##name(CPU *c)
#define ARG(i) (c->r[4 + (i)])   /* a0-a3, t0-t3 = r4..r11 */
#define RET(v) (c->r[2] = (uint32_t)(v))
#define RET64(v) do { uint64_t v_ = (uint64_t)(v); c->r[2] = (uint32_t)v_; c->r[3] = (uint32_t)(v_ >> 32); } while (0)

/* ---- host / despacho ---- */
CPU *host_create(void);
void host_load(uint32_t addr, const void *p, size_t n);
void host_module_add(const GenModule *m);
void host_module_remove(const GenModule *m);
bool host_resolve_export(uint32_t nid, uint32_t *addr);
uint32_t module_load(const char *iso_path, bool *ok);   /* carrega um modulo recompilado do ISO; devolve uid */
uint32_t k_register_module(uint32_t base, uint32_t memsz, uint32_t textsz, uint32_t datasz, uint32_t bsssz, uint32_t gp, const char *name);
void k_unregister_module(uint32_t uid);
extern uint8_t *g_mem;
[[noreturn]] void host_fatal(int code);   /* erro fatal (HLE que falta, instrucao nao suportada...): grava o fim do log da sessao e sai com `code` */

/* ---- kernel ---- */
#define KERR_UNKNOWN_UID 0x800200cbu
#define KERR_TIMEOUT     0x800201a8u
#define KERR_NO_MEMORY   0x80020190u
struct Thread;
void k_init(void);
uint32_t k_alloc(uint32_t size, int type, uint32_t addr, const char *name);   /* devolve endereco, 0 = falha */
bool k_region_free(uint32_t start, uint32_t size);
uint32_t k_probe_low(uint32_t size);
void k_free(uint32_t addr);
uint32_t k_start_root(uint32_t entry, uint32_t gp, uint32_t arglen, uint32_t argp);
int k_run(void);                       /* agenda ate todas as threads terminarem; devolve codigo de saida */
uint64_t k_now_us(void);
void k_block_current(uint64_t timeout_us, bool has_timeout);
uint32_t k_current_uid(void);
void k_delay_current(uint64_t us);
uint32_t k_call_guest(uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);   /* roda funcao do jogo em contexto de interrupcao */

/* ---- utilitarios ---- */
inline const char *mem_cstr(uint32_t a) { return (const char *)(g_mem + MA(a)); }
inline uint32_t rd32(uint32_t a) { return *(uint32_t *)(g_mem + MA(a)); }
inline void wr32(uint32_t a, uint32_t v) { *(uint32_t *)(g_mem + MA(a)) = v; }
inline void wr64(uint32_t a, uint64_t v) { *(uint64_t *)(g_mem + MA(a)) = v; }
inline uint8_t *mem_ptr(uint32_t a) { return g_mem + MA(a); }
#include <vector>
struct DirEnt { std::string name; bool dir; uint32_t size; uint32_t lba; };
std::vector<DirEnt> iso_list_dir(uint32_t lba, uint32_t size);
bool iso_open(const char *path, bool cache = true);   /* abre a ISO (caminho da ISO do usuario); cache = copia a ISO para a RAM em segundo plano */
bool iso_open_folder(const wchar_t *dir);         /* usa a pasta instalada (jogo/): mesma imagem de disco, arquivos soltos (iso_install) */
void iso_close(void);
bool iso_folder_mode(void);
int64_t iso_find(const std::string &path, uint32_t *lba, uint32_t *size, bool *isdir);
bool parse_lbn_path(const std::string &rest, uint32_t *lba, uint32_t *size);   /* 'sce_lbn0xN_size0xM' */
int iso_read(uint32_t lba, uint32_t off, void *dst, uint32_t n);
void pctext_apply(uint32_t lba, uint64_t pos, uint8_t *buf, uint64_t n);   /* textos de PC no lugar dos de PSP (pc_text.cpp) */
