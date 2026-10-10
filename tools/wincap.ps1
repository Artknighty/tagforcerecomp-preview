# Abre o jogo (save de TESTE, sem som), redimensiona a janela e grava gen/wincap.png com o conteudo dela.
# Uso: powershell -ExecutionPolicy Bypass -File tools/wincap.ps1 [largura] [altura] [segundos] [f11]
param([int]$W = 1300, [int]$H = 560, [int]$Wait = 12, [string]$Mode = "")
$env:TF_KEEP_CWD = "1"; $env:NO_AUDIO = "1"
Set-Location (Split-Path $PSScriptRoot -Parent)
$env:TF_LOG_DIR = Join-Path (Get-Location) "gen\logs_teste"   # log fora de gen/logs (so sessoes do usuario)
$env:TF_INI = Join-Path $env:TF_LOG_DIR "teste.ini"; New-Item -ItemType Directory -Force $env:TF_LOG_DIR | Out-Null   # nunca grava no .ini of the player (a troca de renderizador pelo menu salva no .ini)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class WinCap {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
Add-Type -AssemblyName System.Drawing
$iso = $env:TF_ISO   # path of your Tag Force ISO
$p = Start-Process -FilePath ".\gen\run.exe" -ArgumentList ("--iso `"" + $iso + "`"") -PassThru
Start-Sleep -Seconds $Wait
$h = [WinCap]::FindWindow("TagForceRecomp", $null)
if ($h -eq [IntPtr]::Zero) { $p.Refresh(); $h = $p.MainWindowHandle }
if ($h -eq [IntPtr]::Zero) { Stop-Process -Id $p.Id -Force; "janela do jogo nao encontrada"; exit 1 }
if ($Mode -eq "f11") { [WinCap]::SendMessage($h, 0x0100, [IntPtr]0x7A, [IntPtr]0) | Out-Null } else { [WinCap]::MoveWindow($h, 40, 40, $W, $H, $true) | Out-Null }
Start-Sleep -Seconds 2
$r = New-Object WinCap+RECT; [WinCap]::GetWindowRect($h, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc(); [WinCap]::PrintWindow($h, $dc, 0) | Out-Null; $g.ReleaseHdc($dc)
$bmp.Save((Join-Path (Get-Location) "gen\wincap.png")); Stop-Process -Id $p.Id -Force
"janela {0}x{1}" -f ($r.R - $r.L), ($r.B - $r.T)
