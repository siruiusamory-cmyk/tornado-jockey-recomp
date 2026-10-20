import json
import os
import unittest

import d3d8_census as census

BASE = 0x400000
THUNK = 0x608000
GLOBAL = 0x624000


def reg(name):
    return ["reg", [["R", name]]]


def mem(base, disp=0):
    return ["mem", [["R", base], ["S", disp & 0xFFFFFFFF]]]


def absolute_memory(value):
    return ["mem", [["A", value]]]


def imm(value):
    return ["imm", [["S", value]]]


def addr(value):
    return ["addr", [["A", value]]]


def stream(*bodies):
    insns, functions = [], []
    for number, body in enumerate(bodies):
        start = BASE + 0x1000 * (number + 1)
        for offset, (mnemonic, operands) in enumerate(body):
            insns.append([start + offset, mnemonic, operands])
        functions.append([start, f"sub_{start:08X}", start, start + len(body) - 1])
    return {
        "insns": insns,
        "functions": functions,
        "create8_thunk": THUNK,
        "create8_slot": THUNK,
    }


def create_device(holder, out):
    return [
        ("LEA", [reg("EAX"), out]),
        ("PUSH", [reg("EAX")]),
        *[("PUSH", [imm(0)]) for _ in range(6)],
        ("MOV", [reg("ECX"), absolute_memory(holder)]),
        ("MOV", [reg("EDX"), mem("ECX")]),
        ("CALL", [mem("EDX", 0x3C)]),
    ]


def case_unproven_base():
    body = [
        ("CALL", [addr(THUNK)]),
        ("MOV", [absolute_memory(GLOBAL), reg("EAX")]),
        ("MOV", [reg("ESI"), absolute_memory(GLOBAL)]),
        *create_device(GLOBAL, mem("ESI", 0x40)),
        ("CALL", [addr(THUNK)]),
        ("MOV", [absolute_memory(GLOBAL + 4), reg("EAX")]),
        ("MOV", [reg("EDI"), absolute_memory(GLOBAL + 4)]),
        ("MOV", [reg("ECX"), mem("EDI", 0x40)]),
        ("MOV", [reg("EDX"), mem("ECX")]),
        ("CALL", [mem("EDX", 0x40)]),
    ]
    export = stream(body)
    return export, export["insns"][-1][0]


def case_cmp_destination():
    body = [
        ("CALL", [addr(THUNK)]),
        ("MOV", [absolute_memory(GLOBAL), reg("EAX")]),
        ("MOV", [reg("ESI"), absolute_memory(GLOBAL)]),
        ("CALL", [addr(THUNK)]),
        ("CMP", [mem("ESI", 0x40), reg("EAX")]),
        ("MOV", [reg("ECX"), mem("ESI", 0x40)]),
        ("MOV", [reg("EDX"), mem("ECX")]),
        ("CALL", [mem("EDX", 0x20)]),
    ]
    export = stream(body)
    return export, export["insns"][-1][0]


def case_sibling_slots():
    start = BASE + 0x1000
    body = [
        ("CALL", [addr(THUNK)]),
        ("MOV", [absolute_memory(GLOBAL), reg("EAX")]),
        *create_device(GLOBAL, absolute_memory(GLOBAL + 4)),
        ("MOV", [reg("ESI"), absolute_memory(GLOBAL + 4)]),
        ("TEST", [reg("EDI"), reg("EDI")]),
        ("JZ", None),
        ("MOV", [mem("EBP", -4), reg("ESI")]),
        ("JMP", None),
        ("MOV", [reg("EDI"), reg("EDI")]),
        ("MOV", [reg("ECX"), mem("EBP", -4)]),
        ("MOV", [reg("EDX"), mem("ECX")]),
        ("CALL", [mem("EDX", 0x40)]),
    ]
    body[-7] = ("JZ", [addr(start + len(body) - 4)])
    body[-5] = ("JMP", [addr(start + len(body) - 3)])
    export = stream(body)
    return export, export["insns"][-1][0]


CHAIN = 0x400


def create_device_from(holder, out):
    return [
        ("LEA", [reg("EAX"), out]),
        ("PUSH", [reg("EAX")]),
        *[("PUSH", [imm(0)]) for _ in range(6)],
        ("MOV", [reg("ECX"), absolute_memory(holder)]),
        ("MOV", [reg("EDX"), mem("ECX")]),
        ("CALL", [mem("EDX", 0x3C)]),
    ]


def case_chain(depth):
    bodies, target = [], None
    for level in range(depth, 0, -1):
        body = [("MOV", [reg("EBX"), absolute_memory(GLOBAL)])]
        for step in range(1, level + 1):
            body.append(("MOV", [reg("EBX"), mem("EBX", CHAIN + 4 * (step - 1))]))
        body.append(("MOV", [reg("ECX"), mem("EBX")]))
        if level == depth:
            target = len(body)
        body.append(("CALL", [mem("ECX", 0x40)]))
        body += create_device_from(GLOBAL, mem("EBX", CHAIN + 4 * level))
        bodies.append(body)
    bodies.append(
        [
            ("CALL", [addr(THUNK)]),
            ("MOV", [absolute_memory(GLOBAL), reg("EAX")]),
            ("MOV", [reg("EBX"), absolute_memory(GLOBAL)]),
            *create_device_from(GLOBAL, mem("EBX", CHAIN)),
        ]
    )
    export = stream(*bodies)
    return export, BASE + 0x1000 + target


def field_reach(between):
    body = [
        ("CALL", [addr(THUNK)]),
        ("MOV", [absolute_memory(GLOBAL), reg("EAX")]),
        ("MOV", [reg("ESI"), absolute_memory(GLOBAL)]),
        *create_device(GLOBAL, mem("ESI", 0x40)),
        between,
        ("MOV", [reg("ECX"), mem("EDI", 0x40)]),
        ("MOV", [reg("EDX"), mem("ECX")]),
        ("CALL", [mem("EDX", 0x40)]),
    ]
    export = stream(body)
    return export, export["insns"][-1][0]


def case_float_read():
    return field_reach(("FMUL", [mem("EDI", 0x40)]))


def case_and_write():
    return field_reach(("AND", [mem("EDI", 0x40), imm(0)]))


RULES = (
    ("field on an unproven base", case_unproven_base, "candidate"),
    ("CMP with a memory destination", case_cmp_destination, "unresolved"),
    ("store in one sibling branch", case_sibling_slots, "unresolved"),
    ("chain longer than three passes", lambda: case_chain(4), "proven"),
    ("x87 read between the store and the call", case_float_read, "candidate"),
    ("AND between the store and the call", case_and_write, "unresolved"),
)


def offsets():
    table = census.vtable()
    if os.environ.get("D3D8_CENSUS_RED") == "1":
        return {
            name: {off + 4: method for off, method in methods.items()}
            for name, methods in table.items()
        }
    return table


class AnchorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixture = json.loads(census.FIXTURE.read_text())
        cls.tiers = census.classify(fixture, offsets())
        cls.proven = {(s["va"], s["interface"], s["method"]) for s in cls.tiers["proven"]}
        cls.unresolved = {s["va"]: s["offset"] for s in cls.tiers["unresolved"]}

    def test_anchor_tiers(self):
        for name, va, interface, method, tier, offset in census.ANCHORS:
            with self.subTest(anchor=name):
                if interface is None:
                    self.assertIn(va, self.tiers["direct3dcreate8"])
                    continue
                reached = {s["va"]: s["offset"] for s in self.tiers["candidate"]}
                reached |= self.unresolved
                self.assertIn(va, reached)
                self.assertEqual(reached[va], offset)
                if tier == "proven":
                    self.assertIn((va, interface, method), self.proven)


class RuleTest(unittest.TestCase):
    def test_cases(self):
        for name, build, tier in RULES:
            with self.subTest(case=name):
                export, va = build()
                tiers = census.classify(export, offsets())
                proven = {s["va"] for s in tiers["proven"]}
                candidate = {s["va"] for s in tiers["candidate"]}
                self.assertEqual(va in proven, tier == "proven")
                self.assertEqual(va in candidate, tier == "candidate")

    def test_pass_cap(self):
        export, _va = case_chain(census.MAX_PASSES + 2)
        with self.assertRaises(SystemExit) as raised:
            census.analyse(export, offsets())
        self.assertIn(str(census.MAX_PASSES), str(raised.exception))


if __name__ == "__main__":
    unittest.main()
