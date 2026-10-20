import ctypes
import ctypes.wintypes as wt
import os
import struct
import subprocess
import sys
import time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
TH32CS_SNAPPROCESS, PROCESS_VM_READ, PROCESS_QUERY_INFORMATION, PROCESS_TERMINATE = (
    0x2,
    0x10,
    0x400,
    0x1,
)


class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", wt.DWORD),
        ("cntUsage", wt.DWORD),
        ("th32ProcessID", wt.DWORD),
        ("th32DefaultHeapID", ctypes.c_void_p),
        ("th32ModuleID", wt.DWORD),
        ("cntThreads", wt.DWORD),
        ("th32ParentProcessID", wt.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wt.DWORD),
        ("szExeFile", ctypes.c_char * 260),
    ]


def processes():
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    e = PROCESSENTRY32()
    e.dwSize = ctypes.sizeof(e)
    ok = k32.Process32First(snap, ctypes.byref(e))
    while ok:
        yield e.th32ProcessID, e.th32ParentProcessID, e.szExeFile.decode(errors="replace")
        ok = k32.Process32Next(snap, ctypes.byref(e))
    k32.CloseHandle(snap)


def read(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    return buf.raw[: n.value]


def sections(pe_bytes):
    pe = struct.unpack_from("<I", pe_bytes, 0x3C)[0]
    count = struct.unpack_from("<H", pe_bytes, pe + 6)[0]
    opt = pe + 24
    base, size_img = (
        struct.unpack_from("<I", pe_bytes, opt + 28)[0],
        struct.unpack_from("<I", pe_bytes, opt + 56)[0],
    )
    table = opt + struct.unpack_from("<H", pe_bytes, pe + 20)[0]
    out = []
    for i in range(count):
        o = table + 40 * i
        vsize, va, rsize, rptr = struct.unpack_from("<IIII", pe_bytes, o + 8)
        out.append((pe_bytes[o : o + 8].rstrip(b"\0").decode(), va, vsize, rptr, rsize))
    return base, size_img, out


def main():
    game, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(game, "Tornado.exe"), "rb") as f:
        stub = f.read()
    base, size_img, secs = sections(stub)
    text = secs[0]
    launcher = subprocess.Popen([os.path.join(game, "Tornado-WT.exe")], cwd=game)
    print("launcher pid", launcher.pid)
    ours, target, deadline = {launcher.pid}, None, time.time() + 120
    while time.time() < deadline:
        for pid, ppid, name in processes():
            if ppid in ours and pid not in ours:
                ours.add(pid)
                print("child", pid, name)
            if pid in ours and name.lower() == "tornado.exe":
                target = pid
        if target:
            h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, target)
            probe = read(h, base + text[1], 0x10000)
            if probe and probe.count(0) < len(probe) * 0.9:
                time.sleep(10)
                image = read(h, base, size_img)
                k32.CloseHandle(h)
                break
            k32.CloseHandle(h)
        time.sleep(0.5)
    else:
        image = None
    for pid in ours:
        h = k32.OpenProcess(PROCESS_TERMINATE, False, pid)
        if h:
            k32.TerminateProcess(h, 0)
            k32.CloseHandle(h)
    if not image:
        sys.exit(f"no decrypted Tornado.exe seen (target={target}, pids={sorted(ours)})")
    with open(os.path.join(out, "Tornado.mem.bin"), "wb") as f:
        f.write(image)
    rebuilt = bytearray(stub)
    for name, va, vsize, rptr, rsize in secs:
        rebuilt[rptr : rptr + rsize] = image[va : va + rsize].ljust(rsize, b"\0")
        print(f"{name:8} va=0x{va:X} raw={rsize} zero-bytes={image[va : va + rsize].count(0)}")
    with open(os.path.join(out, "Tornado.dumped.exe"), "wb") as f:
        f.write(bytes(rebuilt))
    print("read", len(image), "of", size_img, "bytes from pid", target)


if __name__ == "__main__":
    main()
