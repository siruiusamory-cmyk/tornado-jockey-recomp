"""TJ-STAGE-13D F1: the process-local field map, from two raw save dumps.

Two fresh processes run the same replay to the same Present with TJ_SAVE_AT and TJ_SAVE_RAW=1,
so each dump carries the object bodies with the dwords it actually had. A dword that differs
between them is process-local (a pointer, a handle, an address-derived word, or padding); a
simulation field is identical because the replay is deterministic. port/SAVE_FORMAT.md is the
format read here.
"""

import argparse
import pathlib
import struct
import sys

MAGIC = b"TJSAVE01"
HEADER_BYTES = 64
SECTION_ENTRY = 12
SECTIONS = 8
RECORD_TAGS = (3, 5, 6, 7)

IMAGE_LOW, IMAGE_HIGH = 0x400000, 0x400000 + 0x619000
KIND_NAMES = {1: "actor", 2: "root", 3: "block", 4: "spawner", 5: "owner", 6: "child"}


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def parse(data):
    if len(data) < HEADER_BYTES or data[:8] != MAGIC:
        raise ValueError("not a TJSAVE01 file")
    if u32(data, 8) != 1 or u32(data, 12) != HEADER_BYTES:
        raise ValueError("version or header length")
    count = u32(data, 16)
    if count != SECTIONS:
        raise ValueError(f"section count {count}")
    directory = {}
    for i in range(SECTIONS):
        p = HEADER_BYTES + i * SECTION_ENTRY
        tag, off, size = struct.unpack_from("<III", data, p)
        if tag != i + 1 or off + size > len(data):
            raise ValueError(f"section {tag} out of bounds")
        directory[tag] = (off, size)
    out = []
    for tag in RECORD_TAGS:
        off, size = directory[tag]
        p, end = off, off + size
        while p < end:
            if end - p < 20:
                raise ValueError("truncated record")
            rec_id, kind, class_id, body_bytes, patches = struct.unpack_from("<IIIII", data, p)
            p += 20
            if not rec_id or p + 12 * patches + body_bytes > end:
                raise ValueError("record out of bounds")
            patch = {}
            for _ in range(patches):
                at, block, _block_off = struct.unpack_from("<III", data, p)
                patch[at] = block
                p += 12
            out.append(
                {
                    "kind": kind,
                    "class_id": class_id,
                    "size": body_bytes,
                    "body": data[p : p + body_bytes],
                    "patch": patch,
                }
            )
            p += body_bytes
    return out


def key_of(rec):
    if rec["kind"] == 1:
        return ("actor", rec["class_id"], rec["size"])
    vtable = u32(rec["body"], 0) if len(rec["body"]) >= 4 else 0
    return (KIND_NAMES.get(rec["kind"], rec["kind"]), vtable, rec["size"])


def classify(value, block):
    if block is not None:
        return "private" if block == 0 else "object"
    if IMAGE_LOW <= value < IMAGE_HIGH:
        return "image"
    return "other"


def field_map(left, right, include_other=True):
    rows = {}
    left_by = {}
    for rec in left:
        left_by.setdefault(key_of(rec), []).append(rec)
    seen = {}
    for rec in right:
        key = key_of(rec)
        index = seen.get(key, 0)
        seen[key] = index + 1
        peers = left_by.get(key, [])
        if index >= len(peers):
            continue
        peer = peers[index]
        size = min(peer["size"], rec["size"])
        if peer["size"] != rec["size"]:
            continue
        for off in range(0, size - 3, 4):
            a = u32(peer["body"], off)
            b = u32(rec["body"], off)
            if a == b:
                continue
            cls = classify(a, peer["patch"].get(off))
            if cls == "other" and not include_other:
                continue
            rows.setdefault(key, []).append((off, a, b, cls))
    return rows


def report(rows, detail=None):
    total = 0
    for key in sorted(rows):
        kind, ident, size = key
        counts = {name: 0 for name in ("private", "object", "image", "other")}
        for _off, _a, _b, cls in rows[key]:
            counts[cls] += 1
        total += len(rows[key])
        print(
            f"# fieldmap class kind={kind} key={ident:08X} size={size} "
            f"differ={len(rows[key])} offsets={len({off for off, *_ in rows[key]})} "
            f"private={counts['private']} object={counts['object']} "
            f"image={counts['image']} other={counts['other']}"
        )
        if detail:
            for off, a, b, cls in rows[key]:
                detail.write(
                    f"# fieldmap row kind={kind} key={ident:08X} size={size} off={off:04X} "
                    f"a={a:08X} b={b:08X} cls={cls}\n"
                )
    return total


KIND_ID = {"actor": 1, "root": 2, "spawner": 4, "owner": 5, "child": 6}


def emit_header(rows, path):
    offsets, entries = [], []
    for key in sorted(rows):
        kind, ident, size = key
        union = sorted({off for off, *_ in rows[key]})
        entries.append((KIND_ID[kind], ident, size, len(offsets), len(union)))
        offsets += union
    lines = [
        "// Generated by tools/field_map.py --emit-header from two TJ-STAGE-13D raw dumps.",
        "// port/SAVE_FORMAT.md section 8. Do not edit: regenerate and re-record the run ids.",
        "#pragma once",
        "#include <cstdint>",
        "",
        "struct KeepLocal {",
        "    uint32_t kind;   // the record's kind: 1 actor, 2 root, 4 spawner, 5 owner, 6 child",
        "    uint32_t key;    // an actor's class id, otherwise the vtable in body[0]",
        "    uint32_t size;   // the record's body size, which tells three classes apart",
        "    uint32_t first;  // index into KEEP_LOCAL_OFFSET",
        "    uint32_t count;",
        "};",
        "",
        f"static const uint32_t KEEP_LOCAL_OFFSET[{len(offsets)}] = {{",
    ]
    lines += [
        "    " + " ".join(f"0x{off:04X}," for off in offsets[i : i + 12])
        for i in range(0, len(offsets), 12)
    ]
    lines += ["};", "", f"static const KeepLocal KEEP_LOCAL[{len(entries)}] = {{"]
    lines += [
        f"    {{{kind}, 0x{ident:08X}u, {size}, {first}, {count}}},"
        for kind, ident, size, first, count in entries
    ]
    lines += ["};", ""]
    path.write_text("\n".join(lines), encoding="ascii")
    return len(offsets), len(entries)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", type=pathlib.Path, nargs="+", help="two or more raw dumps")
    parser.add_argument("--rows", type=pathlib.Path, help="write every differing dword here")
    parser.add_argument("--emit-header", type=pathlib.Path, help="write the F2 keep-local table")
    args = parser.parse_args(argv)
    if len(args.paths) < 2:
        parser.error("two or more dumps are needed")
    dumps = [parse(path.read_bytes()) for path in args.paths]
    print("# fieldmap files " + " ".join(str(len(dump)) for dump in dumps))
    rows = {}
    for dump in dumps[1:]:
        for key, row in field_map(dumps[0], dump).items():
            rows.setdefault(key, []).extend(row)
    detail = args.rows.open("w", encoding="utf-8") if args.rows else None
    try:
        total = report(rows, detail)
    finally:
        if detail:
            detail.close()
    if args.emit_header:
        offsets, classes = emit_header(rows, args.emit_header)
        print(f"# fieldmap header {args.emit_header} classes={classes} offsets={offsets}")
    print(f"# fieldmap total differ={total} dumps={len(dumps)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
