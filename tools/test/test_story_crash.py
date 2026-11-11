import os, sys, shutil, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(ROOT)

W = os.path.join(ROOT, "gen", "play_w")
if os.path.exists(W):
    try: shutil.rmtree(W)
    except: pass
os.makedirs(os.path.join(W, "gen"), exist_ok=True)
os.makedirs(os.path.join(W, "logs"), exist_ok=True)

# Copy save
save_src = os.path.join(ROOT, "ms0")
if not os.path.exists(save_src):
    save_src = os.path.join(ROOT, "gen", "ms0")
if os.path.exists(save_src):
    shutil.copytree(save_src, os.path.join(W, "ms0"), dirs_exist_ok=True)

# Generate inputs like tools/test/story.sh
# C="300:4000,306:0,900:8,906:0,1100:4000,1106:0,1600:4000,1606:0,2000:4000,2006:0,2300:4000,2306:0"
# X = 4000 every 60 frames
c_inputs = "300:4000,306:0,900:8,906:0,1100:4000,1106:0,1600:4000,1606:0,2000:4000,2006:0,2300:4000,2306:0"
x_inputs = ",".join(f"{f}:4000,{f+6}:0" for f in range(2500, 4000, 60))
all_inputs = f"{c_inputs},{x_inputs}"

env = dict(os.environ)
env["TF_KEEP_CWD"] = "1"
env["TF_SEGUNDO_PLANO"] = "1"
env["TF_NO_MOVIES"] = "1"
env["NO_PACE"] = "1"
env["AUTO_PRESS"] = all_inputs
env["EXIT_FRAME"] = "4000"
env["TF_LOG_DIR"] = os.path.abspath(os.path.join(W, "logs"))
env["TF_INI"] = os.path.abspath(os.path.join(W, "logs", "teste.ini"))
env["NO_AUDIO"] = "1"
env["TF_MODS_DIR"] = os.path.abspath(os.path.join(ROOT, "mods"))
env["IO_TRACE"] = "1"

exe = os.path.join(ROOT, "TagForceRecomp.exe")
game_dir = os.path.join(ROOT, "jogo")

print(f"Running story test in {W}...")
r = subprocess.run([exe, "--dados", game_dir], cwd=W, env=env, timeout=60)
print(f"Exit code: {r.returncode}")

log_files = [os.path.join(W, "logs", f) for f in os.listdir(os.path.join(W, "logs")) if f.endswith(".log")]
if log_files:
    latest = max(log_files, key=os.path.getmtime)
    print(f"--- Log ({latest}) ---")
    with open(latest, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        print("".join(lines[-40:]))
