import struct
import tempfile
import unittest
from pathlib import Path

import check_symbols

HEADER = "va,kind,name,ref\n"


def make_image(path, spans):
    data = bytearray(0x200)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x40)
    data[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x44, 0x14C, len(spans), 0, 0, 0, 0xE0, 0x102)
    struct.pack_into("<H", data, 0x58, 0x10B)
    struct.pack_into("<I", data, 0x58 + 28, 0x400000)
    for index, (rva, size) in enumerate(spans):
        struct.pack_into("<III", data, 0x138 + 40 * index, size, rva, size)
    path.write_bytes(data)


class CheckSymbolsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "port").mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, text):
        (self.root / "port" / "note.md").write_text(text, encoding="utf-8")

    def table(self, text):
        (self.root / "port" / "symbols.csv").write_text(text, encoding="utf-8")

    def valid(self):
        self.write("the factory 0x401000\n")
        self.table(HEADER + "0x00401000,FUNCTION,factory,port/note.md\n")

    def joined(self, image=None):
        return "\n".join(check_symbols.check(self.root, image))

    def test_clean_tree_passes(self):
        self.valid()
        image = self.root / "game.exe"
        make_image(image, [(0x1000, 0x2000)])
        self.assertEqual(self.joined(), "")
        self.assertEqual(self.joined(image), "")

    def test_bad_header(self):
        self.write("the factory 0x401000\n")
        self.table("kind,name\nFUNCTION,factory\n")
        self.assertIn("bad header", self.joined())

    def test_malformed_row(self):
        self.write("the factory 0x401000\n")
        bad = (
            "0x00401000,FUNCTION\n"
            "0x4010aa,FUNCTION,factory,port/note.md\n"
            "0x00401001,patch,factory,port/note.md\n"
            "0x00401002,FUNCTION,2factory,port/note.md\n"
        )
        self.table(HEADER + bad)
        errors = self.joined()
        self.assertIn("bad column count", errors)
        self.assertIn("bad va", errors)
        self.assertIn("bad kind", errors)
        self.assertIn("bad name", errors)

    def test_duplicate_va(self):
        self.write("the factory 0x401000\n")
        self.table(
            HEADER + "0x00401000,FUNCTION,factory,port/note.md\n"
            "0x00401000,DATA,factory_data,port/note.md\n"
        )
        self.assertIn("duplicate va", self.joined())

    def test_duplicate_name(self):
        self.write("the factory 0x401000 and 0x401001\n")
        self.table(
            HEADER + "0x00401000,FUNCTION,factory,port/note.md\n"
            "0x00401001,FUNCTION,factory,port/note.md\n"
        )
        self.assertIn("duplicate name", self.joined())

    def test_va_outside_image(self):
        self.write("the factory FUN_00500000\n")
        self.table(HEADER + "0x00500000,FUNCTION,factory,port/note.md\n")
        image = self.root / "game.exe"
        make_image(image, [(0x1000, 0x2000)])
        self.assertIn("outside image sections", self.joined(image))

    def test_missing_ref(self):
        self.write("the factory 0x401000\n")
        self.table(HEADER + "0x00401000,FUNCTION,factory,port/missing.md\n")
        self.assertIn("ref not found", self.joined())

    def test_ref_without_citation(self):
        self.write("nothing here\n")
        self.table(HEADER + "0x00401000,FUNCTION,factory,port/note.md\n")
        self.assertIn("does not cite", self.joined())

    def test_doc_role_missing(self):
        self.write("the factory 0x401000 and the allocator 0x401234\n")
        self.table(HEADER + "0x00401000,FUNCTION,factory,port/note.md\n")
        self.assertIn("role allocator 0x401234 missing from symbols.csv", self.joined())


if __name__ == "__main__":
    unittest.main()
