# Teste de janela do renderizador por GPU: troca software <-> GPU pelo menu (WM_COMMAND), redimensiona, tela cheia, overlay F3.
# Grava gen/wt_<n>.png (captura da area do jogo na TELA: PrintWindow nao pega swapchain DXGI). Uso: powershell -File tools/test/gpu_wintest.ps1 [exe]
param([string]$Exe = ".\gen\pkg\TagForceRecomp\TagForceRecomp_bg.exe", [int]$Wait = 20)
$env:TF_KEEP_CWD = "1"; $env:NO_AUDIO = "1"; $env:TF_NO_MOVIES = "1"; $env:TF_RES = "2"; $env:TF_GPU = "0"
Set-Location (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent)   # repository root
$env:TF_LOG_DIR = Join-Path (Get-Location) "gen\logs_teste"   # log fora de gen/logs (so sessoes do usuario)
$env:TF_INI = Join-Path $env:TF_LOG_DIR "teste.ini"; New-Item -ItemType Directory -Force $env:TF_LOG_DIR | Out-Null   # nunca grava no .ini of the player (a troca de renderizador pelo menu salva no .ini)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class WT {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
Add-Type -AssemblyName System.Drawing
$data = Join-Path (Get-Location) "gen\pkg\TagForceRecomp\jogo"   # installed game data (sh tools/install_dev.sh)
$p = Start-Process -FilePath $Exe -ArgumentList ("--dados `"" + $data + "`"") -PassThru
Start-Sleep -Seconds $Wait
$h = [WT]::FindWindow("TagForceRecomp", $null)
if ($h -eq [IntPtr]::Zero) { $p.Refresh(); $h = $p.MainWindowHandle }
if ($h -eq [IntPtr]::Zero) { Stop-Process -Id $p.Id -Force; "janela nao encontrada"; exit 1 }
[WT]::MoveWindow($h, 40, 40, 1000, 640, $true) | Out-Null; [WT]::SetForegroundWindow($h) | Out-Null
function Cap($n) {
  Start-Sleep -Seconds 2
  $r = New-Object WT+RECT; [WT]::GetClientRect($h, [ref]$r) | Out-Null
  $pt = New-Object WT+POINT; [WT]::ClientToScreen($h, [ref]$pt) | Out-Null
  $w = $r.R - $r.L; $hh = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, $bmp.Size); $g.Dispose()
  $bmp.Save((Join-Path (Get-Location) ("gen\wt_" + $n + ".png"))); $bmp.Dispose(); "cap $n ${w}x${hh}"
}
Cap "1_software"
[WT]::SendMessage($h, 0x0111, [IntPtr]1191, [IntPtr]0) | Out-Null; Cap "2_d3d11"
[WT]::SendMessage($h, 0x0111, [IntPtr]1192, [IntPtr]0) | Out-Null; Cap "3_vulkan"
[WT]::MoveWindow($h, 40, 40, 1400, 500, $true) | Out-Null; Cap "4_vulkan_larga"
[WT]::SendMessage($h, 0x0100, [IntPtr]0x72, [IntPtr]0) | Out-Null; Cap "5_vulkan_fps"
[WT]::SendMessage($h, 0x0100, [IntPtr]0x7A, [IntPtr]0) | Out-Null; Cap "6_vulkan_tela_cheia"
[WT]::SendMessage($h, 0x0100, [IntPtr]0x7A, [IntPtr]0) | Out-Null; Cap "7_vulkan_janela"
[WT]::SendMessage($h, 0x0111, [IntPtr]1191, [IntPtr]0) | Out-Null; Cap "8_d3d11_de_novo"
[WT]::SendMessage($h, 0x0111, [IntPtr]1192, [IntPtr]0) | Out-Null; Cap "9_vulkan_de_novo"
[WT]::SendMessage($h, 0x0111, [IntPtr]1190, [IntPtr]0) | Out-Null; Cap "10_software_de_novo"
Stop-Process -Id $p.Id -Force
