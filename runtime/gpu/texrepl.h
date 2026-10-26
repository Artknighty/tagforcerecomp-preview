/* Texturas substitutas (mods): troca de texturas do jogo por PNGs de qualquer resolucao, identificadas pelo hash dos dados.
 * Formatos de pasta aceitos (mods/<Mod>/textures ou "textures=<pasta>" no mod.txt):
 *   - textures.ini no formato de pacotes de texturas do PPSSPP ([hashes] CHAVE = caminho.png; chave de 24 hex = 8 + 8 + 8, os
 *     ultimos 8 sao o hash dos dados) - os pacotes existentes funcionam sem conversao;
 *   - arquivos <hash8>.png ou <chave24>.png em qualquer subpasta (sem textures.ini).
 * O hash e o XXH64 (semente 0xBACD7814) dos bytes da textura como estao na memoria (32 bits baixos). So vale na GPU. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <memory>
#include <vector>

struct TexReplImg { int w, h; std::vector<uint32_t> px; std::vector<uint32_t> mips; std::vector<uint8_t> bc; int levels = 0; uint32_t sig = 0xffffffffu;
    size_t bytes() const { return (px.size() + mips.size()) * 4 + bc.size(); } };   /* RGBA (0xAABBGGRR); mips: niveis 1..n
   * em sequencia (media 2x2, como o blit linear da GPU), feitos nas threads de leitura: a GPU so copia (gerar na GPU custava ~5 ms por 2048x2048).
   * bc nao vazio = textura ja comprimida em BC7 (bc7.h; `levels` niveis em sequencia; px/mips vazios), vinda do cache em disco; sig = cor media (variantes) */
void texrepl_make_mips(TexReplImg &img);
/* proxima textura pre-carregada (texturas_recentes.txt) pronta na RAM e ainda nao oferecida, para ja subir para a GPU antes de a cena pedir;
 * so as sem variante por cor (a chave e o hash dos dados). false = nenhuma agora */
bool texrepl_take_preupload(uint32_t *hash, std::shared_ptr<TexReplImg> &img);
void texrepl_init(void);
void texrepl_reload(void);   /* os mods de textura ligados mudaram (Graphics > Textures): refaz o indice e descarta o que veio do pacote anterior; o cache da GPU fica com quem chama */
void texrepl_stats(int *queued, int *ready, uint64_t *loaded, uint64_t *failed, uint64_t *dropped);   /* fila, prontas nao consumidas, e totais da sessao (TF_GPU_DEBUG) */                       /* depois do mods_init */
void texrepl_stats2(uint64_t *ram_mb, uint64_t *ram_hits, uint64_t *decode_ms, int *threads);   /* cache em RAM (MB, acertos), tempo total de decodificacao, threads */
void texrepl_save_recent(void);   /* grava texturas_recentes.txt (pre-carregamento da proxima execucao) */
uint64_t texrepl_prefetched(void);   /* quantas o pre-carregamento ja colocou no cache */
void texrepl_bc_stats(uint64_t *done, uint64_t *ms, size_t *pending, uint64_t *disk_mb, uint64_t *sync);   /* compressao BC7: feitas, tempo total, fila de fundo, tamanho do cache em disco, lidas direto no pedido */
void texrepl_no_bc(void);                      /* a GPU recusou uma textura BC7: volta a ler os PNGs (RGBA) */
typedef void (*TexPrepProgress)(double frac, const wchar_t *msg, void *ctx);
bool texrepl_prewarm(const wchar_t *pack_dir, const wchar_t *app_dir, int card_style, TexPrepProgress cb, void *ctx, std::wstring *err);   /* launcher: fills the BC7 disk cache for the whole pack before the first run */
bool texrepl_active(void);                     /* ha substituicoes indexadas? */
uint32_t texrepl_hash(const void *data, size_t n);
/* 0 = sem substituicao; 1 = carregando (use o original e pergunte de novo depois); 2 = pronta (out; consumida: pedir de novo recarrega) */
/* Texturas indexadas (CLUT) com o mesmo dado e PNGs diferentes por paleta (ex.: moldura da carta por tipo: normal/efeito/magia...): o hash
 * da paleta do pacote nao e reproduzido, entao a variante e escolhida pela COR: target = cor media da textura original ja decodificada
 * com a paleta atual (texrepl_color); vale a variante cujo PNG tem a cor media mais proxima. */
bool texrepl_ambiguous(uint32_t hash);
uint32_t texrepl_color(const uint32_t *rgba, size_t n);   /* cor media (0x00BBGGRR) dos pixels opacos; 0xffffffff se nao ha */
int texrepl_get(uint32_t hash, uint32_t dim, int orig_w, int orig_h, std::shared_ptr<TexReplImg> &out, uint32_t target = 0xffffffffu);
/* ampliacao embutida (Scale2x/EPX, 1x -> 2x ou 4x) das texturas sem substituicao; em segundo plano como as substitutas */
void texrepl_upscale_request(uint32_t hash, int w, int h, const uint32_t *rgba, int scale);   /* idempotente por hash */
int texrepl_upscale_take(uint32_t hash, std::shared_ptr<TexReplImg> &out);   /* 0 = nao pedida/falhou, 1 = calculando, 2 = pronta (consumida) */
void texrepl_upscale_reset(void);   /* a escala mudou: descarta resultados */
/* despejo de texturas (TF_TEX_DUMP=pasta ou menu Mods): grava <hash8>.png das texturas usadas, para criar substituicoes */
bool texrepl_dumping(void);
void texrepl_set_dump(bool on);   /* liga/desliga (pasta texturas_despejo ao lado do .exe) */
void texrepl_dump(uint32_t hash, int w, int h, unsigned psm, uint32_t addr, const uint32_t *rgba, bool replaced);   /* replaced = o pacote tem substituta; as que faltam vao para <pasta>/faltam */
