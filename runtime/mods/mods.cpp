/* Mods, fase 1: SUBSTITUICAO DE ARQUIVOS DO DISCO (sem tocar na ISO do usuario).
 * mods/<pasta>/ ao lado do .exe (pasta de trabalho), com os arquivos no mesmo caminho da ISO, ex.:
 *     mods/MeuMod/PSP_GAME/USRDIR/card/cardm_e.cip
 *     mods/MeuMod/mod.txt        (opcional: name=, author=, version=, description=)
 *     mods/MeuMod/disabled       (se existir, o mod esta desligado; o menu Mods cria/apaga)
 * Quando o jogo abre um arquivo do disco (pelo caminho ou, como faz na maioria das vezes, por "sce_lbn0x..", o setor
 * inicial), o runtime entrega o arquivo do mod. A troca e indexada pelo LBA do arquivo na ISO; aberturas por LBN que
 * caem no MEIO de um arquivo trocado (sub-trechos de um pacote) leem o trecho correspondente do arquivo do mod.
 * Ordem: mods/load_order.txt (menor -> maior prioridade; editado na pagina Mods do lancador); sem o arquivo, ordem alfabetica.
 * Se dois mods trocam o mesmo arquivo/textura, vale o ultimo (o log avisa).
 * Nao se trocam modulos de codigo (BOOT.BIN, *.prx): o codigo e recompilado e a imagem precisa bater. */
#include "hle.h"
#include "mods.h"
#include "util.h"
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

struct ModFile { std::wstring host; uint64_t size; uint32_t lba, iso_size; int mod; std::string iso_path; };
struct Mod { std::wstring dir, label, detail, texext; std::string name, folder; bool enabled; int files = 0; };   /* label = nome, detail = "v1.0 . autor"; texext: pasta externa de texturas (mod.txt: textures=) */
static std::vector<Mod> g_mods;
static std::map<uint32_t, ModFile> g_by_lba;   /* LBA inicial do arquivo na ISO -> arquivo do mod */

static bool exists(const std::wstring &p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
/* pasta dos mods: mods/ na pasta de trabalho; TF_MODS_DIR=pasta usa outra (testes/reproducoes: o pacote de texturas tem 2 GB e
 * antes era copiado para cada reproducao) */
static std::wstring mods_root(void) {
    static std::wstring r; static bool init;
    if (!init) {
        init = true; const wchar_t *e = _wgetenv(L"TF_MODS_DIR"); r = e && *e ? e : L"mods";
        for (auto &ch : r) if (ch == L'/') ch = L'\\';
        while (r.size() > 1 && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    }
    return r;
}

static void read_meta(Mod &m, std::string *author, std::string *version) {   /* mod.txt: chave=valor, UTF-8 */
    FILE *f = _wfopen((m.dir + L"\\mod.txt").c_str(), L"rb"); if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string l = line; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.size() >= 3 && (uint8_t)l[0] == 0xef && (uint8_t)l[1] == 0xbb && (uint8_t)l[2] == 0xbf) l.erase(0, 3);   /* BOM */
        size_t eq = l.find('='); if (eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        for (auto &ch : k) ch = (char)tolower((unsigned char)ch);
        if (k == "name" || k == "nome") m.name = v; else if (k == "author" || k == "autor") *author = v; else if (k == "version" || k == "versao") *version = v; else if (k == "textures" || k == "texturas") m.texext = to_wide(v);
    }
    fclose(f);
}
/* arquivos do mod, recursivo; rel = caminho relativo com '/' */
static void walk(const std::wstring &base, const std::wstring &rel, std::vector<std::pair<std::wstring, uint64_t>> &out) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((base + L"\\" + rel + (rel.empty() ? L"*" : L"\\*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
        std::wstring r = rel.empty() ? n : rel + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk(base, r, out);
        else out.push_back({r, ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* Ordem de prioridade: mods/load_order.txt, uma pasta por linha, da MENOR para a MAIOR prioridade (quem vem depois vence em
 * arquivos, texturas, videos e ganchos de plugin). Pastas fora do arquivo entram no fim (maior prioridade), em ordem alfabetica;
 * os pacotes de textura (Enhanced/TexturasPC) entram no inicio: qualquer mod do usuario vence o pacote. */
static bool is_pack_folder(const std::string &f) { return f == "EnhancedTextures" || f == "TexturasPC"; }
/* fora da lista do lancador: pacotes de textura (Graphics > Textures) e mods com menu proprio no jogo (Cheats) ou sem uso no dia a dia */
static bool is_launcher_hidden(const std::string &f) { return is_pack_folder(f) || f == "Cheats" || f == "CPUvsCPU" || f == "MonstrosNoCampo"; }
static std::vector<std::string> read_order_file(const std::wstring &root) {
    std::vector<std::string> o; FILE *f = _wfopen((root + L"\\load_order.txt").c_str(), L"rb"); if (!f) return o;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string l = line; while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (l.size() >= 3 && (uint8_t)l[0] == 0xef && (uint8_t)l[1] == 0xbb && (uint8_t)l[2] == 0xbf) l.erase(0, 3);
        if (!l.empty() && l[0] != '#') o.push_back(l);
    }
    fclose(f); return o;
}
static void write_order_file(const std::wstring &root, const std::vector<std::string> &folders) {
    FILE *f = _wfopen((root + L"\\load_order.txt").c_str(), L"wb"); if (!f) return;
    fputs("# Mod priority, lowest first: a mod listed later wins when two mods change the same file/texture. Edit in the launcher (Mods page).\r\n", f);
    for (auto &s : folders) fprintf(f, "%s\r\n", s.c_str());
    fclose(f);
}
/* pastas de root em ordem de prioridade (baixa -> alta) */
static std::vector<std::string> ordered_folders(const std::wstring &root) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    std::vector<std::wstring> dirs;
    if (h != INVALID_HANDLE_VALUE) {
        do { std::wstring n = fd.cFileName; if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != L"." && n != L"..") dirs.push_back(n); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(dirs.begin(), dirs.end(), [](const std::wstring &a, const std::wstring &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<std::string> alpha, packs, out, listed = read_order_file(root);
    for (auto &d : dirs) { std::string n = to_utf8(d); (is_pack_folder(n) ? packs : alpha).push_back(n); }
    auto has = [](const std::vector<std::string> &v, const std::string &s) { for (auto &x : v) if (_stricmp(x.c_str(), s.c_str()) == 0) return true; return false; };
    for (auto &p : packs) if (!has(listed, p)) out.push_back(p);
    std::vector<std::string> all = alpha; all.insert(all.end(), packs.begin(), packs.end());
    for (auto &l : listed) for (auto &a : all) if (_stricmp(a.c_str(), l.c_str()) == 0 && !has(out, a)) out.push_back(a);   /* o que o arquivo cita, na ordem dele (pasta que sumiu e ignorada) */
    for (auto &a : alpha) if (!has(listed, a)) out.push_back(a);
    return out;
}
static Mod make_mod(const std::wstring &root, const std::string &folder) {
    Mod m; m.dir = root + L"\\" + to_wide(folder); m.name = m.folder = folder; m.enabled = !exists(m.dir + L"\\disabled");
    std::string author, version; read_meta(m, &author, &version);
    m.label = to_wide(m.name);
    m.detail = to_wide(version.empty() ? "" : "v" + version); if (!author.empty()) m.detail += (m.detail.empty() ? L"" : L" \x00B7 ") + to_wide(author);
    return m;
}
/* API do lancador (nao precisa de mods_init): lista, liga/desliga e reordena direto na pasta */
std::vector<ModEntry> mods_list_dir(const std::wstring &root) {
    std::vector<ModEntry> v;
    for (auto &f : ordered_folders(root)) { Mod m = make_mod(root, f); v.push_back({f, m.label, m.detail, m.enabled, !is_launcher_hidden(f)}); }
    return v;
}
void mods_dir_set_enabled(const std::wstring &root, const std::string &folder, bool on) {
    Mod m = make_mod(root, folder); const std::wstring marker = m.dir + L"\\disabled";
    if (on) DeleteFileW(marker.c_str());
    else if (FILE *f = _wfopen(marker.c_str(), L"wb")) { fputs("Mod switched off in the Mods menu. Delete this file (or use the menu) to switch it on.\n", f); fclose(f); }
}
/* sobe (dir=+1: maior prioridade) ou desce (dir=-1) o mod, trocando com o vizinho visivel mais proximo; grava load_order.txt */
void mods_dir_move(const std::wstring &root, const std::string &folder, int dir) {
    std::vector<std::string> o = ordered_folders(root); int i = -1;
    for (int k = 0; k < (int)o.size(); k++) if (o[k] == folder) i = k;
    if (i < 0) return;
    int j = i + (dir > 0 ? 1 : -1);
    while (j >= 0 && j < (int)o.size() && is_launcher_hidden(o[j])) j += dir > 0 ? 1 : -1;   /* escondidos do lancador: pula */
    if (j < 0 || j >= (int)o.size()) return;
    std::swap(o[i], o[j]); write_order_file(root, o);
}

static void build_dir_patches(void);
void mods_init(void) {
    {   /* migracao: o mod embutido antigo desligava com [mods] caixa_booster=0 no .ini; hoje e o plugin mods/CaixaDeBoosters */
        const std::wstring ini = app_ini_path();
        if (GetPrivateProfileIntW(L"mods", L"caixa_booster", -1, ini.c_str()) == 0) {
            const std::wstring d = mods_root() + L"\\CaixaDeBoosters";
            if (exists(d)) { if (FILE *f = _wfopen((d + L"\\disabled").c_str(), L"wb")) fclose(f); }
            WritePrivateProfileStringW(L"mods", L"caixa_booster", nullptr, ini.c_str());
        }
    }
    for (auto &d : ordered_folders(mods_root())) {   /* ordem de prioridade: o ultimo vence */
        Mod m = make_mod(mods_root(), d);
        int idx = (int)g_mods.size(); g_mods.push_back(m);
        if (!m.enabled) { fprintf(stderr, "[mods] %s: desligado\n", m.name.c_str()); continue; }
        std::vector<std::pair<std::wstring, uint64_t>> files; walk(m.dir, L"", files);
        int n = 0;
        for (auto &fp : files) {
            std::wstring low = fp.first; for (auto &ch : low) ch = (wchar_t)towlower(ch);
            if (low.rfind(L"textures\\", 0) == 0 || low.rfind(L"movie\\", 0) == 0 || low.rfind(L"llm\\", 0) == 0 || low.rfind(L"decks\\", 0) == 0 || low.rfind(L"sprites\\", 0) == 0 || low == L"mod.txt" || low == L"disabled" || low.rfind(L"leia-me", 0) == 0 || low.rfind(L"readme", 0) == 0 || low.find(L'\\') == std::wstring::npos) continue;
            std::string iso = to_utf8(fp.first); for (auto &ch : iso) if (ch == '\\') ch = '/';
            std::string isolow = iso; for (auto &ch : isolow) ch = (char)tolower((unsigned char)ch);
            if (isolow.size() > 4 && (isolow.compare(isolow.size() - 4, 4, ".prx") == 0 || isolow.find("boot.bin") != std::string::npos || isolow.find("eboot.bin") != std::string::npos)) {
                fprintf(stderr, "[mods] %s: %s ignorado (modulo de codigo nao pode ser trocado)\n", m.name.c_str(), iso.c_str()); continue;
            }
            uint32_t lba, size; bool isdir;
            if (iso_find(iso, &lba, &size, &isdir) != 0 || isdir) { fprintf(stderr, "[mods] %s: %s nao existe na ISO (ignorado)\n", m.name.c_str(), iso.c_str()); continue; }
            auto it = g_by_lba.find(lba);
            if (it != g_by_lba.end()) fprintf(stderr, "[mods] %s substitui a troca de %s feita por %s\n", m.name.c_str(), iso.c_str(), g_mods[it->second.mod].name.c_str());
            g_by_lba[lba] = ModFile{m.dir + L"\\" + fp.first, fp.second, lba, size, idx, iso};
            if (fp.second != size) fprintf(stderr, "[mods] %s: %s tem %llu bytes (original %u)\n", m.name.c_str(), iso.c_str(), (unsigned long long)fp.second, size);
            n++;
        }
        g_mods[idx].files = n;
        fprintf(stderr, "[mods] %s: %d arquivo(s) trocado(s)\n", m.name.c_str(), n);
    }
    build_dir_patches();
}
/* Arquivos de mod com tamanho diferente do original: o jogo monta o proprio indice de arquivos (nome -> setor e tamanho,
 * usado para abrir por "sce_lbn0x.._size0x..") lendo os registros de diretorio ISO9660 pelo dispositivo bruto umd1:
 * (BOOT 0882f34c). Para ele pedir o tamanho novo, o tamanho e reescrito nesses registros quando os setores de diretorio
 * sao lidos pelo dispositivo bruto (mods_patch_raw). */
struct DirPatch { uint32_t sector, off, size; };
static std::vector<DirPatch> g_dirpatch;
static void build_dir_patches(void) {
    for (auto &kv : g_by_lba) {
        const ModFile &m = kv.second;
        if (m.size == m.iso_size || m.size > 0xffffffffu) continue;
        size_t slash = m.iso_path.rfind('/'); if (slash == std::string::npos) continue;
        uint32_t dlba, dsize; bool isdir;
        if (iso_find(m.iso_path.substr(0, slash), &dlba, &dsize, &isdir) != 0 || !isdir) continue;
        std::vector<uint8_t> d(dsize); iso_read(dlba, 0, d.data(), dsize);
        for (size_t i = 0; i + 33 < d.size();) {
            uint8_t len = d[i];
            if (!len) { i = (i / 2048 + 1) * 2048; continue; }   /* registros nao cruzam setores */
            if (*(const uint32_t *)&d[i + 2] == m.lba) { g_dirpatch.push_back(DirPatch{dlba + (uint32_t)(i / 2048), (uint32_t)(i % 2048), (uint32_t)m.size}); break; }
            i += len;
        }
    }
}
void mods_patch_raw(uint32_t lba, uint8_t *buf, uint32_t nsectors) {
    for (const auto &p : g_dirpatch) {
        if (p.sector < lba || p.sector >= lba + nsectors) continue;
        uint8_t *r = buf + (size_t)(p.sector - lba) * 2048 + p.off;
        uint32_t s = p.size;
        memcpy(r + 10, &s, 4);                                                    /* tamanho LE */
        r[14] = (uint8_t)(s >> 24); r[15] = (uint8_t)(s >> 16); r[16] = (uint8_t)(s >> 8); r[17] = (uint8_t)s;   /* e BE */
    }
}
static const ModFile *lookup(uint32_t lba, uint64_t *off) {   /* arquivo trocado que contem o setor lba */
    if (g_by_lba.empty()) return nullptr;
    auto it = g_by_lba.upper_bound(lba); if (it == g_by_lba.begin()) return nullptr;
    --it; const ModFile &m = it->second;
    uint64_t sectors = ((uint64_t)m.iso_size + 2047) / 2048; if (!sectors) sectors = 1;
    if (lba >= m.lba + sectors) return nullptr;
    *off = (uint64_t)(lba - m.lba) * 2048; return &m;
}
bool mods_open(uint32_t lba, FILE **fp, uint64_t *base, uint64_t *size) {
    uint64_t off; const ModFile *m = lookup(lba, &off); if (!m) return false;
    FILE *f = _wfopen(m->host.c_str(), L"rb");
    if (!f) { fprintf(stderr, "[mods] nao consegui abrir %s do mod\n", m->iso_path.c_str()); return false; }
    *fp = f; *base = off; *size = m->size > off ? m->size - off : 0;
    static std::map<uint32_t, bool> logged;
    if (!logged[lba]) { logged[lba] = true; fprintf(stderr, "[mods] lendo %s do mod %s%s\n", m->iso_path.c_str(), g_mods[m->mod].name.c_str(), off ? " (trecho)" : ""); }
    return true;
}
bool mods_size(uint32_t lba, uint64_t *size) {
    uint64_t off; const ModFile *m = lookup(lba, &off); if (!m || off) return false;
    *size = m->size; return true;
}

int mods_count(void) { return (int)g_mods.size(); }
int mods_find(const char *folder) { for (int i = 0; i < (int)g_mods.size(); i++) if (g_mods[i].folder == folder) return i; return -1; }   /* by folder name (mods/<folder>); -1 if absent */
bool mods_menu_visible(int i) {
    if (i < 0 || i >= (int)g_mods.size()) return false;
    const std::string &folder = g_mods[i].folder;
    return folder != "EnhancedTextures" && folder != "TexturasPC";
}
void mods_plugin_dirs(std::vector<std::pair<std::string, std::wstring>> &out) { for (auto &m : g_mods) if (m.enabled) out.push_back({m.name, m.dir}); }   /* mods ligados, na ordem de carga (plugins.cpp) */
const wchar_t *mods_label(int i) { return i >= 0 && i < (int)g_mods.size() ? g_mods[i].label.c_str() : L""; }
const wchar_t *mods_detail(int i) { return i >= 0 && i < (int)g_mods.size() ? g_mods[i].detail.c_str() : L""; }
bool mods_enabled(int i) { return i >= 0 && i < (int)g_mods.size() && g_mods[i].enabled; }
void mods_set_enabled(int i, bool on) {
    if (i < 0 || i >= (int)g_mods.size()) return;
    std::wstring marker = g_mods[i].dir + L"\\disabled";
    if (on) DeleteFileW(marker.c_str());
    else if (FILE *f = _wfopen(marker.c_str(), L"wb")) { fputs("Mod switched off in the Mods menu. Delete this file (or use the menu) to switch it on.\n", f); fclose(f); }
    g_mods[i].enabled = on;
}
void mods_open_folder(void) {
    std::wstring root = mods_root();
    if (!exists(root)) {
        CreateDirectoryW(root.c_str(), nullptr);
        if (FILE *f = _wfopen((root + L"\\README.txt").c_str(), L"wb")) {
            fputs("Tag Force Recomp mods\r\n\r\n"
                  "Each mod is a folder in here. Its files go at the same path they have on the ISO, for example:\r\n"
                  "    mods\\MyMod\\PSP_GAME\\USRDIR\\card\\cardm_e.cip\r\n\r\n"
                  "Optional: mods\\MyMod\\mod.txt with the lines name=, author=, version=, description=\r\n"
                  "On/off: Mods menu (applies after restarting the game).\r\n"
                  "Textures: mods\\MyMod\\textures\\ (PPSSPP format). Videos: mods\\MyMod\\movie\\<name>.h264\r\n"
                  "Behavior (plugins): mods\\MyMod\\plugin.dll with the API in mods_sdk/tfmod.h (example: mods_src/CaixaDeBoosters).\r\n"
                  "The game's code modules (BOOT.BIN, *.prx) cannot be replaced. Full guide: docs/MODS.md.\r\n", f);
            fclose(f);
        }
    }
    wchar_t full[MAX_PATH]; GetFullPathNameW(root.c_str(), MAX_PATH, full, nullptr);
    ShellExecuteW(nullptr, L"open", full, nullptr, nullptr, SW_SHOWNORMAL);
}

/* pastas de texturas substitutas dos mods ligados: mods/<Mod>/textures e/ou a pasta externa de "textures=" no mod.txt (texrepl.cpp) */
/* videos substitutos: <mod>\movie\<nome>.h264 (fluxo H.264 Annex B com delimitadores de AU, sem B-frames, mesmo numero de quadros do .pmf original) */
void mods_movie_hd_files(std::vector<std::wstring> &out) {
    for (auto &m : g_mods) {
        if (!m.enabled) continue;
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((m.dir + L"\\movie\\*.h264").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do out.push_back(m.dir + L"\\movie\\" + fd.cFileName); while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
bool mods_enabled_has_file(const char *relative) {
    const std::wstring rel = to_wide(relative);
    for (auto &m : g_mods) if (m.enabled && exists(m.dir + L"\\" + rel)) return true;
    return false;
}
void mods_texture_dirs(std::vector<std::wstring> &out) {
    for (auto &m : g_mods) {
        if (!m.enabled) continue;
        if (exists(m.dir + L"\\textures")) out.push_back(m.dir + L"\\textures");
        if (!m.texext.empty() && exists(m.texext)) out.push_back(m.texext);
    }
}
