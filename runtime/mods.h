/* Mods (fase 1: substituicao de arquivos do disco). Ver mods.cpp e docs/MODS.md. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <string>

void mods_init(void);                     /* depois do iso_open: varre mods/ e monta o indice por LBA */
/* Se o arquivo do disco que comeca no setor lba (aberto pelo nome ou por "sce_lbn") foi trocado por um mod: abre o
 * arquivo do mod (fp, leitura) e devolve true com o deslocamento dentro dele e o tamanho visivel a partir dali. */
bool mods_open(uint32_t lba, FILE **fp, uint64_t *base, uint64_t *size);
bool mods_size(uint32_t lba, uint64_t *size);   /* tamanho do arquivo do mod (getstat) */
/* leitura de setores pelo dispositivo bruto (umd1:): corrige o tamanho nos registros de diretorio dos arquivos trocados */
void mods_patch_raw(uint32_t lba, uint8_t *buf, uint32_t nsectors);

/* API do lancador (sem mods_init): trabalha direto em <root> (pasta mods/). Ordem = prioridade, da menor para a maior (o ultimo vence). */
struct ModEntry { std::string folder; std::wstring label, detail; bool enabled, visible; };   /* visible=false: pacotes de textura (Graphics > Textures) */
std::vector<ModEntry> mods_list_dir(const std::wstring &root);
void mods_dir_set_enabled(const std::wstring &root, const std::string &folder, bool on);
void mods_dir_move(const std::wstring &root, const std::string &folder, int dir);   /* dir>0: sobe a prioridade; dir<0: desce */

/* menu Mods */
int mods_count(void);
int mods_find(const char *folder);        /* indice do mod pela pasta (mods/<pasta>); -1 se nao existe */
bool mods_menu_visible(int i);             /* pacotes Enhanced/Original sao controlados em Graphics > Textures */
void mods_plugin_dirs(std::vector<std::pair<std::string, std::wstring>> &out);   /* {nome, pasta} dos mods ligados (plugins.cpp procura plugin.dll) */
const wchar_t *mods_label(int i);         /* nome do mod */
const wchar_t *mods_detail(int i);        /* "v1.0 \x00B7 autor" (o que o mod.txt tiver) */
bool mods_enabled(int i);
void mods_set_enabled(int i, bool on);    /* grava/apaga mods/<pasta>/disabled; vale no proximo inicio */
void mods_texture_dirs(std::vector<std::wstring> &out);   /* pastas de texturas substitutas dos mods ligados */
bool mods_enabled_has_file(const char *relative);   /* algum mod ligado tem este arquivo (ex.: marca "menu_estilo_pc.txt" do Enhanced) */
void mods_open_folder(void);              /* abre mods/ no Explorer (cria com LEIA-ME se faltar) */
