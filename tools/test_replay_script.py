import os
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "proxy-test"
EXE = BUILD / "replay_test.exe"


def compile_test():
    sources = [
        ROOT / "port" / "proxy" / "replay_script.h",
        ROOT / "port" / "proxy" / "replay_test.cpp",
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

    batch = BUILD / "replay_test.bat"
    batch.write_text(
        f'@call "{vcvars}" >nul 2>&1\n'
        f'@cl /nologo /EHsc /W4 /std:c++17 "{ROOT / "port" / "proxy" / "replay_test.cpp"}" '
        f'/Fe:"{EXE}" /Fo:"{BUILD / "replay_test.obj"}"\n',
        encoding="ascii",
    )
    built = subprocess.run([str(batch)], check=False, capture_output=True, text=True)
    if built.returncode or not EXE.exists():
        raise AssertionError(f"replay test did not compile:\n{built.stdout}{built.stderr}")


class TestReplayScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compile_test()
        cls.ran = subprocess.run([str(EXE)], check=False, capture_output=True, text=True)

    def test_clock_and_parser_cases(self):
        self.assertEqual(
            self.ran.returncode,
            0,
            f"cases failed:\n{self.ran.stdout}{self.ran.stderr}",
        )
        self.assertIn("ok ", self.ran.stdout)


if __name__ == "__main__":
    unittest.main()
