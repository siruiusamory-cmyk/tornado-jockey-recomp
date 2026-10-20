import pathlib
import re
import shutil
import tempfile
import unittest

import check_links

CLEAN = {
    "README.md": "# Top\n\nSee [the notes](docs/notes.md) and [install](docs/notes.md#install).\n",
    "docs/notes.md": "# Notes\n\n## Install\n\nsha256 `" + "A" * 64 + "`, address 0x508F50.\n",
    "port/third_party/vendor.md": "Catch@005088b7 and evidence/missing.log\n",
}


DEAD_COMMIT = "abcdef" + "1"

BROKEN = {
    "README.md": "# Top\n\nSee [the notes](docs/gone.md).\n",
    "docs/notes.md": (
        f"# Notes\n\nAt commit {DEAD_COMMIT} the census moved; the log is `evidence/census.txt`.\n"
    ),
}


DEAD_CASES = {
    "evidence/missing.log": "evidence/ is not in the release",
    "build/missing.log": "build/ is not in the release",
    "game/missing.log": "game/ is not in the release",
    "intake/missing.log": "intake/ is not in the release",
    "port/HANDOFF.md": "port/HANDOFF.md was removed",
    "docs/ENGINEERING_PRINCIPLES.md": "docs/ENGINEERING_PRINCIPLES.md was removed",
}


class CheckLinks(unittest.TestCase):
    def tree(self, files):
        root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, True)
        for name, text in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        return root

    def test_clean_tree_passes(self):
        self.assertEqual(check_links.scan(self.tree(CLEAN)), [])

    def test_each_rule_fails_once(self):
        found = check_links.scan(self.tree(BROKEN))
        self.assertEqual(
            sorted(re.match(r".*: (\w+): ", f).group(1) for f in found),
            [
                "commit",
                "link",
                "path",
            ],
        )

    def test_a_link_to_a_missing_heading_fails(self):
        found = check_links.scan(self.tree({"README.md": "# Top\n\n[here](#nope)\n"}))
        self.assertEqual([re.match(r".*: (\w+): ", f).group(1) for f in found], ["link"])

    def test_every_guarded_path_fails(self):
        for text, detail in DEAD_CASES.items():
            with self.subTest(path=text):
                found = check_links.scan(self.tree({"README.md": f"# Top\n\nmention `{text}`\n"}))
                self.assertEqual(found, [f"README.md:3: path: {detail}"])


if __name__ == "__main__":
    unittest.main()
