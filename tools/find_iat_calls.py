import struct
import sys


def load(path):
    with open(path, "rb") as f:
        d = f.read()
    e = struct.unpack_from("<I", d, 0x3C)[0]
    (nsec,) = struct.unpack_from("<H", d, e + 6)
    (optsz,) = struct.unpack_from("<H", d, e + 20)
    opt = e + 24
    (ib,) = struct.unpack_from("<I", d, opt + 28)
    ddirs = opt + 96
    secoff = opt + optsz
    secs = []
    for i in range(nsec):
        o = secoff + i * 40
        vs, va, rs, ro = struct.unpack_from("<IIII", d, o + 8)
        secs.append((d[o : o + 8].rstrip(b"\0").decode("latin1"), va, vs, ro, rs))
    return d, ib, secs, ddirs


def rva2off(secs, rva):
    for _n, va, vs, ro, rs in secs:
        if va <= rva < va + max(vs, rs):
            return ro + (rva - va)
    return None


def cstr(d, off, n=96):
    if off is None or off >= len(d):
        return None
    raw = d[off : off + n].split(b"\0")[0]
    try:
        s = raw.decode("ascii")
    except UnicodeDecodeError:
        return None
    return s if len(s) >= 2 and s.isprintable() else None


def imports(d, ib, secs, ddirs):
    (rva,) = struct.unpack_from("<I", d, ddirs + 8)
    out = {}
    if not rva:
        return out
    o = rva2off(secs, rva)
    while True:
        ilt, _ts, _fc, name_rva, iat = struct.unpack_from("<IIIII", d, o)
        if not iat and not name_rva:
            break
        dll = cstr(d, rva2off(secs, name_rva), 32) or "?"
        names = []
        src = ilt or iat
        p = rva2off(secs, src)
        while True:
            (ent,) = struct.unpack_from("<I", d, p)
            if not ent:
                break
            if ent & 0x80000000:
                names.append(f"ord{ent & 0xFFFF}")
            else:
                names.append(cstr(d, rva2off(secs, ent + 2), 64) or "?")
            p += 4
        for i, nm in enumerate(names):
            out[ib + iat + i * 4] = (dll, nm)
        o += 20
    return out


def main():
    path, subs = sys.argv[1], [s.lower() for s in sys.argv[2:]]
    d, ib, secs, ddirs = load(path)
    iat = imports(d, ib, secs, ddirs)
    text = next(s for s in secs if s[0] == ".text")
    _n, tva, _vs, tro, trs = text
    body = d[tro : tro + trs]
    hits = []
    for i in range(len(body) - 6):
        if body[i] == 0xFF and body[i + 1] == 0x15:
            (slot,) = struct.unpack_from("<I", body, i + 2)
            tgt = iat.get(slot)
            if tgt and any(s in tgt[1].lower() for s in subs):
                hits.append((ib + tva + i, tgt, body[max(0, i - 40) : i]))
    for va, (dll, nm), pre in hits:
        print(f"{va:08X}  {dll}!{nm}")
        for j, b in enumerate(pre):
            if b == 0x68 and j + 5 <= len(pre):
                (imm,) = struct.unpack_from("<I", pre, j + 1)
                s = cstr(d, rva2off(secs, imm - ib))
                print(f"          push 0x{imm:08X}" + (f'  = "{s}"' if s else ""))
    print(f"{len(hits)} call sites")


main()
