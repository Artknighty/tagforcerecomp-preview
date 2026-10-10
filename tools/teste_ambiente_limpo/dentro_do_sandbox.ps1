<#
  Runs INSIDE Windows Sandbox (started by abrir_sandbox.ps1). Clean-room test of the documented build:
    1. install MSYS2 + build tools from scratch;
    2. copy the repository (mapped read-only at C:\src) to a writable folder;
    3. run tools/recomp_tudo.sh with the ISO (mapped at C:\iso);
    4. start the game headless with the software renderer and check that it draws the title screen (screenshots);
    5. (optional) repeat with Direct3D 11 and Vulkan and report, without failing the test.
  Output: C:\out\teste.log, C:\out\RESULTADO.txt, screenshots (mapped to the host).
#>
param([Parameter(Mandatory = $true)][string]$IsoName)
$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$out = 'C:\out'; $work = 'C:\work\TagforceRecomp'; $msys = 'C:\msys64'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Start-Transcript -Path (Join-Path $out 'teste.log') -Force | Out-Null
$resultado = New-Object System.Collections.ArrayList
function Passo([string]$nome, [bool]$ok, [string]$detalhe = '') {
    $s = if ($ok) { 'PASS' } else { 'FAIL' }
    [void]$resultado.Add(('{0}  {1}  {2}' -f $s, $nome, $detalhe))
    Write-Host ('==== {0}: {1} {2}' -f $s, $nome, $detalhe) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
    Set-Content -Path (Join-Path $out 'RESULTADO.txt') -Value $resultado -Encoding UTF8
}
function Bash([string]$cmd, [string]$logName) {
    # run a command in the MSYS2 UCRT64 login shell and capture its output
    $log = Join-Path $out $logName
    & "$msys\usr\bin\bash.exe" -lc "export MSYSTEM=UCRT64; source /etc/profile; $cmd" *>&1 | Tee-Object -FilePath $log
    return $LASTEXITCODE
}

Write-Host "Clean-room test started $(Get-Date -Format s). ISO: C:\iso\$IsoName"
Passo 'ISO is visible in the sandbox' (Test-Path "C:\iso\$IsoName") "C:\iso\$IsoName"
Passo 'repository is visible in the sandbox' (Test-Path 'C:\src\tools\recomp_tudo.sh')

# ---- 1. MSYS2 -------------------------------------------------------------------------------------------------------------------------------
try {
    $installer = 'C:\msys2-installer.exe'
    $urls = @('https://repo.msys2.org/distrib/msys2-x86_64-latest.exe', 'https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-x86_64-latest.exe')
    $got = $false
    foreach ($u in $urls) { try { Invoke-WebRequest -UseBasicParsing -Uri $u -OutFile $installer; $got = (Get-Item $installer).Length -gt 50MB; if ($got) { break } } catch { Write-Host "download failed: $u" } }
    if (-not $got) { throw 'could not download the MSYS2 installer' }
    & $installer in --confirm-command --accept-messages --root C:/msys64 | Out-Host
    Passo 'MSYS2 installed' (Test-Path "$msys\usr\bin\bash.exe")
    Bash 'pacman --noconfirm -Syuu' 'pacman_1.log' | Out-Null    # first pass may end the shell; run again
    Bash 'pacman --noconfirm -Syuu' 'pacman_2.log' | Out-Null
    $pk = 'mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-binutils mingw-w64-ucrt-x86_64-headers mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-shaderc ' +
          'mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-pillow mingw-w64-ucrt-x86_64-python-numpy'
    $rc = Bash "pacman --noconfirm -S --needed $pk" 'pacman_pacotes.log'
    Passo 'build tools installed (gcc, ffmpeg, shaderc, vulkan headers, python, pillow, numpy)' ($rc -eq 0) "exit $rc"
} catch { Passo 'MSYS2 setup' $false $_.Exception.Message }

# ---- 2. copy the repository -------------------------------------------------------------------------------------------------------------------
try {
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    robocopy C:\src $work /E /XD gen .git /NFL /NDL /NJH /NJS | Out-Null
    Passo 'repository copied to a writable folder' (Test-Path "$work\tools\recomp_tudo.sh") $work
} catch { Passo 'copy repository' $false $_.Exception.Message }

# ---- 3. recomp_tudo.sh (install data, translate modules, build run.exe) -----------------------------------------------------------------------
$rc = Bash "cd /c/work/TagforceRecomp && sh tools/recomp_tudo.sh '/c/iso/$IsoName'" 'recomp_tudo.log'
Passo 'tools/recomp_tudo.sh completed' ($rc -eq 0) "exit $rc"
Passo 'gen\run.exe was produced' (Test-Path "$work\gen\run.exe")
Passo 'behavior mods were built (gen\mods\*\plugin.dll)' ((Get-ChildItem "$work\gen\mods" -Recurse -Filter plugin.dll -ErrorAction SilentlyContinue | Measure-Object).Count -ge 3) ''

# ---- 4. headless boot with the software renderer ---------------------------------------------------------------------------------------------
function Boot([string]$nome, [string]$gpu) {
    $env:TF_GPU = $gpu; $env:EXIT_FRAME = '1500'; $env:SNAP_FRAME = '600,1400'; $env:NO_PACE = '1'; $env:NO_AUDIO = '1'; $env:TF_KEEP_CWD = '1'
    $env:TF_LOG_DIR = "$work\gen\logs_$nome"; $env:TF_INI = "$work\gen\logs_$nome\teste.ini"
    New-Item -ItemType Directory -Force -Path $env:TF_LOG_DIR | Out-Null
    Remove-Item "$work\gen\snap_*.bmp" -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath "$work\gen\run.exe" -ArgumentList @('--iso', "C:\iso\$IsoName") -WorkingDirectory $work -PassThru -WindowStyle Hidden
    if (-not $p.WaitForExit(240000)) { $p.Kill(); return $false }
    $snaps = Get-ChildItem "$work\gen\snap_*.bmp" -ErrorAction SilentlyContinue
    foreach ($s in $snaps) { Copy-Item $s.FullName (Join-Path $out ("{0}_{1}" -f $nome, $s.Name)) -Force }
    Copy-Item "$env:TF_LOG_DIR\*.log" (Join-Path $out ("{0}_sessao.log" -f $nome)) -Force -ErrorAction SilentlyContinue
    return ($snaps.Count -ge 1)
}
if (Test-Path "$work\gen\run.exe") {
    $okSw = Boot 'software' '0'
    Passo 'game boots and draws frames (software renderer)' $okSw 'see software_snap_*.bmp'
    # ---- 5. GPU backends (informational: a virtual GPU may not support them) ----
    foreach ($b in @(@('direct3d11', '1'), @('vulkan', '2'))) {
        $ok = Boot $b[0] $b[1]
        [void]$resultado.Add(('INFO  {0} renderer: {1}' -f $b[0], $(if ($ok) { 'OK' } else { 'did not start (normal in a virtual GPU)' })))
    }
    Set-Content -Path (Join-Path $out 'RESULTADO.txt') -Value $resultado -Encoding UTF8
}

$falhas = ($resultado | Where-Object { $_ -like 'FAIL*' }).Count
Write-Host ''; Write-Host '========== SUMMARY ==========' ; $resultado | ForEach-Object { Write-Host $_ }
Write-Host ("Result: {0}. Files are in the host output folder." -f $(if ($falhas -eq 0) { 'ALL STEPS PASSED' } else { "$falhas step(s) FAILED" }))
Stop-Transcript | Out-Null
