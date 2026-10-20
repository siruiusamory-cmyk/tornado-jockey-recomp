import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "proxy-test"
EXE = BUILD / "bmp_test.exe"

FILE_SIZE = 54 + 16
HEADER = (
    ("magic", 0, 2, b"BM"),
    ("file size", 2, 4, FILE_SIZE.to_bytes(4, "little")),
    ("reserved", 6, 4, b"\0" * 4),
    ("pixel offset", 10, 4, (54).to_bytes(4, "little")),
    ("info size", 14, 4, (40).to_bytes(4, "little")),
    ("width", 18, 4, (2).to_bytes(4, "little")),
    ("height", 22, 4, (2).to_bytes(4, "little")),
    ("planes", 26, 2, (1).to_bytes(2, "little")),
    ("bits per pixel", 28, 2, (24).to_bytes(2, "little")),
    ("compression", 30, 4, (0).to_bytes(4, "little")),
    ("image size", 34, 4, (16).to_bytes(4, "little")),
)


PIXELS = bytes.fromhex("0000ff 302010 0000 ff0000 00ff00 0000")


def compile_writer():
    sources = [ROOT / "port" / "proxy" / "bmp.h", ROOT / "port" / "proxy" / "bmp_test.cpp"]

    if EXE.exists() and all(
        p.exists() and p.stat().st_mtime <= EXE.stat().st_mtime for p in sources
    ):
        return
    vswhere = (
        pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if not vswhere.exists():
        raise AssertionError(f"vswhere.exe not found: {vswhere}")
    vs = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
        check=False,
        capture_output=True,
        text=True,
    ).stdout.strip()
    vcvars = pathlib.Path(vs) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    BUILD.mkdir(parents=True, exist_ok=True)

    batch = BUILD / "bmp_test.bat"
    batch.write_text(
        f'@call "{vcvars}" >nul 2>&1\n'
        f'@cl /nologo /EHsc /W4 /std:c++17 "{ROOT / "port" / "proxy" / "bmp_test.cpp"}" '
        f'/Fe:"{EXE}" /Fo:"{BUILD / "bmp_test.obj"}"\n',
        encoding="ascii",
    )
    built = subprocess.run([str(batch)], check=False, capture_output=True, text=True)
    if built.returncode or not EXE.exists():
        raise AssertionError(f"bmp writer did not compile:\n{built.stdout}{built.stderr}")


class TestBmpWriter(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compile_writer()
        cls.output = pathlib.Path(tempfile.mkdtemp()) / "frame.bmp"
        ran = subprocess.run(
            [str(EXE), str(cls.output)], check=False, capture_output=True, text=True
        )
        if ran.returncode:
            raise AssertionError(f"writer failed: exit {ran.returncode}")
        cls.image = cls.output.read_bytes()

    def test_bmp_header_and_row_order(self):
        self.assertEqual(len(self.image), FILE_SIZE)
        with self.subTest(field="pixel data, bottom row first"):
            self.assertEqual(self.image[54:], PIXELS)
        for name, offset, width, expected in HEADER:
            with self.subTest(field=name):
                self.assertEqual(self.image[offset : offset + width], expected)


if __name__ == "__main__":
    unittest.main()
