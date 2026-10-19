/* Cliente do modelo local (llama-server do llama.cpp via WinHTTP) + registro das decisoes. Ver decisor.h. */
#include "decisor.h"
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <random>
#include <functional>

static void (*g_log)(const char *fmt, ...);
static bool g_on, g_dados, g_externo;
static std::string g_host = "127.0.0.1";
static int g_porta, g_tempo_ms = 3500, g_falhas, g_threads, g_explorar, g_llm_lado = -1;   /* g_llm_lado: so esse jogador usa o modelo (A/B na arena); g_explorar: % das decisoes escolhidas ao acaso */
static ULONGLONG g_pausa_ate, g_ultimo_health;
static bool g_pronto;
static HANDLE g_proc, g_job;
static std::mt19937 g_rng;

static std::string json_escape(const std::string &s) {
    std::string o; o.reserve(s.size() + 8);
    for (unsigned char ch : s) {
        switch (ch) {
        case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break; case '\r': break; case '\t': o += "\\t"; break;
        default: if (ch < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", ch); o += b; } else o += (char)ch;
        }
    }
    return o;
}

static int porta_livre(void) {
    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) return 0;
    int achou = 0;
    for (int p = 18600; p < 18700 && !achou; p++) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (s == INVALID_SOCKET) break;
        sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((u_short)p);
        if (bind(s, (sockaddr *)&a, sizeof a) == 0) achou = p;
        closesocket(s);
    }
    WSACleanup(); return achou;
}

static bool arquivo_existe(const std::wstring &p) { const DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

static std::wstring g_dir; static int g_var = -1;   /* variante do servidor: 0 = llm\vulkan (GPU, ~40 ms), 1 = llm\cpu (~1 s), 2 = llm\ (layout simples, CPU) */
static ULONGLONG g_inicio;
static const wchar_t *VAR[3] = {L"\\llm\\vulkan", L"\\llm\\cpu", L"\\llm"};
static bool tem_variante(int v) { return arquivo_existe(g_dir + VAR[v] + L"\\llama-server.exe"); }
static void parar_servidor(void) { if (g_proc) { TerminateProcess(g_proc, 0); CloseHandle(g_proc); g_proc = nullptr; } g_pronto = false; }
static bool iniciar_servidor(int v) {
    const std::wstring pasta = g_dir + VAR[v], exe = pasta + L"\\llama-server.exe", modelo = g_dir + L"\\llm\\modelo.gguf";
    if (!g_porta) g_porta = porta_livre(); if (!g_porta) { if (g_log) g_log("decisor: sem porta livre para o servidor do modelo"); return false; }
    int th = g_threads; if (th <= 0) { SYSTEM_INFO si; GetSystemInfo(&si); th = (int)si.dwNumberOfProcessors / 3; if (th < 2) th = 2; if (th > 4) th = 4; }
    wchar_t cmd[1500];
    if (v == 0) swprintf(cmd, 1500, L"\"%ls\" -m \"%ls\" --host 127.0.0.1 --port %d -c 3072 -ngl 99 -np 1 --log-disable --no-warmup", exe.c_str(), modelo.c_str(), g_porta);
    else swprintf(cmd, 1500, L"\"%ls\" -m \"%ls\" --host 127.0.0.1 --port %d -c 3072 -t %d -np 1 --log-disable --no-warmup", exe.c_str(), modelo.c_str(), g_porta, th);
    STARTUPINFOW si = {}; si.cb = sizeof si; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS | CREATE_SUSPENDED, nullptr, pasta.c_str(), &si, &pi)) {
        if (g_log) g_log("decisor: nao consegui iniciar llama-server (erro %lu)", GetLastError()); return false;
    }
    if (!g_job) {   /* o servidor morre junto com o jogo, mesmo se o jogo cair */
        g_job = CreateJobObjectW(nullptr, nullptr);
        if (g_job) { JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {}; li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE; SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &li, sizeof li); }
    }
    if (g_job) AssignProcessToJobObject(g_job, pi.hProcess);
    ResumeThread(pi.hThread); CloseHandle(pi.hThread); g_proc = pi.hProcess; g_var = v; g_inicio = GetTickCount64();
    if (g_log) g_log("decisor: llama-server iniciado (%s, porta %d)", v == 0 ? "GPU/Vulkan" : "CPU", g_porta);
    return true;
}
static bool iniciar_melhor(int a_partir) { for (int v = a_partir; v < 3; v++) if (tem_variante(v) && iniciar_servidor(v)) return true; return false; }

void decisor_init(const wchar_t *mod_dir, void (*log)(const char *fmt, ...)) {
    g_log = log;
    wchar_t abs[1024]; std::wstring dir = mod_dir;   /* caminho ABSOLUTO: no jogo o mod vem como "mods\IAAprimorada" (relativo) e o servidor roda com outra pasta de trabalho */
    if (const DWORD n = GetFullPathNameW(mod_dir, 1024, abs, nullptr)) if (n < 1024) dir = abs;
    g_dir = dir;
    const bool tem_modelo = arquivo_existe(dir + L"\\llm\\modelo.gguf") && (tem_variante(0) || tem_variante(1) || tem_variante(2));
    g_on = false;   /* desde 2026-10-06 o modelo e opt-in (config.ini llm=1): na pratica so atrapalhava as regras */
    if (FILE *f = _wfopen((dir + L"\\config.ini").c_str(), L"r")) {
        char l[300];
        while (fgets(l, sizeof l, f)) {
            int v; char t[260];
            if (sscanf(l, "llm=%d", &v) == 1) g_on = v != 0;
            else if (sscanf(l, "tempo_ms=%d", &v) == 1) g_tempo_ms = v;
            else if (sscanf(l, "threads=%d", &v) == 1) g_threads = v;
            else if (sscanf(l, "url=http://%255[^:/]:%d", t, &v) == 2) { g_host = t; g_porta = v; g_externo = true; }
        }
        fclose(f);
    }
    if (const char *e = getenv("TF_IA_LLM")) g_on = atoi(e) != 0;
    if (const char *e = getenv("TF_IA_LLM_TEMPO")) g_tempo_ms = atoi(e);
    if (const char *e = getenv("TF_IA_LLM_URL")) { char t[260]; int v; if (sscanf(e, "http://%255[^:/]:%d", t, &v) == 2) { g_host = t; g_porta = v; g_externo = true; } }
    g_dados = getenv("TF_IA_DADOS") != nullptr;
    if (const char *e = getenv("TF_IA_EXPLORAR")) g_explorar = atoi(e);
    if (const char *e = getenv("TF_IA_LLM_LADO")) g_llm_lado = atoi(e);
    { const char *s = getenv("TF_IA_SEMENTE"); g_rng.seed(s ? (unsigned)atoi(s) : (unsigned)GetTickCount64() ^ GetCurrentProcessId()); }
    if (g_on && !g_externo) { if (!tem_modelo || !iniciar_melhor(0)) g_on = false; }
    if (g_log) g_log("decisor: %s%s%s (limite %d ms)", g_on ? "modelo LIGADO" : "modelo desligado (regras do plugin)", g_dados ? ", registrando dados" : "", g_explorar ? ", explorando" : "", g_tempo_ms);
}
void decisor_fim(void) { parar_servidor(); if (g_job) { CloseHandle(g_job); g_job = nullptr; } }

static bool http(bool post, const char *caminho, const std::string &corpo, std::string &resp, int tempo) {
    bool ok = false; HINTERNET s = nullptr, c = nullptr, r = nullptr;
    const std::wstring host(g_host.begin(), g_host.end()), cam(caminho, caminho + strlen(caminho));
    s = WinHttpOpen(L"TagForceRecomp-IA/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 500, 500, tempo, tempo);
    c = WinHttpConnect(s, host.c_str(), (INTERNET_PORT)g_porta, 0);
    if (c) r = WinHttpOpenRequest(c, post ? L"POST" : L"GET", cam.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (r) {
        static const wchar_t *cab = L"Content-Type: application/json\r\n";
        const BOOL en = post ? WinHttpSendRequest(r, cab, (DWORD)-1, (LPVOID)corpo.data(), (DWORD)corpo.size(), (DWORD)corpo.size(), 0) : WinHttpSendRequest(r, nullptr, 0, nullptr, 0, 0, 0);
        if (en && WinHttpReceiveResponse(r, nullptr)) {
            DWORD cod = 0, n = sizeof cod; WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &cod, &n, nullptr);
            char buf[4096]; n = 0;
            while (WinHttpReadData(r, buf, sizeof buf, &n) && n) resp.append(buf, n);
            ok = cod == 200 && !resp.empty();
        }
    }
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); if (s) WinHttpCloseHandle(s);
    return ok;
}

bool decisor_ativo(void) {
    if (!g_on || GetTickCount64() < g_pausa_ate) return false;
    if (g_pronto) return true;
    const ULONGLONG t = GetTickCount64();
    if (t >= g_ultimo_health + 700) {   /* o modelo ainda esta carregando: pergunta de vez em quando, sem travar o quadro */
        g_ultimo_health = t; std::string r;
        if (http(false, "/health", "", r, 300)) { g_pronto = true; if (g_log) g_log("decisor: modelo pronto"); return true; }
        if (g_proc) {   /* servidor morreu (ex.: sem Vulkan) ou nao ficou pronto em 25 s: tenta a proxima variante (GPU -> CPU) */
            const bool morreu = WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0;
            if (morreu || t > g_inicio + 25000) {
                DWORD cod = 0; if (morreu) GetExitCodeProcess(g_proc, &cod);
                if (g_log) g_log("decisor: servidor %s (codigo 0x%08lx), tentando a proxima variante", morreu ? "terminou" : "nao ficou pronto", cod);
                parar_servidor();
                if (!iniciar_melhor(g_var + 1)) { if (g_log) g_log("decisor: sem modelo, valem as regras"); g_on = false; }
            }
        }
    }
    return false;
}
bool decisor_quer(void) { return true; }   /* sempre: o anel das ultimas decisoes (despejado na marca F7) precisa do texto da situacao */

/* IMPORTANTE: este texto precisa ser identico ao de tools/ai/ia_treino/prompt.py (o modelo e treinado com ele) */
static std::string montar_prompt(const char *tipo, const std::string &sit, const std::vector<Opcao> &op) {
    std::string p = std::string("Yu-Gi-Oh! duel. Choose the best option.\nDECISION: ") + tipo + "\n" + sit + "OPTIONS:\n";
    for (size_t i = 0; i < op.size(); i++) p += std::to_string(i) + ") " + op[i].texto + "\n";
    return p + "ANSWER:\n";
}
static int perguntar(const std::string &prompt, size_t n_op) {
    const char ultimo = (char)('0' + (n_op > 10 ? 9 : n_op - 1));
    std::string corpo = "{\"prompt\":\"" + json_escape(prompt) + "\",\"n_predict\":1,\"temperature\":0,\"cache_prompt\":true,\"grammar\":\"root ::= [0-" + std::string(1, ultimo) + "]\"}";
    std::string resp; if (!http(true, "/completion", corpo, resp, g_tempo_ms)) return -1;
    size_t p = resp.find("\"content\""); if (p == std::string::npos) return -1;
    p = resp.find('"', resp.find(':', p) + 1); if (p == std::string::npos || p + 1 >= resp.size()) return -1;
    const char d = resp[p + 1]; return d >= '0' && d <= '9' ? d - '0' : -1;
}

static std::string json_registro(const char *tipo, int jog, const std::string &sit, const std::vector<Opcao> &op, int escolha, int regra, const char *origem, int base) {
    std::string j = std::string("{\"tipo\":\"") + tipo + "\",\"jog\":" + std::to_string(jog) + ",\"origem\":\"" + origem + "\",\"escolha\":" + std::to_string(escolha) + ",\"regra\":" + std::to_string(regra) + ",\"base\":" + std::to_string(base) + ",\"situacao\":\"" + json_escape(sit) + "\",\"opcoes\":[";
    for (size_t i = 0; i < op.size(); i++) { if (i) j += ","; j += "\"" + json_escape(op[i].texto) + "\""; }
    return j + "]}";
}

/* anel das ultimas decisoes (situacao, opcoes, escolha): despejado no log quando o jogador marca "a CPU errou" (F7 / L3+R3), para eu ver o que a CPU via e escolheu */
struct RegDec { std::string tipo, sit; std::vector<std::string> ops; int jog, idx, regra; std::string origem; ULONGLONG t; };
static std::vector<RegDec> g_anel;
void decisor_despejar(int n) {
    if (!g_log) return;
    const size_t k0 = g_anel.size() > 6 ? g_anel.size() - 6 : 0; const ULONGLONG agora = GetTickCount64();
    g_log("[marca-cpu] #%d: ultimas %zu decisoes do plugin (da mais antiga para a mais recente):", n, g_anel.size() - k0);
    for (size_t k = k0; k < g_anel.size(); k++) {
        const RegDec &d = g_anel[k];
        std::string o; for (size_t i = 0; i < d.ops.size(); i++) o += std::to_string(i) + ") " + d.ops[i].substr(0, 150) + " ";
        g_log("[marca-cpu] decisao J%d ha %llu s [%s]: escolheu %d (regra %d, origem %s) | opcoes: %s", d.jog, (unsigned long long)((agora - d.t) / 1000), d.tipo.c_str(), d.idx, d.regra, d.origem.c_str(), o.substr(0, 700).c_str());
        { std::string sit = d.sit.substr(0, 800); for (char &ch : sit) if (ch == (char)10 || ch == (char)13) ch = (char)47; g_log("[marca-cpu]    situacao: %s", sit.c_str()); }
    }
}
static std::map<size_t, int> g_cache;   /* o motor repete o mesmo teste varias vezes por quadro: mesma situacao + opcoes + jogador = mesma resposta */
int decisor_decidir(const char *tipo, int jog, const std::string &situacao, const std::vector<Opcao> &op, int regra_idx, std::string *motivo) {
    if (op.empty()) return 0;
    if (regra_idx < 0 || regra_idx >= (int)op.size()) regra_idx = (int)op.size() - 1;
    if (op.size() < 2) return 0;
    /* a escolha da regra entra na chave: com o modelo desligado (padrao) quem decide e a regra, e o texto da situacao nao descreve tudo o que ela
     * olha; sem isso uma resposta guardada de antes podia passar por cima da regra atual */
    size_t chave = std::hash<std::string>()(std::string(tipo) + "|" + std::to_string(jog) + "|" + std::to_string(regra_idx) + "|" + situacao);
    for (auto &o : op) chave = chave * 1315423911u + std::hash<std::string>()(o.texto);
    { auto it = g_cache.find(chave); if (it != g_cache.end()) return it->second; }
    int idx = -1; const char *origem = "regra";
    if ((g_llm_lado < 0 || g_llm_lado == jog) && decisor_ativo()) {
        const ULONGLONG t0 = GetTickCount64();
        idx = perguntar(montar_prompt(tipo, situacao, op), op.size());
        const unsigned ms = (unsigned)(GetTickCount64() - t0);
        if (idx < 0 || idx >= (int)op.size()) {
            if (++g_falhas >= 3) { g_pausa_ate = GetTickCount64() + 30000; g_falhas = 0; g_pronto = false; if (g_log) g_log("decisor: 3 falhas seguidas, em repouso por 30 s"); }
            if (g_log) g_log("decisor: sem resposta util (%u ms) para %s", ms, tipo);
            idx = -1;
        } else { g_falhas = 0; origem = "modelo"; if (g_log) g_log("decisor: %s -> %d (%u ms)", tipo, idx, ms); }
    }
    if (idx < 0) idx = regra_idx;
    const int base = idx;   /* a escolha antes da exploracao (modelo, ou a regra se o modelo nao respondeu) */
    if (g_explorar > 0 && (int)(g_rng() % 100) < g_explorar) {   /* exploracao: uma opcao diferente da base (dados de treino) */
        idx = (int)(g_rng() % (op.size() - 1)); if (idx >= base) idx++; origem = "explorar";
    }
    if (g_cache.size() >= 20000) g_cache.clear();   /* a sessao inteira acumulava (uma entrada por situacao nova): zera de vez em quando */
    g_cache[chave] = idx;
    { RegDec r; r.tipo = tipo; r.sit = situacao; for (auto &o : op) r.ops.push_back(o.texto); r.jog = jog; r.idx = idx; r.regra = regra_idx; r.origem = origem; r.t = GetTickCount64(); g_anel.push_back(r); if (g_anel.size() > 12) g_anel.erase(g_anel.begin()); }
    if (g_dados || g_explorar > 0) {   /* uma linha JSON por decisao em ia_decisoes.jsonl (pasta de trabalho); o log do jogo corta linhas longas */
        if (FILE *f = fopen("ia_decisoes.jsonl", "ab")) { const std::string j = json_registro(tipo, jog, situacao, op, idx, regra_idx, origem, base) + "\n"; fwrite(j.data(), 1, j.size(), f); fclose(f); }
    }
    if (motivo) motivo->clear();
    return idx;
}
