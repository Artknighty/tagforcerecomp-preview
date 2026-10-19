/* Renderizador por GPU (Direct3D 11): desenha a "sombra" em resolucao interna maior (ver hle_ge.cpp) e apresenta na janela.
 * O rasterizador por software continua desenhando o quadro nativo 480x272 (o jogo le a VRAM de volta); a GPU refaz cada
 * primitiva dos buffers exibidos em N x a resolucao. Tudo roda na thread do jogo. */
#pragma once
#include <stdint.h>
#include <windows.h>
#include <vector>

struct GpuVtx { float x, y, z, u, v, rw; uint32_t col; };   /* x,y nativos (0..480, 0..272); z 0..65535; u,v em texels; rw = 1/w; col ABGR */
/* estado de uma chamada de desenho; so campos de 32 bits (comparado com memcmp para juntar primitivas em lotes) */
struct GpuState {
    int32_t fb;
    uint32_t ztest, zwrite, zfunc;
    uint32_t clear;                                  /* 0, ou bits 0x100 (cor) 0x200 (alfa) 0x400 (profundidade) do modo clear */
    uint32_t blend, bsf, bdf, beq, fix, sfix;        /* fix = cor fixa ABGR dos fatores 10; sfix = cor fixa do fator de origem quando as duas diferem (bsf = 11) */
    uint32_t atest, afunc, aref, amask;
    uint32_t tex, tw, th;                            /* handle da textura (0 = sem textura) e tamanho */
    uint32_t tfn, talpha, tdbl, tenv;
    uint32_t linear, clampu, clampv;
    int32_t sx1, sy1, sx2, sy2;                      /* scissor nativo, inclusivo */
    uint32_t cmask;                                  /* canais gravados na cor: 1 R, 2 G, 4 B, 8 A (mascara de escrita do PSP por canal) */
    /* stencil (no PSP e o alfa do framebuffer; na GPU fica num buffer de stencil de 8 bits): teste (ref & smask) FUNC (stencil & smask),
     * funcoes e operacoes com a numeracao do PSP; swmask = bits gravados. st = 0: stencil intocado */
    uint32_t st, sfunc, sref, smask, swmask, sfail, szfail, szpass;
    uint32_t aconst;                                 /* 0, ou 0x100 | valor: alfa gravado na cor e constante (o novo valor do stencil) */
    /* sprite 2D (de um atlas): a amostragem fica dentro do retangulo de UV do proprio sprite (texels do PSP). Sem isso o filtro linear numa
     * resolucao maior mistura a borda da peca com o vizinho do atlas (linha fina no meio da arte das cartas). uvclamp = 0: desligado */
    uint32_t uvclamp; float cu0, cv0, cu1, cv1;
};

extern double g_gpu_us[8];                          /* tempo acumulado (us) por etapa, para TF_GPU_DEBUG: 0 flush, 1 downsample, 2 present */
extern double g_gpu_wait_ms;                        /* ms esperando a GPU terminar um gpu_kick (gpu_wait_kicked) */
extern double g_gpu_pipe_ms;                        /* ms gastos criando pipelines (compilacao de sombreadores no driver); o display.cpp mostra nos quadros pesados */
double gpu_now_us(void);
/* backend de GPU: Direct3D 11 (gpu_d3d11.cpp) ou Vulkan (gpu_vulkan.cpp); as funcoes gpu_* (gpu.cpp) chamam o backend escolhido */
struct GpuBackend {
    const char *name;
    bool (*init)(HWND); bool (*ready)(void); void (*release_swapchain)(void); const char *(*adapter_name)(void);
    int (*fb_create)(int, int, int); void (*fb_destroy)(int);
    void (*fb_upload)(int, const uint32_t *, uint32_t, const uint16_t *, uint32_t);
    int (*check_start)(int); bool (*check_get)(int, uint32_t *); bool (*fb_readback)(int, std::vector<uint32_t> &, int *, int *);
    void (*draw)(const GpuState &, const GpuVtx *, int); void (*flush)(void);
    uint32_t (*tex_create)(int, int, const uint32_t *, bool); void (*tex_release)(uint32_t);
    void (*present_fb)(int); void (*present_pixels)(const uint32_t *, int, int); void (*present_last)(void); void (*set_overlay)(const uint32_t *);
    bool (*has_stencil)(void);
    bool (*ring_push)(int, int, int); bool (*ring_read)(int, std::vector<uint32_t> &, int *, int *);
    void (*kick)(void); double (*wait_kicked)(void);
    void (*hold)(void); void (*mid_scope)(bool);   /* opcionais (Vulkan): ordem na fila do 60/120 FPS */
    uint32_t (*tex_create_chain)(int, int, const uint32_t *, const uint32_t *);
    bool (*tex_update)(uint32_t, int, int, int, int, const uint32_t *, uint32_t);   /* opcional: troca um retangulo de uma textura (sem mipmaps), em ordem com os desenhos */   /* opcional: textura com os mipmaps ja prontos (niveis 1..n em sequencia) */
    uint32_t (*tex_create_bc7)(int, int, int, const uint8_t *);   /* opcional: textura BC7 com `levels` niveis em sequencia (bc7.h) */
    double (*wait_kicked_for)(double);   /* opcional: como wait_kicked, mas desiste depois de max_ms (devolve -1; o envio continua pendente) */
};
extern const GpuBackend gpu_backend_d3d11, gpu_backend_vulkan;
bool gpu_select(int renderer);   /* GFX_RENDER_D3D11 / GFX_RENDER_VULKAN: escolhe o backend (ainda sem iniciar); false = renderer invalido */
const char *gpu_backend_name(void);
bool gpu_init(HWND wnd);                             /* cria dispositivo e swapchain (idempotente); false = sem GPU */
bool gpu_ready(void);
void gpu_release_swapchain(void);                    /* ao voltar para o renderizador por software */
bool gpu_has_stencil(void);                          /* os quadros sombra tem buffer de stencil (GpuState::st funciona) */

enum { GPU_FBTEX = 0x40000000 };                    /* GpuState::tex = GPU_FBTEX | id do quadro sombra: usa a imagem da GPU desse quadro como textura (render-to-texture) */
int  gpu_fb_create(int nw, int nh, int res);         /* quadro sombra de nw x nh pixels nativos, res x maior (cor 8888 + profundidade; nw,nh <= 512); -1 se falhar */
void gpu_fb_destroy(int fb);
/* refaz o quadro sombra a partir do quadro nativo (cor 8888 com passo em pixels, profundidade 16 bits) */
void gpu_fb_upload(int fb, const uint32_t *col, uint32_t cstride, const uint16_t *z, uint32_t zstride);
int gpu_check_start(int fb);                                 /* comeca a reduzir o quadro sombra (media res x res) para comparar com o nativo; NAO espera a GPU; devolve um bilhete (-1 = falhou) */
bool gpu_check_get(int ticket, uint32_t *out480x272);        /* resultado (ABGR 480x272) do bilhete; espera a GPU se preciso (normalmente ja terminou, 1-2 quadros depois) */   /* media de cada bloco res x res (ABGR), para comparar com o nativo */
bool gpu_fb_readback(int fb, std::vector<uint32_t> &out, int *w, int *h);   /* imagem inteira em ABGR */

void gpu_draw(const GpuState &s, const GpuVtx *v, int n);   /* triangulos (lista), em coordenadas nativas */
void gpu_flush(void);                                       /* executa os desenhos acumulados */
void gpu_kick(void);                                        /* envia a GPU o que ja foi gravado, sem apresentar (o quadro intermediario fica pronto antes da hora de aparecer) */
double gpu_wait_kicked(void);
double gpu_wait_kicked_for(double max_ms);                  /* espera o ultimo gpu_kick ate max_ms: ms esperados, ou -1 se a GPU ainda nao terminou */
/* 60/120 FPS, ordem na fila: gpu_hold fecha o quadro real sem envia-lo; o intermediario gravado entre gpu_mid_scope(true) e (false) passa na frente
 * dele no gpu_kick seguinte (sem suporte no backend = gpu_kick comum). Qualquer outra gravacao antes solta o quadro real primeiro. */
void gpu_hold(void);
void gpu_mid_scope(bool on);                               /* espera a GPU terminar o ultimo gpu_kick; devolve os ms esperados (0 = ja tinha terminado) */
extern char g_gpu_prof_tag;                                 /* TF_GPU_PROF (Vulkan): o que e a proxima submissao: 'R' quadro real, 'M' intermediario (o resto marca sozinho) */
uint32_t gpu_tex_create(int w, int h, const uint32_t *abgr, bool mips = false, const uint32_t *chain = nullptr);   /* chain: niveis 1..n prontos (TexReplImg::mips); sem suporte no backend = mipmaps gerados na GPU */   /* mips: gera a piramide de mipmaps (texturas substitutas em alta resolucao) */
void gpu_tex_release(uint32_t h);
/* textura ja comprimida em BC7 (bc7.h: niveis 0..levels-1 em sequencia, w e h multiplos de 4); 0 = sem suporte/falhou */
uint32_t gpu_tex_create_bc7(int w, int h, int levels, const uint8_t *data);
bool gpu_tex_update(uint32_t h, int x, int y, int w, int hh, const uint32_t *px, uint32_t stride);   /* retangulo (x,y,w,hh) de px (passo em texels) na textura h; false = sem suporte (recrie a textura) */

void gpu_seq_tag(int tag);
void gpu_present_fb_tag(int fb, int tag);                    /* gpu_seq_tag + gpu_present_fb de uma vez (thread de apresentacao) */                                   /* diagnostico: o proximo gpu_present_fb e um quadro intermediario (1) ou real (0) */
/* caixa-preta dos quadros apresentados (F8, capturas.inc): anel de nslots imagens na VRAM; ring_push reduz o quadro sombra fb na propria GPU (media
 * n x n, n = (res+1)/2: ~960 px de largura) para a posicao slot, sem custo de CPU nem copia para a RAM; ring_read le uma posicao de volta (ABGR) */
bool gpu_ring_push(int fb, int slot, int nslots);
bool gpu_ring_read(int slot, std::vector<uint32_t> &out, int *w, int *h);
void gpu_present_fb(int fb);                                /* mostra o quadro sombra na janela */
void gpu_present_pixels(const uint32_t *xrgb, int w, int h);   /* mostra uma imagem 0x00RRGGBB (quadro nativo) */
void gpu_present_last(void);                                /* repinta o ultimo quadro (WM_PAINT) */
#define GPU_OVW 1920
#define GPU_OVH 1088
void gpu_set_overlay(const uint32_t *rgba);          /* overlay GPU_OVW x GPU_OVH (4x o quadro nativo) ABGR com alfa; nullptr desliga */
