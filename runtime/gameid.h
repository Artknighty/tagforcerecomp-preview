/* Versao do jogo (ULES-00600 europeia, ULUS-10136 americana e as traducoes feitas sobre ela...). O codigo recompilado vale para UMA versao: cada versao
 * tem o proprio executavel (gen/run.exe = europeia; gen/run_<ID>.exe = as outras) e a propria pasta de dados instalados (jogo/ ou jogo_<ID>/). */
#pragma once
#include <string>
#ifndef TF_GAME_ID
#define TF_GAME_ID "ULES-00600"
#endif
std::string game_id_built(void);                        /* a versao para a qual ESTE executavel foi recompilado */
void game_id_set_built(const std::string &id);          /* generic (release) build only: the version is whatever game was installed */
std::string game_id_iso(const std::wstring &iso);       /* id lido do UMD_DATA.BIN de uma ISO ("" se nao for possivel) */
std::string game_id_dir(const std::wstring &dir);       /* idem, de uma pasta de dados instalada */
std::wstring game_data_dir_name(const std::string &id); /* L"jogo" (europeia) ou L"jogo_ULUS10136" */
std::wstring game_exe_name(const std::string &id);      /* L"run.exe" (europeia) ou L"run_ULUS10136.exe" */
/* Se a ISO/pasta escolhida e de OUTRA versao: abre o executavel certo (devolve true: este processo deve sair) ou avisa como compila-lo (true tambem). */
bool game_handoff(const std::string &iso_utf8, const std::wstring &data);
