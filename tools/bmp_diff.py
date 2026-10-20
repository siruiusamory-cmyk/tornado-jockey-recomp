"""Compare two uncompressed BMP frames and report how they differ.

Pixel count, the max per-channel difference, and the connected differing regions (4-neighbour)
with each region's bounding box and size. Optionally writes a difference image, so a reader can
see where a renderer change lands.

usage: python -B tools/bmp_diff.py <a.bmp> <b.bmp> [<diff-out.bmp>]
"""

import struct
import sys


def read_bmp(path):
    with open(path, "rb") as source:
        data = source.read()
    if data[0:2] != b"BM":
        raise SystemExit(f"{path}: not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    header = struct.unpack_from("<I", data, 14)[0]
    if header < 40:
        raise SystemExit(f"{path}: BMP header {header} not supported")
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if compression != 0 or bits not in (24, 32):
        raise SystemExit(f"{path}: {bits}bpp compression {compression} not supported")
    stride = ((width * bits // 8) + 3) // 4 * 4
    bottom_up = height > 0
    height = abs(height)
    step = bits // 8
    rows = []
    for y in range(height):
        start = offset + (height - 1 - y if bottom_up else y) * stride
        row = data[start : start + width * step]
        rows.append([(row[x * step + 2], row[x * step + 1], row[x * step]) for x in range(width)])
    return width, height, rows


def write_bmp(path, width, height, rows):
    stride = (width * 3 + 3) // 4 * 4
    body = bytearray()
    for y in range(height - 1, -1, -1):
        line = bytearray()
        for r, g, b in rows[y]:
            line += bytes((b, g, r))
        body += line + b"\0" * (stride - len(line))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(body), 0, 0, 0, 0)
    with open(path, "wb") as out:
        out.write(header + info + body)


def regions(mask, width, height):
    seen = bytearray(width * height)
    found = []
    for start in range(width * height):
        if not mask[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        left = right = start % width
        top = bottom = start // width
        count = 0
        while stack:
            at = stack.pop()
            count += 1
            x, y = at % width, at // width
            left, right = min(left, x), max(right, x)
            top, bottom = min(top, y), max(bottom, y)
            for n in (at - 1, at + 1, at - width, at + width):
                if 0 <= n < width * height and mask[n] and not seen[n]:
                    nx, ny = n % width, n // width
                    if abs(nx - x) + abs(ny - y) != 1:
                        continue
                    seen[n] = 1
                    stack.append(n)
        found.append(((left, top, right, bottom), count))
    found.sort(key=lambda r: -r[1])
    return found


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    wa, ha, a = read_bmp(argv[1])
    wb, hb, b = read_bmp(argv[2])
    if (wa, ha) != (wb, hb):
        raise SystemExit(f"size differs: {wa}x{ha} against {wb}x{hb}")
    mask = bytearray(wa * ha)
    differing = 0
    above = 0
    total = 0
    peak = 0
    diff_rows = [[(0, 0, 0)] * wa for _ in range(ha)]
    for y in range(ha):
        for x in range(wa):
            pa, pb = a[y][x], b[y][x]
            d = [abs(pa[i] - pb[i]) for i in range(3)]
            if max(d):
                mask[y * wa + x] = 1
                differing += 1
                total += sum(d)
                peak = max(peak, max(d))
                if max(d) > 16:
                    above += 1
                diff_rows[y][x] = tuple(min(255, v * 8) for v in d)
    print(f"size {wa}x{ha} differing_pixels {differing} of {wa * ha}")
    print(f"above_16 {above} max_channel_diff {peak} mean_abs_diff {total / (wa * ha):.4f}")
    boxes = regions(mask, wa, ha)
    print(f"regions {len(boxes)}")
    for (left, top, right, bottom), count in boxes[:7]:
        print(f"  box {left},{top}..{right},{bottom} pixels {count}")
    if len(argv) > 3:
        write_bmp(argv[3], wa, ha, diff_rows)
        print(f"diff_image {argv[3]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
