<#
.SYNOPSIS
  Refresh the README screenshots from a current Windows build.
.EXAMPLE
  ./scripts/capture-readme.ps1 -Preset debug -Interactive
#>
param(
    [string]$Preset = 'debug',
    [string]$QtDir = '',
    [string]$QtModuleDir = '',
    [string[]]$Only = @(),
    [int]$AfterMs = 14000,
    [switch]$Interactive,
    [switch]$Skip3D
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/env.ps1" -QtDir $QtDir
# Optional split Qt installation, useful for keeping capture-only modules in build/.
if ($QtModuleDir) {
    $QtModuleDir = (Resolve-Path -LiteralPath $QtModuleDir).Path
    $env:PATH = "$QtModuleDir/bin;$env:PATH"
    $env:QML_IMPORT_PATH = "$QtModuleDir/qml;$env:CMAKE_PREFIX_PATH/qml"
    $env:QTWEBENGINEPROCESS_PATH = "$QtModuleDir/bin/QtWebEngineProcess.exe"
    $env:QTWEBENGINE_RESOURCES_PATH = "$QtModuleDir/resources"
    $env:QTWEBENGINE_LOCALES_PATH = "$QtModuleDir/translations/qtwebengine_locales"
}
$exe = Join-Path $root "build/$Preset/EcuDiagSim.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "Build the application first: $exe" }
$assets = Join-Path $root 'docs/assets'
New-Item -ItemType Directory -Force -Path $assets | Out-Null

$captures = @(
    @{ Name = 'overview'; Scene = 'drive'; Tab = 0 },
    @{ Name = 'faults'; Scene = 'wheel-fault'; Tab = 0 },
    @{ Name = 'can-trace'; Scene = 'wheel-fault'; Tab = 1 },
    @{ Name = 'lessons'; Scene = 'wheel-fault'; Tab = 3 },
    @{ Name = 'diagnostics'; Scene = 'wheel-fault'; Tab = 2; Args = @('--uds', '2');
       Instruction = 'After the ABS lamp comes on, click Read DTCs, then C0035-13 for its snapshot. Move the pointer off the card.' }
)
if (-not $Skip3D) {
    $captures += @{ Name = 'vehicle-diagnostic'; Scene = 'showroom'; Tab = 0; Args = @('--vehicle-3d') }
}
if ($Interactive -and -not $Skip3D) {
    $captures += @(
        @{ Name = 'vehicle-exterior'; Scene = 'showroom'; Tab = 0; Args = @('--vehicle-3d');
           Instruction = 'Click Exterior. Keep the Perspective camera preset.' },
        @{ Name = 'vehicle-fault'; Scene = 'wheel-fault'; Tab = 0; Args = @('--vehicle-3d');
           Instruction = 'Select Wheel speed sensor FL from the component selector. Keep Diagnostic mode and Perspective.' }
    )
}
foreach ($capture in $captures) {
    if ($Only.Count -gt 0 -and $capture.Name -notin $Only) { continue }
    # The scanner needs a UI read to populate its trouble-code list; a raw scenario
    # UDS request populates only the communication log. Preserve its checked capture.
    if ($capture.Instruction -and -not $Interactive) {
        Write-Host "Skipping $($capture.Name): use -Interactive ($($capture.Instruction))"
        continue
    }
    $out = Join-Path $assets "$($capture.Name).png"
    $scene = Join-Path $PSScriptRoot "screenshots/$($capture.Scene).json"
    $delay = if ($capture.Instruction) { [Math]::Max($AfterMs, 45000) } else { $AfterMs }
    if ($capture.Instruction) { Write-Host "Next capture in $delay ms: $($capture.Instruction)" }
    # Quote paths explicitly for Start-Process, including checkouts containing spaces.
    $appArgs = @('--size', '1600x860', '--speed', '4', '--tab', $capture.Tab,
        '--scenario', "`"$scene`"", '--after', $delay, '--screenshot', "`"$out`"")
    if ($capture.Args) { $appArgs += $capture.Args }
    $started = Get-Date
    # WebEngine needs a visible window to render into QQuickWindow::grabWindow().
    $process = Start-Process -FilePath $exe -ArgumentList $appArgs -PassThru
    $forcedExit = $false
    if (-not $process.WaitForExit($delay + 30000)) {
        $process.Kill()
        $forcedExit = $true
        if (-not (Test-Path -LiteralPath $out) -or (Get-Item -LiteralPath $out).LastWriteTime -lt $started) {
            throw "Screenshot timed out: $($capture.Name)"
        }
        Write-Warning "Screenshot saved, but the renderer did not exit; closed the capture process."
    }
    if ((-not $forcedExit -and $process.ExitCode -ne 0) -or -not (Test-Path -LiteralPath $out) -or
        (Get-Item -LiteralPath $out).LastWriteTime -lt $started) {
        throw "Screenshot was not refreshed: $out"
    }
    Write-Host "Captured $out"
}
Write-Host 'Next: python scripts/make-readme-assets.py (requires Pillow).'
