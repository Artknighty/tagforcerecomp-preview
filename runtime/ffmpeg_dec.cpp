/* Decodificacao ATRAC3 / ATRAC3plus (audio) e H.264 (video dos filmes PSMF) com o libavcodec do FFmpeg (pacote do MSYS2),
 * carregado DINAMICAMENTE: se as DLLs (avcodec-63.dll, avutil-61.dll e dependencias) nao estiverem ao lado do .exe nem em
 * C:\msys64\ucrt64\bin, os *_open devolvem nullptr e o jogo segue sem som ATRAC / sem filmes. Os cabecalhos so dao os
 * tipos; as funcoes vem de GetProcAddress, entao o .exe nao depende das DLLs para abrir. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <string>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
}
#include "ffmpeg_dec.h"

static struct {
    const AVCodec *(*find_decoder)(enum AVCodecID);
    AVCodecContext *(*alloc_context3)(const AVCodec *);
    int (*open2)(AVCodecContext *, const AVCodec *, AVDictionary **);
    int (*send_packet)(AVCodecContext *, const AVPacket *);
    int (*receive_frame)(AVCodecContext *, AVFrame *);
    void (*flush_buffers)(AVCodecContext *);
    void (*free_context)(AVCodecContext **);
    AVPacket *(*packet_alloc)(void);
    void (*packet_free)(AVPacket **);
    AVFrame *(*frame_alloc)(void);
    void (*frame_free)(AVFrame **);
    void *(*mallocz)(size_t);
    void (*ch_layout_default)(AVChannelLayout *, int);
    void (*avfree)(void *);   /* opcional: sem ela os buffers de av_mallocz vazam (como antes) */
} F;

static bool load_ffmpeg(void) {
    static int state = 0;   /* 0 = nao tentou, 1 = ok, -1 = indisponivel */
    if (state) return state > 0;
    state = -1;
    if (getenv("NO_ATRAC")) return false;
    char cname[32], uname[32];
    snprintf(cname, sizeof cname, "avcodec-%d.dll", LIBAVCODEC_VERSION_MAJOR);
    snprintf(uname, sizeof uname, "avutil-%d.dll", LIBAVUTIL_VERSION_MAJOR);
    HMODULE c = LoadLibraryA(cname), u = c ? LoadLibraryA(uname) : nullptr;
    if (!c) {   /* fallback: DLLs do MSYS2 (as dependencias sao procuradas na pasta da propria DLL) */
        std::string dir = "C:/msys64/ucrt64/bin/";
        c = LoadLibraryExA((dir + cname).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        u = c ? LoadLibraryExA((dir + uname).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
    }
    if (!c || !u) { fprintf(stderr, "[atrac] %s nao encontrado: musica, vozes e efeitos ATRAC ficam em silencio\n", cname); return false; }
    auto get = [](HMODULE m, auto &field, const char *name) {
        FARPROC p = GetProcAddress(m, name); memcpy(&field, &p, sizeof p);
        if (!p) fprintf(stderr, "[atrac] falta %s\n", name);
        return p != nullptr;
    };
#define LD(m, field, name) if (!get(m, F.field, name)) return false;
    LD(c, find_decoder, "avcodec_find_decoder") LD(c, alloc_context3, "avcodec_alloc_context3") LD(c, open2, "avcodec_open2")
    LD(c, send_packet, "avcodec_send_packet") LD(c, receive_frame, "avcodec_receive_frame") LD(c, flush_buffers, "avcodec_flush_buffers")
    LD(c, free_context, "avcodec_free_context") LD(c, packet_alloc, "av_packet_alloc") LD(c, packet_free, "av_packet_free")
    LD(u, frame_alloc, "av_frame_alloc") LD(u, frame_free, "av_frame_free") LD(u, mallocz, "av_mallocz")
    LD(u, ch_layout_default, "av_channel_layout_default")
#undef LD
    { FARPROC p = GetProcAddress(u, "av_free"); memcpy(&F.avfree, &p, sizeof p); }
    state = 1;
    fprintf(stderr, "[atrac] decodificador FFmpeg carregado (%s)\n", cname);
    return true;
}

struct AtracDec { AVCodecContext *ctx; AVPacket *pkt; AVFrame *frm; uint8_t *buf; int block; };

AtracDec *atracdec_open(bool plus, int chans, int block_align, const uint8_t *extra, int extra_len) {
    if (!load_ffmpeg() || block_align <= 0 || chans < 1 || chans > 2) return nullptr;
    const AVCodec *codec = F.find_decoder(plus ? AV_CODEC_ID_ATRAC3P : AV_CODEC_ID_ATRAC3);
    if (!codec) return nullptr;
    AtracDec *d = new AtracDec{};
    d->ctx = F.alloc_context3(codec); if (!d->ctx) { delete d; return nullptr; }
    d->ctx->sample_rate = 44100; d->ctx->block_align = block_align;
    F.ch_layout_default(&d->ctx->ch_layout, chans);
    if (extra && extra_len > 0) {
        d->ctx->extradata = (uint8_t *)F.mallocz((size_t)extra_len + AV_INPUT_BUFFER_PADDING_SIZE);
        memcpy(d->ctx->extradata, extra, (size_t)extra_len); d->ctx->extradata_size = extra_len;
    }
    if (F.open2(d->ctx, codec, nullptr) < 0) { fprintf(stderr, "[atrac] avcodec_open2 falhou (%s, %d canais, bloco %d)\n", plus ? "AT3+" : "AT3", chans, block_align); atracdec_close(d); return nullptr; }
    d->pkt = F.packet_alloc(); d->frm = F.frame_alloc();
    d->block = block_align;
    d->buf = (uint8_t *)F.mallocz((size_t)block_align + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!d->pkt || !d->frm || !d->buf) { atracdec_close(d); return nullptr; }
    return d;
}
void atracdec_close(AtracDec *d) {
    if (!d) return;
    if (d->ctx) F.free_context(&d->ctx);
    if (d->pkt) F.packet_free(&d->pkt);
    if (d->frm) F.frame_free(&d->frm);
    if (d->buf && F.avfree) F.avfree(d->buf);
    delete d;
}
void atracdec_flush(AtracDec *d) { if (d) F.flush_buffers(d->ctx); }

static inline int16_t to_s16(float f) { int v = (int)(f * 32768.f); return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
/* Decodifica um bloco; escreve ate max_frames quadros ESTEREO s16 em out (mono e duplicado). Devolve o numero de quadros. */
int atracdec_decode(AtracDec *d, const uint8_t *block, int16_t *out, int max_frames) {
    memcpy(d->buf, block, (size_t)d->block);
    d->pkt->data = d->buf; d->pkt->size = d->block;
    if (F.send_packet(d->ctx, d->pkt) < 0) return 0;
    int n = 0;
    while (F.receive_frame(d->ctx, d->frm) == 0) {
        AVFrame *f = d->frm; int ch = f->ch_layout.nb_channels;
        for (int i = 0; i < f->nb_samples && n < max_frames; i++, n++) {
            float l, r;
            if (f->format == AV_SAMPLE_FMT_FLTP) { l = ((const float *)f->data[0])[i]; r = ch > 1 ? ((const float *)f->data[1])[i] : l; }
            else if (f->format == AV_SAMPLE_FMT_FLT) { l = ((const float *)f->data[0])[i * ch]; r = ch > 1 ? ((const float *)f->data[0])[i * ch + 1] : l; }
            else if (f->format == AV_SAMPLE_FMT_S16P) { out[n * 2] = ((const int16_t *)f->data[0])[i]; out[n * 2 + 1] = ch > 1 ? ((const int16_t *)f->data[1])[i] : out[n * 2]; continue; }
            else { l = r = 0.f; }
            out[n * 2] = to_s16(l); out[n * 2 + 1] = to_s16(r);
        }
    }
    return n;
}

/* ------------------------------------------------------------------ H.264 (filmes) */
struct AvcDec { AVCodecContext *ctx; AVPacket *pkt; AVFrame *frm; uint8_t *buf; int cap; bool drained; };

AvcDec *avcdec_open(void) {
    if (!load_ffmpeg()) return nullptr;
    const AVCodec *codec = F.find_decoder(AV_CODEC_ID_H264);
    if (!codec) return nullptr;
    AvcDec *d = new AvcDec{};
    d->ctx = F.alloc_context3(codec); if (!d->ctx) { delete d; return nullptr; }
    d->ctx->thread_count = 1;   /* 480x272: uma thread basta e nao acrescenta atraso de quadros */
    if (F.open2(d->ctx, codec, nullptr) < 0) { fprintf(stderr, "[mpeg] avcodec_open2 (H.264) falhou\n"); avcdec_close(d); return nullptr; }
    d->pkt = F.packet_alloc(); d->frm = F.frame_alloc();
    if (!d->pkt || !d->frm) { avcdec_close(d); return nullptr; }
    return d;
}
void avcdec_close(AvcDec *d) {
    if (!d) return;
    if (d->ctx) F.free_context(&d->ctx);
    if (d->pkt) F.packet_free(&d->pkt);
    if (d->frm) F.frame_free(&d->frm);
    if (d->buf && F.avfree) F.avfree(d->buf);
    delete d;
}
void avcdec_flush(AvcDec *d) { if (d) { F.flush_buffers(d->ctx); d->drained = false; } }

static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
/* YUV 4:2:0 (BT.601, faixa limitada; faixa cheia no yuvj420p) -> formato do PSP */
static void yuv_to_psp(const AVFrame *f, uint8_t *dst, int stride, int psm, int max_w, int max_h) {
    int w = f->width < max_w ? f->width : max_w, h = f->height < max_h ? f->height : max_h;
    bool full = f->format == AV_PIX_FMT_YUVJ420P;
    for (int y = 0; y < h; y++) {
        const uint8_t *Y = f->data[0] + y * f->linesize[0], *U = f->data[1] + (y >> 1) * f->linesize[1], *V = f->data[2] + (y >> 1) * f->linesize[2];
        for (int x = 0; x < w; x++) {
            int c = full ? Y[x] * 256 : (Y[x] - 16) * 298, d = U[x >> 1] - 128, e = V[x >> 1] - 128;
            uint8_t r, g, b;
            if (full) { r = clamp8((c + 359 * e + 128) >> 8); g = clamp8((c - 88 * d - 183 * e + 128) >> 8); b = clamp8((c + 454 * d + 128) >> 8); }
            else { r = clamp8((c + 409 * e + 128) >> 8); g = clamp8((c - 100 * d - 208 * e + 128) >> 8); b = clamp8((c + 516 * d + 128) >> 8); }
            size_t i = (size_t)y * stride + x;
            switch (psm) {
            case 0: ((uint16_t *)dst)[i] = (uint16_t)((r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11)); break;
            case 1: ((uint16_t *)dst)[i] = (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000); break;
            case 2: ((uint16_t *)dst)[i] = (uint16_t)((r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | 0xf000); break;
            case 4: ((uint32_t *)dst)[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; break;   /* 0x00RRGGBB (apresentacao na janela, video substituto) */
            default: ((uint32_t *)dst)[i] = 0xff000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r; break;
            }
        }
    }
}
int avcdec_decode(AvcDec *d, const uint8_t *data, int size, uint8_t *dst, int stride, int psm, int max_w, int max_h) {
    int ret;
    if (data) {
        if (size + AV_INPUT_BUFFER_PADDING_SIZE > d->cap) {
            d->cap = size + AV_INPUT_BUFFER_PADDING_SIZE + 65536;
            if (d->buf && F.avfree) F.avfree(d->buf);
            d->buf = (uint8_t *)F.mallocz((size_t)d->cap);
            if (!d->buf) { d->cap = 0; return AVERROR(ENOMEM); }
        }
        memcpy(d->buf, data, (size_t)size); memset(d->buf + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        d->pkt->data = d->buf; d->pkt->size = size;
        ret = F.send_packet(d->ctx, d->pkt);
        if (ret == AVERROR(EAGAIN)) {   /* ha um quadro esperando: entrega ele e reenvia o pacote */
            if (F.receive_frame(d->ctx, d->frm) == 0) { yuv_to_psp(d->frm, dst, stride, psm, max_w, max_h); F.send_packet(d->ctx, d->pkt); return 1; }
            ret = F.send_packet(d->ctx, d->pkt);
        }
        if (ret < 0 && ret != AVERROR_INVALIDDATA) return ret;
    } else if (!d->drained) { F.send_packet(d->ctx, nullptr); d->drained = true; }
    if (F.receive_frame(d->ctx, d->frm) == 0) { yuv_to_psp(d->frm, dst, stride, psm, max_w, max_h); return 1; }
    return 0;
}
