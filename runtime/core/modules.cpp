/* Carregador de modulos PSP (PRX/ELF relocavel) a partir do ISO do usuario + ModuleMgrForUser.
 * O codigo de cada modulo ja foi recompilado (gen/<nome>.c) para uma base fixa; aqui so montamos a imagem. */
#include "hle.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <functional>

struct Loaded { const GenModule *g; uint32_t uid; };
static std::vector<Loaded> g_loaded;
static std::string lower(std::string s) { for (auto &ch : s) ch = (char)tolower((unsigned char)ch); return s; }

/* Monta a imagem do modulo (primeiro segmento do ELF) e aplica as relocacoes MIPS. modinfo = offset do sceModuleInfo (p_paddr).
 * Todo offset vindo do arquivo e conferido: um PRX corrompido falha o carregamento em vez de ler/escrever fora do buffer. */
static bool build_image(const std::vector<uint8_t> &f, uint32_t base, std::vector<uint8_t> &img, uint32_t *modinfo) {
    auto in_file = [&](uint64_t o, uint64_t n) { return o + n <= f.size(); };
    auto u32 = [&](size_t o) { uint32_t v = 0; if (in_file(o, 4)) memcpy(&v, &f[o], 4); return v; };
    auto u16 = [&](size_t o) { uint16_t v = 0; if (in_file(o, 2)) memcpy(&v, &f[o], 2); return v; };
    if (f.size() < 0x40 || memcmp(f.data(), "\x7f" "ELF", 4) != 0) return false;
    uint32_t phoff = u32(28), shoff = u32(32); uint16_t shentsize = u16(46), shnum = u16(48);
    if (!in_file(phoff, 0x20)) return false;
    uint32_t p_off = u32(phoff + 4), p_paddr = u32(phoff + 12), p_filesz = u32(phoff + 16), p_memsz = u32(phoff + 20);
    if (!in_file(p_off, p_filesz) || p_filesz > p_memsz) return false;
    img.assign(p_memsz, 0); memcpy(img.data(), &f[p_off], p_filesz);
    *modinfo = p_paddr & 0x7fffffffu;
    auto word = [&](uint32_t off) -> uint32_t * { return (uint64_t)off + 4 <= img.size() ? (uint32_t *)&img[off] : nullptr; };
    std::vector<uint32_t> hi;
    for (unsigned s = 0; s < shnum; s++) {
        size_t sh = shoff + (size_t)s * shentsize;
        if (!in_file(sh, 24) || u32(sh + 4) != 0x700000a0) continue;
        uint32_t off = u32(sh + 16), size = u32(sh + 20);
        if (!in_file(off, size)) return false;
        for (uint32_t i = 0; i < size / 8; i++) {
            uint32_t r_off = u32(off + i * 8), info = u32(off + i * 8 + 4), typ = info & 0xff;
            uint32_t *w = word(r_off);
            if (!w) { fprintf(stderr, "reloc fora da imagem: %08x\n", r_off); return false; }
            if (typ == 2) *w += base;
            else if (typ == 4) *w = (*w & 0xfc000000u) | ((*w + (base >> 2)) & 0x03ffffffu);
            else if (typ == 5) { hi.push_back(r_off); continue; }
            else if (typ == 6) {
                int32_t lo = (int16_t)(*w & 0xffff);
                for (uint32_t ho : hi) {
                    uint32_t *hw = word(ho);
                    uint32_t val = ((*hw & 0xffff) << 16) + lo + base;
                    uint32_t h = ((val >> 16) + ((val & 0x8000) ? 1 : 0)) & 0xffff;
                    *hw = (*hw & 0xffff0000u) | h;
                }
                hi.clear(); *w = (*w & 0xffff0000u) | ((lo + base) & 0xffff);
            } else { fprintf(stderr, "reloc tipo %u desconhecido\n", typ); return false; }
        }
    }
    return true;
}

/* Soma `delta` a um par HI16/LO16 do MIPS: os HI16 pendentes em `his` pegam o carry do LO16 em `lo_word` (como o carregador do firmware). */
static void relocate_hilo(std::vector<uint32_t> &his, uint32_t *lo_word, uint32_t delta, const std::function<uint32_t *(uint32_t)> &word) {
    int32_t lo = (int16_t)(*lo_word & 0xffff);
    for (uint32_t h : his) {
        uint32_t *hp = word(h); if (!hp) continue;
        uint32_t val = ((*hp & 0xffff) << 16) + lo + delta;
        *hp = (*hp & 0xffff0000u) | (((val >> 16) + ((val & 0x8000) ? 1 : 0)) & 0xffff);
    }
    his.clear(); *lo_word = (*lo_word & 0xffff0000u) | ((lo + delta) & 0xffff);
}

/* Importacoes de VARIAVEIS (sceVstub), igual a tools/translator/imports.py:apply_var_imports. O codigo recompilado ja saiu com os
 * enderecos certos; aqui corrigimos a imagem em memoria (palavras de dados R_MIPS_32 e a copia do .text). */
static void apply_var_imports(std::vector<uint8_t> &img, uint32_t base, uint32_t mi) {   /* mi = offset do sceModuleInfo */
    auto at = [&](uint32_t a) -> uint32_t * { return (a >= base && (uint64_t)(a - base) + 4 <= img.size()) ? (uint32_t *)&img[a - base] : nullptr; };
    auto rd = [&](uint32_t a) { const uint32_t *p = at(a); return p ? *p : 0; };
    if ((uint64_t)mi + 0x34 > img.size()) return;
    uint32_t st = *(uint32_t *)&img[mi + 0x2c], se = *(uint32_t *)&img[mi + 0x30];
    for (uint32_t o = st; o < se;) {
        if (!at(o) || (uint64_t)(o - base) + 10 > img.size()) break;
        uint32_t size = img[o - base + 8], nvar = img[o - base + 9];
        if (!size) break;
        if (nvar && size >= 6) {
            uint32_t varp = rd(o + 20);
            for (uint32_t i = 0; i < nvar; i++) {
                uint32_t refp = rd(varp + 8 * i), nid = rd(varp + 8 * i + 4), addr;
                if (!host_resolve_export(nid, &addr)) { fprintf(stderr, "[modulo] variavel importada %08x sem export\n", nid); continue; }
                std::vector<uint32_t> his;
                for (uint32_t r = refp; at(r); r += 4) {   /* a lista de referencias termina em 0 (ou sai da imagem) */
                    uint32_t w = *at(r); if (!w) break;
                    uint32_t typ = w >> 26, a = (w & 0x03ffffffu) << 2; uint32_t *p = at(a); if (!p) continue;
                    if (typ == 2) *p += addr;
                    else if (typ == 5) his.push_back(a);
                    else if (typ == 6) relocate_hilo(his, p, addr, at);
                }
            }
        }
        o += size * 4;
    }
}

uint32_t module_load(const char *path_in, bool *ok) {
    *ok = false;
    std::string p = lower(path_in); for (auto &ch : p) if (ch == 0x5c) ch = '/';
    size_t colon = p.find(':'); if (colon != std::string::npos) p = p.substr(colon + 1);
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    if (p.compare(0, 9, "psp_game/") == 0) p.erase(0, 9);
    /* um modulo pode ter varias VARIANTES recompiladas em bases diferentes (linhas repetidas em tools/translator/modules.tsv), porque
     * o mesmo PRX e carregado em contextos com memoria diferente (ex.: junto do libtitle ou do libstory) */
    std::vector<const GenModule *> cands;
    uint32_t want_lba, want_size; unsigned gnm; const GenModule *gmv = gen_modules(&gnm);
    if (parse_lbn_path(p, &want_lba, &want_size)) {   /* carga por endereco do disco: acha o modulo cujo arquivo esta nesse LBA */
        for (unsigned i = 0; i < gnm; i++) {
            uint32_t l2, s2; bool d2;
            if (iso_find("PSP_GAME/" + std::string(gmv[i].iso_path), &l2, &s2, &d2) == 0 && l2 == want_lba) cands.push_back(&gmv[i]);
        }
    } else
    for (unsigned i = 0; i < gnm; i++) if (lower(gmv[i].iso_path) == p) cands.push_back(&gmv[i]);
    const GenModule *g = nullptr;
    for (auto *cg : cands) for (auto &l : g_loaded) if (l.g == cg) { *ok = true; return l.uid; }   /* ja carregado */
    for (auto *cg : cands) if (k_region_free(cg->base, cg->memsz)) { g = cg; break; }
    if (!g && !cands.empty()) {
        uint32_t sug = k_probe_low(cands[0]->memsz);
        fprintf(stderr, "[modulo] %s: nenhuma base recompilada livre (tentadas:", cands[0]->iso_path);
        for (auto *cg : cands) fprintf(stderr, " 0x%08x", cg->base);
        fprintf(stderr, "). O alocador poria em 0x%08x: acrescente essa base em tools/translator/modules.tsv e regenere.\n", sug);
        return 0x800200d9u;
    }
    if (!g && parse_lbn_path(p, &want_lba, &want_size)) {
        /* modulos de sistema do firmware (USRDIR/module/<nome>.prx: libfont, psmf...): sao substituidos por HLE; devolvemos um modulo "fantasma" */
        uint32_t dl, ds; bool dd;
        if (iso_find("PSP_GAME/USRDIR/module", &dl, &ds, &dd) == 0) {
            for (auto &e : iso_list_dir(dl, ds)) {
                uint32_t l2, s2; bool d2;
                if (!e.dir && iso_find("PSP_GAME/USRDIR/module/" + e.name, &l2, &s2, &d2) == 0 && l2 == want_lba) {
                    static std::map<std::string, uint32_t> ghosts;
                    if (!ghosts.count(e.name)) ghosts[e.name] = k_register_module(0, 0, 0, 0, 0, 0, e.name.c_str());
                    fprintf(stderr, "[modulo] %s (firmware) -> substituido por HLE\n", e.name.c_str());
                    *ok = true; return ghosts[e.name];
                }
            }
        }
    }
    if (!g) { fprintf(stderr, "[modulo] '%s' nao foi recompilado (tools/translator/modules.tsv)\n", path_in); return 0x80020139u; }
    uint32_t lba, size; bool isdir;
    if (iso_find("PSP_GAME/" + std::string(g->iso_path), &lba, &size, &isdir) != 0) { fprintf(stderr, "[modulo] %s ausente no ISO\n", g->iso_path); return 0x80010002u; }
    std::vector<uint8_t> file(size); iso_read(lba, 0, file.data(), size);
    pctext_apply(lba, 0, file.data(), size);   /* textos de "Memory Stick" do BOOT -> textos de PC */
    std::vector<uint8_t> img; uint32_t modinfo = 0;
    if (!build_image(file, g->base, img, &modinfo) || img.size() != g->memsz) {
        fprintf(stderr, "[modulo] %s: imagem incompativel com o codigo recompilado (tamanho %zu, esperado %u)\n", g->name, img.size(), g->memsz); return 0x80020139u;
    }
    apply_var_imports(img, g->base, modinfo);
    if (!k_alloc((uint32_t)img.size(), 2, g->base, g->name)) {
        fprintf(stderr, "[modulo] %s: regiao 0x%08x ocupada (outro modulo ainda carregado?)\n", g->name, g->base); return 0x800200d9u;
    }
    host_load(g->base, img.data(), img.size());
    host_module_add(g);
    uint32_t uid = k_register_module(g->base, g->memsz, g->textsz, g->datasz, g->bsssz, g->gp, g->name);
    g_loaded.push_back({g, uid});
    fprintf(stderr, "[modulo] carregado %s em 0x%08x (uid %x)\n", g->name, g->base, uid);
    *ok = true; return uid;
}

static Loaded *by_uid(uint32_t uid) { for (auto &l : g_loaded) if (l.uid == uid) return &l; return nullptr; }

HLE(sceKernelLoadModule) { bool ok; RET(module_load(mem_cstr(ARG(0)), &ok)); }
HLE(sceKernelStartModule) {   /* (uid, arglen, argp, int *status, opt) */
    Loaded *l = by_uid(ARG(0)); if (!l) { if (ARG(3)) wr32(ARG(3), 0); RET(ARG(0)); return; }   /* modulo fantasma */
    uint32_t status = 0;
    if (l->g->entry) {
        uint32_t gp = c->r[28]; uint32_t a1 = ARG(1), a2 = ARG(2), st = ARG(3), uid = l->uid, ent = l->g->entry, mgp = l->g->gp;
        c->r[28] = mgp; c->r[4] = a1; c->r[5] = a2;
        psp_call(c, ent); status = c->r[2]; c->r[28] = gp;
        if (st) wr32(st, status);
        RET(uid); return;
    }
    if (ARG(3)) wr32(ARG(3), status);
    RET(l->uid);
}
HLE(sceKernelStopModule) { if (ARG(3)) wr32(ARG(3), 0); RET(0); }
HLE(sceKernelUnloadModule) {
    uint32_t uid = ARG(0);
    Loaded *l = by_uid(uid); if (!l) { RET(0); return; }
    host_module_remove(l->g); k_free(l->g->base); k_unregister_module(uid);
    fprintf(stderr, "[modulo] descarregado %s\n", l->g->name);
    g_loaded.erase(std::remove_if(g_loaded.begin(), g_loaded.end(), [&](const Loaded &x) { return x.uid == uid; }), g_loaded.end()); RET(0);
}
HLE(sceKernelGetModuleIdList) { RET(0); }

/* ---- state save / load (savestate.cpp) */
#include <memory>
std::shared_ptr<void> modules_state_save(void) { return std::make_shared<std::vector<Loaded>>(g_loaded); }
void modules_state_load(const std::shared_ptr<void> &p) { g_loaded = *(const std::vector<Loaded> *)p.get(); }
