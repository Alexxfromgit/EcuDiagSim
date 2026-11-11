# Dot-source to get Qt + MSVC into the current PowerShell session:  . ./scripts/env.ps1
param([string]$QtDir = '')
if (-not $QtDir) {
    if ($env:QT_ROOT_DIR) { $QtDir = $env:QT_ROOT_DIR }
    else {
        $QtDir = Get-ChildItem 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' -and (Test-Path "$($_.FullName)\msvc2022_64") } |
            Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 |
            ForEach-Object { "$($_.FullName)\msvc2022_64" }
    }
}
if (-not $QtDir -or -not (Test-Path "$QtDir\bin\qmake.exe")) { throw 'Qt (msvc2022_64) not found. Install it with aqtinstall or pass -QtDir.' }
if ($env:PATH -notlike "*$QtDir\bin*") { $env:PATH = "$QtDir\bin;$env:PATH" }
$env:CMAKE_PREFIX_PATH = $QtDir
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio installation with the C++ toolset found.' }
    Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' 2>$null | Out-Null
}
