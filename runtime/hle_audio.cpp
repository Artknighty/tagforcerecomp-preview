/* HLE: audio (sceAudio, sceSasCore, sceAtrac3plus). Saida real via runtime/audio_out.cpp; SAS decodifica VAG (ADPCM) de
 * verdade; ATRAC via FFmpeg (ffmpeg_dec.cpp; sem as DLLs, silencio com a temporizacao certa). */
#include "hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <vector>
#include "ffmpeg_dec.h"

void audio_out_init(void);
void audio_out_mix(uint64_t *cursor, const int16_t *src, uint32_t frames, bool stereo, int lvol, int rvol);

/* ---- sceAudio: 8 canais; formato 0 = estereo, 0x10 = mono; volume 0..0x8000 ---- */
struct AudioCh { uint32_t samples = 1024, fmt = 0; int lvol = 0x8000, rvol = 0x8000; uint64_t cursor = 0; };
static AudioCh g_ch[8];
static uint32_t g_next_ch;

HLE(sceAudioChReserve) {   /* (ch, samples, fmt) */
    audio_out_init();
    int ch = (int)ARG(0); if (ch < 0 || ch > 7) ch = (int)(g_next_ch++ & 7);
    g_ch[ch].samples = ARG(1); g_ch[ch].fmt = ARG(2); g_ch[ch].cursor = 0; RET(ch);
}
HLE(sceAudioChRelease) { RET(0); }
HLE(sceAudioSetChannelDataLen) { if (ARG(0) < 8) g_ch[ARG(0)].samples = ARG(1); RET(0); }
HLE(sceAudioChangeChannelConfig) { if (ARG(0) < 8) g_ch[ARG(0)].fmt = ARG(1); RET(0); }
HLE(sceAudioChangeChannelVolume) { if (ARG(0) < 8) { g_ch[ARG(0)].lvol = (int)ARG(1); g_ch[ARG(0)].rvol = (int)ARG(2); } RET(0); }
HLE(sceAudioGetChannelRestLength) { RET(0); }
static void audio_out(CPU *c, uint32_t ch, int lvol, int rvol, uint32_t buf, bool block) {
    AudioCh &a = g_ch[ch & 7];
    if (buf) audio_out_mix(&a.cursor, (const int16_t *)mem_ptr(buf), a.samples, a.fmt != 0x10, lvol, rvol);
    static const bool dump = getenv("AUDIO_DUMP") != nullptr;   /* depuracao: gen/audio_ch<n>.raw (s16, 44.1 kHz) */
    if (dump && buf) {
        static FILE *f[8]; char fn[64];
        if (!f[ch & 7]) { snprintf(fn, sizeof fn, "gen/audio_ch%u_%s.raw", ch & 7, a.fmt == 0x10 ? "mono" : "stereo"); f[ch & 7] = fopen(fn, "wb"); }
        if (f[ch & 7]) fwrite(mem_ptr(buf), a.fmt == 0x10 ? 2 : 4, a.samples, f[ch & 7]);
    }
    if (block) k_delay_current((uint64_t)a.samples * 1000000ull / 44100ull);   /* espera a duracao do bloco (tempo virtual) */
    RET(0);
}
HLE(sceAudioOutputBlocking) { audio_out(c, ARG(0), (int)ARG(1), (int)ARG(1), ARG(2), true); }
HLE(sceAudioOutputPannedBlocking) { audio_out(c, ARG(0), (int)ARG(1), (int)ARG(2), ARG(3), true); }
HLE(sceAudioOutputPanned) { audio_out(c, ARG(0), (int)ARG(1), (int)ARG(2), ARG(3), false); }
HLE(sceAudioOutput) { audio_out(c, ARG(0), (int)ARG(1), (int)ARG(1), ARG(2), false); }

/* ---- Sas: 32 vozes VAG (ADPCM do PSP, blocos de 16 bytes = 28 amostras) ou PCM16, com pitch (0x1000 = 44.1 kHz,
 * interpolacao linear), volume por lado (0..0x1000) e envelope simples (ataque imediato, release curto no KeyOff).
 * GetEndFlag reflete o fim real da amostra: o jogo espera efeitos/vozes acabarem (ex.: "Here I go!" do duelo). ---- */
struct SasVoice {
    uint32_t addr = 0, size = 0; bool pcm = false, loopmode = false, on = false, paused = false;
    uint32_t pitch = 0x1000; int lvol = 0x1000, rvol = 0x1000;
    uint32_t blk = 0, loop_blk = 0; bool has_loop = false; int s1 = 0, s2 = 0; int16_t dec[28]; int di = 28; bool dend = false;
    int32_t pcm_loop = -1; uint32_t pcm_pos = 0;
    int cur = 0, nxt = 0; uint32_t frac = 0;   /* frac em 1/4096 */
    float env = 0.f; bool release = false;
};
static SasVoice g_sas[32];
static uint32_t g_grain = 256;
static SasVoice *sas_voice(uint32_t v) { return v < 32 ? &g_sas[v] : nullptr; }

static bool vag_block(SasVoice &v) {   /* decodifica o proximo bloco em v.dec; false = fim */
    static const int f0[5] = {0, 60, 115, 98, 122}, f1[5] = {0, 0, -52, -55, -60};
    for (;;) {
        if (v.dend || (v.blk + 1) * 16 > v.size) return false;
        const uint8_t *b = mem_ptr(v.addr + v.blk * 16);
        int shift = b[0] & 15, pred = std::min(4, b[0] >> 4), flags = b[1];
        if (flags == 7) { v.dend = true; return false; }            /* marcador de fim: nao toca este bloco */
        if (flags & 4) { v.loop_blk = v.blk; v.has_loop = true; }   /* inicio de laco */
        for (int i = 0; i < 28; i++) {
            int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 15;
            int s = ((int16_t)(nib << 12)) >> shift;
            s += (v.s1 * f0[pred] + v.s2 * f1[pred]) >> 6;
            s = std::max(-32768, std::min(32767, s)); v.s2 = v.s1; v.s1 = s; v.dec[i] = (int16_t)s;
        }
        v.di = 0;
        if (flags & 1) {   /* fim do trecho: repete a partir do inicio do laco (flag 2 / loopmode) ou acaba depois deste bloco */
            if (((flags & 2) || v.loopmode) && v.has_loop) v.blk = v.loop_blk; else v.dend = true;
        } else v.blk++;
        return true;
    }
}
static bool sas_next(SasVoice &v, int *out) {   /* proxima amostra decodificada na taxa original */
    if (v.pcm) {
        if (v.pcm_pos >= v.size) { if (v.pcm_loop >= 0 && (uint32_t)v.pcm_loop < v.size) v.pcm_pos = (uint32_t)v.pcm_loop; else return false; }
        *out = *(const int16_t *)mem_ptr(v.addr + v.pcm_pos * 2); v.pcm_pos++; return true;
    }
    if (v.di >= 28) { bool was_end = v.dend; if (was_end || !vag_block(v)) return false; }
    *out = v.dec[v.di++]; return true;
}
static void sas_start(SasVoice &v) {
    v.blk = 0; v.has_loop = false; v.s1 = v.s2 = 0; v.di = 28; v.dend = false; v.pcm_pos = 0; v.frac = 0;
    v.env = 1.f; v.release = false; v.on = true;
    if (!sas_next(v, &v.cur)) { v.on = false; return; }
    if (!sas_next(v, &v.nxt)) v.nxt = 0;
}
static void sas_mix(int32_t *acc, uint32_t n) {   /* acumula todas as vozes em acc (estereo, n quadros) */
    for (auto &v : g_sas) {
        if (!v.on || v.paused) continue;
        for (uint32_t i = 0; i < n && v.on; i++) {
            int s = v.cur + (int)(((int64_t)(v.nxt - v.cur) * (int32_t)v.frac) >> 12);
            s = (int)(s * v.env);
            acc[i * 2] += (s * v.lvol) >> 12; acc[i * 2 + 1] += (s * v.rvol) >> 12;
            if (v.release) { v.env -= 1.f / 1024.f; if (v.env <= 0.f) { v.on = false; break; } }
            v.frac += v.pitch;
            while (v.frac >= 4096) {
                v.frac -= 4096; v.cur = v.nxt;
                int t; if (sas_next(v, &t)) v.nxt = t; else { v.nxt = 0; if (v.cur == 0) { v.on = false; break; } }
            }
        }
    }
}
HLE(__sceSasInit) { g_grain = ARG(1) ? ARG(1) : 256; for (auto &v : g_sas) v = SasVoice{}; audio_out_init(); RET(0); }
HLE(__sceSasSetGrain) { g_grain = ARG(1) ? ARG(1) : 256; RET(0); }
HLE(__sceSasGetGrain) { RET(g_grain); }
HLE(__sceSasSetVoice) {   /* (core, voice, vag, size, loopmode) */
    if (SasVoice *v = sas_voice(ARG(1))) { v->addr = ARG(2); v->size = ARG(3); v->pcm = false; v->loopmode = ARG(4) != 0; }
    static const bool tr = getenv("SAS_TRACE") != nullptr;
    if (tr) {
        fprintf(stderr, "[sas] voz %u vag=%08x tam=%u laco=%u:", ARG(1), ARG(2), ARG(3), ARG(4));
        for (int i = 0; i < 48; i++) fprintf(stderr, " %02x", *mem_ptr(ARG(2) + i));
        fputc(10, stderr);
        char fn[64]; snprintf(fn, sizeof fn, "gen/sas_%08x.vag", ARG(2));
        if (FILE *f = fopen(fn, "wb")) { fwrite(mem_ptr(ARG(2)), 1, ARG(3), f); fclose(f); }
    }
    RET(0);
}
HLE(__sceSasSetVoicePCM) {   /* (core, voice, pcm, samples, loopPos) */
    if (SasVoice *v = sas_voice(ARG(1))) { v->addr = ARG(2); v->size = ARG(3); v->pcm = true; v->pcm_loop = (int32_t)ARG(4); }
    RET(0);
}
HLE(__sceSasSetPitch) { if (SasVoice *v = sas_voice(ARG(1))) v->pitch = std::min(ARG(2), 0x4000u); RET(0); }
HLE(__sceSasSetVolume) {   /* (core, voice, l, r, efeito_l, efeito_r) */
    if (SasVoice *v = sas_voice(ARG(1))) { v->lvol = (int)(int32_t)ARG(2); v->rvol = (int)(int32_t)ARG(3); }
    RET(0);
}
HLE(__sceSasSetKeyOn) { if (SasVoice *v = sas_voice(ARG(1))) sas_start(*v); RET(0); }
HLE(__sceSasSetKeyOff) { if (SasVoice *v = sas_voice(ARG(1))) if (v->on) v->release = true; RET(0); }
HLE(__sceSasSetPause) {   /* (core, mascara, pausar) */
    for (int i = 0; i < 32; i++) if (ARG(1) & (1u << i)) g_sas[i].paused = ARG(2) != 0;
    RET(0);
}
HLE(__sceSasGetPauseFlag) { uint32_t m = 0; for (int i = 0; i < 32; i++) if (g_sas[i].paused) m |= 1u << i; RET(m); }
HLE(__sceSasCore) {   /* (core, out): gera 'grain' quadros estereo */
    static int32_t acc[4096 * 2]; uint32_t n = std::min(g_grain, 4096u);
    memset(acc, 0, n * 8); sas_mix(acc, n);
    if (ARG(1)) { int16_t *o = (int16_t *)mem_ptr(ARG(1)); for (uint32_t i = 0; i < n * 2; i++) o[i] = (int16_t)std::max(-32768, std::min(32767, acc[i])); }
    RET(0);
}
HLE(__sceSasCoreWithMix) {   /* (core, inout, lvol, rvol): soma as vozes ao que ja esta no buffer */
    static int32_t acc[4096 * 2]; uint32_t n = std::min(g_grain, 4096u);
    memset(acc, 0, n * 8); sas_mix(acc, n);
    if (ARG(1)) {
        int16_t *o = (int16_t *)mem_ptr(ARG(1)); int lv = (int)ARG(2), rv = (int)ARG(3);
        for (uint32_t i = 0; i < n; i++) {
            int l = ((o[i * 2] * lv) >> 12) + acc[i * 2], r = ((o[i * 2 + 1] * rv) >> 12) + acc[i * 2 + 1];
            o[i * 2] = (int16_t)std::max(-32768, std::min(32767, l)); o[i * 2 + 1] = (int16_t)std::max(-32768, std::min(32767, r));
        }
    }
    RET(0);
}
HLE(__sceSasGetEndFlag) { uint32_t m = 0; for (int i = 0; i < 32; i++) if (!g_sas[i].on) m |= 1u << i; RET(m); }
HLE(__sceSasGetEnvelopeHeight) { SasVoice *v = sas_voice(ARG(1)); RET(v && v->on ? (uint32_t)(v->env * 0x3fffffff) : 0); }
HLE(__sceSasSetSimpleADSR) { RET(0); }
HLE(__sceSasSetADSR) { RET(0); }
HLE(__sceSasSetADSRmode) { RET(0); }
HLE(__sceSasSetNoise) { RET(0); }
HLE(__sceSasSetSL) { RET(0); }
HLE(__sceSasRevType) { RET(0); }
HLE(__sceSasRevParam) { RET(0); }
HLE(__sceSasRevEVOL) { RET(0); }
HLE(__sceSasRevVON) { RET(0); }

/* ---- Atrac3 / Atrac3plus (musica, vozes e os efeitos do duelo: setar/invocar cartas sao AT3 estereo curtos) ----
 * Le o cabecalho RIFF (fmt/fact/smpl/data): codec, canais, bloco, extradata, total de amostras, laco e o deslocamento da
 * primeira amostra. A temporizacao (amostras por chamada: 2048 no AT3+, 1024 no AT3; fim e laco) e independente do som:
 * sem ela o jogo espera para sempre a voz terminar. O PCM vem do FFmpeg (ffmpeg_dec.cpp), decodificado sob demanda bloco
 * a bloco; sem FFmpeg, silencio. Saida sempre estereo s16 (as variantes MOut, mono, nao sao usadas pelo jogo). */
struct Atrac {
    bool used; uint32_t buf, size, total, pos, spf, chans; int loopnum; int32_t loop_start, loop_end;
    bool plus; uint32_t data_off, data_size, block, extra_off, extra_len, skip;
    AtracDec *dec; std::vector<int16_t> pcm; size_t pcm_rd; int64_t pcm_pos; uint32_t nblk;   /* pcm[pcm_rd] = amostra pcm_pos */
};
static Atrac g_atrac[6];
static Atrac *atrac(uint32_t id) { return id < 6 && g_atrac[id].used ? &g_atrac[id] : nullptr; }
static void atrac_parse(Atrac &a) {
    const uint8_t *p = mem_ptr(a.buf); uint32_t n = a.size;
    a.total = 0; a.spf = 2048; a.chans = 2; a.loop_start = a.loop_end = -1;
    if (n < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return;
    uint32_t off = 12;
    while (off + 8 <= n) {
        uint32_t csz = *(const uint32_t *)(p + off + 4); const uint8_t *c = p + off + 8;
        if (csz > n - off - 8) break;   /* bloco maior que o arquivo (dado corrompido): o deslocamento daria a volta em 32 bits (laco infinito) */
        if (!memcmp(p + off, "fmt ", 4)) {
            uint16_t fmt = *(const uint16_t *)c; a.chans = *(const uint16_t *)(c + 2); a.block = *(const uint16_t *)(c + 12);
            a.plus = fmt != 0x270; a.spf = a.plus ? 2048 : 1024;   /* 0x270 = ATRAC3; 0xfffe (extensivel) = ATRAC3plus */
            uint16_t cb = csz >= 18 ? *(const uint16_t *)(c + 16) : 0;
            if (fmt == 0xfffe && cb >= 22) { a.extra_off = off + 8 + 18 + 22; a.extra_len = cb - 22u; }   /* pula a parte "extensivel" */
            else if (cb) { a.extra_off = off + 8 + 18; a.extra_len = cb; }                                /* AT3: 14 bytes */
        } else if (!memcmp(p + off, "fact", 4) && csz >= 4) {
            a.total = *(const uint32_t *)c;
            if (csz >= 8) a.skip = *(const uint32_t *)(c + 4);
        } else if (!memcmp(p + off, "smpl", 4) && csz >= 36 + 24 && *(const uint32_t *)(c + 28)) {
            a.loop_start = *(const int32_t *)(c + 36 + 8); a.loop_end = *(const int32_t *)(c + 36 + 12);
        } else if (!memcmp(p + off, "data", 4)) { a.data_off = off + 8; a.data_size = csz; break; }
        off += 8 + ((csz + 1) & ~1u);
    }
    if (!a.total && a.block) a.total = a.data_size / a.block * a.spf;
    /* Coordenadas: a amostra 0 que o jogo ve e a amostra bruta fact[1] + atraso fixo do codec (368 no AT3+, 69 no AT3) do
     * fluxo decodificado; os pontos do "smpl" estao em amostras BRUTAS e precisam do mesmo desconto. Sem isso o fim do laco
     * ficava depois do fim da musica (2207375 > 2205182) e o mixer do jogo (0885ae3c), que faz o laco pelas posicoes de
     * GetSoundSample, dava a musica por encerrada ~50 s depois de comecar o duelo. */
    a.skip += a.plus ? 368 : 69;
    if (a.loop_start >= 0) {
        a.loop_start = std::max<int32_t>(0, a.loop_start - (int32_t)a.skip);
        a.loop_end = std::max<int32_t>(a.loop_start, a.loop_end - (int32_t)a.skip);
    }
}
static void atrac_free(Atrac &a) { atracdec_close(a.dec); a.dec = nullptr; a.pcm.clear(); a.pcm.shrink_to_fit(); }
/* n quadros estereo a partir da amostra pos (coordenadas do jogo) em out; reposiciona o decodificador se pos nao for a
 * continuacao do que ja foi decodificado (laco, ResetPlayPosition). Ao reposicionar decodifica um bloco antes do alvo e
 * o descarta: o MDCT precisa do bloco anterior para a sobreposicao. */
static void atrac_fetch(Atrac &a, uint32_t pos, uint32_t n, int16_t *out) {
    if (!a.dec) { memset(out, 0, n * 4); return; }
    size_t avail = (a.pcm.size() - a.pcm_rd) / 2;
    if ((int64_t)pos < a.pcm_pos || (int64_t)pos > a.pcm_pos + (int64_t)avail + a.spf) {
        int64_t raw = (int64_t)pos + a.skip, b = raw / a.spf, start = b > 0 ? b - 1 : 0;
        atracdec_flush(a.dec); a.pcm.clear(); a.pcm_rd = 0; a.nblk = (uint32_t)start; a.pcm_pos = start * a.spf - (int64_t)a.skip;
    }
    static int16_t tmp[4096 * 2];
    for (;;) {
        avail = (a.pcm.size() - a.pcm_rd) / 2;
        if (a.pcm_pos + (int64_t)avail >= (int64_t)pos + n) break;
        uint64_t boff = (uint64_t)a.data_off + (uint64_t)a.nblk * a.block;
        if ((uint64_t)(a.nblk + 1) * a.block > a.data_size || boff + a.block > a.size) break;   /* fim dos dados ou do buffer */
        int got = atracdec_decode(a.dec, mem_ptr(a.buf + (uint32_t)boff), tmp, 4096);
        static const bool tr = getenv("ATRAC_TRACE") != nullptr;
        if (tr && got != (int)a.spf) fprintf(stderr, "[atrac] bloco %u (pos %u) decodificou %d amostras\n", a.nblk, pos, got);
        a.nblk++;
        if (a.pcm_rd > (1u << 16)) { a.pcm.erase(a.pcm.begin(), a.pcm.begin() + (ptrdiff_t)a.pcm_rd); a.pcm_rd = 0; }
        a.pcm.insert(a.pcm.end(), tmp, tmp + got * 2);
    }
    if ((int64_t)pos > a.pcm_pos) {   /* descarta o que vem antes de pos */
        size_t drop = (size_t)std::min<int64_t>((int64_t)pos - a.pcm_pos, (int64_t)(a.pcm.size() - a.pcm_rd) / 2);
        a.pcm_rd += drop * 2; a.pcm_pos += (int64_t)drop;
    }
    avail = (a.pcm.size() - a.pcm_rd) / 2;
    uint32_t k = (uint32_t)std::min<size_t>(n, avail);
    memcpy(out, a.pcm.data() + a.pcm_rd, (size_t)k * 4);
    if (k < n) memset(out + k * 2, 0, (size_t)(n - k) * 4);
    a.pcm_rd += (size_t)k * 2; a.pcm_pos += k;
}
HLE(sceAtracSetDataAndGetID) {   /* (buf, size) */
    for (uint32_t i = 0; i < 6; i++) if (!g_atrac[i].used) {
        Atrac &a = g_atrac[i]; atrac_free(a); a = Atrac{}; a.used = true; a.buf = ARG(0); a.size = ARG(1); atrac_parse(a);
        if (a.block && a.data_off)
            a.dec = atracdec_open(a.plus, (int)a.chans, (int)a.block, a.extra_len ? mem_ptr(a.buf + a.extra_off) : nullptr, (int)a.extra_len);
        a.pcm_pos = 1ll << 40;   /* forca reposicionar na primeira leitura */
        static const bool tr = getenv("ATRAC_TRACE") != nullptr;
        if (tr) fprintf(stderr, "[atrac] id %u: %s %u amostras, %u/quadro, %u canais, bloco %u, extra %u, skip %u, dados %u+%u (buffer %u), laco %d..%d%s\n",
                        i, a.plus ? "AT3+" : "AT3", a.total, a.spf, a.chans, a.block, a.extra_len, a.skip, a.data_off, a.data_size, a.size,
                        a.loop_start, a.loop_end, a.dec ? "" : " [sem decodificador]");
        RET(i); return;
    }
    RET(0x80630003u);   /* sem IDs livres */
}
HLE(sceAtracReinit) { RET(0); }
HLE(sceAtracReleaseAtracID) { if (ARG(0) < 6) { g_atrac[ARG(0)].used = false; atrac_free(g_atrac[ARG(0)]); } RET(0); }
HLE(sceAtracResetPlayPosition) { if (Atrac *a = atrac(ARG(0))) a->pos = ARG(1); RET(0); }
HLE(sceAtracSetLoopNum) {
    Atrac *a = atrac(ARG(0)); if (!a) { RET(0x80630007u); return; }
    if (a->loop_start < 0) { RET(0x80630021u); return; }   /* sem informacao de laco */
    a->loopnum = (int)ARG(1); RET(0);
}
HLE(sceAtracGetMaxSample) { Atrac *a = atrac(ARG(0)); if (ARG(1)) wr32(ARG(1), a ? a->spf : 2048); RET(0); }
HLE(sceAtracGetSoundSample) {   /* (id, *endSample, *loopStart, *loopEnd) */
    Atrac *a = atrac(ARG(0)); if (!a) { RET(0x80630007u); return; }
    if (ARG(1)) wr32(ARG(1), a->total ? a->total - 1 : 0);
    if (ARG(2)) wr32(ARG(2), (uint32_t)a->loop_start); if (ARG(3)) wr32(ARG(3), (uint32_t)a->loop_end);
    RET(0);
}
HLE(sceAtracGetLoopStatus) {   /* (id, *loopNum, *loopStatus) */
    Atrac *a = atrac(ARG(0));
    if (ARG(1)) wr32(ARG(1), a ? (uint32_t)a->loopnum : 0); if (ARG(2)) wr32(ARG(2), a && a->loop_start >= 0 ? 1 : 0);
    RET(0);
}
HLE(sceAtracGetRemainFrame) { if (ARG(1)) wr32(ARG(1), 0xffffffffu); RET(0); }   /* arquivo todo na memoria */
HLE(sceAtracGetNextSample) { Atrac *a = atrac(ARG(0)); if (ARG(1)) wr32(ARG(1), a && a->pos < a->total ? std::min(a->spf, a->total - a->pos) : 0); RET(0); }
HLE(sceAtracGetNextDecodePosition) { Atrac *a = atrac(ARG(0)); if (!a || a->pos >= a->total) { RET(0x80630024u); return; } if (ARG(1)) wr32(ARG(1), a->pos); RET(0); }
HLE(sceAtracDecodeData) {   /* (id, out, *samples, *finish, *remainFrame) */
    Atrac *a = atrac(ARG(0)); if (!a) { RET(0x80630007u); return; }
    uint32_t n = 0, prev_pos = a->pos; bool end = false;
    /* laco (pontos ja convertidos em atrac_parse; o min com o total e so protecao). Sem voltar ao inicio do laco, a musica
     * "acabava" e o mixer do jogo girava sem bloquear (travamento). */
    bool looping = a->loopnum != 0 && a->loop_start >= 0 && (uint32_t)a->loop_start < a->total;
    uint32_t lim = a->total;
    if (looping && a->loop_end >= 0) lim = std::min(lim, (uint32_t)a->loop_end + 1);
    if (looping && a->pos >= lim) { a->pos = (uint32_t)a->loop_start; if (a->loopnum > 0) a->loopnum--; }
    if (a->pos >= lim) end = true;
    else {
        n = std::min(a->spf, lim - a->pos);
        if (ARG(1)) atrac_fetch(*a, a->pos, n, (int16_t *)mem_ptr(ARG(1)));
        static const bool dump = getenv("AUDIO_DUMP") != nullptr;   /* PCM entregue ao jogo por ID: gen/atrac_<id>.raw */
        if (dump && ARG(1)) {
            static FILE *f[6]; char fn[64];
            if (!f[ARG(0)]) { snprintf(fn, sizeof fn, "gen/atrac_%u.raw", ARG(0)); f[ARG(0)] = fopen(fn, "wb"); }
            if (f[ARG(0)]) fwrite(mem_ptr(ARG(1)), 4, n, f[ARG(0)]);
        }
        a->pos += n;
        if (a->pos >= lim) {
            if (looping) { a->pos = (uint32_t)a->loop_start; if (a->loopnum > 0) a->loopnum--; }
            else end = true;
        }
    }
    static const bool tr = getenv("ATRAC_TRACE") != nullptr;
    if (tr && (end || a->pos < prev_pos)) {
        extern uint64_t display_frames(void);
        fprintf(stderr, "[atrac] id %u quadro %llu: %s (pos %u -> %u, total %u, loopnum %d)\n", ARG(0), (unsigned long long)display_frames(),
                end ? "fim" : "laco", prev_pos, a->pos, a->total, a->loopnum);
    }
    if (ARG(2)) wr32(ARG(2), n); if (ARG(3)) wr32(ARG(3), end ? 1 : 0); if (ARG(4)) wr32(ARG(4), 0xffffffffu);
    RET(n == 0 && end ? 0x80630024u : 0);
}
