"""Copia para gen/ as DLLs do FFmpeg (MSYS2 UCRT64) que o run.exe carrega em tempo de execucao para o ATRAC
(avcodec + avutil) e todas as suas dependencias que estao em C:/msys64/ucrt64/bin (as do Windows ficam de fora).
Uso: python tools/ffmpeg_dlls.py  (chamado pelo build_run.sh; so copia o que falta ou mudou)."""
import os, re, shutil, subprocess, sys

BIN = r'C:/msys64/ucrt64/bin'
OBJDUMP = os.path.join(BIN, 'objdump.exe')
import glob


def _roots():   # a versao muda com o FFmpeg do MSYS2 (avcodec-NN.dll): usa as que estiverem instaladas
    return [os.path.basename(sorted(glob.glob(os.path.join(BIN, n + '-*.dll')))[-1]) for n in ('avcodec', 'avutil') if glob.glob(os.path.join(BIN, n + '-*.dll'))]


ROOTS = _roots()


def deps(dll):
    out = subprocess.run([OBJDUMP, '-p', os.path.join(BIN, dll)], capture_output=True, text=True).stdout
    return re.findall(r'DLL Name: (\S+)', out)


def main():
    dst = sys.argv[1] if len(sys.argv) > 1 else 'gen'
    if len(ROOTS) < 2:
        print('ffmpeg_dlls: FFmpeg do MSYS2 nao instalado (pacman -S mingw-w64-ucrt-x86_64-ffmpeg); ATRAC fica mudo')
        return
    seen, todo = set(), list(ROOTS)
    while todo:
        d = todo.pop()
        if d.lower() in seen or not os.path.exists(os.path.join(BIN, d)):
            continue
        seen.add(d.lower())
        todo += deps(d)
    n = 0
    for d in sorted(seen):
        s, t = os.path.join(BIN, d), os.path.join(dst, d)
        if not os.path.exists(t) or os.path.getmtime(t) < os.path.getmtime(s):
            shutil.copy2(s, t); n += 1
    print('ffmpeg_dlls: %d DLLs necessarias, %d copiadas para %s' % (len(seen), n, dst))


main()
