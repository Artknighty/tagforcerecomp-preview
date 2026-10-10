/* Textos de PC: o jogo fala de "Memory Stick" (verificando, nao remova, nao inserido, sem espaco...) nas telas de
 * carregar/salvar e no menu do titulo. Aqui esses textos sao trocados por equivalentes de PC NA LEITURA dos arquivos
 * (a ISO do usuario nao e alterada): BOOT.BIN (carregado pelo module_load) e title_menu_e.ehp (lido por setor pelo
 * jogo, sem nome: a troca e feita pelo LBA do arquivo). Cada texto novo ocupa o espaco do original e o resto vira zero
 * (as strings sao terminadas em zero). Os originais tem %c (o simbolo TM) e %s (espaco livre) consumidos em ordem pelo
 * sprintf do jogo: os textos novos NAO tem especificadores (tirar so o %c e deixar o %s trocaria os argumentos).
 * Sem o texto original no arquivo (outra versao da ISO), nada e trocado e o log avisa. TF_PSP_TEXT=1 desliga.
 * Por enquanto so ingles (the maintainers' language); os outros idiomas ficam com o texto original. */
#include "hle.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

struct TextFix { const char *file; const char16_t *from, *to; };
static const TextFix g_fixes[] = {
    {"PSP_GAME/SYSDIR/BOOT.BIN",
     u"Checking Memory Stick%c.\nPlease do not remove the Memory Stick%c or turn off power.",
     u"Checking save data...\nPlease do not close the game."},
    {"PSP_GAME/SYSDIR/BOOT.BIN",
     u"There is not enough free space on this Memory Stick%c.\nYou need at least %s of free space to save data from this game.\nContinue anyway?",
     u"There is not enough free disk space.\nYou need more free space to save data from this game.\nContinue anyway?"},
    {"PSP_GAME/SYSDIR/BOOT.BIN",
     u"There is not enough free space on this Memory Stick%c.\nYou need at least %s of free space to save data from this game.\nWould you like to delete a data file to free up additional space?",
     u"There is not enough free disk space.\nYou need more free space to save data from this game.\nWould you like to delete a data file to free up additional space?"},
    {"PSP_GAME/SYSDIR/BOOT.BIN",
     u"A Memory Stick%c has not been inserted. At least %s of free space is required to save data from this game.\nDo you wish to start the game anyway?",
     u"The save data folder could not be accessed.\nDo you wish to start the game anyway?"},
    {"PSP_GAME/SYSDIR/BOOT.BIN",
     u"Loading...\nDo not remove the Memory Stick%c or turn off the power.",
     u"Loading...\nPlease do not close the game."},
    {"PSP_GAME/SYSDIR/BOOT.BIN",
     u"A Memory Stick%c has not been inserted.",
     u"Save data folder not found."},
    {"PSP_GAME/USRDIR/title_menu/title_menu_e.ehp",
     u"Previously saved data must be overwritten to create new game data on the Memory Stick%c.\nIs this OK?",
     u"Previously saved data must be overwritten to create new game data.\nIs this OK?"},
    {"PSP_GAME/USRDIR/title_menu/title_menu_e.ehp",
     u"New game data will be created on the Memory Stick%c.\nIs this OK?",
     u"New game data will be created.\nIs this OK?"},
    {"PSP_GAME/USRDIR/title_menu/title_menu_e.ehp",
     u"New game data will be created on the Memory Stick%c.\nAll currently saved game data will be deleted.\nIs this OK?",
     u"New game data will be created.\nAll currently saved game data will be deleted.\nIs this OK?"},
    {"PSP_GAME/USRDIR/title_menu/title_menu_e.ehp",
     u"You may not be able to download data from the Yu-Gi-Oh! Website if the Memory Stick%c doesn't have enough free space.\n\n"
     u"(START button: go to data deletion function)\n\nAlso, make sure not to remove the Memory Stick%c or turn off power when "
     u"connected to the Yu-Gi-Oh! Website.\n\nConnect to the Yu-Gi-Oh! Website?",
     u"You may not be able to download data from the Yu-Gi-Oh! Website if there isn't enough free disk space.\n\n"
     u"(START button: go to data deletion function)\n\nAlso, make sure not to close the game while connected to the "
     u"Yu-Gi-Oh! Website.\n\nConnect to the Yu-Gi-Oh! Website?"},
    {"PSP_GAME/USRDIR/title_menu/title_menu_e.ehp",
     u"Game data must exist on the Memory Stick%c\nin order to connect to the Yu-Gi-Oh! Website.",
     u"Saved game data must exist\nin order to connect to the Yu-Gi-Oh! Website."},
};

struct Patch { uint32_t off; std::vector<uint8_t> bytes; };
static std::map<uint32_t, std::vector<Patch>> g_patches;   /* LBA do arquivo -> trocas (posicao no arquivo) */

static size_t u16len(const char16_t *s) { size_t n = 0; while (s[n]) n++; return n; }
static void build(void) {
    static bool done; if (done) return; done = true;
    if (getenv("TF_PSP_TEXT")) return;
    std::map<std::string, std::vector<uint8_t>> files;   /* arquivo inteiro (so os poucos da tabela) */
    std::map<std::string, uint32_t> lbas;
    int ok = 0, total = 0;
    for (const auto &fx : g_fixes) {
        total++;
        if (!files.count(fx.file)) {
            uint32_t lba, size; bool isdir;
            if (iso_find(fx.file, &lba, &size, &isdir) != 0 || isdir) { files[fx.file]; continue; }
            std::vector<uint8_t> d(size); iso_read(lba, 0, d.data(), size);
            files[fx.file] = std::move(d); lbas[fx.file] = lba;
        }
        const std::vector<uint8_t> &d = files[fx.file];
        size_t nf = u16len(fx.from), nt = u16len(fx.to);
        if (d.empty() || nt > nf) continue;
        const uint8_t *from = (const uint8_t *)fx.from; size_t fb = nf * 2;
        /* a string inteira: precedida de zero (ou inicio) e seguida do terminador */
        for (size_t i = 0; i + fb + 2 <= d.size(); i += 2) {
            if (memcmp(&d[i], from, fb) || d[i + fb] || d[i + fb + 1]) continue;
            if (i >= 2 && (d[i - 2] || d[i - 1])) continue;
            Patch p; p.off = (uint32_t)i; p.bytes.assign(fb, 0); memcpy(p.bytes.data(), fx.to, nt * 2);
            g_patches[lbas[fx.file]].push_back(std::move(p)); ok++;
            break;
        }
    }
    fprintf(stderr, "[texto] textos de PC: %d de %d trocas encontradas%s\n", ok, total, ok < total ? " (ISO diferente? os que faltam ficam com o original)" : "");
}
/* aplica as trocas que caem em [pos, pos+n) de um arquivo do disco (identificado pelo LBA) */
void pctext_apply(uint32_t lba, uint64_t pos, uint8_t *buf, uint64_t n) {
    build();
    auto it = g_patches.find(lba); if (it == g_patches.end()) return;
    for (const auto &p : it->second) {
        uint64_t a = std::max<uint64_t>(pos, p.off), b = std::min<uint64_t>(pos + n, (uint64_t)p.off + p.bytes.size());
        if (a < b) memcpy(buf + (a - pos), p.bytes.data() + (a - p.off), (size_t)(b - a));
    }
}
