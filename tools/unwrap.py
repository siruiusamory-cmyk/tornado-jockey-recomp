import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
OVERLAY = ROOT / "intake" / "unwrapped" / "Tornado.exe"
STUB = ROOT / "intake" / "installed" / "Tornado.exe"
WORKING = ROOT / "intake" / "crack" / "dump" / "Tornado-WT.exe"
OUT = ROOT / "intake" / "repaired" / "Tornado.exe"

WINDOW = 16

PATCHED = (
    (0x1000, 2442276, 0x1000),
    (0x256000, 355526, 0x256000),
    (0x2AD000, 0x1E000, 0x2AD000),
    (0x2CB000, 0x900, 0x614000),
)
IAT = (0x256000, 0x256488)
RSRC = (0x2CC000, 16384)
RSRC_WORKING = 0x615000


def build() -> bytes:
    overlay = bytearray(OVERLAY.read_bytes())
    stub = STUB.read_bytes()
    working = WORKING.read_bytes()

    if working[RSRC_WORKING : RSRC_WORKING + RSRC[1]] != stub[RSRC[0] : RSRC[0] + RSRC[1]]:
        raise ValueError("stub .rsrc differs from the working image .rsrc")
    overlay[RSRC[0] : RSRC[0] + RSRC[1]] = stub[RSRC[0] : RSRC[0] + RSRC[1]]

    runs = []
    for start, length, source in PATCHED:
        i = 0
        while i < length:
            if IAT[0] <= start + i < IAT[1] or overlay[start + i] == working[source + i]:
                i += 1
                continue
            j = i
            while (
                j < length
                and not IAT[0] <= start + j < IAT[1]
                and overlay[start + j] != working[source + j]
            ):
                j += 1
            offset = start + i
            if j - i > WINDOW:
                raise ValueError(
                    f"differing run at 0x{offset:X} is {j - i} bytes, over the {WINDOW}-byte window"
                )
            overlay[offset : start + j] = working[source + i : source + j]
            runs.append((offset, j - i))
            i = j
    print(
        f"{len(runs)} runs repaired, {sum(n for _, n in runs)} bytes, "
        f"longest {max(n for _, n in runs)}"
    )
    return bytes(overlay)


def main() -> None:
    image = build()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(image)
    print(f"wrote {OUT} {len(image)} bytes")


if __name__ == "__main__":
    main()
