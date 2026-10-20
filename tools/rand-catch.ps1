
param([int]$Runs = 200, [int]$CaptureAt = 2900, [int]$TimeoutSeconds = 200,
      [switch]$SelfCheck)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force evidence | Out-Null
$anchorFrame = 2837
$anchor = '00640055'

function Read-Run([string]$Log) {
    $probe = @{}
    $counts = @{}
    foreach ($line in [IO.File]::ReadLines($Log)) {
        if ($line -match '^# probe 6C8910 (\d+) (\S+)$') {
            $probe[[int]$Matches[1]] = $Matches[2]
        } elseif ($line -match '^# watch frame=(\d+) ') {
            $f = [int]$Matches[1]
            $counts[$f] = 1 + $counts[$f]
        }
    }
    $last = if ($probe.Count) { ($probe.Keys | Measure-Object -Maximum).Maximum } else { 0 }
    return @{ Probe = $probe; Counts = $counts; Last = $last }
}

function First-Diff($Ref, $Run) {
    $limit = [Math]::Min($Ref.Last, $Run.Last)
    for ($f = 1; $f -le $limit; $f++) {
        if (-not $Ref.Probe.ContainsKey($f)) { continue }
        if (-not $Run.Probe.ContainsKey($f)) { continue }
        if ($Ref.Probe[$f] -ne $Run.Probe[$f]) { return $f }
    }
    return 0
}

if ($SelfCheck) {

    $a = @{ Probe = @{}; Counts = @{}; Last = 5 }
    $b = @{ Probe = @{}; Counts = @{}; Last = 5 }
    foreach ($f in 1..5) { $a.Probe[$f] = 'AA'; $b.Probe[$f] = 'AA' }
    "twin diff=$(First-Diff $a $b) (want 0)"
    $b.Probe[3] = 'BB'
    "shifted diff=$(First-Diff $a $b) (want 3)"
    if ((First-Diff $a $b) -ne 3) { throw 'detector self-check failed' }
    exit 0
}

$day = Get-Date -Format 'yyyyMMdd'
$stamp = Get-Date -Format 'HHmmss'
$table = Join-Path 'evidence' "tj-stage-12d-catch-$stamp.txt"
$rows = [Collections.Generic.List[string]]::new()
$rows.Add("# TJ-STAGE-12D L1 catch loop, started $(Get-Date -Format o)")
$rows.Add("# fixture tools/fixtures/replay-menu-level1.txt capture $CaptureAt probe 6C8910 arm 1, watch unset")
$rows.Add("# reference must read $anchor at Present $anchorFrame")
$env:TJ_REPLAY = (Resolve-Path -LiteralPath 'tools\fixtures\replay-menu-level1.txt').Path
$env:TJ_PROBE = '6C8910'
$env:TJ_ARM = '1'
$env:TJ_FIXED_CURSOR = '1'
Remove-Item Env:TJ_WATCH -ErrorAction SilentlyContinue
Remove-Item Env:TJ_FAITHFUL -ErrorAction SilentlyContinue

$reference = $null
$failed = 0
$short = 0
$launches = 0
$started = Get-Date
$result = 2
for ($i = 1; $i -le $Runs; $i++) {
    $launches = $i
    $name = "c3a$i-$stamp"
    $record = Join-Path 'evidence' "rand-catch-$day-$name.txt"
    $log = Join-Path 'evidence' "proxy-log-$day-$name.txt"
    $capture = Join-Path 'evidence' "proxy-capture-$day-$name.bmp"
    $windows = Join-Path 'evidence' "proxy-windows-$day-$name.txt"
    $out = pwsh -NoProfile -File tools\proxy-run.ps1 -Name $name -CaptureAt $CaptureAt -TimeoutSeconds $TimeoutSeconds 2>&1
    $code = $LASTEXITCODE
    $out | Set-Content $record
    $game = -1
    $line = @($out | Where-Object { $_ -match '^foreground_samples=' }) | Select-Object -Last 1
    if ($line -match 'game=(\d+)') { $game = [int]$Matches[1] }
    if ($code -or -not (Test-Path -LiteralPath $log)) {

        $failed++
        $reached = 0
        $lastLine = ''
        if (Test-Path -LiteralPath $log) {
            $reached = (Read-Run $log).Last
            $lastLine = @(Get-Content -LiteralPath $log -Tail 1) | Select-Object -First 1
        }
        "result=failed exit=$code last_present=$reached last_line=$lastLine" | Add-Content $record
        $rows.Add("run=$i result=failed exit=$code last_present=$reached last_line=$lastLine foreground_game=$game record=$record log=$log")
        Write-Host "run $i/$Runs failed exit=$code last_present=$reached"
        Remove-Item -LiteralPath $windows, $capture -Force -ErrorAction SilentlyContinue
        $rows | Set-Content $table
        continue
    }
    $run = Read-Run $log
    $at = $run.Probe[$anchorFrame]
    $calls = $run.Counts[$anchorFrame]
    $arm = @(Select-String -LiteralPath $log -Pattern '^# probe arm ' | Select-Object -Last 1).Line
    if ($run.Last -lt $anchorFrame) {
        $short++
        $rows.Add("run=$i result=short frames_to=$($run.Last) foreground_game=$game record=$record log=$log")
        Write-Host "run $i/$Runs short ($($run.Last) Presents)"
        Remove-Item -LiteralPath $windows, $capture -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "build\game-hashes-$name-before.txt", "build\game-hashes-$name-after.txt" -Force -ErrorAction SilentlyContinue
        $rows | Set-Content $table
        continue
    }
    if (-not $reference) {
        if ($at -ne $anchor) {

            $rows.Add("run=$i result=divergent first_diff=$anchorFrame ref=$anchor run=$at ref_calls=- run_calls=$calls frames_to=$($run.Last) foreground_game=$game log=$log")
            $rows.Add("run=$i kept $log $capture")
            Write-Host "CAUGHT first run at Present ${anchorFrame}: class $anchor against run $at (calls $calls)"
            $result = 0
            $rows | Set-Content $table
            break
        }
        $reference = $run

        $rows.Add("run=$i result=reference frames_to=$($run.Last) at$anchorFrame=$at calls=$calls foreground_game=$game log=$log $arm")
        Write-Host "run $i/$Runs reference frames=$($run.Last) at $anchorFrame = $at calls=$calls"
        Remove-Item -LiteralPath $windows, $capture -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "build\game-hashes-$name-before.txt", "build\game-hashes-$name-after.txt" -Force -ErrorAction SilentlyContinue
        $rows | Set-Content $table
        continue
    }
    $diff = First-Diff $reference $run
    if ($diff) {
        $rows.Add("run=$i result=divergent first_diff=$diff ref=$($reference.Probe[$diff]) run=$($run.Probe[$diff]) ref_calls=$($reference.Counts[$diff]) run_calls=$($run.Counts[$diff]) frames_to=$($run.Last) foreground_game=$game log=$log")
        $rows.Add("run=$i kept $log $capture")
        Write-Host "CAUGHT run $i at Present ${diff}: reference $($reference.Probe[$diff]) (calls $($reference.Counts[$diff])) against run $($run.Probe[$diff]) (calls $($run.Counts[$diff]))"
        $result = 0
        $rows | Set-Content $table
        break
    }
    $rows.Add("run=$i result=identical frames_to=$($run.Last) at$anchorFrame=$at calls=$calls foreground_game=$game $arm")
    Write-Host "run $i/$Runs identical frames=$($run.Last) at $anchorFrame = $at calls=$calls"
    Remove-Item -LiteralPath $record, $windows, $log, $capture -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "build\game-hashes-$name-before.txt", "build\game-hashes-$name-after.txt" -Force -ErrorAction SilentlyContinue
    if ($i % 10 -eq 0) {
        $free = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
        $rows.Add("# progress runs=$i failed=$failed short=$short disk_free_gb=$free")
        if ($free -lt 8) { throw "disk free $free GB under the 8 GB floor" }
    }
    $rows | Set-Content $table
}
$elapsed = [int]((Get-Date) - $started).TotalSeconds
$rows.Add("# end $(Get-Date -Format o) runs=$launches failed=$failed short=$short elapsed_s=$elapsed result=$result")
$rows | Set-Content $table
Write-Host "table=$table runs=$launches failed=$failed short=$short elapsed_s=$elapsed result=$result"
if ($result -eq 0) { exit 0 }
exit 2
