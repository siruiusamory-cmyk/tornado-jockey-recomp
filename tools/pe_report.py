import math
import struct
import sys


def entropy(b):
    if not b:
        return 0.0
    counts = [0] * 256
    for x in b:
        counts[x] += 1
    n = len(b)
    return -sum(c / n * math.log2(c / n) for c in counts if c)


def report(path):
    with open(path, "rb") as f:
        d = f.read()
    e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
    assert d[e_lfanew : e_lfanew + 4] == b"PE\0\0", "no PE signature"
    coff = e_lfanew + 4
    (nsec,) = struct.unpack_from("<H", d, coff + 2)
    (timestamp,) = struct.unpack_from("<I", d, coff + 4)
    (optsz,) = struct.unpack_from("<H", d, coff + 16)
    opt = coff + 20
    (magic,) = struct.unpack_from("<H", d, opt)
    (ep,) = struct.unpack_from("<I", d, opt + 16)
    (ib,) = struct.unpack_from("<I", d, opt + 28)
    link = (d[opt + 2], d[opt + 3])
    ddirs = opt + (96 if magic == 0x10B else 112)
    sec_rva, sec_size = struct.unpack_from("<II", d, ddirs + 8 * 4)
    imp_rva, imp_size = struct.unpack_from("<II", d, ddirs + 8 * 1)
    sect = opt + optsz
    print(f"## {path}")
    print(
        f"file_size={len(d)} e_lfanew=0x{e_lfanew:X} machine_dirs={nsec} "
        f"timestamp=0x{timestamp:08X} linker={link[0]}.{link[1]} "
        f"image_base=0x{ib:X} entry_rva=0x{ep:X} opt_magic=0x{magic:X}"
    )
    print(f"cert_dir rva=0x{sec_rva:X} size={sec_size} (file offset = rva for these builds)")
    print(f"import_dir rva=0x{imp_rva:X} size={imp_size}")
    end = 0
    print("| section | vsize | raw_off | raw_size | entropy | flags |")
    for i in range(nsec):
        o = sect + i * 40
        name = d[o : o + 8].rstrip(b"\0").decode("latin1")
        vs, _va, rs, ro = struct.unpack_from("<IIII", d, o + 8)
        (chars,) = struct.unpack_from("<I", d, o + 36)
        body = d[ro : ro + rs]
        end = max(end, ro + rs)
        fl = "".join(
            [
                "X" if chars & 0x20000000 else "-",
                "R" if chars & 0x40000000 else "-",
                "W" if chars & 0x80000000 else "-",
            ]
        )
        print(f"| {name} | 0x{vs:X} | 0x{ro:X} | {rs} | {entropy(body):.2f} | {fl} |")
    endloc = sec_rva if sec_size else len(d)
    print(f"last_section_raw_end=0x{end:X} overlay_end=0x{endloc:X} overlay_size={endloc - end}")
    print(f"overlay_entropy={entropy(d[end:endloc]):.2f}" if endloc > end else "no overlay")
    print()


for p in sys.argv[1:]:
    report(p)
