<#
.SYNOPSIS
  Configure, build, test and run EcuDiagSim on Windows (MSVC + Ninja + Qt 6).

.EXAMPLE
  ./scripts/build.ps1                 # configure + build the 'debug' preset
  ./scripts/build.ps1 -Test           # ... and run ctest
  ./scripts/build.ps1 -Preset release -Run
#>
param(
    [string]$Preset = 'debug',
    [string]$QtDir = '',
    [switch]$Test,
    [switch]$Run,
    [string[]]$AppArgs = @()
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot\env.ps1" -QtDir $QtDir

Push-Location $root
try {
    cmake --preset $Preset
    if ($LASTEXITCODE) { throw 'configure failed' }
    cmake --build --preset $Preset
    if ($LASTEXITCODE) { throw 'build failed' }
    if ($Test) {
        ctest --preset $Preset
        if ($LASTEXITCODE) { throw 'tests failed' }
    }
    if ($Run) {
        & "$root\build\$Preset\EcuDiagSim.exe" @AppArgs
    }
}
finally { Pop-Location }
