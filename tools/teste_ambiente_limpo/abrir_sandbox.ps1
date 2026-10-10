<#
.SYNOPSIS
  Opens Windows Sandbox (a clean, disposable Windows) and runs the clean-room build test inside it.

.DESCRIPTION
  The test installs MSYS2 and the build tools from scratch, copies this repository, runs tools/recomp_tudo.sh with YOUR ISO, and checks that the
  game boots to the title screen. Results (a log, RESULTADO.txt and screenshots) are written to the output folder on the host.
  The repository and the ISO folder are mapped READ-ONLY into the sandbox; nothing on your machine is modified.

.PARAMETER Repo
  Folder of the repository to test (default: the repository that contains this script).
.PARAMETER Iso
  Full path to your Yu-Gi-Oh! GX Tag Force ISO (European release ULES00600 v1.01).
.PARAMETER Out
  Folder on the host that receives the results (default: <Repo>\gen\teste_limpo_resultado).
.PARAMETER MemoryMB
  Memory for the sandbox (default 8192).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\teste_ambiente_limpo\abrir_sandbox.ps1 -Iso "D:\Games\Tag Force.iso"
#>
param(
    [Parameter(Mandatory = $true)][string]$Iso,
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Out = '',
    [int]$MemoryMB = 8192
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) { throw "ISO not found: $Iso" }
if (-not (Test-Path -LiteralPath (Join-Path $Repo 'tools\recomp_tudo.sh'))) { throw "Not a Tag Force Recomp repository: $Repo" }
if (-not $Out) { $Out = Join-Path $Repo 'gen\teste_limpo_resultado' }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Get-ChildItem -LiteralPath $Out -Force | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

$sandbox = Get-Command WindowsSandbox.exe -ErrorAction SilentlyContinue
if (-not $sandbox) {
    Write-Host 'Windows Sandbox is not enabled on this PC.' -ForegroundColor Yellow
    Write-Host 'Enable it once (administrator PowerShell), then restart Windows:'
    Write-Host '    Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM -All'
    Write-Host '(Requires Windows 10/11 Pro, Enterprise or Education, and virtualization enabled in the BIOS.)'
    exit 1
}

$isoDir = Split-Path -Parent (Resolve-Path -LiteralPath $Iso).Path
$isoName = Split-Path -Leaf $Iso
function Esc([string]$s) { [System.Security.SecurityElement]::Escape($s) }
$cmd = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\src\tools\teste_ambiente_limpo\dentro_do_sandbox.ps1 -IsoName "' + $isoName + '"'
$wsb = @"
<Configuration>
  <VGpu>Enable</VGpu>
  <Networking>Enable</Networking>
  <MemoryInMB>$MemoryMB</MemoryInMB>
  <MappedFolders>
    <MappedFolder><HostFolder>$(Esc $Repo)</HostFolder><SandboxFolder>C:\src</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$(Esc $isoDir)</HostFolder><SandboxFolder>C:\iso</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$(Esc $Out)</HostFolder><SandboxFolder>C:\out</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
  </MappedFolders>
  <LogonCommand><Command>$(Esc $cmd)</Command></LogonCommand>
</Configuration>
"@
$file = Join-Path $env:TEMP 'tagforce_teste_limpo.wsb'
Set-Content -LiteralPath $file -Value $wsb -Encoding UTF8
Write-Host "Starting Windows Sandbox ($MemoryMB MB). Results will appear in: $Out"
Write-Host 'The first steps download MSYS2 and packages (10-20 minutes). Watch C:\out\teste.log (also on the host) for progress.'
Start-Process -FilePath $sandbox.Source -ArgumentList ('"' + $file + '"')
