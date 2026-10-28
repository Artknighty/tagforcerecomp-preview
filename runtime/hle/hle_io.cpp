/* HLE: IoFileMgrForUser. disc0:/umd0: -> ISO do usuario (somente leitura); ms0: -> pasta ./ms0 no host. */
#include "hle.h"
#include "mods.h"
#include <stdio.h>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <direct.h>
#include <windows.h>

struct File {
    enum Kind { ISO, HOST, DIR, RAW, MOD } kind = ISO;   /* MOD: arquivo do disco trocado por um mod (mods.cpp) */
    uint32_t lba = 0, size = 0; int64_t pos = 0; uint64_t base = 0;
    FILE *fp = nullptr; std::vector<DirEnt> ents; size_t di = 0;
    bool has_async = false; int64_t async_res = 0; uint32_t async_cb = 0;
    bool closed = false;   /* sceIoCloseAsync: o descritor so sai da tabela quando o resultado e colhido (Wait/PollAsync) */
    uint64_t serial = 0; std::string path; uint32_t flags = 0;   /* which open() made it, and how (a loaded state reopens the host files; savestate.cpp) */
};
static uint64_t g_serial;
static std::map<uint32_t, File> g_files;
static uint32_t g_next_fd = 3;
static std::string g_cwd = "disc0:/";

#define ERR_NOENT 0x80010002u
#define ERR_BADF 0x80010009u

static void lower(std::string &s) { for (auto &ch : s) ch = (char)tolower((unsigned char)ch); }

/* separa "dev:/caminho" -> (dev, caminho); resolve relativo pelo cwd */
static void split_path(std::string p, std::string &dev, std::string &rest) {
    for (auto &ch : p) if (ch == 0x5c) ch = '/';
    size_t colon = p.find(':');
    if (colon == std::string::npos) { p = g_cwd + (g_cwd.back() == '/' ? "" : "/") + p; colon = p.find(':'); }
    dev = p.substr(0, colon); lower(dev);
    rest = p.substr(colon + 1);
    while (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
}
static std::string host_path(const std::string &rest) { return "ms0/" + rest; }
static bool is_disc(const std::string &dev) { return dev == "disc0" || dev == "umd0" || dev == "disc" || dev == "umd" || dev == "umd1"; }

/* lista de diretorio do ISO */

static void fill_stat(uint32_t p, bool dir, uint32_t size) {
    memset(mem_ptr(p), 0, 0x58);
    wr32(p, dir ? 0x1000 | 0x1ff : 0x2000 | 0x1ff); wr32(p + 4, dir ? 0x10 : 0x20); wr64(p + 8, size);
}

static bool io_trace() { static const bool t = getenv("IO_TRACE") != nullptr; return t; }   /* IO_TRACE=1: opens/reads */
static int64_t do_open_(const std::string &path, uint32_t flags) {
    std::string dev, rest; split_path(path, dev, rest);
    File f;
    if ((is_disc(dev) || dev == "umd1") && rest.empty()) {   /* dispositivo bruto: posicao e tamanhos em setores de 2048 bytes */
        f.kind = File::RAW;
    } else if (is_disc(dev)) {
        uint32_t lba, size; bool isdir;
        if (!parse_lbn_path(rest, &lba, &size) && iso_find(rest, &lba, &size, &isdir) != 0) {
            /* a Field Spell added by a mod has no background file of its own (duel/bg/fmagic<id>.ehp): the game would retry forever, so it gets the default one */
            static const char kBg[] = "PSP_GAME/USRDIR/duel/bg/fmagic";
            const size_t n = sizeof(kBg) - 1;
            if (rest.size() != n + 8 || rest.compare(0, n, kBg) != 0 || iso_find(std::string(kBg) + "0000.ehp", &lba, &size, &isdir) != 0) return ERR_NOENT;
        }
        if (flags & 0x0002) return 0x80010001u;   /* escrita em ISO */
        f.kind = File::ISO; f.lba = lba; f.size = size;
        { extern void mpeg_movie_opened(uint32_t); mpeg_movie_opened(lba); }   /* video substituto de um mod (hle_mpeg.cpp) */
        uint64_t msize;
        if (mods_open(lba, &f.fp, &f.base, &msize)) { f.kind = File::MOD; f.size = (uint32_t)std::min<uint64_t>(msize, 0xffffffffu); }
    } else if (dev == "ms0" || dev == "fatms" || dev == "host0") {
        const char *mode = (flags & 0x0002) ? ((flags & 0x0400) ? "w+b" : "r+b") : "rb";
        std::string hp = host_path(rest);
        f.fp = fopen(hp.c_str(), mode);
        if (!f.fp && (flags & 0x0200)) { f.fp = fopen(hp.c_str(), "w+b"); }
        if (!f.fp) return ERR_NOENT;
        f.kind = File::HOST;
    } else return ERR_NOENT;
    f.serial = ++g_serial; f.flags = flags; if (f.kind == File::HOST) f.path = host_path(rest);
    uint32_t fd = g_next_fd++; g_files[fd] = f;
    if (io_trace()) fprintf(stderr, "[io] open %s -> fd %u (lba %u, %u bytes)\n", path.c_str(), fd, f.lba, f.size);
    return fd;
}
/* abertura que falhou: o jogo tenta de novo, as vezes para sempre (versao do jogo com arquivo diferente, mod errado...): os primeiros erros vao para o log */
static int64_t do_open(const std::string &path, uint32_t flags) {
    int64_t r = do_open_(path, flags); static int n;
    if ((r < 0 || (uint64_t)r >= 0x80000000ull) && n < 30) { n++; fprintf(stderr, "[io] ERRO ao abrir '%s' (flags %x) -> %08x\n", path.c_str(), flags, (uint32_t)r); }
    return r;
}
static int64_t do_read(uint32_t fd, uint32_t buf, uint32_t n) {
    auto it = g_files.find(fd); if (it == g_files.end()) return ERR_BADF;
    File &f = it->second;
    if (io_trace()) fprintf(stderr, "[io] read fd %u pos 0x%llx n 0x%x -> %08x\n", fd, (unsigned long long)f.pos, n, buf);
    if (f.kind == File::RAW) {
        int r = iso_read((uint32_t)f.pos, 0, mem_ptr(buf), n * 2048); if (r < 0) return -1;
        mods_patch_raw((uint32_t)f.pos, mem_ptr(buf), n);   /* indice de arquivos do jogo ve o tamanho dos mods */
        f.pos += n; return n;
    }
    if (f.kind == File::ISO) {
        if (f.pos >= f.size) return 0;
        uint32_t left = (uint32_t)(f.size - f.pos); if (n > left) n = left;
        int r = iso_read(f.lba, (uint32_t)f.pos, mem_ptr(buf), n); if (r < 0) return -1;
        pctext_apply(f.lba, (uint64_t)f.pos, mem_ptr(buf), (uint64_t)r);
        f.pos += r; return r;
    }
    if (f.kind == File::MOD) {
        if (f.pos >= f.size) return 0;
        uint32_t left = (uint32_t)(f.size - f.pos); if (n > left) n = left;
        _fseeki64(f.fp, (int64_t)(f.base + (uint64_t)f.pos), SEEK_SET);
        size_t r = fread(mem_ptr(buf), 1, n, f.fp); f.pos += (int64_t)r; return (int64_t)r;
    }
    if (f.kind == File::HOST) { size_t r = fread(mem_ptr(buf), 1, n, f.fp); return (int64_t)r; }
    return ERR_BADF;
}
static int64_t do_lseek(uint32_t fd, int64_t off, uint32_t whence) {
    auto it = g_files.find(fd); if (it == g_files.end()) return ERR_BADF;
    File &f = it->second;
    if (f.kind == File::RAW || f.kind == File::ISO || f.kind == File::MOD) {
        if (f.kind == File::RAW) { f.pos = whence == 0 ? off : f.pos + off; return f.pos; }
        int64_t np = whence == 0 ? off : whence == 1 ? f.pos + off : (int64_t)f.size + off;
        if (np < 0) return -1; f.pos = np; return np;
    }
    if (f.kind == File::HOST) { _fseeki64(f.fp, off, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END); return _ftelli64(f.fp); }
    return ERR_BADF;
}
static int64_t do_close(uint32_t fd) {
    auto it = g_files.find(fd); if (it == g_files.end()) return ERR_BADF;
    if (it->second.fp) fclose(it->second.fp);
    g_files.erase(it); return 0;
}
static int64_t do_getstat(const std::string &path, uint32_t p) {
    std::string dev, rest; split_path(path, dev, rest);
    if (is_disc(dev)) {
        uint32_t lba, size; bool isdir;
        if (iso_find(rest, &lba, &size, &isdir) != 0) return ERR_NOENT;
        uint64_t msize; if (!isdir && mods_size(lba, &msize)) size = (uint32_t)msize;   /* tamanho do arquivo do mod */
        fill_stat(p, isdir, size); return 0;
    }
    if (dev == "ms0" || dev == "fatms") {
        WIN32_FILE_ATTRIBUTE_DATA d; if (!GetFileAttributesExA(host_path(rest).c_str(), GetFileExInfoStandard, &d)) return ERR_NOENT;
        fill_stat(p, (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, d.nFileSizeLow); return 0;
    }
    return ERR_NOENT;
}
static int64_t do_dopen(const std::string &path) {
    std::string dev, rest; split_path(path, dev, rest);
    File f; f.kind = File::DIR;
    if (is_disc(dev)) {
        uint32_t lba, size; bool isdir;
        if (iso_find(rest, &lba, &size, &isdir) != 0 || !isdir) return ERR_NOENT;
        f.ents = iso_list_dir(lba, size);
    } else if (dev == "ms0" || dev == "fatms") {
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((host_path(rest) + "/*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return ERR_NOENT;
        do { f.ents.push_back({fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, fd.nFileSizeLow}); } while (FindNextFileA(h, &fd));
        FindClose(h);
    } else return ERR_NOENT;
    f.serial = ++g_serial;
    uint32_t id = g_next_fd++; g_files[id] = f; return id;
}

HLE(sceIoOpen) { if (io_trace()) fprintf(stderr, "[io] sceIoOpen(%s, %x)%c", mem_cstr(ARG(0)), ARG(1), 10); RET(do_open(mem_cstr(ARG(0)), ARG(1))); }
HLE(sceIoClose) { RET(do_close(ARG(0))); }
HLE(sceIoRead) { RET(do_read(ARG(0), ARG(1), ARG(2))); }
HLE(sceIoWrite) {
    uint32_t fd = ARG(0);
    if (fd == 1 || fd == 2) { fwrite(mem_ptr(ARG(1)), 1, ARG(2), stderr); RET(ARG(2)); return; }
    auto it = g_files.find(fd); if (it == g_files.end() || it->second.kind != File::HOST) { RET(ERR_BADF); return; }
    RET(fwrite(mem_ptr(ARG(1)), 1, ARG(2), it->second.fp));
}
HLE(sceIoLseek) { RET64(do_lseek(ARG(0), (int64_t)(((uint64_t)ARG(3) << 32) | ARG(2)), ARG(4))); }   /* offset 64-bit em a2:a3, whence em t0 */
HLE(sceIoLseek32) { RET(do_lseek(ARG(0), (int32_t)ARG(1), ARG(2))); }
HLE(sceIoGetstat) { RET(do_getstat(mem_cstr(ARG(0)), ARG(1))); }
HLE(sceIoDopen) { RET(do_dopen(mem_cstr(ARG(0)))); }
HLE(sceIoDclose) { RET(do_close(ARG(0))); }
HLE(sceIoDread) {
    auto it = g_files.find(ARG(0)); if (it == g_files.end()) { RET(ERR_BADF); return; }
    File &f = it->second; if (f.di >= f.ents.size()) { RET(0); return; }
    DirEnt &e = f.ents[f.di++]; uint32_t p = ARG(1);
    /* SceIoDirent = d_stat (0x58) + d_name[256] + d_private + dummy = 0x160 bytes: zera so d_stat e d_name (antes 0x168: 8 bytes alem da struct
     * do jogo, e o d_private, um ponteiro que e do jogo) */
    memset(mem_ptr(p), 0, 0x158); fill_stat(p, e.dir, e.size);
    strncpy((char *)mem_ptr(p + 0x58), e.name.c_str(), 255); RET(1);
}
HLE(sceIoMkdir) { std::string d, r; split_path(mem_cstr(ARG(0)), d, r); if (d == "ms0") { CreateDirectoryA(host_path(r).c_str(), nullptr); RET(0); } else RET(ERR_NOENT); }
HLE(sceIoRmdir) { std::string d, r; split_path(mem_cstr(ARG(0)), d, r); if (d == "ms0") { RemoveDirectoryA(host_path(r).c_str()); RET(0); } else RET(ERR_NOENT); }
HLE(sceIoRemove) { std::string d, r; split_path(mem_cstr(ARG(0)), d, r); if (d == "ms0") { DeleteFileA(host_path(r).c_str()); RET(0); } else RET(ERR_NOENT); }
HLE(sceIoRename) { std::string d, r, d2, r2; split_path(mem_cstr(ARG(0)), d, r); split_path(mem_cstr(ARG(1)), d2, r2);
    if (d == "ms0" && d2 == "ms0") { rename(host_path(r).c_str(), host_path(r2).c_str()); RET(0); } else RET(ERR_NOENT); }
HLE(sceIoChdir) { std::string p = mem_cstr(ARG(0)); for (auto &ch : p) if (ch == 0x5c) ch = '/'; g_cwd = p; RET(0); }
HLE(sceIoSync) { RET(0); }
HLE(sceIoDevctl) { RET(0x80010086u); }   /* nao suportado */
HLE(sceIoIoctl) { RET(0); }
HLE(sceIoGetDevType) { RET(4); }
HLE(sceIoGetFdList) { RET(0); }

/* ---- assincrono: executa na hora e guarda o resultado para WaitAsync/PollAsync ---- */
static void set_async(uint32_t fd, int64_t res) { File &f = g_files[fd]; f.has_async = true; f.async_res = res; }
HLE(sceIoOpenAsync) { int64_t fd = do_open(mem_cstr(ARG(0)), ARG(1)); if (fd < 0 || (uint64_t)fd >= 0x80000000ull) { RET(fd); return; } set_async((uint32_t)fd, fd); RET(fd); }   /* erros sao 0x8001xxxx positivos (sem entrada falsa na tabela) */
HLE(sceIoReadAsync) { int64_t r = do_read(ARG(0), ARG(1), ARG(2)); if (!g_files.count(ARG(0))) { RET(ERR_BADF); return; } set_async(ARG(0), r); RET(0); }
HLE(sceIoWriteAsync) {   /* nenhum modulo do jogo chama (conferido nos gen/ *.c em 2026-10-14); antes so fingia gravar */
    auto it = g_files.find(ARG(0)); if (it == g_files.end() || it->second.kind != File::HOST) { RET(ERR_BADF); return; }
    set_async(ARG(0), (int64_t)fwrite(mem_ptr(ARG(1)), 1, ARG(2), it->second.fp)); RET(0);
}
HLE(sceIoLseekAsync) { int64_t r = do_lseek(ARG(0), (int64_t)(((uint64_t)ARG(3) << 32) | ARG(2)), ARG(4)); if (!g_files.count(ARG(0))) { RET(ERR_BADF); return; } set_async(ARG(0), r); RET(0); }
HLE(sceIoLseek32Async) { int64_t r = do_lseek(ARG(0), (int32_t)ARG(1), ARG(2)); if (!g_files.count(ARG(0))) { RET(ERR_BADF); return; } set_async(ARG(0), r); RET(0); }
HLE(sceIoIoctlAsync) { if (!g_files.count(ARG(0))) { RET(ERR_BADF); return; } set_async(ARG(0), 0); RET(0); }
HLE(sceIoCloseAsync) {
    auto it = g_files.find(ARG(0)); if (it == g_files.end()) { RET(ERR_BADF); return; }
    if (it->second.fp) { fclose(it->second.fp); it->second.fp = nullptr; }
    it->second.has_async = true; it->second.async_res = 0; it->second.kind = File::DIR; it->second.ents.clear(); it->second.closed = true; RET(0);
}
static void wait_async(CPU *c) {   /* Wait e Poll sao iguais: a operacao ja terminou quando foi pedida */
    auto it = g_files.find(ARG(0)); if (it == g_files.end()) { RET(ERR_BADF); return; }
    if (!it->second.has_async) { RET(0x80020323u); return; }   /* SCE_KERNEL_ERROR_NOASYNC */
    wr64(ARG(1), (uint64_t)it->second.async_res); it->second.has_async = false;
    if (it->second.closed) g_files.erase(it);
    RET(0);
}
HLE(sceIoWaitAsync) { wait_async(c); }
HLE(sceIoWaitAsyncCB) { wait_async(c); }
HLE(sceIoPollAsync) { wait_async(c); }
HLE(sceIoSetAsyncCallback) { RET(0); }
HLE(sceIoChangeAsyncPriority) { RET(0); }
HLE(sceIoCancel) { RET(0); }

/* ---- state save / load (savestate.cpp): the file table with the position of each host file. A file closed since the save is reopened, one opened
 * since is closed. Data the game wrote to ms0 after the save stays on disk (only the game's memory goes back). */
#include <memory>
struct IoSnap { std::map<uint32_t, File> files; std::map<uint32_t, int64_t> hostpos; uint32_t next_fd; std::string cwd; };
std::shared_ptr<void> io_state_save(void) {
    auto s = std::make_shared<IoSnap>();
    s->files = g_files; s->next_fd = g_next_fd; s->cwd = g_cwd;
    for (auto &kv : g_files) if (kv.second.kind == File::HOST && kv.second.fp) s->hostpos[kv.first] = _ftelli64(kv.second.fp);
    return s;
}
void io_state_load(const std::shared_ptr<void> &p) {
    const IoSnap &s = *(const IoSnap *)p.get();
    auto kept = [&](const File &cur, uint32_t fd) { auto it = s.files.find(fd); return it != s.files.end() && it->second.serial == cur.serial && it->second.fp == cur.fp; };
    for (auto &kv : g_files) if (kv.second.fp && !kept(kv.second, kv.first)) { fclose(kv.second.fp); kv.second.fp = nullptr; }
    std::map<uint32_t, File> nf;
    for (auto &kv : s.files) {
        File f = kv.second;
        if (f.fp) {
            auto cur = g_files.find(kv.first);
            if (cur != g_files.end() && cur->second.serial == f.serial && cur->second.fp == f.fp) f.fp = cur->second.fp;   /* still open */
            else if (f.kind == File::HOST) f.fp = fopen(f.path.c_str(), (f.flags & 0x0002) ? "r+b" : "rb");
            else if (f.kind == File::MOD) { uint64_t msize; if (!mods_open(f.lba, &f.fp, &f.base, &msize)) f.fp = nullptr; }
            else f.fp = nullptr;
            if (f.fp && f.kind == File::HOST) { auto h = s.hostpos.find(kv.first); if (h != s.hostpos.end()) _fseeki64(f.fp, h->second, SEEK_SET); }
        }
        nf[kv.first] = f;
    }
    g_files = nf; g_next_fd = s.next_fd; g_cwd = s.cwd;
}
