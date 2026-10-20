# Diagnostic matrix: runs the game several times at the title screen (Press Start), each with one setting changed,
# and writes one report (diag_matrix_result.txt) plus a copy of every log (diag_matrix_logs\).
# Needs the diagnostic package (diag.txt next to the exe). Run it with run_diag_matrix.bat; takes about 8 minutes, do not touch the game windows.
param([int]$Secs = 45, [int]$Only = 0)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here
$exe = Join-Path $here 'TagForceRecomp.exe'
$data = Join-Path $here 'jogo'
$diag = Join-Path $here 'diag.txt'
$logs = Join-Path $here 'logs'
$out = Join-Path $here 'diag_matrix_logs'
$secs = $Secs
if (-not (Test-Path $exe)) { throw 'TagForceRecomp.exe not found next to this script' }
if (-not (Test-Path $data)) { throw 'Install the game first: run TagForceRecomp.exe once and let it install from the ISO' }
New-Item -ItemType Directory -Force $out | Out-Null
$hadDiag = Test-Path $diag
$origDiag = if ($hadDiag) { Get-Content $diag -Raw } else { '' }

# name, diag.txt options, environment variables
$configs = @(
    @{ n = '01_default';         o = @();                                 e = @{};                s = 0 },
    @{ n = '02_two_buffers';     o = @('d3d_buffers=2');                  e = @{};                s = 0 },
    @{ n = '03_four_buffers';    o = @('d3d_buffers=4');                  e = @{};                s = 0 },
    @{ n = '04_latency2';        o = @('d3d_latency=2');                  e = @{};                s = 0 },
    @{ n = '05_vsync';           o = @();                                 e = @{ TF_SYNC = '1' }; s = 0 },
    @{ n = '06_fps30';           o = @();                                 e = @{ TF_FPS = '30' }; s = 0 }
)
if ($Only -gt 0) { $configs = $configs | Select-Object -First $Only }
$results = @()
$i = 0
foreach ($c in $configs) {
    $i++
    $cs = if ($c.s -gt 0) { $c.s } else { $secs }
    Write-Host ("[{0}/{1}] {2} ({3} s)..." -f $i, $configs.Count, $c.n, $cs)
    $lines = @('Diagnostic build: extra audio/latency/resource lines in logs.') + $c.o
    Set-Content -Path $diag -Value $lines -Encoding ASCII
    foreach ($k in $c.e.Keys) { Set-Item -Path ("Env:" + $k) -Value $c.e[$k] }
    $before = @(Get-ChildItem $logs -Filter 'sessao_*.log' -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
    $p = Start-Process -FilePath $exe -ArgumentList @('--dados', ('"' + $data + '"')) -PassThru
    Start-Sleep -Seconds $cs
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    Start-Sleep -Seconds 2
    foreach ($k in $c.e.Keys) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }
    $new = Get-ChildItem $logs -Filter 'sessao_*.log' | Where-Object { $before -notcontains $_.Name } | Sort-Object Length -Descending | Select-Object -First 1
    if (-not $new) { $results += ("{0}: NO LOG" -f $c.n); continue }
    $dest = Join-Path $out ($c.n + '.log')
    Copy-Item $new.FullName $dest -Force
    $txt = Get-Content $dest -ErrorAction SilentlyContinue
    # per-second audio lines, skipping the first 10 s (loading and the intro movie)
    $aud = @($txt | Where-Object { $_ -match 'device:' } | Select-Object -Skip 10)
    $q = @(); $st = @()
    foreach ($l in $aud) {
        if ($l -match '(\d+) quadros') { $q += [double]$Matches[1] }
        if ($l -match 'stretch max ([0-9.]+)%') { $st += [double]$Matches[1] }
    }
    $late = @($txt | Where-Object { $_ -match 'intermediario atrasado' }).Count
    $heavy = @($txt | Where-Object { $_ -match 'quadro pesado' }).Count
    $autoOff = @($txt | Where-Object { $_ -match 'phase lock turned off automatically' }).Count -gt 0
    $pres = @($txt | Where-Object { $_ -match '^\[d3d\] per s' } | Select-Object -Skip 10)
    $presMax = 0.0; $busy = @()
    foreach ($l in $pres) {
        if ($l -match 'Present\(\) [0-9.]+ ms in \d+ calls \(longest ([0-9.]+)\)') { $presMax = [Math]::Max($presMax, [double]$Matches[1]) }
        if ($l -match 'GPU busy ([0-9.]+) ms') { $busy += [double]$Matches[1] }
    }
    $slow = @($q | Where-Object { $_ -le 59 }).Count
    $avgQ = if ($q.Count) { [Math]::Round(($q | Measure-Object -Average).Average, 2) } else { 0 }
    $avgSt = if ($st.Count) { [Math]::Round(($st | Measure-Object -Average).Average, 2) } else { 0 }
    $maxSt = if ($st.Count) { ($st | Measure-Object -Maximum).Maximum } else { 0 }
    $avgBusy = if ($busy.Count) { [Math]::Round(($busy | Measure-Object -Average).Average, 1) } else { 0 }
    $results += ('{0}: frames/s avg {1}, seconds at or below 59 fps {2}/{3}, audio stretch avg {4}% max {5}%, late intermediate frames {6}, heavy frames {7}, longest Present() {8} ms, GPU busy avg {9} ms/s, phase lock switched off by the game: {10}' -f $c.n, $avgQ, $slow, $q.Count, $avgSt, $maxSt, $late, $heavy, $presMax, $avgBusy, $autoOff)
}
if ($hadDiag) { Set-Content -Path $diag -Value $origDiag -Encoding ASCII } else { Remove-Item $diag -ErrorAction SilentlyContinue }
$sys = @(Get-Content (Join-Path $out '01_default.log') -ErrorAction SilentlyContinue | Where-Object { $_ -match '^\[sys\]|^\[gpu\]|^\[d3d\] swap' })
$report = @('Tag Force Recomp diagnostic matrix', (Get-Date -Format 's'), '') + $sys + @('') + $results + @('', 'Lower "seconds at or below 59 fps" and lower stretch are better. Send this file and the diag_matrix_logs folder.')
Set-Content -Path (Join-Path $here 'diag_matrix_result.txt') -Value $report -Encoding UTF8
Write-Host ''
$results | ForEach-Object { Write-Host $_ }
Write-Host ''
Write-Host 'Done. Send diag_matrix_result.txt and the diag_matrix_logs folder.'
