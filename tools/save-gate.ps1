
param(
    [int]$N = 3400,
    [int]$Gap = 600,
    [int]$TimeoutSeconds = 240,
    [string]$Replay = 'tools\fixtures\replay-menu-level1.txt',
    [string]$SavePath = (Join-Path $env:TEMP 'tj-save-13.tjs'),
    [string]$Terms = '6D09C0+1C,6D09C0+4,6D09C0+8,6D09D4+E5C,6D09D4+E60,6D15E0+EC98,6C8910,6D15E0+EC94@6D09D4+1E74.F20,6D15E0+EC94@6D09D4+1E74.F24,6D15E0+EC94@6D09D4+1E74.F28,6D15E0+EC94@6D09D4+1E74.F30,6D15E0+EC94@6D09D4+1E74.28,6D15E0+EC94@6D09D4+1E74.2C,6D15E0+EC94@6D09D4+1E74.30,6D15E0+EC94@6D09D4+1E74.FDC,6D15E0+EC94@6D09D4+1E74.FE0'
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force evidence | Out-Null
$stamp = Get-Date -Format 'HHmmss'
$day = Get-Date -Format 'yyyyMMdd'
$record = "evidence\save-gate-$day-$stamp.txt"
$lines = [Collections.Generic.List[string]]::new()
function Note([string]$text) { $lines.Add($text); Write-Host $text; $lines | Set-Content $record }

$env:TJ_REPLAY = (Resolve-Path -LiteralPath $Replay).Path
$env:TJ_PROBE = $Terms
$env:TJ_FIXED_CURSOR = '1'
Remove-Item Env:TJ_FAITHFUL -ErrorAction SilentlyContinue
Remove-Item Env:TJ_SAVE_AT -ErrorAction SilentlyContinue
Remove-Item Env:TJ_LOAD -ErrorAction SilentlyContinue

$name = "sva$stamp"
$out = pwsh -NoProfile -File tools\proxy-run.ps1 -Name $name -CaptureAt ($N + $Gap) -TimeoutSeconds $TimeoutSeconds 2>&1
$code = $LASTEXITCODE
$out | Set-Content "evidence\save-gate-$day-$stamp-a.txt"
Note ("A exit=$code " + (@($out | Where-Object { $_ -match '^foreground_samples=' }) -join ''))
if ($code) { throw "run A failed (exit $code)" }
$logA = "evidence\proxy-log-$day-$name.txt"
$frameA = "evidence\proxy-capture-$day-$name.bmp"
$hashA = (Get-FileHash -LiteralPath $frameA -Algorithm SHA256).Hash

$env:TJ_SAVE_AT = "$N"
$env:TJ_SAVE_PATH = $SavePath
$name = "svb$stamp"
$out = pwsh -NoProfile -File tools\proxy-run.ps1 -Name $name -CaptureAt ($N + 50) -TimeoutSeconds $TimeoutSeconds 2>&1
$code = $LASTEXITCODE
$out | Set-Content "evidence\save-gate-$day-$stamp-b.txt"
Note ("B exit=$code " + (@($out | Where-Object { $_ -match '^foreground_samples=' }) -join ''))
if ($code) { throw "run B failed (exit $code)" }
Remove-Item Env:TJ_SAVE_AT -ErrorAction SilentlyContinue
Remove-Item Env:TJ_SAVE_PATH -ErrorAction SilentlyContinue
$logB = "evidence\proxy-log-$day-$name.txt"
$saveRow = (Select-String -LiteralPath $logB -Pattern '^# save write ' | ForEach-Object { $_.Line })
Note "writer: $saveRow"
if (-not (Test-Path -LiteralPath $SavePath)) { throw "run B wrote no save file at $SavePath" }
$saveHash = (Get-FileHash -LiteralPath $SavePath -Algorithm SHA256).Hash
Note ("save file: path=$SavePath bytes=$((Get-Item -LiteralPath $SavePath).Length) sha256=$saveHash")

$env:TJ_LOAD = $SavePath
$name = "svc0$stamp"
$out = pwsh -NoProfile -File tools\proxy-run.ps1 -Name $name -CaptureAt ($N + 50) -TimeoutSeconds $TimeoutSeconds 2>&1
$code = $LASTEXITCODE
$out | Set-Content "evidence\save-gate-$day-$stamp-c0.txt"
Note ("C0 exit=$code " + (@($out | Where-Object { $_ -match '^foreground_samples=' }) -join ''))
if ($code) { throw "pilot run C0 failed (exit $code)" }
$logC0 = "evidence\proxy-log-$day-$name.txt"
$loadRow = (Select-String -LiteralPath $logC0 -Pattern '^# load applied ' | ForEach-Object { $_.Line })
if (-not $loadRow) {
    Select-String -LiteralPath $logC0 -Pattern '^# load ' | ForEach-Object { Note $_.Line }
    throw "the loader did not apply (no '# load applied' row)"
}
Note "loader: $loadRow"
$null = $loadRow -match 'frame=(\d+)'
$loadFrame = [int]$Matches[1]

$name = "svc$stamp"
$out = pwsh -NoProfile -File tools\proxy-run.ps1 -Name $name -CaptureAt ($loadFrame + $Gap) -TimeoutSeconds $TimeoutSeconds 2>&1
$code = $LASTEXITCODE
$out | Set-Content "evidence\save-gate-$day-$stamp-c.txt"
Note ("C exit=$code " + (@($out | Where-Object { $_ -match '^foreground_samples=' }) -join ''))
if ($code) { throw "run C failed (exit $code)" }
Remove-Item Env:TJ_LOAD -ErrorAction SilentlyContinue
$logC = "evidence\proxy-log-$day-$name.txt"
$frameC = "evidence\proxy-capture-$day-$name.bmp"
$hashC = (Get-FileHash -LiteralPath $frameC -Algorithm SHA256).Hash

$ref = @{}
$mir = @{}
foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $logA))) {
    if ($line -notmatch '^# probe (\S+) (\d+) (\S+)$') { continue }
    $frame = [int]$Matches[2]
    if ($frame -gt $N -and $frame -le ($N + $Gap)) { $ref["$($Matches[1]) $($frame - $N)"] = $Matches[3] }
}
foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $logC))) {
    if ($line -notmatch '^# probe (\S+) (\d+) (\S+)$') { continue }
    $frame = [int]$Matches[2]
    if ($frame -gt $loadFrame -and $frame -le ($loadFrame + $Gap)) { $mir["$($Matches[1]) $($frame - $loadFrame)"] = $Matches[3] }
}
$same = 0
$differ = 0
$firstRow = ''
$byTerm = @{}
foreach ($key in ($ref.Keys | Sort-Object)) {
    if (-not $mir.ContainsKey($key)) { if (-not $firstRow) { $firstRow = "missing row in C: $key" }; ++$differ; continue }
    if ($mir[$key] -ne $ref[$key]) {
        if (-not $firstRow) { $firstRow = "$key A=$($ref[$key]) C=$($mir[$key])" }
        $term = $key.Split(' ')[0]
        $byTerm[$term] = 1 + $byTerm[$term]
        ++$differ
    } else {
        ++$same
    }
}
Note "rows compared=$($ref.Count) equal=$same differ=$differ (A Presents $($N+1)..$($N+$Gap) against C $($loadFrame+1)..$($loadFrame+$Gap))"
foreach ($term in ($byTerm.Keys | Sort-Object)) { Note "  $term differs on $($byTerm[$term]) row(s)" }
Note "frame hash A@$($N+$Gap)=$hashA"
Note "frame hash C@$($loadFrame+$Gap)=$hashC"
if (-not $differ -and $hashA -eq $hashC) {
    Note "save-gate ok: $($ref.Count) rows and the frame reproduced from the save file"
    exit 0
}
Note "save-gate differs: first row $firstRow"
exit 1
