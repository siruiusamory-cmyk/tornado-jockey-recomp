"""Resolve the Stage 3 d3d8 proxy log to census call sites.

The log is written by port/proxy/d3d8_proxy.cpp: one line per (interface, slot,
caller return address) per flush, carrying the calls since the previous flush.
Return addresses are mapped to CALL instructions with the same Ghidra listing the
census uses (tools/d3d8_census.py load_export, filled by run_export), so the call
sites are compared against the census anchors in exactly the census coordinates.

usage: python -B tools/proxy_log.py evidence/proxy-log-<date>.txt
"""

import argparse
import itertools
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import d3d8_census

CALL = re.compile(r"^call ([01]) ([0-9]{1,3}) ([A-Za-z_]\w*) ([0-9A-F]{8}) ([0-9]+)$")
ARGS = re.compile(r"^# args ([01]) ([0-9]{1,3}) ([A-Za-z_]\w*) ((?:[0-9A-F]{8} ){6})([0-9]+)$")
HEADER = re.compile(r"^# d3d8 proxy log v1 image_base 0x([0-9A-F]{8})\b")
LONGEST_X86_INSN = 15


CANDIDATE = ("Release (candidate)", 0x44BC93, "IDirect3D8", "Release", "candidate", 0x08)


PRESENT_MENU = ("Present (menu)", 0x4DE64A, "IDirect3DDevice8", "Present", "runtime", 0x3C)


def parse_log(text):
    base = None
    records = []
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            found = HEADER.match(line)
            if found:
                base = int(found.group(1), 16)
            continue
        found = CALL.match(line)
        if not found:
            raise ValueError(f"line {number}: not a proxy call line: {line!r}")
        records.append(
            {
                "iface": int(found.group(1)),
                "slot": int(found.group(2)),
                "name": found.group(3),
                "caller": int(found.group(4), 16),
                "count": int(found.group(5)),
            }
        )
    if base is None:
        raise ValueError("no '# d3d8 proxy log v1 image_base' header")
    if not records:
        raise ValueError("the log holds no call line")
    return base, records


def parse_args(text):
    sets = {}
    for line in text.splitlines():
        line = line.strip()
        if line == "# args armed":
            continue
        if not line.startswith("# args "):
            continue
        found = ARGS.match(line)
        if not found:
            raise ValueError(f"not an argument line: {line!r}")
        values = tuple(int(value, 16) for value in found.group(4).split())
        key = (int(found.group(1)), int(found.group(2)), found.group(3), values)
        sets[key] = max(sets.get(key, 0), int(found.group(5)))
    return sets


def call_end_map(insns):
    ends = {}
    for (va, mnemonic, _), (next_va, _, _) in itertools.pairwise(insns):
        if mnemonic == "CALL" and 0 < next_va - va <= LONGEST_X86_INSN:
            ends[next_va] = va
    return ends


def resolve(base, records, ends, image_base):
    sites = {}
    for record in records:
        end = record["caller"] - base + image_base
        key = (record["iface"], record["slot"], record["name"], ends.get(end))
        sites[key] = sites.get(key, 0) + record["count"]
    return sites


def verdicts(sites, anchors):
    by_va = {}
    for (iface, slot, name, va), count in sites.items():
        by_va.setdefault(va, []).append((iface, slot, name, count))
    rows = []
    for name, va, _iface, method, _tier, _offset in anchors:
        here = sorted(by_va.get(va, []))
        if any(slot_name == method for _i, _s, slot_name, _c in here):
            state = "CONFIRMED"
        elif here:
            state = "REFUTED"
        else:
            state = "NOT SEEN"
        rows.append((name, va, state, here))
    return rows


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "log", type=pathlib.Path, help="proxy log written by build\\proxy\\d3d8.dll"
    )
    parser.add_argument("--anchors", action="store_true", help="show the four Stage 3 anchors only")
    parser.add_argument("--args", action="store_true", help="show the recorded argument sets")
    args = parser.parse_args(argv)

    export = d3d8_census.load_export()
    text = args.log.read_text()
    base, records = parse_log(text)
    sites = resolve(base, records, call_end_map(export["insns"]), export["image_base"])
    calls = sum(record["count"] for record in records)
    unmatched = sum(count for (_, _, _, va), count in sites.items() if va is None)
    print(f"{len(records)} log lines, {calls} calls, {len(sites)} (slot, call site) pairs")
    print(f"{unmatched} calls with no CALL instruction ending at the return address")

    anchors = [anchor for anchor in d3d8_census.ANCHORS if anchor[0] != "Direct3DCreate8"]
    anchors += [CANDIDATE, PRESENT_MENU]
    for name, va, state, here in verdicts(sites, anchors):
        seen = ", ".join(
            f"{slot_name}[{iface}.{slot}] x{count}" for iface, slot, slot_name, count in here
        )
        print(f"ANCHOR {name:<16} 0x{va:08X} {state:<9} {seen}")

    if args.args:
        sets = parse_args(text)
        print(
            "interface slot name                     arguments                                                calls"
        )
        for (iface, slot, name, values), count in sorted(
            sets.items(), key=lambda item: (-item[1], item[0][1], item[0][2], item[0][3])
        ):
            shown = " ".join(f"{value:08X}" for value in values)
            print(f"{iface:>9} {slot:>4} {name:<24} {shown} {count:>6}")

    if not args.anchors:
        print("interface slot name                     call site  calls")
        for (iface, slot, name, va), count in sorted(sites.items(), key=lambda item: -item[1]):
            where = "-" if va is None else f"0x{va:08X}"
            print(f"{iface:>9} {slot:>4} {name:<24} {where:>10} {count:>6}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
