import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import field_map


def save(records):
    body = bytearray(64 + 8 * 12)
    directory = []
    tag_bytes = {tag: bytearray() for tag in (1, 2, 4, 6, 8)}
    for kind, class_id, body_bytes, patches in records:
        tag = 3 if kind == 2 else 5 if kind == 1 else 7
        tag_bytes.setdefault(tag, bytearray())
        tag_bytes[tag] += struct.pack(
            "<IIIII", len(records), kind, class_id, len(body_bytes), len(patches)
        )
        for off, block in patches:
            tag_bytes[tag] += struct.pack("<III", off, block, 0)
        tag_bytes[tag] += body_bytes
    body[0:8] = field_map.MAGIC
    struct.pack_into("<IIIIII", body, 8, 1, 64, 8, len(records), 0, 0)
    for i in range(8):
        payload = tag_bytes.get(i + 1, bytearray())
        offset = len(body)
        directory.append((i + 1, offset, len(payload)))
        body += payload
    struct.pack_into("<" + "III" * 8, body, 64, *(v for entry in directory for v in entry))
    return bytes(body)


def body(*dwords):
    return b"".join(struct.pack("<I", value) for value in dwords)


CASES = (
    (
        "a pointer dword is process-local, a simulation float is not",
        [(1, 16, body(0x0065A7A0, 0x3F800000, 0x14A2B3C0), [])],
        [(1, 16, body(0x0065A7A0, 0x3F800000, 0x17C9D4E0), [])],
        [("actor", 16, 12, 0x08, 0x14A2B3C0, 0x17C9D4E0, "other")],
    ),
    (
        "a patched dword is classified by its patch, not by its value",
        [(1, 16, body(0, 0x14A2B3C0), [(0, 0), (4, 7)])],
        [(1, 16, body(0, 0x17C9D4E0), [(0, 0), (4, 7)])],
        [("actor", 16, 8, 0x04, 0x14A2B3C0, 0x17C9D4E0, "object")],
    ),
    (
        "an image address and an equal field are not in the map",
        [(1, 16, body(0x0065A7A0, 0x00000003), [])],
        [(1, 16, body(0x0065A7A0, 0x00000003), [])],
        [],
    ),
    (
        "a non-actor class is keyed by its own vtable",
        [(2, 0xFFFFFFFF, body(0x00656A40, 0x11111111), [])],
        [(2, 0xFFFFFFFF, body(0x00656A40, 0x22222222), [])],
        [("root", 0x00656A40, 8, 0x04, 0x11111111, 0x22222222, "other")],
    ),
)


class TestFieldMap(unittest.TestCase):
    def test_cases(self):
        for name, left, right, expected in CASES:
            with self.subTest(name):
                rows = field_map.field_map(
                    field_map.parse(save(left)), field_map.parse(save(right))
                )
                found = [
                    (kind, ident, size, off, a, b, cls)
                    for (kind, ident, size), row in rows.items()
                    for off, a, b, cls in row
                ]
                self.assertEqual(expected, found)

    def test_private_patch(self):
        rows = field_map.field_map(
            field_map.parse(save([(1, 16, body(0x14A2B3C0, 1), [(0, 0)])])),
            field_map.parse(save([(1, 16, body(0x17C9D4E0, 1), [(0, 0)])])),
        )
        self.assertEqual("private", next(iter(rows.values()))[0][3])

    def test_malformed(self):
        good = bytearray(save([(1, 16, body(1), [])]))
        for name, data in (
            ("magic", bytes(b"TJSAVE02" + good[8:])),
            ("section count", good.replace(struct.pack("<I", 8), struct.pack("<I", 7))),
            ("truncated", bytes(good[:-4])),
        ):
            with self.subTest(name), self.assertRaises(ValueError):
                field_map.parse(data)


if __name__ == "__main__":
    unittest.main()
