
param(
    [int]$Take = 2950,
    [int]$Gap = 600,
    [int]$TimeoutSeconds = 180,
    [string]$Replay = 'tools\fixtures\replay-snapshot.txt',
    [string]$Terms = '6D09C0+1C,6D09C0+4,6D09C0+8,6D09D4+E5C,6D09D4+E60,6D15E0+EC98,6C8910,6D15E0+EC94@6D09D4+1E74.F20,6D15E0+EC94@6D09D4+1E74.F24,6D15E0+EC94@6D09D4+1E74.F28,6D15E0+EC94@6D09D4+1E74.F30,6D15E0+EC94@6D09D4+1E74.28,6D15E0+EC94@6D09D4+1E74.2C,6D15E0+EC94@6D09D4+1E74.30,6D15E0+EC94@6D09D4+1E74.FDC,6D15E0+EC94@6D09D4+1E74.FE0'
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force evidence | Out-Null
$stamp = Get-Date -Format 'HHmmss'
$day = Get-Date -Format 'yyyyMMdd'
$end = $Take + 2 * $Gap

$env:TJ_REPLAY = (Resolve-Path -LiteralPath $Replay).Path
$env:TJ_PROBE = $Terms
$env:TJ_FIXED_CURSOR = '1'
$env:TJ_CENSUS = "$end"
$env:TJ_SNAPSHOT = "$Take"
Remove-Item Env:TJ_FAITHFUL -ErrorAction SilentlyContinue
$name = "snap$stamp"

$out = pwsh -NoProfile -File tools\proxy-run.ps1 -Name $name -CaptureAt $end -TimeoutSeconds $TimeoutSeconds 2>&1
$code = $LASTEXITCODE
$out | Set-Content "evidence\snapshot-gate-$day-$stamp-run.txt"
$out | Where-Object { $_ -match 'foreground_samples|identical_to_before|dropin_present|capture_seen|Exception' }
if ($code) { throw "snapshot run failed (exit $code), see evidence\snapshot-gate-$day-$stamp-run.txt" }

$log = "evidence\proxy-log-$day-$name.txt"
$takeRow = Select-String -LiteralPath $log -Pattern '^# snapshot take ' | ForEach-Object { $_.Line }
$restoreRow = Select-String -LiteralPath $log -Pattern '^# snapshot restore ' | ForEach-Object { $_.Line }
$takeRow
$restoreRow
if (-not $takeRow -or -not $restoreRow) { throw "no snapshot rows in $log" }
$skipped = if ($restoreRow -match 'skipped=(\d+)') { [int]$Matches[1] } else { -1 }

$pass1 = @{}
$pass2 = @{}
foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $log))) {
    if ($line -notmatch '^# probe (\S+) (\d+) (\S+)$') { continue }
    $term = $Matches[1]
    $frame = [int]$Matches[2]
    if ($frame -gt $Take -and $frame -le ($Take + $Gap)) { $pass1["$term $frame"] = $Matches[3] }
    if ($frame -gt ($Take + $Gap) -and $frame -le $end) { $pass2["$term $($frame - $Gap)"] = $Matches[3] }
}
$same = 0
$differ = 0
$firstRow = ''
$byTerm = @{}
foreach ($key in ($pass1.Keys | Sort-Object)) {
    if (-not $pass2.ContainsKey($key)) { $firstRow = "missing row in pass 2: $key"; ++$differ; continue }
    if ($pass2[$key] -ne $pass1[$key]) {
        if (-not $firstRow) { $firstRow = "$key pass1=$($pass1[$key]) pass2=$($pass2[$key])" }
        $term = $key.Split(' ')[0]
        $byTerm[$term] = 1 + $byTerm[$term]
        ++$differ
    } else {
        ++$same
    }
}
"rows compared=$($pass1.Count) equal=$same differ=$differ"
foreach ($term in ($byTerm.Keys | Sort-Object)) { "  $term differs on $($byTerm[$term]) row(s)" }

$first = $null
$second = $null
foreach ($line in (Select-String -LiteralPath $log -Pattern '^# snapshot frame frame=(\d+) hash=(\S+) pass=(\w+)$' | ForEach-Object { $_.Line })) {
    $null = $line -match 'frame=(\d+) hash=(\S+) pass=(\w+)'
    if ($Matches[3] -eq 'first' -and [int]$Matches[1] -eq ($Take + $Gap)) { $first = $Matches[2] }
    if ($Matches[3] -eq 'second' -and [int]$Matches[1] -eq $end) { $second = $Matches[2] }
}
"frame hash at $($Take + $Gap) pass1=$first pass2=$second"
if (-not $first -or -not $second) { throw "the two frame hashes for the gap were not logged" }

if (-not $differ -and $first -eq $second -and $skipped -eq 0) {
    "snapshot ok: $($pass1.Count) rows and the frame at $($Take + $Gap) reproduced from Present $Take"
    exit 0
}
"snapshot differs: first row $firstRow"
if ($skipped) { "restore skipped $skipped region(s)" }
exit 1
