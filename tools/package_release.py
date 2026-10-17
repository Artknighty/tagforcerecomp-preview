"""Builds the release package: a zip the player extracts and runs, with nothing to install.

    python tools/package_release.py [--out gen/release] [--no-build] [--version 0.1-preview]

The package contains NO game data and no game code. It holds:
  TagForceRecomp.exe   the generic runtime (build with runtime/generic_index.cpp: no game modules linked in)
  avcodec/avutil.dll   slim LGPL-only FFmpeg with just the ATRAC3/3+ and H.264 decoders (tools/build_ffmpeg_lite.sh)
  mods/                the behavior mods of mods_src/ (plugin.dll), switched on or off in the Mods menu
  tcc/ include/        TinyCC (LGPL) and the one runtime header the generated code includes
  tools/               the Allegrex -> C translator (Python scripts) and the embedded Python that runs it
  gen/nids_hle.tsv     names of the system calls the runtime implements
On first run the program installs the game files from the player's ISO and translates the game's code on the player's machine
(runtime/prepare.cpp); the C code is then compiled in memory by TinyCC (runtime/jit.cpp).
Needs (on the build machine only): MSYS2 UCRT64 (g++, windres, glslc, FFmpeg), Python 3. Downloads TinyCC and the embeddable Python
once into <out>/cache."""
import os, re, shutil, subprocess, sys, urllib.request, zipfile, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TCC_URL = 'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip'
PY_URL = 'https://www.python.org/ftp/python/3.11.0/python-3.11.0-embed-amd64.zip'
MSYS = r'C:\msys64\ucrt64\bin'
# translator: everything build_modules.py -> recomp.py imports, plus the module tables
TOOLS = ['build_modules.py', 'recomp.py', 'allegrex.py', 'pspmod.py', 'elfscan.py', 'imports.py', 'merge.tsv', 'modules.tsv', 'modules_ULUS10136.tsv']
PY_KEEP = ['python.exe', 'python311.dll', 'python3.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'LICENSE.txt', 'python311.zip']
SKIP_MODS = {'MonstrosNoCampo'}   # prototype that needs art generated from the game; not part of the first release
RUNTIME = ['ctl', 'main_run', 'gameid', 'host', 'kernel', 'iso', 'isofolder', 'install', 'hle_misc', 'hle_io', 'hle_ge', 'hle_audio', 'modules', 'vfpu',
           'hle_utility', 'display', 'hle_mpeg', 'hle_font', 'launcher', 'text_input', 'diag', 'audio_out', 'raster_pool', 'ffmpeg_dec', 'menu', 'mouse_ui',
           'gfx', 'imgio', 'texrepl', 'bc7', 'plugins', 'modbox', 'gpu', 'interp', 'pacing', 'hdfont', 'jit', 'gpu_d3d11', 'gpu_vulkan', 'pc_text', 'mods', 'util',
           'prepare', 'dark_ui', 'slider_dialog', 'generic_index']

DECOMP_DIRS = ['decomp/libduelrecord', 'decomp/libtitle', 'decomp/libcharalist', 'decomp/libstory', 'decomp/libfield', 'decomp/libduel_eng',
               'decomp/libduel_mgr', 'decomp/libduel_draw', 'decomp/boot']


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
    env = dict(os.environ, PATH=MSYS + os.pathsep + os.environ['PATH'])
    if os.path.isdir(pkg): shutil.rmtree(pkg)
    os.makedirs(pkg)
    os.chdir(ROOT)

    # 1. the generic runtime
    exe = os.path.join(pkg, 'TagForceRecomp.exe')
    if '--no-build' not in args:
        run([sys.executable, 'tools/spv2h.py'], env=env)   # Vulkan shaders -> gen/gpu_spv.h
        rc = os.path.join(out, 'app.rc')
        open(rc, 'w').write('1 24 "%s"\n' % os.path.join(ROOT, 'runtime', 'app.manifest').replace('\\', '/'))
        res = os.path.join(out, 'app_res.o'); run(['windres', rc, '-O', 'coff', '-o', res], env=env)
        srcs = ['runtime/%s.cpp' % n for n in RUNTIME] + ['decomp/decomp.cpp'] + sorted(f.replace(os.sep, '/') for d in DECOMP_DIRS for f in glob.glob(d + '/*.cpp'))
        run(['g++', '-std=c++17', '-O2', '-DPSP_TRACE', '-DTF_GAME_ID="ULES-00600"', '-Wall', '-Wno-misleading-indentation', '-Iruntime', '-o', exe] + srcs + [res,
             '-static', '-mwindows', '-ldwmapi', '-lgdi32', '-luser32', '-lcomdlg32', '-lshell32', '-lwinmm', '-lsynchronization', '-ld3d11', '-ldxgi', '-ld3dcompiler',
             '-lole32', '-loleaut32', '-luuid', '-lwindowscodecs', '-lgdiplus'], env=env)
    elif not os.path.exists(exe):
        sys.exit('--no-build needs an exe; build once without it')
    if os.path.isfile('yugioh.png'):
        shutil.copy('yugioh.png', pkg)

    # 2. slim LGPL-only FFmpeg (tools/build_ffmpeg_lite.sh builds it once into gen/ffmpeg_lite)
    if not glob.glob('gen/ffmpeg_lite/avcodec-*.dll'):
        run([os.path.join(os.path.dirname(MSYS), 'usr', 'bin', 'bash.exe'), '-lc', 'sh tools/build_ffmpeg_lite.sh'], env=dict(env, MSYSTEM='UCRT64', CHERE_INVOKING='1'))
    for f in glob.glob('gen/ffmpeg_lite/*'): shutil.copy(f, pkg)

    # 3. behavior mods
    mods = [d for d in sorted(os.listdir('mods_src')) if os.path.isdir(os.path.join('mods_src', d)) and d not in SKIP_MODS]
    run(['sh', 'tools/build_plugins.sh'] + mods, env=dict(env, MODS_OUT=os.path.join(pkg, 'mods')))

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
    shutil.copy('runtime/psp_rt.h', os.path.join(pkg, 'include', 'psp_rt.h'))

    # 5. translator + embedded Python
    td = os.path.join(pkg, 'tools'); os.makedirs(td)
    for f in TOOLS: shutil.copy(os.path.join('tools', f), td)
    open(os.path.join(td, 'translator_version.txt'), 'w').write(version + '\n')
    pz = fetch(PY_URL, os.path.join(cache, 'python-embed.zip'))
    pd = os.path.join(td, 'python'); os.makedirs(pd)
    with zipfile.ZipFile(pz) as z:
        for n in PY_KEEP: z.extract(n, pd)
    open(os.path.join(pd, 'python311._pth'), 'w').write('python311.zip\n..\n.\n')   # '..' = tools/, where the translator scripts are
    run([sys.executable, 'tools/gerar_nids.py'])
    os.makedirs(os.path.join(pkg, 'gen')); shutil.copy('gen/nids_hle.tsv', os.path.join(pkg, 'gen', 'nids_hle.tsv'))

    # 6. documents
    for f in ('LICENSE', 'THIRD_PARTY.md', 'README.md'): shutil.copy(f, pkg)
    shutil.copytree('docs', os.path.join(pkg, 'docs'), ignore=shutil.ignore_patterns('adr', 'agents', 'historico'))
    open(os.path.join(pkg, 'START HERE.txt'), 'w').write(
        'Tag Force Recomp - %s\r\n\r\n'
        '1. Run TagForceRecomp.exe.\r\n'
        '2. Click "Select ISO..." and pick YOUR OWN copy of Yu-Gi-Oh! GX Tag Force (European release, ULES-00600).\r\n'
        '3. Click "Install and play". The first run copies the game files and prepares the game (a few minutes, only once).\r\n'
        '   Next time just click "Play"; the ISO is not needed any more.\r\n\r\n'
        'Nothing from the game is included in this download; everything is made on your computer from your own copy.\r\n'
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


if __name__ == '__main__':
    main()
