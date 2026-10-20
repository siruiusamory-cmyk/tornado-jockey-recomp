import re
import struct
import sys
from pathlib import Path

KINDS = ("FUNCTION", "DATA", "VTABLE", "STRING")
HEADER = "va,kind,name,ref"
VA_ROW = re.compile(r"0x[0-9A-F]{6,8}")
NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
DOC_VA = re.compile(r"0x([0-9A-Fa-f]{6,8})\b|FUN_([0-9A-Fa-f]{6,8})\b")
ROLE_TIE = re.compile(
    r"(?i)\b(vtable|factory|allocator|thunk|pump|constructor|ctor|procedure)\b"
    r"[\s:`]*?0x(4[0-9A-Fa-f]{5})\b"
)


def load_rows(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != HEADER:
        return [], [f"{path}:1 bad header, want {HEADER}"]
    rows = []
    errors = []
    for number, line in enumerate(lines[1:], 2):
        fields = line.split(",")
        if len(fields) != 4:
            errors.append(f"{path}:{number} bad column count {len(fields)}")
            continue
        va, kind, name, ref = fields
        if not VA_ROW.fullmatch(va):
            errors.append(f"{path}:{number} bad va {va!r}")
            continue
        if kind not in KINDS:
            errors.append(f"{path}:{number} bad kind {kind!r}")
            continue
        if not NAME.fullmatch(name):
            errors.append(f"{path}:{number} bad name {name!r}")
            continue
        rows.append((int(va, 16), kind, name, ref, number))
    return rows, errors


def duplicate_errors(csv_path, rows):
    errors = []
    for column, label in ((0, "va"), (2, "name")):
        seen = {}
        for row in rows:
            key = row[column]
            shown = f"0x{key:06X}" if column == 0 else key
            if key in seen:
                errors.append(
                    f"{csv_path}:{row[4]} duplicate {label} {shown} first at line {seen[key]}"
                )
            else:
                seen[key] = row[4]
    return errors


def section_spans(image_path):
    data = image_path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError(f"{image_path}: not an MZ image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe : pe + 4] != b"PE\0\0":
        raise ValueError(f"{image_path}: no PE signature")
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    base = struct.unpack_from("<I", data, opt + (28 if magic == 0x10B else 24))[0]
    spans = []
    for index in range(count):
        header = opt + opt_size + 40 * index
        virtual_size, virtual_address, raw_size = struct.unpack_from("<III", data, header + 8)
        size = virtual_size or raw_size
        spans.append((base + virtual_address, base + virtual_address + size))
    return spans


def check(root, image):
    root = Path(root)
    csv_path = root / "port" / "symbols.csv"
    rows, errors = load_rows(csv_path)
    errors += duplicate_errors(csv_path, rows)
    spans = section_spans(image) if image is not None else None
    table = {row[0] for row in rows}
    for va, kind, name, ref, number in rows:
        if not ref or Path(ref).is_absolute() or not (root / ref).is_file():
            errors.append(f"{csv_path}:{number} ref not found {ref!r}")
        elif va not in {
            int(found or named, 16)
            for found, named in DOC_VA.findall((root / ref).read_text(encoding="utf-8"))
        }:
            errors.append(f"{csv_path}:{number} ref {ref} does not cite 0x{va:06X}")
        if spans is not None and not any(start <= va < end for start, end in spans):
            errors.append(f"{csv_path}:{number} va 0x{va:06X} outside image sections")
    for doc in sorted((root / "port").glob("*.md")):
        text = doc.read_text(encoding="utf-8")
        for match in ROLE_TIE.finditer(text):
            va = int(match.group(2), 16)
            if va not in table:
                line = text.count("\n", 0, match.start()) + 1
                errors.append(
                    f"{doc}:{line} role {match.group(1)} 0x{va:06X} missing from symbols.csv"
                )
    return errors


def main():
    root = Path(__file__).resolve().parent.parent
    image = root / "game" / "Tornado.exe"
    if not image.is_file():
        image = None
        print("image: absent")
    errors = check(root, image)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"symbols: {len(load_rows(root / 'port' / 'symbols.csv')[0])} rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
