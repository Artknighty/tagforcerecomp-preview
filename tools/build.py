"""Python implementation of build.sh for clean native execution on Windows."""
import os, sys, glob, re, shutil, subprocess
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

UCRT64 = r"C:\msys64\ucrt64\bin"
ENV = dict(os.environ)
ENV["PATH"] = UCRT64 + os.pathsep + ENV.get("PATH", "")

GXX = os.path.join(UCRT64, "g++.exe")
WINDRES = os.path.join(UCRT64, "windres.exe")

PKG = os.path.join(ROOT, "gen", "pkg", "TagForceRecomp")
OBJDIR = os.path.join(ROOT, "gen", "pkg", "obj")
os.makedirs(PKG, exist_ok=True)
os.makedirs(OBJDIR, exist_ok=True)

# 1. Vulkan shaders (spv2h.py)
subprocess.run([sys.executable, os.path.join("tools", "build", "spv2h.py")], check=True)

# 2. Resource file
rc_path = os.path.join("gen", "pkg", "app.rc")
res_o = os.path.join("gen", "pkg", "app_res.o")
with open(rc_path, "w", encoding="utf-8") as f:
    f.write('1 24 "runtime/app.manifest"\n')
    if os.path.exists("runtime/app.ico"):
        f.write('1 ICON "runtime/app.ico"\n')
    f.write('201 RCDATA "assets/ui/xbox-controller.png"\n')

subprocess.run([WINDRES, rc_path, "-O", "coff", "-o", res_o], check=True, env=ENV)

# 3. Sources
sources = []
for pattern in ["runtime/*/*.cpp", "decomp/decomp.cpp", "decomp/*/*.cpp"]:
    sources.extend(glob.glob(pattern))
sources = sorted(list(set(sources)))

# App version
appver = "0.6.0"
changelog_path = os.path.join(ROOT, "CHANGELOG.md")
if os.path.exists(changelog_path):
    m = re.search(r"^## \[([0-9][0-9.]*)\]", open(changelog_path, encoding="utf-8").read(), re.M)
    if m:
        appver = m.group(1)

opt = os.environ.get("OPT", "-O2")
gameid = "ULES-00600"
stamp = f"{opt} {gameid} {appver}"
flags_file = os.path.join(OBJDIR, "flags.txt")
if not os.path.exists(flags_file) or open(flags_file).read().strip() != stamp:
    for f in glob.glob(os.path.join(OBJDIR, "*.o")) + glob.glob(os.path.join(OBJDIR, "*.d")):
        try: os.remove(f)
        except: pass
    with open(flags_file, "w") as f:
        f.write(stamp)

INCLUDES = [
    "-Iruntime",
    "-Iruntime/core",
    "-Iruntime/hle",
    "-Iruntime/gpu",
    "-Iruntime/ui",
    "-Iruntime/audio",
    "-Iruntime/mods"
]
BASE_FLAGS = [
    "-std=c++17", opt, "-g", "-DPSP_TRACE",
    f"-DTF_GAME_ID=\"{gameid}\"",
    f"-DTF_APP_VERSION=\"{appver}\"",
    "-Wall", "-Wno-misleading-indentation"
] + INCLUDES

def needs_rebuild(src, obj, dep):
    if not os.path.exists(obj) or not os.path.exists(dep):
        return True
    obj_mtime = os.path.getmtime(obj)
    if os.path.getmtime(src) > obj_mtime:
        return True
    try:
        content = open(dep, "r", encoding="utf-8", errors="ignore").read()
        parts = content.replace("\\\n", " ").split()
        for p in parts[1:]:
            p = p.strip()
            if p and os.path.exists(p) and os.path.getmtime(p) > obj_mtime:
                return True
    except:
        return True
    return False

tasks = []
all_objs = []
for s in sources:
    obj_name = s.replace("/", "_").replace("\\", "_").replace(".cpp", ".o")
    obj_path = os.path.join(OBJDIR, obj_name)
    dep_path = os.path.join(OBJDIR, obj_name.replace(".o", ".d"))
    all_objs.append(obj_path)
    if needs_rebuild(s, obj_path, dep_path):
        tasks.append((s, obj_path, dep_path))

def compile_one(item):
    s, obj, dep = item
    print(f"g++ {s}")
    cmd = [GXX] + BASE_FLAGS + ["-MMD", "-MF", dep, "-c", s, "-o", obj]
    r = subprocess.run(cmd, env=ENV)
    if r.returncode != 0:
        raise RuntimeError(f"Compilation failed for {s}")

if tasks:
    jobs = os.cpu_count() or 4
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        futures = [ex.submit(compile_one, t) for t in tasks]
        for f in futures:
            f.result()

exe_out = os.environ.get("OUT", os.path.join(PKG, "TagForceRecomp.exe"))
print(f"linking {exe_out}...")
link_cmd = [GXX, "-o", exe_out] + all_objs + [res_o,
    "-static", "-mwindows", "-lwinhttp", "-ldwmapi", "-lgdi32", "-luser32",
    "-lcomdlg32", "-lshell32", "-lwinmm", "-ldinput8", "-ldxguid",
    "-lsynchronization", "-ld3d11", "-ldxgi", "-ld3dcompiler",
    "-lole32", "-loleaut32", "-luuid", "-lwindowscodecs", "-lgdiplus"
]
subprocess.run(link_cmd, check=True, env=ENV)

if "OUT" not in os.environ:
    bg_exe = os.path.join(PKG, "TagForceRecomp_bg.exe")
    try: shutil.copy2(exe_out, bg_exe)
    except Exception as e: print("note: TagForceRecomp_bg.exe is in use")

    root_exe = os.path.join(ROOT, "TagForceRecomp.exe")
    try: shutil.copy2(exe_out, root_exe)
    except Exception as e: print("note: ./TagForceRecomp.exe is in use")

print(f"built: {exe_out}")
