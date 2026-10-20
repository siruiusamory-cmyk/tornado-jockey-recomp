
param([string]$Name = 'run1', [int]$CaptureAt = 500, [switch]$Focus, [switch]$Foreground,
      [int]$TimeoutSeconds = 60, [switch]$Trace)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$root = (Get-Location).Path
New-Item -ItemType Directory -Force (Join-Path $root 'evidence') | Out-Null
$game = Join-Path $root 'game'
$dropin = Join-Path $game 'd3d8.dll'
$stamp = Get-Date -Format 'yyyyMMdd'
$log = Join-Path $root "evidence\proxy-log-$stamp-$Name.txt"
$shot = Join-Path $root "evidence\proxy-main-menu-$stamp-$Name.png"
$capture = Join-Path $root "evidence\proxy-capture-$stamp-$Name.bmp"
$windows = Join-Path $root "evidence\proxy-windows-$stamp-$Name.txt"
$before = Join-Path $root "build\game-hashes-$Name-before.txt"
$after = Join-Path $root "build\game-hashes-$Name-after.txt"

function Get-TreeHashes([string]$dir) {
    $prefix = (Get-Item -LiteralPath $dir).FullName.Length + 1
    Get-ChildItem -LiteralPath $dir -Recurse -File -Force |
        ForEach-Object {
            '{0} {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash,
            $_.FullName.Substring($prefix)
        } | Sort-Object
}

function Get-FamilyPids([int]$Root) {
    $snapshot = Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId
    $family = @($Root)
    do {
        $added = 0
        foreach ($p in $snapshot) {
            if ($family -contains [int]$p.ParentProcessId -and $family -notcontains [int]$p.ProcessId) {
                $family += [int]$p.ProcessId
                $added++
            }
        }
    } while ($added)
    return $family
}

function Get-GameWindow([int]$Owner) {
    $fallback = [IntPtr]::Zero
    foreach ($h in [ProxyRunWin]::AllTopLevel()) {
        if ([int][ProxyRunWin]::PidOf($h) -ne $Owner) { continue }
        if ([ProxyRunWin]::Class($h) -eq 'Tornado Jockey') { return $h }
        if ($fallback -eq [IntPtr]::Zero) { $fallback = $h }
    }
    return $fallback
}

function Get-ForegroundOwner {
    $h = [ProxyRunWin]::GetForegroundWindow()
    $ownerId = [int][ProxyRunWin]::PidOf($h)
    $image = if ($ownerId) { (Get-Process -Id $ownerId -ErrorAction SilentlyContinue).ProcessName } else { '' }
    "pid=$ownerId image=$image class=$([ProxyRunWin]::Class($h)) title=$([ProxyRunWin]::Title($h))"
}

if (-not ('ProxyRunWin' -as [type])) {
    Add-Type -AssemblyName System.Drawing
    Add-Type @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class ProxyRunWin {
    public delegate bool EnumFn(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumFn f, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumFn f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, ref uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint attach, uint attachTo, bool on);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L; public int T; public int R; public int B; }
    // Attaching this thread to the game window's thread lifts the foreground lock that
    // refuses SetForegroundWindow from a background process. Only the game's hwnd is ever
    // passed in, and no input is synthesized.
    public static void FocusGame(IntPtr hwnd) {
        uint self = GetCurrentThreadId();
        uint target = TidOf(hwnd);
        AttachThreadInput(self, target, true);
        ShowWindow(hwnd, 9);
        SetWindowPos(hwnd, new IntPtr(-1), 0, 0, 0, 0, 0x0003);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        AttachThreadInput(self, target, false);
    }
    public static bool Visible(IntPtr h) { return IsWindowVisible(h) && Class(h) != "IME" && Class(h) != "MSCTFIME UI"; }
    public static List<IntPtr> AllTopLevel() {
        var found = new List<IntPtr>();
        EnumWindows(delegate(IntPtr h, IntPtr l) { found.Add(h); return true; }, IntPtr.Zero);
        return found;
    }
    public static uint PidOf(IntPtr h) {
        uint owner = 0;
        GetWindowThreadProcessId(h, ref owner);
        return owner;
    }
    public static uint TidOf(IntPtr h) {
        uint owner = 0;
        return GetWindowThreadProcessId(h, ref owner);
    }
    public static int CountButtons(IntPtr h) {
        int count = 0;
        EnumChildWindows(h, delegate(IntPtr c, IntPtr l) {
            var cls = new StringBuilder(64);
            GetClassName(c, cls, 64);
            if (cls.ToString() == "Button") count++;
            return true;
        }, IntPtr.Zero);
        return count;
    }
    public static string Class(IntPtr h) {
        var cls = new StringBuilder(256);
        GetClassName(h, cls, 256);
        return cls.ToString();
    }
    public static string Title(IntPtr h) {
        var text = new StringBuilder(256);
        GetWindowText(h, text, 256);
        return text.ToString();
    }
    public static bool ClickOk(IntPtr dialog) {
        IntPtr button = IntPtr.Zero;
        EnumChildWindows(dialog, delegate(IntPtr h, IntPtr l) {
            var cls = new StringBuilder(64);
            GetClassName(h, cls, 64);
            if (cls.ToString() != "Button") return true;
            if (button == IntPtr.Zero) button = h;
            var text = Title(h).Replace("&", "").Trim().ToLowerInvariant();
            if (text == "ok" || text == "close" || text == "yes" || text == "continue") { button = h; return false; }
            return true;
        }, IntPtr.Zero);
        if (button != IntPtr.Zero) { SendMessage(button, 0x00F5, IntPtr.Zero, IntPtr.Zero); return true; }
        // The engine's script errors are Internet Explorer dialog frames whose OK is
        // drawn inside the web content, so there is no button window to click.
        SendMessage(dialog, 0x0010, IntPtr.Zero, IntPtr.Zero);
        return false;
    }
}
'@
}

$busy = @(Get-CimInstance Win32_Process | Where-Object {
        $_.ExecutablePath -and
        $_.ExecutablePath.StartsWith("$game\", [StringComparison]::OrdinalIgnoreCase)
    })
if ($busy.Count) {
    $busy | ForEach-Object { "running_pid=$($_.ProcessId) image=$($_.ExecutablePath)" }
    throw "refusing to start: $($busy.Count) process(es) run from $game"
}
"no process runs from $game"

if (Test-Path -LiteralPath $dropin) { throw "refusing to start: $dropin already exists" }
"$dropin absent"

$beforeList = Get-TreeHashes $game
$beforeList | Set-Content $before
"hashed $($beforeList.Count) files into $before"

$free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
while ($free -lt 4) {
    "free_gb=$([math]::Round($free, 1)) under 4, waiting"
    Start-Sleep -Seconds 10
    $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
}
"free_gb=$([math]::Round($free, 1))"

$gamePid = 0
$identical = $false
$removed = $true
$windowRows = [ordered]@{}
try {
    Copy-Item -LiteralPath (Join-Path $root 'build\proxy\d3d8.dll') -Destination $dropin
    "dropped $dropin"

    $env:TJ_D3D8_LOG = $log
    $env:TJ_CAPTURE_AT = "$CaptureAt"
    $env:TJ_CAPTURE_OUT = $capture
    $env:TJ_SILENT_SCRIPT_ERRORS = '1'
    $background = -not $Foreground -and -not $Focus
    if ($background) { $env:TJ_BACKGROUND = '1' } else { Remove-Item Env:TJ_BACKGROUND -ErrorAction SilentlyContinue }
    $env:TJ_GAME_DIR = 'self'
    "background=$background"
    "foreground_before=$(Get-ForegroundOwner)"
    "capture_at=$CaptureAt capture_out=$capture"

    $launch = if ($Trace) {
        pwsh -NoProfile -File tools\oracle-launch.ps1 -Exe (Join-Path $game 'Tornado.exe') -WorkDir $game -Trace -Wait ($TimeoutSeconds * 1000) 2>&1
    } else {
        pwsh -NoProfile -File tools\oracle-launch.ps1 -Exe (Join-Path $game 'Tornado.exe') -WorkDir $game 2>&1
    }
    $launch
    $printed = $launch | Where-Object { $_ -match '^PID=\d+' } | Select-Object -First 1
    if (-not $printed) { throw 'the launcher printed no PID' }
    $gamePid = [int](($printed -split '=')[1])

    $run = Get-Process -Id $gamePid
    $run.PriorityClass = 'BelowNormal'
    $run.ProcessorAffinity = 0x3F
    "game_pid=$gamePid priority=$($run.PriorityClass) affinity=0x$('{0:X}' -f $run.ProcessorAffinity)"

    $started = Get-Date
    $foregroundSeen = [int][ProxyRunWin]::PidOf([ProxyRunWin]::GetForegroundWindow())

    $foregroundSamples = 0
    $foregroundGame = 0
    $tick = 0
    $dismissed = 0
    $title = ''
    $present = $false
    $presentSeen = $false
    $captured = $false

    while (((Get-Date) - $started).TotalSeconds -lt $TimeoutSeconds) {
        Start-Sleep -Milliseconds 250
        $process = Get-Process -Id $gamePid -ErrorAction SilentlyContinue
        if (-not $process) {
            if (Test-Path -LiteralPath $log) {
                $calls = @(Get-Content $log | Where-Object { $_ -like 'call *' })
                "log_call_lines=$($calls.Count)"
                $calls | Select-Object -Last 5
            }
            throw "the game exited ($([int]((Get-Date) - $started).TotalSeconds)s) exit=$($run.ExitCode)"
        }

        $foregroundPid = [int][ProxyRunWin]::PidOf([ProxyRunWin]::GetForegroundWindow())
        $foregroundSamples++
        if ($foregroundPid -eq $gamePid) { $foregroundGame++ }
        if ($foregroundPid -ne $foregroundSeen) {
            "foreground_change t=$([int]((Get-Date) - $started).TotalSeconds)s pid=$foregroundPid"
            $foregroundSeen = $foregroundPid
        }

        $tick++
        if ($tick % 4) { continue }
        $captured = Select-String -LiteralPath $log -Pattern '^# capture n=' -Quiet -ErrorAction SilentlyContinue
        if ($captured -and -not $foregroundDuring) { $foregroundDuring = Get-ForegroundOwner }

        $window = Get-GameWindow $gamePid
        if ($Focus -and $window -ne [IntPtr]::Zero) { [ProxyRunWin]::FocusGame($window) }

        $family = Get-FamilyPids $gamePid
        $dialogs = @()
        foreach ($h in [ProxyRunWin]::AllTopLevel()) {
            $owner = [int][ProxyRunWin]::PidOf($h)
            $class = [ProxyRunWin]::Class($h)
            $mine = $family -contains $owner
            if (-not ($mine -or $class -eq '#32770')) { continue }
            $key = [int64]$h
            if (-not $windowRows.Contains($key)) {
                $image = (Get-Process -Id $owner -ErrorAction SilentlyContinue).ProcessName
                $windowRows[$key] = 'hwnd=0x{0:X} pid={1} image={2} tid={3} class={4} title={5}' -f $key, $owner, $image, [ProxyRunWin]::TidOf($h), $class, [ProxyRunWin]::Title($h)
            }
            if ($mine -and $h -ne $window -and [ProxyRunWin]::Visible($h) -and
                ($class -eq '#32770' -or $class -like '*DlgFrame*' -or
                    [ProxyRunWin]::CountButtons($h) -gt 0)) {
                $dialogs += $h
            }
        }
        foreach ($dialog in $dialogs) {
            $dialogTitle = [ProxyRunWin]::Title($dialog)
            $dialogClass = [ProxyRunWin]::Class($dialog)
            $clicked = [ProxyRunWin]::ClickOk($dialog)
            $dismissed++
            "dismissed_dialog=$dismissed class='$dialogClass' title='$dialogTitle' button=$clicked"
        }
        if ($dialogs.Count) { continue }
        $title = if ($window -ne [IntPtr]::Zero) { [ProxyRunWin]::Title($window) } else { '' }
        $presentSeen = Select-String -LiteralPath $log -Pattern ' 15 Present ' -Quiet -ErrorAction SilentlyContinue
        if ($title -and ((Get-Date) - $started).TotalSeconds -ge 20 -and $presentSeen -and
            ($captured -or $CaptureAt -le 0)) {
            $present = $true
            break
        }
    }
    if ($Focus) {
        $foreground = [ProxyRunWin]::GetForegroundWindow()
        "foreground_pid=$([int][ProxyRunWin]::PidOf($foreground))"
        "foreground_is_game=$(([int][ProxyRunWin]::PidOf($foreground)) -eq $gamePid)"
    }
    "present_seen=$presentSeen"
    "capture_seen=$captured"
    if ($foregroundDuring) { "foreground_during=$foregroundDuring" }
    Select-String -LiteralPath $log -Pattern '^# capture (armed|device) ' |
        ForEach-Object { "capture_log=$($_.Line)" }
    if ($captured) {
        Select-String -LiteralPath $log -Pattern '^# capture n=' |
            ForEach-Object { "capture_log=$($_.Line)" }
    }
    "dialogs_dismissed=$dismissed"
    "title=$title"
    $elapsed = [int]((Get-Date) - $started).TotalSeconds
    "foreground_samples=$foregroundSamples game=$foregroundGame elapsed_s=$elapsed"
    "elapsed_s=$elapsed"

    $process = Get-Process -Id $gamePid -ErrorAction SilentlyContinue
    $hwnd = Get-GameWindow $gamePid
    if (-not $process -or $hwnd -eq [IntPtr]::Zero -or -not [ProxyRunWin]::Title($hwnd)) {
        throw 'the main window never appeared'
    }
    $process.Modules | Where-Object { $_.ModuleName -eq 'd3d8.dll' } |
        ForEach-Object { "d3d8=$($_.FileName)" }

    if ($Focus) { [ProxyRunWin]::FocusGame($hwnd) }

    if ($background) {
        "screenshot=skipped background=1"
    } else {
        Start-Sleep -Milliseconds 900
        $rect = New-Object ProxyRunWin+RECT
        [void][ProxyRunWin]::GetWindowRect($hwnd, [ref]$rect)
        $bitmap = New-Object System.Drawing.Bitmap ($rect.R - $rect.L), ($rect.B - $rect.T)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($rect.L, $rect.T, 0, 0, $bitmap.Size)
        $graphics.Dispose()
        $bitmap.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
        "screenshot=$shot"
    }
    if (Test-Path -LiteralPath $log) { "log_call_lines=$(@(Get-Content $log | Where-Object { $_ -like 'call *' }).Count)" }

    Stop-Process -Id $gamePid -Force
    "stopped_pid=$gamePid"
    "foreground_after=$(Get-ForegroundOwner)"
} finally {

    $windowRows.Values | Set-Content $windows
    "window_table=$windows rows=$($windowRows.Count)"
    if ($gamePid -and (Get-Process -Id $gamePid -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $gamePid -Force
        Wait-Process -Id $gamePid -Timeout 10 -ErrorAction SilentlyContinue
    }
    for ($try = 0; $try -lt 40 -and (Test-Path -LiteralPath $dropin); $try++) {
        try { Remove-Item -LiteralPath $dropin -Force -ErrorAction Stop } catch { Start-Sleep -Milliseconds 250 }
    }
    $removed = -not (Test-Path -LiteralPath $dropin)
    "dropin_removed=$removed"
    $afterList = Get-TreeHashes $game
    $afterList | Set-Content $after
    $identical = ($beforeList -join "`n") -eq ($afterList -join "`n")
    "identical_to_before=$identical"
    if (-not $identical) {
        Compare-Object $beforeList $afterList | ForEach-Object { "changed $($_.SideIndicator) $($_.InputObject)" }
    }
    "dropin_present=$([bool](Test-Path -LiteralPath $dropin))"
}

if (-not $identical -or -not $removed) { exit 1 }
exit 0
