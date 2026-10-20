import bisect
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "game" / "Tornado.exe"
EXE_SHA256 = "9D2A7C67B45A2E07893A11F9905A6FBD66A8E8F61091A8C3EA24DBF1182D55E1"
HEADER = pathlib.Path(__file__).with_name("vtable") / "d3d8.h"
HEADER_SHA256 = "413DD32B1D8472CCE3C51589F61F11402C6193616828DBDBC4C7C7A89AC46C1C"
HEADER_URL = "https://raw.githubusercontent.com/wine-mirror/wine/wine-10.0/include/d3d8.h"
HEADER_PIN = (
    "wine-10.0, tag object 2e9cb99369e5ead9d7fe9836dcd43c05e42890bc, "
    "commit b073859675060c9211fcbccfd90e4e87520dc2c2"
)
PROJECT_LOCATION = ROOT / "build" / "ghidra"
PROJECT_NAME = "TJG"
EXPORT = PROJECT_LOCATION / PROJECT_NAME / "d3d8-export.json"
EXPORT_FORMAT = 2
SITES_MD = ROOT / "port" / "D3D8_CALL_SITES.md"
FIXTURE = pathlib.Path(__file__).with_name("fixtures") / "d3d8-anchors.json"
EVIDENCE = ROOT / "evidence"


ANCHORS = (
    ("Direct3DCreate8", 0x50839A, None, "Direct3DCreate8", "proven", None),
    ("CreateDevice", 0x5070BA, "IDirect3D8", "CreateDevice", "unresolved", 0x3C),
    ("GetBackBuffer", 0x507129, "IDirect3DDevice8", "GetBackBuffer", "unresolved", 0x40),
    ("Present", 0x4211F8, "IDirect3DDevice8", "Present", "unresolved", 0x3C),
)
MAX_PASSES = 50

REG_ALIAS = {
    "EAX": "EAX",
    "AX": "EAX",
    "AL": "EAX",
    "AH": "EAX",
    "EBX": "EBX",
    "BX": "EBX",
    "BL": "EBX",
    "BH": "EBX",
    "ECX": "ECX",
    "CX": "ECX",
    "CL": "ECX",
    "CH": "ECX",
    "EDX": "EDX",
    "DX": "EDX",
    "DL": "EDX",
    "DH": "EDX",
    "ESI": "ESI",
    "SI": "ESI",
    "EDI": "EDI",
    "DI": "EDI",
    "EBP": "EBP",
    "BP": "EBP",
    "ESP": "ESP",
    "SP": "ESP",
}
VOLATILE = ("EAX", "ECX", "EDX")
BRANCH = re.compile(r"^(J|RET|LOOP)")
NO_WRITE = {"CMP", "TEST", "PUSH", "CALL", "NOP", "INT3", "BT", "FLD", "FCOMP"}


COPY = {"MOV", "MOVQ", "MOVD", "MOVSS", "MOVLPS", "MOVHPS", "MOVLPD", "MOVUPS", "MOVAPS", "MOVDQA"}


X87_WRITE = {
    "FST",
    "FSTP",
    "FIST",
    "FISTP",
    "FISTTP",
    "FBSTP",
    "FSTSW",
    "FNSTSW",
    "FSTCW",
    "FNSTCW",
    "FSAVE",
    "FNSAVE",
    "FSTENV",
    "FNSTENV",
}


def writes_memory(mnem):
    if mnem in NO_WRITE:
        return False
    if mnem.startswith("F"):
        return mnem in X87_WRITE
    return True


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def method_lists(text):
    out = {}
    for interface in ("IDirect3D8", "IDirect3DDevice8"):
        start = text.index(f"DECLARE_INTERFACE_({interface},")
        end = text.index("\n};", start)
        out[interface] = re.findall(
            r"STDMETHOD_?\((?:[^,()]+,)?\s*([A-Za-z_]\w*)\s*\)\s*\(", text[start:end]
        )
    return out


def vtable(text=None):
    if text is None:
        if sha256(HEADER) != HEADER_SHA256:
            raise SystemExit("tools/vtable/d3d8.h does not match its pin")
        text = HEADER.read_text(encoding="latin1")
    return {
        name: {i * 4: method for i, method in enumerate(names)}
        for name, names in method_lists(text).items()
    }


def operand(kind, objects):
    return [kind, objects]


def objects_of(op):
    return op[1]


def kind_of(op):
    return op[0]


def registers(op):
    return [o[1] for o in op[1] if o[0] == "R"]


def scalars(op):
    return [o[1] for o in op[1] if o[0] == "S"]


def absolute(op):
    for o in op[1]:
        if o[0] == "A":
            return o[1]
    return None


def gpr(name):
    return REG_ALIAS.get(name.upper())


def memreference(op):
    if kind_of(op) != "mem":
        return None
    regs = [r for r in (gpr(n) for n in registers(op)) if r]
    vals = scalars(op)
    if len(regs) == 1 and len(vals) <= 1:
        return regs[0], (vals[0] if vals else 0)
    if not regs and len(vals) == 1:
        return None, vals[0]
    if not regs and absolute(op) is not None and not vals:
        return None, absolute(op)
    return None


class State:
    def __init__(self, methods, create8, field_keys, seed=None):
        self.methods = methods
        self.create8 = create8
        self.field_keys = field_keys
        self.offsets = {off for table in methods.values() for off in table}
        self.globals = dict(seed.globals) if seed else {}
        self.fields = dict(seed.fields) if seed else {}
        self.ambiguous = set(seed.ambiguous) if seed else {}
        self.field_store = dict(seed.field_store) if seed else {}
        self.reg = {}
        self.slots = {}
        self.pushes = []
        self.sites = {}
        self.unresolved = {}
        self.direct3dcreate8 = []
        self.createdevice = []
        self.function_entry = None
        self.va = None

    def loc(self, key):
        if key[0] == "g":
            return self.globals.get(key[1])
        if key[0] == "s":
            return self.slots.get(key)
        return None if key in self.ambiguous else self.fields.get(key)

    def key(self, base, disp):
        if base is None:
            return ("g", disp)
        held = self.reg.get(base, (None, None))[0]
        if isinstance(held, tuple) and held[0] == "addr" and disp == 0:
            return held[1]
        if base in ("EBP", "ESP"):
            return ("s", self.function_entry, base, disp)
        if self.field_keys == "displacement":
            return ("f", disp)
        if isinstance(held, tuple) and held[0] == "iface" and held[3] is not None:
            return ("f", held[3], disp)
        return None

    def store(self, key, value):
        if key is None or not isinstance(value, tuple) or value[0] not in ("iface", "vtable"):
            return
        if key[0] == "s":
            self.slots[key] = value
            return
        if value[0] == "iface" and not value[2]:
            return
        if key[0] == "g":
            self.globals[key[1]] = value
            return
        if value[0] == "iface":
            self.field_store[key] = self.va
        old = self.fields.get(key)
        if old is not None and old[:2] != value[:2]:
            self.fields.pop(key)
            self.ambiguous.add(key)
        elif key not in self.ambiguous:
            self.fields[key] = value

    def loaded(self, value, key, va):
        if isinstance(value, tuple) and value[0] == "iface":
            return ("iface", value[1], False, key, va)
        return value

    def clear(self, key):
        if key[0] == "g":
            self.globals.pop(key[1], None)
        elif key[0] == "s":
            self.slots.pop(key, None)
        else:
            self.fields.pop(key, None)

    def source(self, op):
        kind = kind_of(op)
        if kind == "reg":
            regs = registers(op)
            if len(regs) == 1:
                return self.reg.get(gpr(regs[0]), (None, "argument"))
            return None, "unsupported-operand"
        if kind == "imm":
            return None, "constant"
        refd = memreference(op)
        if refd is None:
            return None, "unsupported-operand"
        base, disp = refd
        held = self.reg.get(base, (None, None))[0] if base else None
        if disp == 0 and isinstance(held, tuple) and held[0] == "iface":
            return ("vtable", held[1], held[3], held[4] if len(held) > 4 else None), "vtable"
        key = self.key(base, disp)
        if key is None:
            return None, "unproven-base"
        return self.loaded(self.loc(key), key, self.va), "unknown-memory"

    def address(self, op):
        absolute_value = absolute(op)
        if kind_of(op) == "addr" and absolute_value is not None:
            return ("g", absolute_value)
        refd = memreference(op)
        if refd is None:
            return None
        return self.key(*refd)

    def trace(self, identity, load_va):
        if isinstance(identity, tuple) and identity[0] == "f":
            return (self.field_store.get(identity), load_va, identity[-1])
        return None

    def step(self, va, mnem, ops):
        self.va = va
        if mnem == "CALL":
            self.call(va, ops)
            return
        if BRANCH.match(mnem) or mnem == "RET":
            for name in VOLATILE:
                self.reg.pop(name, None)
            self.pushes = []
            return
        if mnem == "PUSH" and ops:
            self.pushes.append(self.source(ops[0])[0])
            return
        if not ops:
            return
        dst = ops[0]
        src = ops[1] if len(ops) > 1 else None
        if kind_of(dst) == "reg" and len(registers(dst)) == 1:
            name = gpr(registers(dst)[0])
            if name is None:
                return
            if mnem == "LEA" and src is not None:
                key = self.address(src)
                self.reg[name] = (("addr", key), "address") if key else (None, "computed")
            elif mnem in COPY and src is not None:
                self.reg[name] = self.source(src)
            elif mnem == "POP" or mnem not in NO_WRITE:
                self.reg[name] = (None, "computed")
            return
        key = self.address(dst)
        if key is None:
            return
        if mnem in COPY and src is not None:
            self.store(key, self.source(src)[0])
        elif writes_memory(mnem):
            self.clear(key)

    def call(self, va, ops):
        if not ops:
            return
        target = absolute(ops[0]) if kind_of(ops[0]) == "addr" else None
        refd = memreference(ops[0])
        receiver = gpr(registers(ops[0])[0]) if refd and len(registers(ops[0])) == 1 else None
        create8 = target is not None and target in self.create8
        if create8:
            self.direct3dcreate8.append(va)
        if receiver is not None and refd[1] in self.offsets:
            self.site(va, refd[1], receiver)
        self.pushes = []
        for name in VOLATILE:
            self.reg.pop(name, None)
        if create8:
            self.reg["EAX"] = (("iface", "IDirect3D8", True, ("call", va)), "return")

    def site(self, va, disp, receiver):
        value, reason = self.reg.get(receiver, (None, "argument"))
        if not isinstance(value, tuple):
            self.note_unresolved(va, disp, reason)
            return
        if value[0] == "addr":
            self.note_unresolved(va, disp, "indirect-function-pointer")
            return
        if value[0] != "vtable":
            self.note_unresolved(va, disp, "receiver-is-object-not-vtable")
            return
        interface = value[1]
        method = self.methods[interface].get(disp)
        if method is None:
            self.note_unresolved(va, disp, f"offset-not-in-{interface}")
            return
        self.note_site(va, interface, method, disp, self.trace(value[2], value[3]))
        if interface == "IDirect3D8" and method == "CreateDevice":
            self.createdevice.append(va)
            out = self.out_slot()
            if out is not None:
                self.store(out, ("iface", "IDirect3DDevice8", True, ("call", va)))

    def note_site(self, va, interface, method, disp, trace):
        self.sites[va] = (va, interface, method, disp, trace)
        self.unresolved.pop(va, None)

    def note_unresolved(self, va, disp, reason):
        self.unresolved[va] = (va, disp, reason)
        self.sites.pop(va, None)

    def out_slot(self):
        args = self.pushes[-7:]
        if len(args) == 7 and isinstance(args[0], tuple) and args[0][0] == "addr":
            return args[0][1]
        return None


def analyse(export, methods, field_keys="identity"):
    starts, ends, entries = [], [], []
    for entry, _name, body_start, body_end in sorted(export["functions"], key=lambda f: f[2]):
        starts.append(body_start)
        ends.append(body_end)
        entries.append(entry)

    def function_of(va):
        index = bisect.bisect_right(starts, va) - 1
        if index >= 0 and va <= ends[index]:
            return entries[index]
        return None

    groups = {}
    for index, (va, _mnem, _ops) in enumerate(export["insns"]):
        groups.setdefault(function_of(va), []).append(index)
    create8 = {export["create8_thunk"]} if export.get("create8_thunk") else set()
    if export.get("create8_slot"):
        create8.add(export["create8_slot"])
    state = None
    previous = None
    for passes in range(1, MAX_PASSES + 1):
        state = State(methods, create8, field_keys, seed=state)
        for entry in sorted(groups, key=lambda value: (value is None, value or 0)):
            state.function_entry = entry
            run_function(state, export["insns"], groups[entry])
        current = snapshot(state)
        if current == previous:
            break
        previous = current
    else:
        raise SystemExit(f"the census did not reach a fixed point in {MAX_PASSES} passes")
    names = {f[0]: f[1] for f in export["functions"]}
    resolved = {}
    for va, interface, method, disp, trace in state.sites.values():
        resolved[va] = {
            "va": va,
            "function": names.get(function_of(va), "?"),
            "interface": interface,
            "method": method,
            "offset": disp,
            "trace": trace,
        }
    unresolved = {}
    for va, disp, reason in state.unresolved.values():
        unresolved.setdefault(
            va,
            {
                "va": va,
                "function": names.get(function_of(va), "?"),
                "offset": disp,
                "reason": reason,
            },
        )
    return {
        "resolved": sorted(resolved.values(), key=lambda s: s["va"]),
        "unresolved": sorted(unresolved.values(), key=lambda s: s["va"]),
        "direct3dcreate8": sorted(set(state.direct3dcreate8)),
        "createdevice": sorted(set(state.createdevice)),
        "function_of": function_of,
        "names": names,
        "callers": callers(export, function_of),
        "passes": passes,
    }


def snapshot(state):
    return (
        tuple(sorted((repr(key), repr(value)) for key, value in state.globals.items())),
        tuple(sorted((repr(key), repr(value)) for key, value in state.fields.items())),
        tuple(sorted(map(repr, state.ambiguous))),
        tuple(sorted(state.sites)),
        tuple(sorted(state.unresolved)),
    )


def run_function(state, insns, indices):
    body = [insns[i] for i in indices]
    if not body:
        return
    state.slots = {}
    position = {va: index for index, (va, _mnem, _ops) in enumerate(body)}
    leaders = {0}
    for index, (_va, mnem, ops) in enumerate(body):
        branch = mnem == "RET" or BRANCH.match(mnem)
        if not branch:
            continue
        if index + 1 < len(body):
            leaders.add(index + 1)
        if ops and kind_of(ops[0]) == "addr":
            target = absolute(ops[0])
            if target in position:
                leaders.add(position[target])
    order = sorted(leaders)
    blocks = [(order[k], order[k + 1] - 1) for k in range(len(order) - 1)]
    blocks.append((order[-1], len(body) - 1))
    block_of = {}
    for number, (start, end) in enumerate(blocks):
        for index in range(start, end + 1):
            block_of[index] = number
    successors = []
    for start, end in blocks:
        _va, mnem, ops = body[end]
        follow = [block_of[end + 1]] if end + 1 < len(body) else []
        if mnem == "RET":
            successors.append([])
        elif mnem == "JMP":
            target = position.get(absolute(ops[0])) if ops and kind_of(ops[0]) == "addr" else None
            successors.append([block_of[target]] if target is not None else [])
        elif ops and kind_of(ops[0]) == "addr" and BRANCH.match(mnem):
            target = position.get(absolute(ops[0]))
            jump = [block_of[target]] if target is not None else []
            successors.append(jump + follow)
        else:
            successors.append(follow)
    incoming = {0: ({}, {})}
    worklist = [0]
    while worklist:
        number = worklist.pop()
        regs, slots = incoming.get(number, ({}, {}))
        state.reg = dict(regs)
        state.slots = dict(slots)
        state.pushes = []
        for index in range(*_bounds(blocks[number])):
            va, mnem, ops = body[index]
            state.step(va, mnem, ops)
        for successor in successors[number]:
            out = (dict(state.reg), dict(state.slots))
            merged = incoming.get(successor)
            if merged is None:
                incoming[successor] = out
                worklist.append(successor)
                continue
            combined = (
                {
                    name: value
                    for name, value in merged[0].items()
                    if name in out[0] and out[0][name][0] == value[0]
                },
                {
                    key: value
                    for key, value in merged[1].items()
                    if key in out[1] and out[1][key] == value
                },
            )
            if combined != merged:
                incoming[successor] = combined
                worklist.append(successor)


def _bounds(block):
    return block[0], block[1] + 1


def callers(export, function_of):
    out = {}
    for va, mnem, ops in export["insns"]:
        if mnem != "CALL" or not ops or kind_of(ops[0]) != "addr":
            continue
        target = absolute(ops[0])
        if target is None:
            continue
        callee, caller = function_of(target), function_of(va)
        if callee is not None and caller is not None:
            out.setdefault(callee, set()).add(caller)
    return {key: sorted(value) for key, value in out.items()}


def run_export():
    import os
    import warnings

    warnings.filterwarnings("ignore")
    os.environ.setdefault("GHIDRA_INSTALL_DIR", r"C:\Users\hears\tools\ghidra_12.1.2_PUBLIC")
    import pyghidra

    with pyghidra.open_program(
        str(EXE),
        project_location=str(PROJECT_LOCATION),
        project_name=PROJECT_NAME,
        analyze=not (PROJECT_LOCATION / PROJECT_NAME).exists(),
        program_name="Tornado",
    ) as api:
        from ghidra.program.model.lang import Register
        from ghidra.program.model.scalar import Scalar

        prog = api.getCurrentProgram()
        listing = prog.getListing()
        symbols = prog.getSymbolTable()
        text = prog.getMemory().getBlock(".text")
        insns = []
        thunk = None
        slot = None
        iterator = listing.getInstructions(text.getStart(), True)
        while iterator.hasNext():
            ins = iterator.next()
            if not text.contains(ins.getAddress()):
                break
            va = int(ins.getAddress().getOffset())
            mnem = ins.getMnemonicString()
            ops = []
            for index in range(ins.getNumOperands()):
                objects = []
                for obj in ins.getOpObjects(index):
                    if isinstance(obj, Register):
                        objects.append(["R", obj.getName()])
                    elif isinstance(obj, Scalar):
                        objects.append(["S", int(obj.getUnsignedValue())])
                    else:
                        objects.append(["A", int(obj.getOffset())])
                text_repr = ins.getDefaultOperandRepresentation(index)
                if "[" in text_repr:
                    kind = "mem"
                elif any(o[0] == "A" for o in objects):
                    kind = "addr"
                elif any(o[0] == "S" for o in objects):
                    kind = "imm"
                else:
                    kind = "reg"
                ops.append([kind, objects])
            if mnem in ("JMP", "CALL"):
                for reference in ins.getOperandReferences(0):
                    symbol = symbols.getPrimarySymbol(reference.getToAddress())
                    if symbol is not None and "Direct3DCreate8" in symbol.getName():
                        if mnem == "JMP":
                            thunk = va
                        else:
                            slot = int(reference.getToAddress().getOffset())
            insns.append([va, mnem, ops])
        functions = [
            [
                int(f.getEntryPoint().getOffset()),
                f.getName(),
                int(f.getBody().getMinAddress().getOffset()),
                int(f.getBody().getMaxAddress().getOffset()),
            ]
            for f in prog.getFunctionManager().getFunctions(True)
        ]
        export = {
            "format": EXPORT_FORMAT,
            "image_sha256": EXE_SHA256,
            "image_base": int(prog.getImageBase().getOffset()),
            "insns": insns,
            "functions": functions,
            "create8_thunk": thunk,
            "create8_slot": slot,
        }
    EXPORT.parent.mkdir(parents=True, exist_ok=True)
    EXPORT.write_text(json.dumps(export))
    return export


def load_export(refresh=False):
    if not refresh and EXPORT.exists():
        cached = json.loads(EXPORT.read_text())
        if cached.get("image_sha256") == EXE_SHA256 and cached.get("format") == EXPORT_FORMAT:
            return cached
    return run_export()


def summary(sites):
    per = {}
    for site in sites:
        entry = per.setdefault(
            (site["interface"], site["method"]), {"offset": site["offset"], "count": 0, "vas": []}
        )
        entry["count"] += 1
        if len(entry["vas"]) < 3:
            entry["vas"].append(site["va"])
    return sorted(per.items(), key=lambda item: (-item[1]["count"], item[0]))


def classify(export, methods):
    proven = analyse(export, methods)
    legacy = analyse(export, methods, "displacement")
    reasons = {site["va"]: site["reason"] for site in proven["unresolved"]}
    candidates = [
        dict(site, tier="candidate")
        for site in legacy["resolved"]
        if reasons.get(site["va"]) == "unproven-base"
    ]
    claimed = {site["va"] for site in proven["resolved"]} | {site["va"] for site in candidates}
    return {
        "proven": [dict(site, tier="proven") for site in proven["resolved"]],
        "candidate": sorted(candidates, key=lambda site: site["va"]),
        "unresolved": [site for site in proven["unresolved"] if site["va"] not in claimed],
        "direct3dcreate8": proven["direct3dcreate8"],
        "createdevice": legacy["createdevice"],
        "function_of": proven["function_of"],
        "names": proven["names"],
        "callers": proven["callers"],
        "passes": (proven["passes"], legacy["passes"]),
    }


def trace_cells(trace):
    if not trace:
        return ("-", "-", "-")
    store_va, load_va, disp = trace
    return (
        "-" if store_va is None else f"0x{store_va:08X}",
        "-" if load_va is None else f"0x{load_va:08X}",
        f"0x{disp:02X}",
    )


def call_chains(result, present_entries):
    lines = []
    for function in present_entries:
        lines.append(
            "- `{}` 0x{:08X} calls Present.".format(result["names"].get(function, "?"), function)
        )
        level, frontier = 1, sorted(result["callers"].get(function, []))
        while frontier and level <= 3:
            labels = ["`{}` 0x{:08X}".format(result["names"].get(f, "?"), f) for f in frontier]
            lines.append(f"  - caller level {level}: {', '.join(labels)}")
            frontier = sorted({c for f in frontier for c in result["callers"].get(f, [])})
            level += 1
    return lines


def render(result):
    rows = summary(result["proven"])
    present_entries = sorted(
        {
            result["function_of"](site["va"])
            for site in result["proven"]
            if site["method"] == "Present"
        }
    )
    lines = [
        "# D3D8 call sites",
        "",
        "Generated by `python tools\\d3d8_census.py`; do not edit by hand.",
        "",
        f"- image: the game copy's `Tornado.exe` sha256 `{EXE_SHA256}`",
        f"- vtable source: {HEADER_URL}",
        f"- pin: {HEADER_PIN}",
        f"- header sha256: `{HEADER_SHA256}`",
        "- trace rule and its limits: the docstring of `tools\\d3d8_census.py`",
        "",
        (
            f"Totals: {len(result['proven'])} proven sites, "
            f"{len(result['candidate'])} candidates, "
            f"{len(result['unresolved'])} unresolved, "
            f"{len({(s['interface'], s['method']) for s in result['proven']})} proven methods, "
            f"{len({(s['interface'], s['method']) for s in result['candidate']})} candidate methods, "
            f"{len(result['direct3dcreate8'])} Direct3DCreate8 calls, "
            f"{result['passes'][0]} passes proven, {result['passes'][1]} passes candidate."
        ),
        "",
        (
            "A candidate is a site the proven rule leaves `unproven-base` and the old "
            "displacement-keyed field rule would resolve. It is INFERRED, never counted "
            "as resolved, and waits for the Stage 3 d3d8.dll proxy call log."
        ),
        "",
        "## Per method, proven",
        "",
        "| method | offset | sites | first VAs |",
        "|---|---|---|---|",
    ]
    for (interface, method), entry in rows:
        first = ", ".join(f"0x{va:08X}" for va in entry["vas"])
        lines.append(
            f"| {interface}::{method} | 0x{entry['offset']:02X} | {entry['count']} | {first} |"
        )
    lines += ["", "## Render loop entry", ""]
    lines += call_chains(result, present_entries) or ["- no proven Present site."]
    shaped = [site for site in result["candidate"] + result["unresolved"] if site["offset"] == 0x3C]
    if not present_entries and shaped:
        lines.append("")
        lines.append(
            "Present-shaped calls (offset 0x3C) the trace rule refuses to name, first three:"
        )
        lines += [
            f"- 0x{site['va']:08X} in `{site['function']}` ({site.get('tier', 'unresolved')})"
            for site in shaped[:3]
        ]
    lines += [
        "",
        "## Proven sites",
        "",
        "| VA | function | interface | method | offset |",
        "|---|---|---|---|---|",
    ]
    for site in result["proven"]:
        lines.append(
            "| 0x{:08X} | {} | {} | {} | 0x{:02X} |".format(
                site["va"], site["function"], site["interface"], site["method"], site["offset"]
            )
        )
    lines += [
        "",
        "## Candidates",
        "",
        "Displacement-keyed field reach: the store VA that would type it, the load VA that",
        "reads it, and the displacement. INFERRED, not proven.",
        "",
        "| VA | function | interface | method | offset | store VA | load VA | displacement |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for site in result["candidate"]:
        store_va, load_va, disp = trace_cells(site["trace"])
        lines.append(
            "| 0x{:08X} | {} | {} | {} | 0x{:02X} | {} | {} | {} |".format(
                site["va"],
                site["function"],
                site["interface"],
                site["method"],
                site["offset"],
                store_va,
                load_va,
                disp,
            )
        )
    lines += [
        "",
        "## Unresolved",
        "",
        "Candidates whose receiver does not trace by the documented rule.",
        "They are listed, never guessed.",
        "",
        "| VA | function | offset | reason |",
        "|---|---|---|---|",
    ]
    for site in result["unresolved"]:
        lines.append(
            "| 0x{:08X} | {} | 0x{:02X} | {} |".format(
                site["va"], site["function"], site["offset"], site["reason"]
            )
        )
    return "\n".join(lines) + "\n"


def check(result):
    problems = []
    proven = {(s["va"], s["interface"], s["method"]) for s in result["proven"]}
    unresolved = {s["va"]: s for s in result["unresolved"]}
    for name, va, interface, method, tier, offset in ANCHORS:
        if tier == "proven":
            if interface is None:
                if va not in result["direct3dcreate8"]:
                    problems.append(f"{name}: 0x{va:08X} is not a proven Direct3DCreate8 site")
            elif (va, interface, method) not in proven:
                problems.append(f"{name}: 0x{va:08X} is not a proven {interface}::{method}")
            continue
        site = unresolved.get(va)
        if site is None:
            problems.append(f"{name}: 0x{va:08X} must be listed unresolved, not resolved")
        elif site["offset"] != offset:
            problems.append(
                f"{name}: 0x{va:08X} is unresolved at 0x{site['offset']:02X}, not 0x{offset:02X}"
            )
    return problems


def write_fixture(export):
    wanted = [va for _name, va, _interface, _method, _tier, _offset in ANCHORS]
    if export.get("create8_thunk"):
        wanted.append(export["create8_thunk"])
    bounds = sorted((f[2], f[3], f[0]) for f in export["functions"])
    keep = {entry for start, end, entry in bounds if any(start <= va <= end for va in wanted)}
    starts = [start for start, _e, _n in bounds]
    fixture = {
        "format": EXPORT_FORMAT,
        "image_sha256": export["image_sha256"],
        "image_base": export["image_base"],
        "create8_thunk": export["create8_thunk"],
        "create8_slot": export.get("create8_slot"),
        "functions": [f for f in export["functions"] if f[0] in keep],
        "insns": [],
    }
    for va, mnem, ops in export["insns"]:
        index = bisect.bisect_right(starts, va) - 1
        if index >= 0 and va <= bounds[index][1] and bounds[index][2] in keep:
            fixture["insns"].append([va, mnem, ops])
    FIXTURE.parent.mkdir(parents=True, exist_ok=True)
    FIXTURE.write_text(json.dumps(fixture))


def main():
    if sha256(EXE) != EXE_SHA256:
        raise SystemExit(f"{EXE} is not the repaired image")
    methods = vtable()
    export = load_export(refresh="--refresh" in sys.argv)
    tiers = classify(export, methods)
    problems = check(tiers)
    print(
        f"{len(tiers['proven'])} proven, {len(tiers['candidate'])} candidates, "
        f"{len(tiers['unresolved'])} unresolved, "
        f"{len({(s['interface'], s['method']) for s in tiers['proven']})} proven methods, "
        f"{len({(s['interface'], s['method']) for s in tiers['candidate']})} candidate methods, "
        f"create8 {len(tiers['direct3dcreate8'])}, "
        f"passes {tiers['passes'][0]}/{tiers['passes'][1]}"
    )
    if "--fixture" in sys.argv:
        write_fixture(export)
        print(f"fixture {FIXTURE}")
    if "--check" in sys.argv or problems:
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        record = [
            (
                f"proven {len(tiers['proven'])} candidate {len(tiers['candidate'])} "
                f"unresolved {len(tiers['unresolved'])}"
            )
        ]
        record += [f"ANCHOR {name} 0x{va:08X} {tier}" for name, va, _i, _m, tier, _o in ANCHORS]
        record += ["FAIL " + problem for problem in problems] or ["PASS"]
        (EVIDENCE / "d3d8-census-check.txt").write_text("\n".join(record) + "\n")
        if problems:
            raise SystemExit("self-check failed:\n" + "\n".join(problems))
        print("self-check PASS")
    SITES_MD.write_text(render(tiers))


if __name__ == "__main__":
    main()
