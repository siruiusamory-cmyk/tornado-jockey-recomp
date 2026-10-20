
param(
    [string]$Exe = (Join-Path (Split-Path $PSScriptRoot -Parent) 'game\Tornado.exe'),
    [string]$WorkDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'game'),
    [switch]$NoPatch,
    [switch]$Trace,
    [switch]$Diag,
    [int]$Wait = 0
)

$env:TJ_GAME_DIR = 'self'
$Slot = [IntPtr]0x6ACB34
$Name = 'FreeEnvironmentStringsA'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class Shim {
    [StructLayout(LayoutKind.Sequential)]
    public struct STARTUPINFO {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    public static extern bool CreateProcessA(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit,
        int flags, IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool WriteProcessMemory(IntPtr p, IntPtr addr, byte[] buf, IntPtr n, out IntPtr written);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool ReadProcessMemory(IntPtr p, IntPtr addr, byte[] buf, IntPtr n, out IntPtr read);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool VirtualProtectEx(IntPtr p, IntPtr addr, IntPtr n, int newprot, out int oldprot);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint ResumeThread(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetExitCodeProcess(IntPtr h, out uint code);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool TerminateProcess(IntPtr h, uint code);
    [StructLayout(LayoutKind.Sequential)]
    public struct DEBUG_EVENT { public int code; public int pid; public int tid;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 176)] public byte[] u; }
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool WaitForDebugEvent(ref DEBUG_EVENT ev, uint ms);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool ContinueDebugEvent(int pid, int tid, int status);
}
'@

$si = New-Object Shim+STARTUPINFO
$si.cb = [Runtime.InteropServices.Marshal]::SizeOf($si)
$pi = New-Object Shim+PROCESS_INFORMATION
$CREATE_SUSPENDED = 0x4
if ($Trace) { $CREATE_SUSPENDED = 0x6 }
$ok = [Shim]::CreateProcessA($Exe, $null, [IntPtr]::Zero, [IntPtr]::Zero, $false, $CREATE_SUSPENDED,
                             [IntPtr]::Zero, $WorkDir, [ref]$si, [ref]$pi)
if (-not $ok) { throw "CreateProcessA failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
"PID=$($pi.dwProcessId)"

if (-not $NoPatch) {
    $probe = New-Object byte[] 24
    $n = [IntPtr]::Zero
    if (-not [Shim]::ReadProcessMemory($pi.hProcess, $Slot, $probe, [IntPtr]24, [ref]$n)) {
        throw "ReadProcessMemory failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    $before = [Text.Encoding]::ASCII.GetString($probe, 0, 7)
    if ($before -ne 'FreeEnv') { throw "unexpected bytes before patch at 0x6ACB34: '$before'" }

    $old = 0
    if (-not [Shim]::VirtualProtectEx($pi.hProcess, $Slot, [IntPtr]24, 0x04, [ref]$old)) {
        throw "VirtualProtectEx failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    $bytes = [Text.Encoding]::ASCII.GetBytes($Name + [char]0)
    $w = [IntPtr]::Zero
    $ok = [Shim]::WriteProcessMemory($pi.hProcess, $Slot, $bytes, [IntPtr]$bytes.Length, [ref]$w)
    [void][Shim]::VirtualProtectEx($pi.hProcess, $Slot, [IntPtr]24, $old, [ref]$old)
    if (-not $ok -or $w.ToInt64() -ne $bytes.Length) {
        throw "WriteProcessMemory failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    $check = New-Object byte[] 24
    [void][Shim]::ReadProcessMemory($pi.hProcess, $Slot, $check, [IntPtr]24, [ref]$n)
    "patched 0x6ACB34 <- '$([Text.Encoding]::ASCII.GetString($check,0,23))' ($($bytes.Length) bytes written)"
}

[void][Shim]::ResumeThread($pi.hThread)
[void][Shim]::CloseHandle($pi.hThread)

if ($Trace) {
    $de = New-Object Shim+DEBUG_EVENT
    $de.u = New-Object byte[] 176
    $deadline = (Get-Date).AddMilliseconds($Wait)
    while ((Get-Date) -lt $deadline) {
        if (-not [Shim]::WaitForDebugEvent([ref]$de, 500)) { continue }
        $status = 0x00010002
        switch ($de.code) {
            1  { $c = [BitConverter]::ToUInt32($de.u, 4)
                 $a = [BitConverter]::ToUInt64($de.u, 20)
                 $where = if ($a -ge 0x400000 -and $a -lt 0x700000) { "GAME+0x$('{0:X}' -f ($a - 0x400000))" } else { "0x$('{0:X16}' -f $a)" }
                 "exception code=0x$('{0:X8}' -f $c) at $where first_chance=$([BitConverter]::ToUInt32($de.u, 156))"
                 $status = -2147418111 }   # DBG_EXCEPTION_NOT_HANDLED, let the app's own handler run
            6  { "load-dll base=0x{0:X16}" -f [BitConverter]::ToUInt64($de.u, 12) }
            7  { "unload-dll base=0x{0:X16}" -f [BitConverter]::ToUInt64($de.u, 0) }
            5  { "exit code=0x{0:X8}" -f [BitConverter]::ToUInt32($de.u, 4) }
            default { "event code=$($de.code)" }
        }
        [void][Shim]::ContinueDebugEvent($de.pid, $de.tid, $status)
        if ($de.code -eq 5) { break }
    }
} elseif ($Wait -gt 0) {
    if ([Shim]::WaitForSingleObject($pi.hProcess, [uint32]$Wait) -eq 0) {
        $code = 0
        [void][Shim]::GetExitCodeProcess($pi.hProcess, [ref]$code)
        "exited within ${Wait}ms, exit_code=0x$('{0:X8}' -f $code)"
    } else { "still running after ${Wait}ms" }
}
[void][Shim]::CloseHandle($pi.hProcess)
