
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$total = [Diagnostics.Stopwatch]::StartNew()

function Invoke-Step([string]$Name, [scriptblock]$Body) {
    $global:LASTEXITCODE = 0
    $t = [Diagnostics.Stopwatch]::StartNew()
    & $Body
    $code = $LASTEXITCODE
    $t.Stop()
    '{0,-26} {1,6:n1}s' -f $Name, $t.Elapsed.TotalSeconds
    if ($code) { throw "$Name failed (exit $code)" }
}

Invoke-Step 'ruff check tools' { python -m ruff check tools }
Invoke-Step 'ruff format --check' { python -m ruff format --check tools }
Invoke-Step 'link check' { python tools\check_links.py }
Invoke-Step 'unit tests' { python -B -m unittest discover -s tools -p 'test_*.py' }
Invoke-Step 'symbols check' { python -B tools\check_symbols.py }

$total.Stop()
'check total                {0,6:n1}s' -f $total.Elapsed.TotalSeconds
