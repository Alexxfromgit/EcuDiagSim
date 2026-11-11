<#
.SYNOPSIS
  Packs a built EcuDiagSim.exe with the Qt runtime into a portable zip plus a SHA-256 checksum.

.EXAMPLE
  ./scripts/package-windows.ps1 -Preset release -Version 0.1.0 -OutDir dist
#>
param(
    [string]$Preset = 'release',
    [Parameter(Mandatory)][string]$Version,
    [string]$OutDir = 'dist'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\$Preset\EcuDiagSim.exe"
if (-not (Test-Path $exe)) { throw "$exe not found - build the '$Preset' preset first." }

$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $windeployqt) {
    . "$PSScriptRoot\env.ps1"
    $windeployqt = Get-Command windeployqt
}

$name = "EcuDiagSim-$Version-win-x64"
$out = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
$stage = Join-Path $out $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $exe $stage
& $windeployqt.Source --release --qmldir (Join-Path $root 'src\app\qml') --no-translations `
    --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime (Join-Path $stage 'EcuDiagSim.exe')
if ($LASTEXITCODE) { throw 'windeployqt failed' }

# The Visual C++ runtime as DLLs next to the exe (app-local deployment), so the zip runs on a PC
# without the runtime installed. windeployqt would otherwise add the vc_redist installer instead.
if (-not $env:VCToolsRedistDir) { . "$PSScriptRoot\env.ps1" }
$crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $crt) { throw "Visual C++ runtime not found under $env:VCToolsRedistDir" }
Copy-Item (Join-Path $crt.FullName '*.dll') $stage

Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-Item (Join-Path $root 'THIRD-PARTY-NOTICES.md') $stage
Copy-Item (Join-Path $root 'README.md') $stage
Copy-Item -Recurse (Join-Path $root 'docs') (Join-Path $stage 'docs')

$zip = Join-Path $out "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Remove-Item -Recurse -Force $stage

$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
# LF line ending, so `sha256sum -c` works on Linux, macOS and Git Bash as well.
[IO.File]::WriteAllText((Join-Path $out "$name.zip.sha256"), "$hash  $name.zip`n", [Text.Encoding]::ASCII)
Write-Host "$zip`n$hash"
