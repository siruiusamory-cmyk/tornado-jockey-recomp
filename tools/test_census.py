import os
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "proxy-test"
EXE = BUILD / "census_test.exe"


def compile_test():
    sources = [
        ROOT / "port" / "proxy" / "census.h",
        ROOT / "port" / "proxy" / "census_test.cpp",
    ]

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
    batch = BUILD / "census_test.bat"
    batch.write_text(
        f'@call "{vcvars}" >nul 2>&1\n'
        f'@cl /nologo /EHsc /W4 /std:c++17 "{ROOT / "port" / "proxy" / "census_test.cpp"}" '
        f'/Fe:"{EXE}" /Fo:"{BUILD / "census_test.obj"}"\n',
        encoding="ascii",
    )
    built = subprocess.run([str(batch)], check=False, capture_output=True, text=True)
    if built.returncode or not EXE.exists():
        raise AssertionError(f"census test did not compile:\n{built.stdout}{built.stderr}")


class TestCensus(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compile_test()
        cls.ran = subprocess.run([str(EXE)], check=False, capture_output=True, text=True)

    def test_class_decode_cases(self):
        self.assertEqual(
            self.ran.returncode,
            0,
            f"cases failed:\n{self.ran.stdout}{self.ran.stderr}",
        )
        self.assertIn("ok ", self.ran.stdout)


if __name__ == "__main__":
    unittest.main()
