<#
.SYNOPSIS
  Starts the app, optionally with a lesson, and saves a screenshot after a delay.
.EXAMPLE
  ./scripts/screenshot.ps1 -Out shot.png -Scenario 1 -Tab 2 -Speed 2 -AfterMs 9000
#>
param(
    [Parameter(Mandatory)][string]$Out,
    [string]$Preset = 'debug',
    [string]$Scenario = '',
    [int]$Tab = 0,
    [double]$Speed = 1,
    [int]$AfterMs = 4000
)
. "$PSScriptRoot\env.ps1"
$exe = Join-Path (Split-Path -Parent $PSScriptRoot) "build\$Preset\EcuDiagSim.exe"
$appArgs = @('--tab', $Tab, '--speed', $Speed, '--after', $AfterMs, '--screenshot', $Out)
if ($Scenario) { $appArgs += @('--scenario', $Scenario) }
$p = Start-Process -FilePath $exe -ArgumentList $appArgs -PassThru
if (-not $p.WaitForExit($AfterMs + 30000)) { $p.Kill(); throw 'application did not exit' }
if (-not (Test-Path $Out)) { throw 'no screenshot written' }
