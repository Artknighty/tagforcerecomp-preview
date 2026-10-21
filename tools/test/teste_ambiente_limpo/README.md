# Clean-room build test (Windows Sandbox)

Checks that the documented build really works on a **fresh Windows** with nothing installed: MSYS2 and every tool are installed from
scratch inside [Windows Sandbox](https://learn.microsoft.com/windows/security/application-security/application-isolation/windows-sandbox/),
the repository is built with `tools/build.sh`, the game data is installed from **your** ISO with `tools/install_dev.sh`, and the game is started
headless to confirm it translates its code and draws frames.
Nothing on your computer is changed: the repository and the ISO folder are mapped read-only, and the sandbox is discarded when closed.

## One-time setup

Windows 10/11 **Pro/Enterprise/Education** with virtualization enabled in the BIOS. In an administrator PowerShell, then restart:

```powershell
Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM -All
```

## Run

```powershell
powershell -ExecutionPolicy Bypass -File tools\test\teste_ambiente_limpo\abrir_sandbox.ps1 -Iso "D:\path\to\Tag Force.iso"
```

Optional: `-Repo <folder>` (default: this repository), `-Out <folder>` (default `gen\teste_limpo_resultado`), `-MemoryMB 8192`.

The sandbox window opens and runs `dentro_do_sandbox.ps1`. It takes roughly 20–40 minutes (downloads MSYS2 and packages, ~2 min of compilation of the
generated code). Progress is written to the output folder on the host, so you can follow `teste.log` from outside.

## Result

`RESULTADO.txt` in the output folder lists each step as `PASS` or `FAIL`:

1. MSYS2 and the build tools install
2. the repository is copied and `tools/build.sh` completes (`gen\pkg\TagForceRecomp\TagForceRecomp.exe` and the behavior-mod plugins exist)
3. `tools/install_dev.sh` installs the game data from the ISO
4. the game boots and draws frames with the **software renderer** (`software_snap_*.bmp` are screenshots you can look at)
5. informational: the same with Direct3D 11 and Vulkan (a virtual GPU may not support them; that is not a failure)

The other files are the full logs (`teste.log`, `pacman_*.log`, `build.log`, `install.log`, `*_sessao.log`). If a step fails, the log of that step says what is missing:
that is exactly the information to fix in the README or the scripts.

## Notes

- Only the European release **ULES00600 v1.01** is supported.
- Packages are the **latest** from MSYS2 at the time you run the test: a failure caused by a newer FFmpeg or compiler is a real finding for users, too.
