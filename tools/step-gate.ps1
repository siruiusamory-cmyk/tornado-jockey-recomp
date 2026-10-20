
param(
    [int]$Runs = 5,
    [string]$Replay = 'tools\fixtures\replay-menu-level1.txt',
    [int]$CaptureAt = 3700,
    [int]$TimeoutSeconds = 200,
    [string]$Terms = '6D09C0+1C,6D09C0+4,6D09C0+8,6D15E0+EC94@6D09D4+1E74.F20,6D15E0+EC94@6D09D4+1E74.F24,6D15E0+EC94@6D09D4+1E74.F28,6D15E0+EC94@6D09D4+1E74.F30',
    [string]$Perturb = ''
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force evidence | Out-Null
$stamp = Get-Date -Format 'HHmmss'
$day = Get-Date -Format 'yyyyMMdd'
$script:firstRows = $null

function Get-Rows([string]$Log) {
    $rows = [Collections.Generic.List[string]]::new()
    foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $Log))) {
        if ($line -match '^# probe (\S+) (\d+) (\S+)$' -and [int]$Matches[2] -le $CaptureAt) {
            $rows.Add("$($Matches[1]) $($Matches[2]) $($Matches[3])")
        }
    }
    return $rows
}

function Get-StepRows([string]$Log) {
    $rows = [Collections.Generic.List[object]]::new()
    foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $Log))) {
        if ($line -match '^# step count frame=(\d+) proxy=(\d+) sim=(\d+)$' -and [int]$Matches[1] -le $CaptureAt) {
            $rows.Add([pscustomobject]@{ Frame = [int]$Matches[1]; Proxy = [int]$Matches[2]; Sim = [int]$Matches[3] })
        }
    }
    return $rows
}

function Get-Sha([string]$Text) {
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($Text)))
}

for ($i = 1; $i -le $Runs; $i++) {
    $fixture = if ($Perturb -and $i -eq 3) { $Perturb } else { $Replay }
    $name = "stepg$i-$stamp"
    $env:TJ_REPLAY = (Resolve-Path -LiteralPath $fixture).Path
    $env:TJ_PROBE = $Terms
    $env:TJ_FIXED_CURSOR = '1'
    Remove-Item Env:TJ_FAITHFUL -ErrorAction SilentlyContinue

    for ($attempt = 1; $attempt -le 2; $attempt++) {
        $out = pwsh -NoProfile -File tools\proxy-run.ps1 -Name $name -CaptureAt $CaptureAt -TimeoutSeconds $TimeoutSeconds 2>&1
        $code = $LASTEXITCODE
        if (-not $code) { break }
        Write-Host "step-gate run $i attempt $attempt did not finish (exit $code)"
    }
    $runLog = "evidence\step-gate-$day-$stamp-run$i.txt"
    $out | Set-Content $runLog
    if ($code) {
        $out | Where-Object { $_ -match 'Exception|exited|failed' } | Select-Object -Last 3 | ForEach-Object { Write-Host "  $_" }
        throw "step-gate run $i failed (exit $code), see $runLog"
    }
    $log = "evidence\proxy-log-$day-$name.txt"
    $rows = Get-Rows $log
    $steps = Get-StepRows $log
    $maxDiff = 0
    foreach ($row in $steps) { $maxDiff = [Math]::Max($maxDiff, [Math]::Abs($row.Proxy - $row.Sim)) }
    if ($steps.Count -eq 0) { throw "step-gate run $i logged no step-count row, see $log" }
    if ($maxDiff -ne 0) { throw "step-gate run ${i}: proxy count and [sim+0x1C] differ by $maxDiff" }

    $previous = $null
    $short = 0
    foreach ($row in $steps) {
        if ($previous -and ($row.Frame - $previous.Frame) -eq 60 -and ($row.Proxy - $previous.Proxy) -ne 60) { $short++ }
        $previous = $row
    }
    if ($short) { throw "step-gate run ${i}: $short sample interval(s) did not run 60 steps" }
    $sha = Get-Sha ($rows -join "`n")
    Write-Host ('step-gate run {0} rows={1} step_rows={2} max_diff={3} sha256={4}' -f $i, $rows.Count, $steps.Count, $maxDiff, $sha)

    if ($i -eq 1) {
        $script:firstRows = $rows
        continue
    }
    if ($sha -ne (Get-Sha ($script:firstRows -join "`n"))) {
        for ($r = 0; $r -lt [Math]::Min($rows.Count, $script:firstRows.Count); $r++) {
            if ($rows[$r] -ne $script:firstRows[$r]) {
                Write-Host "  first difference at row $r"
                Write-Host "  run 1: $($script:firstRows[$r])"
                Write-Host "  run ${i}: $($rows[$r])"
                break
            }
        }
        throw "step-gate run $i differs from run 1 (rows $($rows.Count) against $($script:firstRows.Count))"
    }
}
Write-Host "step-gate ok: $Runs runs, identical rows to frame $CaptureAt"
exit 0
