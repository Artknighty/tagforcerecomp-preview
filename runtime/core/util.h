/* Utilitarios do runtime usados por varios modulos (antes cada arquivo tinha a sua copia). */
#pragma once
#include <string>
#include <stdint.h>

std::wstring exe_dir(void);            /* pasta do .exe, com a barra final */
int tf_diag_opt(const char *key, int def);   /* "key=value" line of diag.txt next to the .exe (diagnostic builds), else def */
bool tf_lat_debug(void);             /* latency/audio diagnostics: TF_LAT_DEBUG is set, or a file named diag.txt sits next to the .exe */
std::wstring app_ini_path(void);       /* tagforce_recomp.ini ao lado do .exe (configuracoes do jogador) */
std::string to_utf8(const std::wstring &w);
std::wstring to_wide(const std::string &s);
bool quadro_na_lista(const char *lista, uint64_t quadro);   /* lista "n1,n2,..." de variavel de depuracao (SNAP_FRAME, TF_INTERP_SNAP...) contem o quadro? */
#include <functional>
/* grava um BMP de 24 bits (w x h, de cima para baixo); fill(y, bgr) preenche a linha y (w*3 bytes, ordem B,G,R). false se nao abriu o arquivo */
bool write_bmp24(const char *fn, int w, int h, const std::function<void(int, uint8_t *)> &fill);
