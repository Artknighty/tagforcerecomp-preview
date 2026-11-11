"""Python replay runner for Tag Force Recomp.
Replays a recorded session with exact player config, GPU rendering, HD textures, and 120 FPS.
"""
import os, sys, shutil, subprocess, glob

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(ROOT)

session = sys.argv[1] if len(sys.argv) > 1 else "sessao_20261108_000336"
tag = sys.argv[2] if len(sys.argv) > 2 else "_test120"
end_frame = int(sys.argv[3]) if len(sys.argv) > 3 else 1500

logd = os.path.join(ROOT, "logs")
inputs_file = os.path.join(logd, f"{session}.inputs")
if not os.path.exists(inputs_file):
    logd = os.path.join(ROOT, "gen", "logs")
    inputs_file = os.path.join(logd, f"{session}.inputs")

if not os.path.exists(inputs_file):
    sys.exit(f"inputs file not found: {inputs_file}")

replay_dir = os.path.join(ROOT, "gen", "replay", f"{session}{tag}")
if os.path.exists(replay_dir):
    try: shutil.rmtree(replay_dir)
    except: pass
os.makedirs(os.path.join(replay_dir, "gen"), exist_ok=True)
os.makedirs(os.path.join(replay_dir, "logs"), exist_ok=True)

# Copy session save if available, otherwise fresh copy of ms0
src_ms0 = os.path.join(logd, f"{session}_ms0")
if not os.path.exists(src_ms0):
    src_ms0 = os.path.join(ROOT, "ms0")

if os.path.exists(src_ms0):
    shutil.copytree(src_ms0, os.path.join(replay_dir, "ms0"), dirs_exist_ok=True)

# Copy player's ini to replay test ini
test_ini = os.path.join(replay_dir, "logs", "teste.ini")
if os.path.exists("tagforce_recomp.ini"):
    shutil.copy2("tagforce_recomp.ini", test_ini)

# Exe
exe = os.path.join(ROOT, "gen", "pkg", "TagForceRecomp", "TagForceRecomp_bg.exe")
if not os.path.exists(exe):
    exe = os.path.join(ROOT, "TagForceRecomp.exe")

env = dict(os.environ)
env["TF_KEEP_CWD"] = "1"
env["TF_LOG_DIR"] = os.path.abspath(os.path.join(replay_dir, "logs"))
env["TF_INI"] = os.path.abspath(test_ini)
env["TF_SEGUNDO_PLANO"] = "1"
env["TF_CONFIRM_AUTO"] = "0"
env["EXIT_FRAME"] = str(end_frame)
env["AUTO_PRESS_FILE"] = os.path.abspath(inputs_file)
env["NO_AUDIO"] = "1"

game_dir = os.path.join(ROOT, "jogo")
cmd = [exe, "--dados", game_dir]

print(f"Running replay {session} up to frame {end_frame} in {replay_dir}...")
r = subprocess.run(cmd, cwd=replay_dir, env=env, timeout=120)
print(f"Replay exit code: {r.returncode}")

log_files = glob.glob(os.path.join(replay_dir, "logs", "*.log"))
if log_files:
    latest_log = max(log_files, key=os.path.getmtime)
    print(f"--- Output log ({latest_log}) ---")
    with open(latest_log, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        print("".join(lines[-40:]))
