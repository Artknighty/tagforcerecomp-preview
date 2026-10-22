/* Decisor: um modelo de linguagem pequeno e LOCAL escolhe entre as acoes que o plugin enumerou. O modelo so recebe a situacao em texto e uma lista numerada de
 * opcoes ja legais (com as consequencias calculadas pelo motor do jogo) e responde um digito. Quem roda o modelo e o llama-server (llama.cpp), que acompanha o mod
 * em <mod>/llm/ (llama-server.exe + modelo.gguf): o plugin o inicia sozinho (processo escondido, morre junto com o jogo) em uma porta livre.
 * Se o modelo nao estiver pronto, demorar mais que o limite ou responder lixo, vale a regra do plugin (nunca trava o jogo).
 * Configuracao (config.ini da pasta do mod): llm=0/1 (padrao DESLIGADO desde 2026-10-06: o modelo e opcional e as regras do plugin decidem; precisa de
 * llm/modelo.gguf), url=http://host:porta (servidor llama.cpp ja rodando; nao inicia o proprio), tempo_ms=3500, threads=N; variaveis TF_IA_LLM /
 * TF_IA_LLM_URL / TF_IA_LLM_TEMPO as sobrepoem.
 * Pesquisa/treino: TF_IA_DADOS=1 grava cada decisao ([dec] {json}); TF_IA_EXPLORAR=<0..100> escolhe uma opcao aleatoria nessa % das decisoes (dados de treino). */
#pragma once
#include <string>
#include <vector>
#include <wchar.h>

struct Opcao { std::string texto; };

void decisor_init(const wchar_t *mod_dir, void (*log)(const char *fmt, ...));
bool decisor_ativo(void);   /* modelo carregado e respondendo */
bool decisor_quer(void);    /* montar o texto da decisao? (sempre: o anel das ultimas decisoes, despejado na marca F7, precisa dele) */
/* Decide entre as opcoes: o modelo (se pronto), senao a exploracao aleatoria (se ligada), senao a regra do plugin (regra_idx). Sempre devolve um indice 0..n-1.
 * jog = jogador que decide; sempre grava uma linha [dec] {json} (estado, opcoes, escolha, origem) quando decisor_quer(). */
int decisor_decidir(const char *tipo, int jog, const std::string &situacao, const std::vector<Opcao> &op, int regra_idx, std::string *motivo = nullptr);
void decisor_fim(void);     /* encerra o servidor do modelo */
void decisor_despejar(int n);   /* log: as ultimas decisoes (F7 / L3+R3 = "a CPU errou") */
