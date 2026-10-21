/* Game version (ULES-00600 European, ULUS-10136 American and the fan translations made on top of it...). One executable runs every version: each
 * one has its own installed data folder (jogo/ or jogo_<ID>/) and its own translated code (gen/ or gen/<ID>/, prepare.cpp). */
#pragma once
#include <string>
#ifndef TF_GAME_ID
#define TF_GAME_ID "ULES-00600"
#endif
std::string game_id_built(void);                        /* the version being played (set from the installed data at start) */
void game_id_set_built(const std::string &id);
std::string game_id_iso(const std::wstring &iso);       /* id read from the UMD_DATA.BIN of an ISO ("" if it cannot be read) */
std::string game_id_dir(const std::wstring &dir);       /* the same, from an installed data folder */
std::wstring game_data_dir_name(const std::string &id); /* L"jogo" (European) or L"jogo_ULUS10136" */
