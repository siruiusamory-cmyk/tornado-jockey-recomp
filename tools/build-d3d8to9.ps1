
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$root = (Get-Location).Path
$src = Join-Path $root 'port\third_party\d3d8to9'
$out = Join-Path $root 'build\proxy'
New-Item -ItemType Directory -Force $out | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: $vswhere" }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'no Visual Studio install with the x86 C++ tools' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'

New-Item -ItemType Directory -Force (Join-Path $out 'd3d8to9-obj') | Out-Null
$sources = Get-ChildItem (Join-Path $src 'source') -Filter *.cpp | ForEach-Object { '"' + $_.FullName + '"' }
$bat = Join-Path $out 'build-d3d8to9.bat'
@"
@echo off
call "$vcvars" >nul
cl /nologo /LD /O2 /MT /EHsc /W3 /std:c++17 /DD3D8TO9NOLOG /D_CRT_SECURE_NO_WARNINGS /Fo"$out\d3d8to9-obj\\" $($sources -join ' ') /link /DEF:"$src\res\d3d8.def" d3d9.lib user32.lib gdi32.lib shell32.lib /OUT:"$out\d3d8to9.dll"
if errorlevel 1 exit /b 1
dumpbin /nologo /exports "$out\d3d8to9.dll"
"@ | Set-Content -Encoding ascii $bat

$log = Join-Path $env:TEMP 'tj-build-d3d8to9.txt'
$build = Start-Process cmd.exe -ArgumentList '/c', $bat -WorkingDirectory $out -NoNewWindow -PassThru -RedirectStandardOutput $log
$build.PriorityClass = 'BelowNormal'
$build.WaitForExit()
Get-Content $log
if ($build.ExitCode) { throw "d3d8to9 build failed (exit $($build.ExitCode))" }
