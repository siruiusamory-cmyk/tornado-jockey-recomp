import struct
import sys

WANT_IAT = "--iat" in sys.argv
if WANT_IAT:
    sys.argv.remove("--iat")
p = sys.argv[1] if len(sys.argv) > 1 else r"C:\TornadoJockey\intake\unwrapped\Tornado.exe"
with open(p, "rb") as f:
    d = f.read()
e = struct.unpack_from("<I", d, 0x3C)[0]
(nsec,) = struct.unpack_from("<H", d, e + 6)
(optsz,) = struct.unpack_from("<H", d, e + 20)
opt = e + 24
secs = []
for i in range(nsec):
    o = opt + optsz + i * 40
    vs, va, rs, ro = struct.unpack_from("<IIII", d, o + 8)
    secs.append((va, vs, ro, rs))


def o2(rva):
    for va, vs, ro, rs in secs:
        if va <= rva < va + max(vs, rs):
            return ro + (rva - va)
    return None


(image_base,) = struct.unpack_from("<I", d, opt + 28)
(imp,) = struct.unpack_from("<I", d, opt + 96 + 8)
o = o2(imp)
while True:
    ilt, _ts, _fc, nr, iat = struct.unpack_from("<IIIII", d, o)
    if not nr and not iat:
        break
    dll = d[o2(nr) :].split(b"\0")[0].decode("latin1")
    names = []
    q = o2(ilt or iat)
    index = 0
    while True:
        (ent,) = struct.unpack_from("<I", d, q)
        if not ent:
            break
        if ent & 0x80000000:
            names.append(f"ord{ent & 0xFFFF}")
        else:
            raw = d[o2(ent + 2) :].split(b"\0")[0]
            names.append(raw.decode("ascii") if all(32 <= c < 127 for c in raw) else "<scrambled>")
        if WANT_IAT:
            print(f"{dll} {names[-1]} {image_base + iat + 4 * index:08X}")
        index += 1
        q += 4
    if not WANT_IAT:
        print(f"{dll} {len(names)} " + " ".join(names))
    o += 20
