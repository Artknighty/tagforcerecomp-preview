/* HLE: sceMpeg (filmes PSMF: abertura, invocacoes/ataques de monstros, Exodia, creditos).
 * O jogo usa o player de exemplo do SDK: uma thread de leitura enche o anel (sceMpegRingbufferPut chama o callback do
 * jogo, que le o arquivo), o laco principal pede um access unit de video/audio (GetAvcAu/GetAtracAu, com PTS a 90 kHz) e
 * o decodifica (AvcDecode -> RGBA no buffer do jogo, largura 512; AtracDecode -> 2048 amostras estereo). O fim e quando a
 * leitura acabou e o anel voltou a ficar vazio (AvailableSize == total), e entao AvcDecodeStop esvazia o decodificador.
 * Aqui os pacotes sao copiados do anel na hora para um demultiplexador proprio (MPEG-PS): video = stream 0xE0 (H.264 com
 * delimitadores de AU, NAL 9), audio = stream privado 0xBD (4 bytes de cabecalho por pacote; quadros ATRAC3+ de 8 + 744
 * bytes com sincronia 0x0FD0). O anel so aparece "vazio" quando tudo o que entrou ja foi consumido pelo jogo. PTS: o video
 * so traz PTS no 1o quadro de cada GOP, os outros sao calculados (29,97 fps = 3003); audio = 2048 amostras = 4180. */
#include "hle.h"
#include "ffmpeg_dec.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <deque>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>

#define MPEG_NO_DATA 0x80618001u
static const bool g_mtrace = getenv("MPEG_TRACE") != nullptr;
uint64_t display_frames(void);
void mods_movie_hd_files(std::vector<std::wstring> &out);

/* ---- video substituto em alta resolucao (mod): <mod>\movie\<nome>.h264 acompanha o filme <nome>.pmf quadro a quadro. Cada quadro que o
 * decodificador original entrega faz o decodificador HD entregar o proximo quadro dele (1920x1088); o display_present mostra essa imagem no lugar
 * do quadro do jogo (o player copia o quadro decodificado direto na VRAM, nao desenha textura). O audio e o tempo continuam os do filme original. */
static struct Hd {
    std::wstring pending;                              /* arquivo achado quando o jogo abriu o .pmf correspondente */
    bool on = false; std::vector<uint8_t> data; std::vector<size_t> aus;
    /* thread de decodificacao: decodifica e converte os quadros HD adiantada (ate 6 prontos), a thread do jogo so retira o proximo */
    std::thread th; std::mutex mu; std::condition_variable cv; std::deque<std::vector<uint32_t>> ready; bool quit = false, done = false;
    std::vector<uint32_t> cur, prox; uint32_t ver = 0; bool valid = false;   /* prox: ultimo quadro HD retirado; vai para a tela no avanco seguinte (ver hd_advance) */
} H;
static void hd_open(void);
static uint32_t g_hd_devidos;   /* quadros que o decodificador original ja entregou e o HD ainda nao (ver hd_advance) */
static bool g_created;   /* sceMpegCreate ja foi chamado (o jogo cria o mpeg ANTES de abrir o arquivo) */
void mpeg_movie_opened(uint32_t lba) {   /* chamado em todo sceIoOpen de arquivo do disco; o achado vale ate o proximo sceMpegCreate */
    std::vector<std::wstring> files; mods_movie_hd_files(files);
    for (auto &f : files) {
        size_t a = f.find_last_of(L"\\/") + 1, b = f.find_last_of(L'.'); std::string stem;
        for (size_t i = a; i < b; i++) stem += (char)f[i];
        uint32_t l = 0, sz = 0; bool dir;
        if (iso_find("PSP_GAME/USRDIR/movie/" + stem + ".pmf", &l, &sz, &dir) == 0 && l == lba) H.pending = f;   /* o ultimo mod vence */
    }
    if (g_created && !H.pending.empty() && !H.on) hd_open();
}
static void hd_close(void) {
    if (H.th.joinable()) { { std::lock_guard<std::mutex> lk(H.mu); H.quit = true; } H.cv.notify_all(); H.th.join(); }
    H.on = false; H.valid = false; H.quit = false; H.done = false; H.ready.clear();
    H.data.clear(); H.data.shrink_to_fit(); H.aus.clear(); H.cur.clear(); H.cur.shrink_to_fit(); H.prox.clear(); H.prox.shrink_to_fit();
}
static std::wstring g_hd_file;
static void hd_worker(void) {
    {   /* le o arquivo (centenas de MB) e acha os access units aqui, fora da thread do jogo */
        std::vector<uint8_t> data; std::vector<size_t> aus;
        if (FILE *f = _wfopen(g_hd_file.c_str(), L"rb")) {
            _fseeki64(f, 0, SEEK_END); long long n = _ftelli64(f); _fseeki64(f, 0, SEEK_SET); if (n < 0) n = 0; data.resize((size_t)n);   /* 64 bits: video de mod pode passar de 2 GB */
            if (fread(data.data(), 1, (size_t)n, f) != (size_t)n) data.clear();
            fclose(f);
        }
        for (size_t i = 0; i + 4 < data.size(); i++) if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1 && data[i + 3] == 9) aus.push_back((i > 0 && data[i - 1] == 0) ? i - 1 : i);
        { std::lock_guard<std::mutex> lk(H.mu); H.data = std::move(data); H.aus = std::move(aus); }
        fprintf(stderr, "[mpeg] video substituto HD: %zu quadros (%ls)\n", H.aus.size(), g_hd_file.c_str());
    }
    AvcDec *dec = H.aus.empty() ? nullptr : avcdec_open(); if (!dec) { std::lock_guard<std::mutex> lk(H.mu); H.done = true; H.cv.notify_all(); return; }
    std::vector<uint32_t> px;
    for (size_t i = 0; i < H.aus.size(); i++) {
        { std::unique_lock<std::mutex> lk(H.mu); H.cv.wait(lk, [] { return H.quit || H.ready.size() < 6; }); if (H.quit) break; }
        size_t s = H.aus[i], e = i + 1 < H.aus.size() ? H.aus[i + 1] : H.data.size();
        px.resize((size_t)1920 * 1088);
        if (avcdec_decode(dec, H.data.data() + s, (int)(e - s), (uint8_t *)px.data(), 1920, 4, 1920, 1088) == 1) {
            { std::lock_guard<std::mutex> lk(H.mu); H.ready.push_back(std::move(px)); }
            H.cv.notify_all(); px = std::vector<uint32_t>();
        }
    }
    avcdec_close(dec);
    { std::lock_guard<std::mutex> lk(H.mu); H.done = true; } H.cv.notify_all();
}
static void hd_open(void) {
    hd_close();
    if (H.pending.empty()) return;
    g_hd_file = H.pending; H.pending.clear();
    H.ver = 0; H.on = true; g_hd_devidos = 0; H.th = std::thread(hd_worker);
}
/* O decodificador original entregou um quadro: o proximo quadro HD passa a ser o exibido. NAO espera a thread HD (antes esperava: o 1o quadro da
 * abertura segurava o jogo ~400 ms enquanto ela lia os ~220 MB do arquivo): enquanto o HD nao chega, aparece o quadro original do filme; os quadros
 * devidos (g_hd_devidos) sao alcancados assim que a thread entrega, descartando os que ficaram para tras (ela decodifica bem mais rapido que 30 fps). */
static void hd_advance(uint32_t) {
    if (!H.on) return;
    std::lock_guard<std::mutex> lk(H.mu);
    g_hd_devidos++;
    bool pegou = false;
    /* um quadro de atraso: o quadro que o jogo acabou de decodificar so vai para a tela no vblank seguinte (o player copia para a VRAM depois); mostrar
     * o HD na hora deixava o video 1 quadro (33 ms) adiantado em relacao ao filme original e ao som */
    while (g_hd_devidos && !H.ready.empty()) { H.cur = std::move(H.prox); H.prox = std::move(H.ready.front()); H.ready.pop_front(); g_hd_devidos--; pegou = true; }
    if (pegou) { H.cv.notify_all(); if (!H.cur.empty()) { H.valid = true; H.ver++; } }
    if (H.done && H.ready.empty()) g_hd_devidos = 0;   /* acabaram os quadros HD: o ultimo continua na tela */
}
const uint32_t *mpeg_hd_present(int *w, int *h, uint32_t *ver) {   /* quadro HD a apresentar no lugar do quadro do jogo (filme com video substituto em andamento) */
    if (!H.on || !H.valid) return nullptr;
    *w = 1920; *h = 1088; if (ver) *ver = H.ver; return H.cur.data();
}

struct VAu { std::vector<uint8_t> data; int64_t pts; };
struct AAu { std::vector<uint8_t> data; int64_t pts; };
static struct Mpeg {
    uint32_t rb = 0, packets = 0;                     /* anel do jogo */
    std::vector<uint8_t> ps; size_t ps_pos = 0;       /* MPEG-PS ainda nao demultiplexado */
    bool header_skipped = false, eof = false;
    uint64_t fed = 0, stream_size = 0;                /* bytes recebidos (depois do cabecalho PSMF) / tamanho do fluxo */
    std::vector<uint8_t> ves; std::vector<size_t> vstarts; int64_t vpts_pending = -1; /* ES de video e inicios de AU */
    std::deque<VAu> vq; int64_t vnext = -1;           /* AUs completos; proximo PTS calculado */
    std::vector<uint8_t> aes; int64_t apts_base = -1; uint64_t aframes = 0; int achans = 2;
    std::deque<AAu> aq;
    VAu vcur; AAu acur;                               /* AU entregue por Get*Au e decodificado por *Decode */
    AvcDec *vdec = nullptr; AtracDec *adec = nullptr; int ablock = 0;
    uint64_t vframes = 0;
} M;

static void mpeg_reset_streams(void) {
    M.ps.clear(); M.ps_pos = 0; M.ves.clear(); M.vstarts.clear(); M.vpts_pending = -1; M.vq.clear(); M.vnext = -1;
    M.aes.clear(); M.apts_base = -1; M.aframes = 0; M.aq.clear();
    if (M.vdec) avcdec_flush(M.vdec);
    if (M.adec) atracdec_flush(M.adec);
}
static size_t pending_bytes(void) {   /* dados que entraram no anel e o jogo ainda nao consumiu */
    size_t n = M.ps.size() - M.ps_pos + M.ves.size() + M.aes.size();
    for (auto &a : M.vq) n += a.data.size();
    for (auto &a : M.aq) n += a.data.size();
    return n;
}
static uint32_t ring_free(void) {
    size_t used = (pending_bytes() + 2047) / 2048;
    return used >= M.packets ? 0 : M.packets - (uint32_t)used;
}

/* ---- video: separa o ES em access units pelo delimitador (00 00 01 09) */
static void video_close_au(size_t end) {   /* AU [vstarts[0], end) completo */
    VAu au; au.data.assign(M.ves.begin() + (ptrdiff_t)M.vstarts[0], M.ves.begin() + (ptrdiff_t)end);
    au.pts = M.vnext; M.vnext += 3003;
    M.vq.push_back(std::move(au));
    M.vstarts.erase(M.vstarts.begin());
}
static void video_append(const uint8_t *p, size_t n, int64_t pts) {
    size_t old = M.ves.size();
    M.ves.insert(M.ves.end(), p, p + n);
    if (pts >= 0) M.vpts_pending = pts;
    for (size_t i = old >= 3 ? old - 3 : 0; i + 3 < M.ves.size(); i++) {
        if (M.ves[i] == 0 && M.ves[i + 1] == 0 && M.ves[i + 2] == 1 && (M.ves[i + 3] & 0x1f) == 9) {
            size_t s = (i > 0 && M.ves[i - 1] == 0) ? i - 1 : i;   /* inclui o 4o zero do codigo de inicio longo */
            if (!M.vstarts.empty()) video_close_au(s);
            /* PTS do GOP: ancora a contagem (reancora so se divergir mais de 3 quadros: descontinuidade) */
            if (M.vpts_pending >= 0) { if (M.vnext < 0 || llabs(M.vpts_pending - M.vnext) > 3 * 3003) M.vnext = M.vpts_pending; M.vpts_pending = -1; }
            if (M.vnext < 0) M.vnext = 0;
            M.vstarts.push_back(s);
        }
    }
    /* descarta o que ja virou AU (mantem a partir do AU aberto) */
    size_t keep = M.vstarts.empty() ? (M.ves.size() > 3 ? M.ves.size() - 3 : 0) : M.vstarts[0];
    if (keep > 65536) { M.ves.erase(M.ves.begin(), M.ves.begin() + (ptrdiff_t)keep); for (auto &s : M.vstarts) s -= keep; }
}
/* ---- audio: 4 bytes de cabecalho por pacote; os 2 ultimos = onde comeca o 1o quadro no pacote (para o PTS) */
static void audio_append(const uint8_t *p, size_t n, int64_t pts) {
    if (n < 4) return;
    uint32_t first = ((uint32_t)p[2] << 8) | p[3];
    if (pts >= 0 && M.apts_base < 0) {   /* PTS do quadro que comeca em "first": deduz a base (quadro 0) */
        uint64_t idx = (M.aes.size() + first + (uint64_t)M.aframes * 752) / 752;
        M.apts_base = pts - (int64_t)(idx * 4180);
    }
    M.aes.insert(M.aes.end(), p + 4, p + n);
    size_t q = 0;
    while (q + 8 <= M.aes.size()) {
        const uint8_t *h = &M.aes[q];
        if (h[0] != 0x0f || h[1] != 0xd0) { q++; continue; }   /* ressincroniza */
        size_t sz = ((size_t)(h[2] & 3) << 8 | h[3]) * 8 + 8;
        if (q + 8 + sz > M.aes.size()) break;
        if (!M.ablock) { M.ablock = (int)sz; M.achans = ((h[2] >> 2) & 7) == 1 ? 1 : 2; }
        AAu au; au.data.assign(h + 8, h + 8 + sz);
        au.pts = (M.apts_base >= 0 ? M.apts_base : 0) + (int64_t)(M.aframes * 2048 * 90000 / 44100);
        M.aframes++;
        M.aq.push_back(std::move(au));
        q += 8 + sz;
    }
    M.aes.erase(M.aes.begin(), M.aes.begin() + (ptrdiff_t)q);
}
static int64_t pes_pts(const uint8_t *b) {
    return ((int64_t)((b[0] >> 1) & 7) << 30) | ((int64_t)b[1] << 22) | ((int64_t)(b[2] >> 1) << 15) | ((int64_t)b[3] << 7) | (b[4] >> 1);
}
static void demux(void) {   /* consome pacotes completos de M.ps */
    std::vector<uint8_t> &d = M.ps; size_t &p = M.ps_pos;
    if (!M.header_skipped && d.size() - p >= 16 && !memcmp(&d[p], "PSMF", 4)) {   /* o anel pode comecar no cabecalho */
        uint32_t off = ((uint32_t)d[p + 8] << 24) | ((uint32_t)d[p + 9] << 16) | ((uint32_t)d[p + 10] << 8) | d[p + 11];
        if (d.size() - p < off) return;
        p += off; M.header_skipped = true; M.fed -= std::min<uint64_t>(M.fed, off);   /* fed conta so o fluxo */
    }
    M.header_skipped = true;
    for (;;) {
        if (d.size() - p < 6) break;
        if (d[p] != 0 || d[p + 1] != 0 || d[p + 2] != 1) { p++; continue; }   /* ressincroniza */
        uint8_t sid = d[p + 3];
        if (sid == 0xba) { if (d.size() - p < 14) break; size_t l = 14 + (d[p + 13] & 7); if (d.size() - p < l) break; p += l; continue; }
        if (sid == 0xb9) { p += 4; continue; }   /* fim do programa */
        size_t len = ((size_t)d[p + 4] << 8) | d[p + 5];
        if (d.size() - p < 6 + len) break;
        const uint8_t *body = &d[p + 6];
        if ((sid == 0xe0 || sid == 0xbd) && len >= 3) {
            size_t hl = body[2]; int64_t pts = (body[1] & 0x80) && hl >= 5 ? pes_pts(body + 3) : -1;
            if (3 + hl <= len) {
                if (sid == 0xe0) video_append(body + 3 + hl, len - 3 - hl, pts);
                else audio_append(body + 3 + hl, len - 3 - hl, pts);
            }
        }
        p += 6 + len;
    }
    if (p > (1u << 20)) { d.erase(d.begin(), d.begin() + (ptrdiff_t)p); p = 0; }
}
static void finish_stream(void) {   /* fim do arquivo: o ultimo AU de video fica completo */
    if (M.eof) return;
    M.eof = true;
    if (!M.vstarts.empty()) video_close_au(M.ves.size());
    M.ves.clear(); M.aes.clear(); M.ps.clear(); M.ps_pos = 0;
    if (g_mtrace) fprintf(stderr, "[mpeg] fim do arquivo: %zu AUs de video e %zu de audio na fila\n", M.vq.size(), M.aq.size());
}

/* TF_NO_MOVIES=1: sceMpegInit falha e o jogo pula os filmes (como antes de 2026-09-29). Usado pelos testes/reproducoes
 * gravados sem filmes (a linha do tempo muda: a abertura dura ~90 s). O log sempre registra cada filme ("[mpeg] filme"),
 * e o replay.sh usa isso para saber se a sessao foi gravada com filmes. */
HLE(sceMpegInit) {
    static const bool off = getenv("TF_NO_MOVIES") != nullptr;
    if (off) { RET(0x80618005u); return; }
    fprintf(stderr, "[mpeg] filme iniciado (quadro %llu)\n", (unsigned long long)display_frames());
    RET(0);
}
HLE(sceMpegFinish) { RET(0); }
HLE(sceMpegQueryMemSize) { RET(0x10000); }
HLE(sceMpegCreate) {   /* (mpeg*, data*, size, ringbuffer*, frameWidth, mode, ddrtop) */
    wr32(ARG(0), ARG(1) + 0x100);
    M.rb = ARG(3); M.packets = M.rb ? rd32(M.rb) : 0;
    M.eof = false; M.header_skipped = false; M.fed = 0; M.vframes = 0; M.ablock = 0;
    mpeg_reset_streams();
    g_created = true; hd_open();
    if (!M.vdec) M.vdec = avcdec_open();
    if (g_mtrace) fprintf(stderr, "[mpeg] create: anel %08x com %u pacotes, H.264 %s\n", M.rb, M.packets, M.vdec ? "ok" : "INDISPONIVEL");
    RET(0);
}
HLE(sceMpegDelete) {
    g_created = false; hd_close();
    fprintf(stderr, "[mpeg] fim do filme: %llu quadros de video, %llu de audio\n", (unsigned long long)M.vframes, (unsigned long long)M.aframes);
    mpeg_reset_streams(); atracdec_close(M.adec); M.adec = nullptr; RET(0);
}
HLE(sceMpegRingbufferQueryMemSize) { RET(ARG(0) * 2048); }
HLE(sceMpegRingbufferConstruct) {   /* (rb*, packets, data*, size, cb, cbparam) */
    uint32_t rb = ARG(0);
    wr32(rb, ARG(1)); wr32(rb + 4, 0); wr32(rb + 8, 0); wr32(rb + 12, ARG(1)); wr32(rb + 16, 2048); wr32(rb + 20, ARG(2));
    wr32(rb + 24, ARG(4)); wr32(rb + 28, ARG(5)); wr32(rb + 32, ARG(2) + ARG(1) * 2048); wr32(rb + 36, 0); wr32(rb + 40, 0); RET(0);
}
HLE(sceMpegRingbufferDestruct) { RET(0); }
HLE(sceMpegRingbufferAvailableSize) {
    uint32_t f = ring_free();
    if (ARG(0)) wr32(ARG(0) + 12, f);
    RET(f);
}
HLE(sceMpegRingbufferPut) {   /* (rb*, numPackets, available): o callback do jogo le do arquivo para o anel */
    uint32_t rb = ARG(0), want = ARG(1), packets = rd32(rb), written = rd32(rb + 8), freep = ring_free();
    if (want > freep) want = freep;
    uint32_t pos = packets ? written % packets : 0; if (want > packets - pos) want = packets - pos;
    if (!want) { RET(0); return; }
    uint32_t data = rd32(rb + 20), cb = rd32(rb + 24), cbp = rd32(rb + 28);
    int32_t n = (int32_t)k_call_guest(cb, data + pos * 2048, want, cbp, 0);
    if (n < 0) { RET(n); return; }
    if (n > 0) {   /* copia os pacotes para o demultiplexador: a area do anel pode ser reusada na hora */
        const uint8_t *src = mem_ptr(data + pos * 2048);
        M.ps.insert(M.ps.end(), src, src + (size_t)n * 2048); M.fed += (uint64_t)n * 2048;
        demux();
    }
    if ((uint32_t)n < want || (M.stream_size && M.fed >= M.stream_size)) finish_stream();
    wr32(rb + 8, written + (uint32_t)n); wr32(rb + 4, rd32(rb + 4) + (uint32_t)n); wr32(rb + 12, ring_free());
    RET(n);
}
HLE(sceMpegQueryStreamOffset) {   /* (mpeg, streamdata, *offset): cabecalho PSMF, campos big-endian */
    const uint8_t *p = mem_ptr(ARG(1)); uint32_t off = ((uint32_t)p[8] << 24) | ((uint32_t)p[9] << 16) | ((uint32_t)p[10] << 8) | p[11];   /* (uint32_t: byte >= 0x80 << 24 estouraria o int) */
    wr32(ARG(2), off); RET(0);
}
HLE(sceMpegQueryStreamSize) {
    const uint8_t *p = mem_ptr(ARG(0)); uint32_t sz = ((uint32_t)p[12] << 24) | ((uint32_t)p[13] << 16) | ((uint32_t)p[14] << 8) | p[15];
    M.stream_size = sz;
    wr32(ARG(1), sz); RET(0);
}
static uint32_t g_stream_uid = 0x6100;
HLE(sceMpegRegistStream) { RET(g_stream_uid++); }
HLE(sceMpegUnRegistStream) { RET(0); }
HLE(sceMpegMallocAvcEsBuf) { RET(0x1); }
HLE(sceMpegFreeAvcEsBuf) { RET(0); }
static void au_write(uint32_t au, int64_t pts, uint32_t size) {   /* SceMpegAu: pts (hi, lo), dts (hi, lo), esBuffer, esSize */
    if (!au) return;
    wr32(au, (uint32_t)((uint64_t)pts >> 32)); wr32(au + 4, (uint32_t)pts);
    wr32(au + 8, (uint32_t)((uint64_t)pts >> 32)); wr32(au + 12, (uint32_t)pts); wr32(au + 20, size);
}
HLE(sceMpegInitAu) {   /* (mpeg, esBuffer, au*) */
    uint32_t au = ARG(2);
    if (au) { wr32(au, 0xffffffffu); wr32(au + 4, 0xffffffffu); wr32(au + 8, 0xffffffffu); wr32(au + 12, 0xffffffffu); wr32(au + 16, ARG(1)); wr32(au + 20, 0); }
    RET(0);
}
HLE(sceMpegGetAvcAu) {   /* (mpeg, stream, au*, attr*) */
    if (M.vq.empty()) { RET(MPEG_NO_DATA); return; }
    M.vcur = std::move(M.vq.front()); M.vq.pop_front();
    au_write(ARG(2), M.vcur.pts, (uint32_t)M.vcur.data.size());
    if (ARG(3)) wr32(ARG(3), 1);
    RET(0);
}
HLE(sceMpegGetAtracAu) {   /* (mpeg, stream, au*, attr*) */
    if (M.aq.empty()) { RET(MPEG_NO_DATA); return; }
    M.acur = std::move(M.aq.front()); M.aq.pop_front();
    au_write(ARG(2), M.acur.pts, (uint32_t)M.acur.data.size());
    if (ARG(3)) wr32(ARG(3), 0);
    RET(0);
}
static void video_out(int r, uint32_t init_addr) {
    if (r == 1) M.vframes++;
    static int nlog;
    if (g_mtrace && (r != 1 || (M.vframes % 120) == 1) && nlog++ < 40)
        fprintf(stderr, "[mpeg] avc: resultado %d (au %zu bytes, quadros ok %llu)\n", r, M.vcur.data.size(), (unsigned long long)M.vframes);
    if (init_addr) wr32(init_addr, r == 1 ? 1 : 0);
}
HLE(sceMpegAvcDecode) {   /* (mpeg, au*, frameWidth, buffer**, init*) */
    uint32_t buf = ARG(3) ? rd32(ARG(3)) : 0; int r = 0;
    if (M.vdec && buf && !M.vcur.data.empty())
        r = avcdec_decode(M.vdec, M.vcur.data.data(), (int)M.vcur.data.size(), mem_ptr(buf), (int)ARG(2), 3, (int)ARG(2), 272);
    if (r == 1) hd_advance(buf);
    video_out(r, ARG(4)); M.vcur.data.clear(); RET(0);
}
HLE(sceMpegAvcDecodeStop) {   /* (mpeg, frameWidth, buffer**, status*): entrega os quadros atrasados, um por chamada */
    uint32_t buf = ARG(2) ? rd32(ARG(2)) : 0; int r = 0;
    if (M.vdec && buf) r = avcdec_decode(M.vdec, nullptr, 0, mem_ptr(buf), (int)ARG(1), 3, (int)ARG(1), 272);
    if (r == 1) hd_advance(buf);
    video_out(r, ARG(3)); RET(0);
}
HLE(sceMpegAvcDecodeFlush) { if (M.vdec) avcdec_flush(M.vdec); RET(0); }
HLE(sceMpegFlushAllStream) { mpeg_reset_streams(); RET(0); }
HLE(sceMpegAtracDecode) {   /* (mpeg, au*, buffer, init): um quadro ATRAC3+ -> 2048 amostras estereo s16 */
    uint32_t out = ARG(2);
    if (!out) { RET(0); return; }
    int16_t *o = (int16_t *)mem_ptr(out); int got = 0;
    if (!M.adec && M.ablock) M.adec = atracdec_open(true, M.achans, M.ablock, nullptr, 0);
    if (M.adec && !M.acur.data.empty()) got = atracdec_decode(M.adec, M.acur.data.data(), o, 2048);
    if (got < 2048) memset(o + got * 2, 0, (size_t)(2048 - got) * 4);
    M.acur.data.clear();
    RET(0);
}
HLE(sceMpegQueryAtracEsSize) { if (ARG(1)) wr32(ARG(1), 2112); if (ARG(2)) wr32(ARG(2), 8192); RET(0); }
