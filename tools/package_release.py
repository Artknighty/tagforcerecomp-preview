"""Builds the release package: a zip the player extracts and runs, with nothing to install.

    python tools/package_release.py [--out gen/release] [--no-build] [--version 0.1-preview]

The package contains NO game data and no game code. It holds:
  TagForceRecomp.exe   the generic runtime (no game code inside)
  avcodec/avutil.dll   slim LGPL-only FFmpeg with just the ATRAC3/3+ and H.264 decoders (tools/build/build_ffmpeg_lite.sh)
  tcc/ include/        TinyCC (LGPL) and the one runtime header the generated code includes
  tools/               the Allegrex -> C translator (tools/translator/, Python scripts) and the embedded Python that runs it
  gen/nids_hle.tsv     names of the system calls the runtime implements
The main zip carries NO mods (it stays as close to the original game as possible). The behavior mods of plugins/src/ (plugin.dll) go in a
second zip, TagForceRecomp-Mods-<version>.zip, whose mods/ folder the player copies next to the exe; each ships switched off.
On first run the program installs the game files from the player's ISO and translates the game's code on the player's machine
(runtime/core/prepare.cpp); the C code is then compiled in memory by TinyCC (runtime/core/jit.cpp).
Needs (on the build machine only): MSYS2 UCRT64 (g++, windres, glslc, FFmpeg), Python 3. Downloads TinyCC and the embeddable Python
once into <out>/cache."""
import os, re, shutil, subprocess, sys, urllib.request, zipfile, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TCC_URL = 'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip'
PY_URL = 'https://www.python.org/ftp/python/3.11.0/python-3.11.0-embed-amd64.zip'
MSYS = r'C:\msys64\ucrt64\bin'
MSYS_USR = r'C:\msys64\usr\bin'
# translator: everything build_modules.py -> recomp.py imports, plus the module tables
TOOLS = ['build_modules.py', 'recomp.py', 'allegrex.py', 'pspmod.py', 'elfscan.py', 'imports.py', 'merge.tsv', 'modules.tsv', 'modules_ULUS10136.tsv']
PY_KEEP = ['python.exe', 'python311.dll', 'python3.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'LICENSE.txt', 'python311.zip']
SKIP_MODS = {'MonstersOnField'}   # prototype that needs art generated from the game; not part of the first release


def run(cmd, **kw):
    print('>', ' '.join(cmd) if isinstance(cmd, list) else cmd, flush=True)
    if isinstance(cmd, list) and kw.get('env'):   # CreateProcess looks programs up in OUR PATH, not in env's
        exe = shutil.which(cmd[0], path=kw['env']['PATH'])
        if exe: cmd = [exe] + cmd[1:]
    r = subprocess.run(cmd, **kw)
    if r.returncode: sys.exit('failed: %s' % cmd)


def fetch(url, dest):
    if not os.path.exists(dest):
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        print('downloading', url, flush=True)
        urllib.request.urlretrieve(url, dest)
    return dest


def main():
    args = sys.argv[1:]
    out = os.path.abspath(args[args.index('--out') + 1]) if '--out' in args else os.path.join(ROOT, 'gen', 'release')
    version = args[args.index('--version') + 1] if '--version' in args else 'preview'
    pkg = os.path.join(out, 'TagForceRecomp')
    cache = os.path.join(out, 'cache')
    env = dict(os.environ, PATH=MSYS + os.pathsep + MSYS_USR + os.pathsep + os.environ['PATH'])
    if os.path.isdir(pkg) and '--no-build' not in args: shutil.rmtree(pkg)
    os.makedirs(pkg, exist_ok=True)
    os.chdir(ROOT)

    # 1. the generic runtime
    exe = os.path.join(pkg, 'TagForceRecomp.exe')
    if '--no-build' not in args:
        run([sys.executable, 'tools/build/spv2h.py'], env=env)   # Vulkan shaders -> gen/gpu_spv.h
        rc = os.path.join(out, 'app.rc')
        manifest = os.path.join(ROOT, 'runtime', 'app.manifest').replace('\\', '/')
        icon = os.path.join(ROOT, 'runtime', 'app.ico').replace('\\', '/')
        open(rc, 'w').write('1 24 "%s"\n1 ICON "%s"\n' % (manifest, icon))
        res = os.path.join(out, 'app_res.o'); run(['windres', rc, '-O', 'coff', '-o', res], env=env)
        srcs = [f.replace(os.sep, '/') for f in sorted(glob.glob('runtime/*/*.cpp')) + ['decomp/decomp.cpp'] + sorted(glob.glob('decomp/*/*.cpp'))]   # same set as tools/build.sh
        run(['g++', '-std=c++17', '-O2', '-DPSP_TRACE', '-DTF_GAME_ID="ULES-00600"', '-Wall', '-Wno-misleading-indentation', '-Iruntime', '-Iruntime/core', '-Iruntime/hle', '-Iruntime/gpu', '-Iruntime/ui', '-Iruntime/audio', '-Iruntime/mods', '-o', exe] + srcs + [res,
             '-static', '-mwindows', '-ldwmapi', '-lgdi32', '-luser32', '-lcomdlg32', '-lshell32', '-lwinmm', '-lsynchronization', '-ld3d11', '-ldxgi', '-ld3dcompiler',
             '-lole32', '-loleaut32', '-luuid', '-lwindowscodecs', '-lgdiplus'], env=env)
    elif not os.path.exists(exe):
        sys.exit('--no-build needs an exe; build once without it')
    if os.path.isfile('assets/branding/yugioh.png'):
        shutil.copy('assets/branding/yugioh.png', pkg)

    # 2. slim LGPL-only FFmpeg (tools/build/build_ffmpeg_lite.sh builds it once into gen/ffmpeg_lite)
    if not glob.glob('gen/ffmpeg_lite/avcodec-*.dll'):
        run([os.path.join(MSYS_USR, 'bash.exe'), '-lc', 'sh tools/build/build_ffmpeg_lite.sh'], env=dict(env, MSYSTEM='UCRT64', CHERE_INVOKING='1'))
    for f in glob.glob('gen/ffmpeg_lite/*'): shutil.copy(f, pkg)

    # 3. behavior mods
    mods = [d for d in sorted(os.listdir('plugins/src')) if os.path.isdir(os.path.join('plugins/src', d)) and d not in SKIP_MODS]
    mods_pkg = os.path.join(out, 'TagForceRecomp-Mods')   # the mods are NOT part of the main package: they get their own zip
    if os.path.isdir(mods_pkg): shutil.rmtree(mods_pkg)
    mods_out = os.path.join(mods_pkg, 'mods')
    os.makedirs(mods_out, exist_ok=True)
    for mod in mods:
        src = os.path.join('plugins/src', mod)
        dst = os.path.join(mods_out, mod)
        os.makedirs(dst, exist_ok=True)
        for name in os.listdir(src):
            source = os.path.join(src, name)
            if name.lower().endswith(('.cpp', '.c', '.h')):
                continue
            target = os.path.join(dst, name)
            if os.path.isdir(source): shutil.copytree(source, target, dirs_exist_ok=True)
            else: shutil.copy2(source, target)
        srcs = sorted(glob.glob(os.path.join(src, '*.cpp')) + glob.glob(os.path.join(src, '*.c')))
        if srcs:
            run(['g++', '-std=c++17', '-O2', '-Wall', '-Wno-misleading-indentation', '-shared', '-static', '-s',
                 '-Iplugins/sdk', '-o', os.path.join(dst, 'plugin.dll')] + srcs + ['-lwinhttp', '-lws2_32'], env=env)
        with open(os.path.join(dst, 'disabled'), 'w') as f:   # same marker the Mods page writes: the player opts in
            f.write('Mod switched off in the Mods menu. Delete this file (or use the menu) to switch it on.\n')
    open(os.path.join(mods_pkg, 'INSTALL.txt'), 'w').write(
        'Tag Force Recomp - behavior mods - %s\r\n\r\n'
        '1. Copy the "mods" folder of this zip into your TagForceRecomp folder (next to TagForceRecomp.exe); merge if it already exists.\r\n'
        '2. Run TagForceRecomp.exe and open the Mods page: every mod here starts switched OFF; turn on the ones you want (applies on restart).\r\n\r\n'
        'Each mod folder has a README.md. Do not combine mods that hook the same game function (Card Randomizer, Player/Opponent Deck\r\n'
        'Randomizer and Better Decks all replace the decks): see docs/MODS.md. The SDK to make your own mods is in the source repository.\r\n' % version)

    # 4. TinyCC
    tz = fetch(TCC_URL, os.path.join(cache, 'tcc.zip'))
    with zipfile.ZipFile(tz) as z:
        for n in z.namelist():
            if n.startswith('tcc/') and not n.startswith(('tcc/doc/', 'tcc/examples/')) and not n.endswith(('tcc.exe', 'i386-win32-tcc.exe')) and not n.endswith('/'):
                dst = os.path.join(pkg, n); os.makedirs(os.path.dirname(dst), exist_ok=True)
                with z.open(n) as s, open(dst, 'wb') as d: shutil.copyfileobj(s, d)
    for n in ('lib/libtcc1-32.a',):
        p = os.path.join(pkg, 'tcc', n)
        if os.path.exists(p): os.remove(p)
    os.makedirs(os.path.join(pkg, 'include'), exist_ok=True)
    shutil.copy('runtime/core/psp_rt.h', os.path.join(pkg, 'include', 'psp_rt.h'))

    # 5. translator + embedded Python
    td = os.path.join(pkg, 'tools'); os.makedirs(td)
    tr = os.path.join(td, 'translator'); os.makedirs(tr)
    for f in TOOLS: shutil.copy(os.path.join('tools', 'translator', f), tr)
    open(os.path.join(td, 'translator_version.txt'), 'w').write(version + '\n')
    pz = fetch(PY_URL, os.path.join(cache, 'python-embed.zip'))
    pd = os.path.join(td, 'python'); os.makedirs(pd)
    with zipfile.ZipFile(pz) as z:
        for n in PY_KEEP: z.extract(n, pd)
    open(os.path.join(pd, 'python311._pth'), 'w').write('python311.zip\n../translator\n.\n')   # ../translator = tools/translator/, where the translator scripts are
    run([sys.executable, 'tools/translator/gerar_nids.py'])
    os.makedirs(os.path.join(pkg, 'gen')); shutil.copy('gen/nids_hle.tsv', os.path.join(pkg, 'gen', 'nids_hle.tsv'))

    # 6. documents
    for f in ('LICENSE', 'THIRD_PARTY.md', 'README.md'): shutil.copy(f, pkg)
    shutil.copytree('docs', os.path.join(pkg, 'docs'), ignore=shutil.ignore_patterns('adr', 'agents', 'internal'))
    open(os.path.join(pkg, 'START HERE.txt'), 'w').write(
        'Tag Force Recomp - %s\r\n\r\n'
        '1. Run TagForceRecomp.exe.\r\n'
        '2. Click "Select ISO..." and pick YOUR OWN copy of Yu-Gi-Oh! GX Tag Force (European release, ULES-00600).\r\n'
        '3. Click "Install and play". The first run copies the game files and prepares the game (a few minutes, only once).\r\n'
        '   Next time just click "Play"; the ISO is not needed any more.\r\n\r\n'
        'Nothing from the game is included in this download; everything is made on your computer from your own copy.\r\n'
        'Mods are a separate download (TagForceRecomp-Mods zip of the same release): copy its mods folder next to the exe.\r\n'
        'This is an early preview: see docs/PREVIEW.md. Do not move TagForceRecomp.exe out of this folder.\r\n' % version)

    # 7. checks: nothing that looks like game data
    bad = [p for p in glob.glob(os.path.join(pkg, '**', '*'), recursive=True) if os.path.isfile(p) and p.lower().endswith(('.iso', '.prx', '.bin', '.pbp', '.c', '.bmp', '.at3', '.pmf'))]
    if bad: sys.exit('suspicious files in the package: %s' % bad)

    # 8. zip
    zp = os.path.join(out, 'TagForceRecomp-%s-win64.zip' % version)
    if os.path.exists(zp): os.remove(zp)
    with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dp, dn, fn in os.walk(pkg):
            for f in fn:
                p = os.path.join(dp, f); z.write(p, os.path.join('TagForceRecomp', os.path.relpath(p, pkg)))
    print('package: %s (%.1f MB)' % (zp, os.path.getsize(zp) / 1048576))
    mz = os.path.join(out, 'TagForceRecomp-Mods-%s.zip' % version)
    if os.path.exists(mz): os.remove(mz)
    with zipfile.ZipFile(mz, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dp, dn, fn in os.walk(mods_pkg):
            for f in fn:
                p = os.path.join(dp, f); z.write(p, os.path.relpath(p, mods_pkg))
    print('mods package: %s (%.1f MB)' % (mz, os.path.getsize(mz) / 1048576))


if __name__ == '__main__':
    main()
