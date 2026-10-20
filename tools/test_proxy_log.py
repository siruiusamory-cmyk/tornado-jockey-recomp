import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import proxy_log


def log(*rows):
    return "# d3d8 proxy log v1 image_base 0x00400000\n" + "".join(f"{row}\n" for row in rows)


RESOLVE = (
    (
        "3-byte call [reg+disp8]: the return address is start + 3",
        log("call 0 15 CreateDevice 00401003 2"),
        [(0x401000, "CALL", []), (0x401003, "MOV", []), (0x401010, "RET", [])],
        {(0, 15, "CreateDevice", 0x401000): 2},
    ),
    (
        "6-byte call [reg+disp32]: the return address is start + 6",
        log("call 1 16 Present 00402006 1"),
        [(0x402000, "CALL", []), (0x402006, "MOV", [])],
        {(1, 16, "Present", 0x402000): 1},
    ),
    (
        "a gap longer than an instruction is padding, not a call end",
        log("call 1 40 GetBackBuffer 00403040 1"),
        [(0x403000, "CALL", []), (0x403040, "MOV", [])],
        {(1, 40, "GetBackBuffer", None): 1},
    ),
    (
        "flushes of one call site add up, two sites stay apart",
        log("call 0 8 Release 0044BC98 3", "call 0 8 Release 0044BC98 4"),
        [(0x44BC93, "CALL", []), (0x44BC98, "MOV", [])],
        {(0, 8, "Release", 0x44BC93): 7},
    ),
)


VERDICTS = (
    (
        "the anchor VA calls the named slot",
        {(0, 15, "CreateDevice", 0x5070BA): 1},
        [("CreateDevice", 0x5070BA, "unresolved", 0x3C)],
        [("CreateDevice", 0x5070BA, "CONFIRMED", [(0, 15, "CreateDevice", 1)])],
    ),
    (
        "the anchor VA calls a different slot",
        {(0, 8, "Release", 0x5070BA): 5},
        [("CreateDevice", 0x5070BA, "unresolved", 0x3C)],
        [("CreateDevice", 0x5070BA, "REFUTED", [(0, 8, "Release", 5)])],
    ),
    (
        "a slot never called before the menu is not a refutation",
        {(1, 16, "Present", 0x4211F8): 2},
        [("GetBackBuffer", 0x507129, "unresolved", 0x40)],
        [("GetBackBuffer", 0x507129, "NOT SEEN", [])],
    ),
)

MALFORMED = (
    ("missing count", log("call 0 15 CreateDevice 00401003")),
    ("not a call line", log("hello")),
    ("interface out of range", log("call 2 15 CreateDevice 00401003 1")),
    ("no header", "call 0 15 CreateDevice 00401003 1\n"),
)


def args(*rows):
    return "# d3d8 proxy log v1 image_base 0x00400000\n" + "".join(
        f"# args {row}\n" for row in rows
    )


ARG_SETS = (
    (
        "one device argument line",
        args("1 50 SetRenderState 0000000A 00000002 00000000 00000000 00000000 00000000 7"),
        {(1, 50, "SetRenderState", (10, 2, 0, 0, 0, 0)): 7},
    ),
    (
        "the periodic flush repeats a set with a growing cumulative count",
        args(
            "1 61 SetTexture 00000000 00000001 00000000 00000000 00000000 00000000 3",
            "1 61 SetTexture 00000000 00000001 00000000 00000000 00000000 00000000 9",
        ),
        {(1, 61, "SetTexture", (0, 1, 0, 0, 0, 0)): 9},
    ),
    (
        "interface 0 carries the CreateDevice presentation parameters",
        args("0 15 CreateDevice 00000001 00000258 000001C2 00000016 00000001 00000000 1"),
        {(0, 15, "CreateDevice", (1, 600, 450, 22, 1, 0)): 1},
    ),
    (
        "the armed marker is not an argument line",
        args("armed")
        + "# args 1 88 SetPixelShader FFFFFFFF 00000000 00000000 00000000 00000000 00000000 2\n",
        {(1, 88, "SetPixelShader", (0xFFFFFFFF, 0, 0, 0, 0, 0)): 2},
    ),
    ("no argument lines", log("call 1 15 Present 004211FB 2"), {}),
)

MALFORMED_ARGS = (
    ("five values", args("1 50 SetRenderState 0000000A 00000002 00000000 00000000 00000000 7")),
    (
        "a value out of range",
        args("1 50 SetRenderState 0000000A 00000002 00000000 00000000 00000000 00 7"),
    ),
)


class TestProxyLog(unittest.TestCase):
    def test_resolve(self):
        for name, text, insns, expected in RESOLVE:
            with self.subTest(name):
                base, records = proxy_log.parse_log(text)
                sites = proxy_log.resolve(base, records, proxy_log.call_end_map(insns), 0x400000)
                self.assertEqual(expected, sites)

    def test_verdicts(self):
        for name, sites, anchors, expected in VERDICTS:
            with self.subTest(name):
                parsed = [
                    (method, va, 0, method, tier, offset) for method, va, tier, offset in anchors
                ]
                self.assertEqual(expected, proxy_log.verdicts(sites, parsed))

    def test_malformed(self):
        for name, text in MALFORMED:
            with self.subTest(name), self.assertRaises(ValueError):
                proxy_log.parse_log(text)

    def test_arg_sets(self):
        for name, text, expected in ARG_SETS:
            with self.subTest(name):
                self.assertEqual(expected, proxy_log.parse_args(text))

    def test_call_lines_survive_argument_lines(self):
        text = args("1 50 SetRenderState 0000000A 00000002 00000000 00000000 00000000 00000000 7")
        text += "call 1 15 Present 004211FB 2\n"
        _base, records = proxy_log.parse_log(text)
        self.assertEqual(2, sum(record["count"] for record in records))

    def test_malformed_arg_sets(self):
        for name, text in MALFORMED_ARGS:
            with self.subTest(name), self.assertRaises(ValueError):
                proxy_log.parse_args(text)

    def test_fixture_carries_the_runtime_present_site(self):
        fixture = json.loads(
            (pathlib.Path(proxy_log.__file__).parent / "fixtures" / "d3d8-anchors.json").read_text()
        )
        self.assertEqual(
            [proxy_log.PRESENT_MENU[1]], [site["va"] for site in fixture["runtime_sites"]]
        )
        self.assertIn("Present 004DE64D", fixture["runtime_sites"][0]["log_line"])


if __name__ == "__main__":
    unittest.main()
