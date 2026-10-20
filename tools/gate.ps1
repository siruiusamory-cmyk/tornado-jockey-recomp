
param([switch]$Oracle, [switch]$Bless, [switch]$BlessGameplay, [string]$Frame = '',
      [switch]$Step, [string]$StepReplay = '', [string]$StepPerturb = '')

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force evidence | Out-Null
$record = [Collections.Generic.List[string]]::new()
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$record.Add("gate $stamp")
$total = [Diagnostics.Stopwatch]::StartNew()
$golden = 'evidence/oracle-menu.sha256'
$goldenGameplay = 'evidence/oracle-gameplay.sha256'

function Test-OracleFrame([string]$Path, [string]$Golden) {
    if (-not (Test-Path -LiteralPath $Golden)) { throw "no golden $Golden" }
    if (-not (Test-Path -LiteralPath $Path)) { throw "missing frame $Path" }
    $want = (Get-Content $Golden).Trim()
    $have = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    "frame $Path sha256 $have golden $want"
    if ($have -ne $want) { throw "frame $Path does not match the golden $Golden" }
}

function Invoke-Step([string]$Name, [scriptblock]$Body) {
    $global:LASTEXITCODE = 0
    $t = [Diagnostics.Stopwatch]::StartNew()
    try {
        & $Body
        $code = $LASTEXITCODE
    } catch {
        $code = 1
    }
    $t.Stop()
    $line = '{0,-26} {1,6:n1}s  {2}' -f $Name, $t.Elapsed.TotalSeconds, $(if ($code) { 'FAIL' } else { 'ok' })
    Write-Host $line
    $record.Add($line)
    if ($code) {
        $total.Stop()
        $record.Add(('gate total                 {0,6:n1}s' -f $total.Elapsed.TotalSeconds))
        $record | Set-Content "evidence/gate-$stamp.txt"
        throw "$Name failed"
    }
}

Invoke-Step 'stage 1 oracle (frame)' {
    $png = 'evidence/oracle-main-menu-20261017.png'
    $want = '4B31C0F50D347A43E7984EC8195B6E5078EFD406896233DC80D48AC269216062'
    if (-not (Test-Path $png)) { throw "missing recorded frame $png" }
    if ((Get-FileHash $png -Algorithm SHA256).Hash -ne $want) { throw "recorded frame hash drifted: $png" }
}

Invoke-Step 'unwrap determinism' {
    foreach ($n in 1, 2) {
        python -B tools/unwrap.py | Out-Null
        if ($LASTEXITCODE) { throw "unwrap build $n failed" }
        $h = (Get-FileHash intake/repaired/Tornado.exe -Algorithm SHA256).Hash
        if ($h -ne '9D2A7C67B45A2E07893A11F9905A6FBD66A8E8F61091A8C3EA24DBF1182D55E1') {
            throw "unwrap build $n hash $h is not the recorded image"
        }
        $record.Add("unwrap build $n $h")
    }
}

Invoke-Step 'stage 2 oracle (census)' { python -B tools/d3d8_census.py --check }

Invoke-Step 'vulture --min-confidence 80' { python -m vulture --min-confidence 80 tools }

$oracleFailed = $false
function Try-Step([string]$Name, [scriptblock]$Body) {
    try { Invoke-Step $Name $Body } catch { $script:oracleFailed = $true }
}
if ($Oracle -or $Bless -or $BlessGameplay) {
    Invoke-Step 'oracle capture (menu)' {
        $env:TJ_D3D8_ARGS = '0'
        $env:TJ_FIXED_CURSOR = '1'
        $env:TJ_FAITHFUL = '1'
        try { $run = pwsh -NoProfile -File tools\proxy-run.ps1 -Name oracle 2>&1 }
        finally { Remove-Item Env:TJ_FIXED_CURSOR, Env:TJ_FAITHFUL -ErrorAction SilentlyContinue }
        $code = $LASTEXITCODE
        $run | Set-Content "evidence/oracle-run-$stamp.txt"
        $run | Where-Object {
            $_ -match 'capture_log|present_seen|capture_seen|dialogs_dismissed|identical_to_before|dropin_removed|dropin_present|foreground|Exception'
        }
        if ($code) { throw "proxy-run failed ($code)" }
    }
    $capture = "evidence\proxy-capture-$(Get-Date -Format yyyyMMdd)-oracle.bmp"
    if ($Bless) {
        Invoke-Step 'oracle bless (menu)' {
            if (-not (Test-Path -LiteralPath $capture)) { throw "missing capture $capture" }
            (Get-FileHash -LiteralPath $capture -Algorithm SHA256).Hash | Set-Content $golden
            "golden $golden $((Get-Content $golden).Trim())"
        }
    }
    Try-Step 'oracle frame (menu)' { Test-OracleFrame $capture $golden }

    Invoke-Step 'oracle capture (gameplay)' {
        $env:TJ_D3D8_ARGS = '0'
        $env:TJ_REPLAY = "$PWD\tools\fixtures\replay-menu-level1.txt"
        $env:TJ_FIXED_CURSOR = '1'
        $env:TJ_FAITHFUL = '1'
        try {
            $run = pwsh -NoProfile -File tools\proxy-run.ps1 -Name gameplay -CaptureAt 3700 -TimeoutSeconds 150 2>&1
        } finally {
            Remove-Item Env:TJ_REPLAY, Env:TJ_FIXED_CURSOR, Env:TJ_FAITHFUL -ErrorAction SilentlyContinue
        }
        $code = $LASTEXITCODE
        $run | Set-Content "evidence/oracle-run-$stamp-gameplay.txt"
        $run | Where-Object {
            $_ -match 'capture_log|present_seen|capture_seen|dialogs_dismissed|identical_to_before|dropin_removed|dropin_present|foreground|Exception'
        }
        if ($code) { throw "proxy-run failed ($code)" }
    }
    $gameplay = "evidence\proxy-capture-$(Get-Date -Format yyyyMMdd)-gameplay.bmp"
    if ($BlessGameplay) {
        Invoke-Step 'oracle bless (gameplay)' {
            if (-not (Test-Path -LiteralPath $gameplay)) { throw "missing capture $gameplay" }
            (Get-FileHash -LiteralPath $gameplay -Algorithm SHA256).Hash | Set-Content $goldenGameplay
            "golden $goldenGameplay $((Get-Content $goldenGameplay).Trim())"
        }
    }
    Try-Step 'oracle frame (gameplay)' { Test-OracleFrame $gameplay $goldenGameplay }
    if ($oracleFailed) { throw 'one or more oracle frames do not match their golden' }
}
if ($Frame) { Invoke-Step 'oracle frame (file)' { Test-OracleFrame $Frame $golden } }

if ($Step) {
    Invoke-Step 'fixed step (5 runs)' {
        $stepArgs = @()
        if ($StepReplay) { $stepArgs += @('-Replay', $StepReplay) }
        if ($StepPerturb) { $stepArgs += @('-Perturb', $StepPerturb) }
        $run = pwsh -NoProfile -File tools\step-gate.ps1 @stepArgs 2>&1
        $run | Where-Object { $_ -match '^step-gate|first difference|run \d+:|Exception' }
        if ($LASTEXITCODE) { throw 'step-gate failed' }
    }
}

$total.Stop()
$line = 'gate total                 {0,6:n1}s' -f $total.Elapsed.TotalSeconds
Write-Host $line
$record.Add($line)
$record | Set-Content "evidence/gate-$stamp.txt"
