/* Modo de diagnostico grafico: grava capturas da tela ao longo da sessao para depois analisar ("analise as ultimas capturas e veja se tem algo errado").
 * Incluido por display.cpp (usa g_frames, to_wide, img_save_*). Ligar: variavel TF_CAPTURAS=<segundos> (1 = padrao de 4 s) ou a tecla F9 (liga/desliga) durante o jogo.
 *   F8        = "isto esta errado": depois da nota (ver MARCAS abaixo) grava o quadro inteiro (PNG, resolucao interna) com a etiqueta "marca"
 *               (nunca e apagado pelo anel), mais 2 quadros 10 e 20 depois e a caixa-preta dos ~1,5 s anteriores
 *   periodico = a cada N segundos (JPEG, no maximo 960 px de largura); cenas paradas (quase identicas a ultima salva) sao puladas, no maximo 1 por minuto
 *   cena      = ~0,75 s depois de carregar um modulo do jogo (troca de tela: titulo, duelo, loja...)
 *   lento     = quadro que demorou mais de 250 ms (engasgo)
 * Pasta: <pasta dos logs>/<sessao>_capturas/ com indice.tsv (arquivo, quadro, ms desde o inicio, etiqueta, largura x altura). O anel guarda as ultimas TF_CAPTURAS_MAX (900)
 * capturas periodicas/cena/lento. tools/analysis/capturas_analisar.py monta folhas de contato e aponta quadros suspeitos. Nada disso vai para o git (sao imagens do jogo). */
#include "imgio.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
std::wstring diag_session_prefix(void);   /* diag.cpp: <pasta dos logs>/<sessao> */
static std::atomic<int> g_grav_pend;      /* capturas/sequencias sendo gravadas em segundo plano (ao sair, o jogo espera: seq_esperar_gravacao) */
static std::mutex g_cap_mx;               /* indice.tsv das capturas (as threads de gravacao acrescentam linhas) */
static double g_pausa_ms;                 /* pausa pedida pelo jogador (caixa de nota do F7/F8): frame_stats desconta das estatisticas [perf] */

static bool g_cap_on, g_cap_init, g_cap_marca;
static int g_cap_int_ms = 4000, g_cap_max = 900;
static uint64_t g_cap_evt_frame, g_cap_t0, g_cap_last_ms, g_cap_last_saved_ms;
static unsigned g_cap_n;
static std::wstring g_cap_dir;
static uint8_t g_cap_sig[144]; static bool g_cap_sig_ok;
static std::vector<std::wstring> g_cap_ring;

static void cap_init(void) {
    if (g_cap_init) return;
    g_cap_init = true; g_cap_t0 = GetTickCount64();
    if (const char *e = getenv("TF_CAPTURAS")) { g_cap_on = *e && strcmp(e, "0") != 0; const int s = atoi(e); if (s >= 2) g_cap_int_ms = s * 1000; }   /* "0" = desligado (antes ligava) */
    if (const char *e = getenv("TF_CAPTURAS_MAX")) { const int m = atoi(e); if (m >= 20) g_cap_max = m; }
}
static void cap_toggle(void) {
    cap_init(); g_cap_on = !g_cap_on;
    fprintf(stderr, "[captura] modo diagnostico %s (a cada %d s, F8 marca o quadro atual)%c", g_cap_on ? "LIGADO" : "desligado", g_cap_int_ms / 1000, 10);
}
static void cap_marcar(void) { cap_init(); g_cap_marca = true; fprintf(stderr, "[captura] marca: o quadro atual sera gravado inteiro%c", 10); }
void display_capture_event(const char *tag) {   /* chamado quando um modulo do jogo e carregado: captura a tela ~0,75 s depois */
    cap_init(); if (!g_cap_on) return;
    (void)tag; g_cap_evt_frame = g_frames + 45;
}
static const char *cap_tag_devida(uint64_t frames) {
    cap_init();
    static uint64_t t_ant;
    const uint64_t agora = GetTickCount64();
    const bool lento = t_ant && agora - t_ant > 250; t_ant = agora;
    if (g_cap_marca) return "marca";
    if (!g_cap_on) return nullptr;
    if (g_cap_evt_frame && frames >= g_cap_evt_frame) return "cena";
    if (lento && agora - g_cap_last_ms > 5000) return "lento";
    if (agora - g_cap_last_ms >= (uint64_t)g_cap_int_ms) return "periodico";
    return nullptr;
}
static void cap_salvar(const char *tag, const uint32_t *src, int w, int h) {   /* src = 0x00RRGGBB */
    if (!src || w < 16 || h < 16) return;
    const uint64_t agora = GetTickCount64(); g_cap_last_ms = agora;
    const bool marca = !strcmp(tag, "marca");
    if (!strcmp(tag, "cena")) g_cap_evt_frame = 0;
    /* assinatura 16x9 de luminancia: pula capturas periodicas de cenas paradas */
    uint8_t sig[144];
    for (int by = 0; by < 9; by++) for (int bx = 0; bx < 16; bx++) {
        uint32_t soma = 0, n = 0;
        for (int y = by * h / 9; y < (by + 1) * h / 9; y += 4) for (int x = bx * w / 16; x < (bx + 1) * w / 16; x += 4) { const uint32_t p = src[(size_t)y * w + x]; soma += (((p >> 16) & 255) * 30 + ((p >> 8) & 255) * 59 + (p & 255) * 11) / 100; n++; }
        sig[by * 16 + bx] = n ? (uint8_t)(soma / n) : 0;
    }
    int dif = 0; if (g_cap_sig_ok) for (int i = 0; i < 144; i++) dif += abs((int)sig[i] - (int)g_cap_sig[i]);
    if (!strcmp(tag, "periodico") && g_cap_sig_ok && dif < 144 * 2 && agora - g_cap_last_saved_ms < 60000) return;
    memcpy(g_cap_sig, sig, 144); g_cap_sig_ok = true; g_cap_last_saved_ms = agora;
    if (g_cap_dir.empty()) { g_cap_dir = diag_session_prefix() + L"_capturas/"; CreateDirectoryW(g_cap_dir.c_str(), nullptr); }
    const int f = marca ? 1 : (w + 959) / 960; const int ow = w / f, oh = h / f;
    std::vector<uint32_t> out((size_t)ow * oh);
    for (int y = 0; y < oh; y++) for (int x = 0; x < ow; x++) { const uint32_t p = src[(size_t)(y * f) * w + x * f]; out[(size_t)y * ow + x] = 0xff000000u | ((p & 255) << 16) | (p & 0xff00) | ((p >> 16) & 255); }
    char nome[96]; snprintf(nome, sizeof nome, "%05u_q%llu_%s.%s", ++g_cap_n, (unsigned long long)g_frames, tag, marca ? "png" : "jpg");
    const std::wstring wn = g_cap_dir + to_wide(nome);
    /* a compressao (PNG da marca na resolucao interna: ~250 ms a 8x) e a gravacao vao para uma thread; o jogo nao espera */
    auto img = std::make_shared<std::vector<uint32_t>>(std::move(out));
    const std::string nm = nome, tg = tag; const std::wstring ixp = g_cap_dir + L"indice.tsv"; const uint64_t quadro = g_frames, ms = agora - g_cap_t0;
    g_grav_pend++;
    std::thread([img, wn, nm, tg, ixp, quadro, ms, marca, ow, oh]() {
        const std::string un = to_utf8(wn);
        const bool ok = marca ? img_save_png(un.c_str(), ow, oh, img->data()) : img_save_jpg(un.c_str(), ow, oh, img->data());
        if (!ok) fprintf(stderr, "[captura] falha ao gravar %s%c", nm.c_str(), 10);
        else {
            std::lock_guard<std::mutex> lk(g_cap_mx);
            if (FILE *ix = _wfopen(ixp.c_str(), L"ab")) { fprintf(ix, "%s%c%llu%c%llu%c%s%c%dx%d%c", nm.c_str(), 9, (unsigned long long)quadro, 9, (unsigned long long)ms, 9, tg.c_str(), 9, ow, oh, 10); fclose(ix); }
            if (marca) fprintf(stderr, "[captura] %s gravada (marca)%c", nm.c_str(), 10);
        }
        g_grav_pend--;
    }).detach();
    if (marca) { g_cap_marca = false; return; }
    g_cap_ring.push_back(wn);
    while ((int)g_cap_ring.size() > g_cap_max) { DeleteFileW(g_cap_ring.front().c_str()); g_cap_ring.erase(g_cap_ring.begin()); }
}

/* ---- MARCAS DO JOGADOR: F7 (ou L3 + R3 no controle) = "a CPU errou", F8 = "tem algo errado na tela".
 * A tecla so PEDE a marca. Em display_present (marcas_processar, logo depois do laco de mensagens: ponto seguro, nada do quadro foi decidido ainda)
 * abre a caixa de nota, que PAUSA o jogo, e so com Enter e algum texto a marca e gravada: log "[marca-cpu|marca-graf] #n quadro N", captura inteira
 * do quadro, a nota "[marca-...-nota] #n: texto" e, conforme o tipo, o despejo dos plugins (tfmod_marca: CPUvsCPU = tabuleiro, BetterAI =
 * decisoes) ou a caixa-preta + configuracao grafica + texturas desenhadas. Esc ou nota vazia DESCARTAM a marca: nada vai para o log nem para a pasta
 * de capturas (2026-10-08: a marca era gravada mesmo com Esc). Com o jogo parado, o que se grava depois da caixa e o mesmo de antes
 * (o anel da caixa-preta so anda quando um quadro e apresentado). Relatorios: tools/analysis/marcas_cpu.py e tools/analysis/capturas_analisar.py.
 * Testes: TF_MARCA_QUADRO / TF_MARCAG_QUADRO = quadros onde marcar (como F7 / F8), TF_MARCA_SEM_NOTA=1 = grava sem abrir a caixa,
 * TF_MARCA_NOTA_AUTO="texto" = grava com esse texto sem abrir a caixa (vazio = como Esc: descarta). */
void display_notify(const char *msg);
void plugins_marca(int n);
void text_input_begin_multiline(const std::wstring &desc, const std::wstring &initial, int maxlen);
int text_input_poll(std::wstring *out);
bool text_input_busy(void);
void audio_out_skip(bool on);
extern volatile int g_diag_paused;
static int g_marcag_n, g_burst_n; static uint64_t g_burst_next;
static bool g_marcag_pedida, g_nota_aberta;
static bool g_marcag_piscada;   /* P: flicker mark; the note box may be left empty (the flicker is then searched automatically) */
static int g_marcag_rapida;   /* marca rapida do controle (sem caixa de nota, nota fixa): 0 = nao, 1 = L3 grafico, 2 = R3 performance, 3 = Start+Select audio */
static void marca_cpu(void) { if (!g_nota_aberta) g_marcag_pedida = true; }   /* F7 / L3+R3: agora a mesma marca geral do F8 */
static void marca_graf(void) { if (!g_nota_aberta) g_marcag_pedida = true; }
static void marca_piscada(void) { if (!g_nota_aberta) { g_marcag_pedida = true; g_marcag_piscada = true; } }
static void marca_rapida(int tipo) { if (!g_nota_aberta) { g_marcag_pedida = true; g_marcag_rapida = tipo; } }   /* marca sem pausar o jogo */
static void marca_nota_log(int n, const std::string &u0, const char *pref) {
    /* a nota pode ter varias linhas: no log vira uma linha so, com " / " no lugar de cada quebra (os relatorios leem o log por linha) */
    std::string u; for (size_t i = 0; i < u0.size(); i++) {
        const unsigned char c = (unsigned char)u0[i];
        if (c == '\r') { if (i + 1 < u0.size() && u0[i + 1] == '\n') i++; u += " / "; }
        else if (c == '\n') u += " / ";
        else u += c < 0x20 ? ' ' : (char)c;
    }
    fprintf(stderr, "[%s-nota] #%d: %s%c", pref, n, u.c_str(), 10);
}
static bool nota_vazia(const std::string &s) { for (char c : s) if ((unsigned char)c > ' ') return false; return true; }
/* caixa de nota, na thread do jogo (parado ate Enter/Esc); true = gravar a marca, com o texto em *nota */
static bool marca_nota_modal(bool graf, std::string *nota, bool piscada = false) {
    nota->clear();
    if (getenv("TF_MARCA_SEM_NOTA")) return true;
    if (const char *a = getenv("TF_MARCA_NOTA_AUTO")) { *nota = a; return !nota_vazia(*nota); }
    g_nota_aberta = true; g_diag_paused++; audio_out_skip(true); const ULONGLONG t0 = GetTickCount64();
    (void)graf; text_input_begin_multiline(piscada ? L"Where is the flicker? (optional) Enter = new line, Ctrl+Enter = save (empty = searched automatically), Esc = discard" : L"What is wrong? Enter = new line, Ctrl+Enter = save, Esc or empty = discard", L"", 2000);
    std::wstring t; int st = 1;
    for (;;) {
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        st = text_input_poll(&t); if (st != 1) break;
        Sleep(15);
    }
    audio_out_skip(false); if (g_diag_paused > 0) g_diag_paused--; g_nota_aberta = false;
    g_pausa_ms += (double)(GetTickCount64() - t0);   /* o jogo ficou parado esperando o texto: nao e lentidao */
    if (st != 2) return false;
    *nota = to_utf8(t); return piscada || !nota_vazia(*nota);
}


/* ---- CAIXA-PRETA dos quadros APRESENTADOS: cada quadro que sai na janela, real (R) ou intermediario do 60 FPS (M), e reduzido NA PROPRIA GPU (media
 * n x n, ~960 px de largura) para um anel de TF_SEQ_RING imagens na VRAM (padrao 360 = 3 s a 120 FPS, quadros de 480x272; ate 120 ficam em ~960 px; 0 desliga), com a hora da saida. Custo ~0: nada de CPU,
 * nada vem para a RAM. Numa marca F8 confirmada o anel e lido UMA vez: sao os quadros ANTES do F8 (o que o jogador acabou de ver; a caixa de nota
 * pausa o jogo e nada e apresentado enquanto ela esta aberta) e vao para <sessao>_capturas/seq_<n>/NN_<R|M>_<ms>.png + indice.tsv, gravados numa
 * thread (o jogo nao espera). Nos replays,
 * TF_SEQ_FRAME=n grava o anel no quadro n (os ~90 quadros apresentados antes dele). tools/analysis/seq_analisar.py mede o movimento quadro a quadro e aponta
 * piscadas. (A versao anterior lia cada quadro inteiro na hora, 33 MB a 8x: o jogo ficava lento, o 60 FPS era suspenso no meio e a piscada do xadrez
 * do Duel Result escapou por isso.) */
struct SeqMeta { char tag; bool susp; double ms; uint64_t frame; };
static std::vector<SeqMeta> g_seqm; static uint64_t g_seq_pos; static int g_seq_n = -1, g_seq_id;
static double seq_now_ms(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1000.0 / (double)f.QuadPart; }
static int seq_ring_n(void) {
    if (g_seq_n < 0) { const char *e = getenv("TF_SEQ_RING"); g_seq_n = e ? std::max(0, std::min(600, atoi(e))) : 360; g_seqm.assign((size_t)g_seq_n, SeqMeta{}); }
    return g_seq_n;
}
/* GRAVACAO CONTINUA (TF_SEQ_AUTO=<arquivo.bin>, para replays): TODO quadro apresentado (R e M) vira uma miniatura 160x90 RGB nesse arquivo (o anel de
 * 600 e esvaziado a cada 400 quadros; um engasgo curto nesses pontos, aparece como intervalo grande no ms). Formato: "TFSEQ1\n", w, h (int32), depois por
 * quadro: tag (char), susp (char), 2 bytes 0, ms (float), quadro do jogo (uint64), w*h*3 bytes. tools/analysis/piscadas_analisar.py procura quadros que destoam e voltam
 * (A-B-A) em qualquer duracao, o que uma janela de 1,5 s nunca pega numa sessao inteira. */
static const char *seq_auto_path(void) { static const char *e = getenv("TF_SEQ_AUTO"); return e && *e ? e : nullptr; }
static void seq_auto_flush(int n) {
    const int N = seq_ring_n(); const char *path = seq_auto_path(); if (!N || !path || n <= 0) return;
    static FILE *f = nullptr;
    if (!f) { f = fopen(path, "wb"); if (!f) return; const int32_t hd[2] = {160, 90}; fputs("TFSEQ1\n", f); fwrite(hd, 4, 2, f); }
    const uint64_t k1 = g_seq_pos, k0 = k1 > (uint64_t)n ? k1 - (uint64_t)n : 0;
    std::vector<uint32_t> px; std::vector<uint8_t> th(160 * 90 * 3);
    for (uint64_t k = k0; k < k1; k++) {
        const int slot = (int)(k % (uint64_t)N); int w = 0, h = 0;
        if (!gpu_ring_read(slot, px, &w, &h) || w < 160 || h < 90) continue;
        for (int y = 0; y < 90; y++) { const int ya = y * h / 90, yb = std::max(ya + 1, (y + 1) * h / 90);
            for (int x = 0; x < 160; x++) { const int xa = x * w / 160, xb = std::max(xa + 1, (x + 1) * w / 160); uint32_t r = 0, g = 0, b = 0, c = 0;
                for (int yy = ya; yy < yb; yy++) for (int xx = xa; xx < xb; xx++) { const uint32_t p = px[(size_t)yy * w + xx]; r += p & 255; g += (p >> 8) & 255; b += (p >> 16) & 255; c++; }
                uint8_t *o = &th[((size_t)y * 160 + x) * 3]; o[0] = (uint8_t)(r / c); o[1] = (uint8_t)(g / c); o[2] = (uint8_t)(b / c); } }
        const SeqMeta &m = g_seqm[slot]; const char hd[4] = {m.tag, (char)m.susp, 0, 0}; const float ms = (float)m.ms; const uint64_t fr = m.frame;
        fwrite(hd, 1, 4, f); fwrite(&ms, 4, 1, f); fwrite(&fr, 8, 1, f); fwrite(th.data(), 1, th.size(), f);
    }
    fflush(f);
}
void display_seq_present(int fb, int tag) {   /* chamado por gpu_present_fb antes de mostrar o quadro (a reducao entra na fila da GPU antes da apresentacao) */
    const int N = seq_ring_n(); if (!N || fb < 0) return;
    const int slot = (int)(g_seq_pos % (uint64_t)N);
    if (!gpu_ring_push(fb, slot, N)) return;
    g_seqm[slot] = SeqMeta{tag ? 'M' : 'R', pace_mid_suspended(), seq_now_ms(), g_frames};
    g_seq_pos++;
    if (seq_auto_path() && g_seq_pos % 400 == 0) seq_auto_flush(400);
}
/* le o anel (do mais antigo ao mais novo) e grava os PNGs numa thread */
static void seq_gravar(const char *motivo) {
    const int N = seq_ring_n(); if (!N || !g_seq_pos) return;
    struct Q { char tag; bool susp; double ms; uint64_t frame; int w, h; std::vector<uint32_t> px; };
    auto qs = std::make_shared<std::vector<Q>>();
    const uint64_t n = std::min<uint64_t>(g_seq_pos, (uint64_t)N), k0 = g_seq_pos - n;
    const double t0 = g_seqm[k0 % (uint64_t)N].ms, tl = seq_now_ms();
    for (uint64_t k = k0; k < g_seq_pos; k++) {
        const int slot = (int)(k % (uint64_t)N); Q q; int w = 0, h = 0;
        if (!gpu_ring_read(slot, q.px, &w, &h)) continue;   /* posicao de antes de trocar renderizador/resolucao: perdida */
        const SeqMeta &m = g_seqm[slot]; q.tag = m.tag; q.susp = m.susp; q.ms = m.ms - t0; q.frame = m.frame; q.w = w; q.h = h;
        for (uint32_t &p : q.px) p |= 0xff000000u;   /* o readback vem em ABGR = bytes RGBA */
        qs->push_back(std::move(q));
    }
    if (qs->empty()) return;
    const int id = ++g_seq_id;
    std::wstring dir = diag_session_prefix() + L"_capturas/"; CreateDirectoryW(dir.c_str(), nullptr);
    char nome[48]; snprintf(nome, sizeof nome, "seq_%d/", id); dir += to_wide(nome); CreateDirectoryW(dir.c_str(), nullptr);
    fprintf(stderr, "[captura] sequencia %d (%s, quadro %llu): os %u quadros apresentados antes deste ponto (%.0f ms), lidos em %.0f ms; gravando em seq_%d%c", id, motivo,
            (unsigned long long)g_frames, (unsigned)qs->size(), qs->back().ms, seq_now_ms() - tl, id, 10);
    g_grav_pend++;
    std::thread([qs, dir, id]() {
        FILE *ix = _wfopen((dir + L"indice.tsv").c_str(), L"wb");
        if (ix) fprintf(ix, "#indice%ctipo%cms%carquivo%c60fps_suspenso%cquadro%c", 9, 9, 9, 9, 9, 10);
        for (size_t i = 0; i < qs->size(); i++) {
            const Q &q = (*qs)[i]; char fn[64]; snprintf(fn, sizeof fn, "%03u_%c_%07.2f.png", (unsigned)i, q.tag, q.ms);
            const std::string un = to_utf8(dir + to_wide(fn)); img_save_png(un.c_str(), q.w, q.h, q.px.data());
            if (ix) fprintf(ix, "%u%c%c%c%.2f%c%s%c%d%c%llu%c", (unsigned)i, 9, q.tag, 9, q.ms, 9, fn, 9, q.susp ? 1 : 0, 9, (unsigned long long)q.frame, 10);
        }
        if (ix) fclose(ix);
        fprintf(stderr, "[captura] sequencia %d gravada: %u quadros em seq_%d%c", id, (unsigned)qs->size(), id, 10);
        g_grav_pend--;
    }).detach();
}
static void seq_esperar_gravacao(void) {   /* ao sair do jogo: termina de gravar capturas e a caixa-preta (ate 15 s) */
    if (seq_auto_path()) seq_auto_flush((int)(g_seq_pos % 400));   /* grava o resto da gravacao continua (TF_SEQ_AUTO) */
    for (int i = 0; g_grav_pend > 0 && i < 1500; i++) Sleep(10);
}
static void seq_quadro(void) {   /* a cada display_present: TF_SEQ_FRAME (replays) */
    static const char *tf = getenv("TF_SEQ_FRAME"); static bool done;
    if (tf && !done && g_frames >= strtoull(tf, nullptr, 10)) { done = true; seq_gravar("TF_SEQ_FRAME"); }
}
