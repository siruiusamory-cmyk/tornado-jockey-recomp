"""Measure a frame pair: HUD ink, and a draw class's contribution ratio.

Ink is the total absolute distance from the rectangle's median colour and its centroid, for
each frame; a shift moves the centroid, a coverage change moves the mass.

The contribution of one draw is `full - skip` at each pixel, so everything the two runs share
cancels exactly; the ratio of the second frame's contribution to the first frame's is the
alpha ratio of that draw when both frames hold the same background and texture.

usage: python -B tools/frame_probe.py ink <a.bmp> <b.bmp> <x0> <y0> <x1> <y1>
       python -B tools/frame_probe.py diff <a.bmp> <a-skip.bmp> <b.bmp> <b-skip.bmp>
                                          <x0> <y0> <x1> <y1>
"""

import sys

import bmp_diff


def median(values):
    ordered = sorted(values)
    return ordered[(len(ordered) - 1) // 2]


def rect_rows(rows, rect):
    x0, y0, x1, y1 = rect
    return [row[x0:x1] for row in rows[y0:y1]]


def ink(rows, rect):
    patch = rect_rows(rows, rect)
    pixels = [p for row in patch for p in row]
    background = tuple(median([p[c] for p in pixels]) for c in range(3))
    mass = 0.0
    xsum = 0.0
    ysum = 0.0
    x0, y0, _, _ = rect
    for y, row in enumerate(patch):
        for x, p in enumerate(row):
            weight = max(abs(p[c] - background[c]) for c in range(3))
            mass += weight
            xsum += weight * (x0 + x)
            ysum += weight * (y0 + y)
    if not mass:
        return background, 0.0, 0.0, 0.0
    return background, mass, xsum / mass, ysum / mass


def contribution(full_rows, skip_rows, rect):
    x0, y0, x1, y1 = rect
    out = []
    for y in range(y0, y1):
        row = []
        for x in range(x0, x1):
            full = full_rows[y][x]
            skip = skip_rows[y][x]
            row.append(tuple(full[c] - skip[c] for c in range(3)))
        out.append(row)
    return out


def diff_ratio(a, b, floor=3.0):
    pairs = []
    totals = [0.0, 0.0, 0.0]
    totals_b = [0.0, 0.0, 0.0]
    for row_a, row_b in zip(a, b):
        for da, db in zip(row_a, row_b):
            if max(abs(v) for v in da) <= floor:
                continue
            pairs.append((da, db))
            for c in range(3):
                totals[c] += da[c]
                totals_b[c] += db[c]
    count = len(pairs)
    if not count:
        return 0, (0.0, 0.0, 0.0), 0.0
    ratios = tuple(totals_b[c] / totals[c] if totals[c] else 0.0 for c in range(3))
    mean_a = sum(v for p in pairs for v in p[0]) / (3 * count)
    mean_b = sum(v for p in pairs for v in p[1]) / (3 * count)
    var_a = sum((v - mean_a) ** 2 for p in pairs for v in p[0])
    var_b = sum((v - mean_b) ** 2 for p in pairs for v in p[1])
    cov = sum((p[0][c] - mean_a) * (p[1][c] - mean_b) for p in pairs for c in range(3))
    corr = cov / (var_a * var_b) ** 0.5 if var_a and var_b else 0.0
    return count, ratios, corr


def main(argv):
    if len(argv) < 4:
        raise SystemExit(__doc__)
    mode = argv[1]
    if mode == "ink" and len(argv) == 8:
        _, _, a = bmp_diff.read_bmp(argv[2])
        _, _, b = bmp_diff.read_bmp(argv[3])
        rect = tuple(int(v) for v in argv[4:8])
        for label, rows in (("a", a), ("b", b)):
            background, mass, x, y = ink(rows, rect)
            print(f"{label} background {background} ink_mass {mass:.0f} centroid {x:.2f},{y:.2f}")
        return 0
    if mode == "diff" and len(argv) == 10:
        pa = bmp_diff.read_bmp(argv[2])
        sa = bmp_diff.read_bmp(argv[3])
        pb = bmp_diff.read_bmp(argv[4])
        sb = bmp_diff.read_bmp(argv[5])
        rect = tuple(int(v) for v in argv[6:10])
        ca = contribution(pa[2], sa[2], rect)
        cb = contribution(pb[2], sb[2], rect)
        count, ratios, corr = diff_ratio(ca, cb)
        print(
            f"pixels {count} ratio {ratios[0]:.3f},{ratios[1]:.3f},{ratios[2]:.3f} corr {corr:.3f}"
        )
        return 0
    raise SystemExit(__doc__)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
