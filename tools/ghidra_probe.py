import os
import sys

os.environ.setdefault("GHIDRA_INSTALL_DIR", r"C:\Users\hears\tools\ghidra_12.1.2_PUBLIC")
import pyghidra

EXE = r"C:\TornadoJockey\intake\installed\Tornado-WT.exe"
LOC = r"C:\TornadoJockey\build\ghidra\TJ"
PEC = (0x464000, 0x46F000)
PCC = (0x46F000, 0x470000)


def to_int(v):
    return v.getUnsignedValue() if hasattr(v, "getUnsignedValue") else int(v)


def scan(api):
    from ghidra.program.model.address import AddressSet

    prog = api.getCurrentProgram()
    listing = prog.getListing()
    fm = prog.getFunctionManager()
    calls, scal = [], []
    it = listing.getInstructions(AddressSet(prog.getMinAddress(), prog.getMaxAddress()), True)
    n = 0
    while it.hasNext():
        ins = it.next()
        n += 1
        va = ins.getAddress().getOffset()
        for k in range(ins.getNumOperands()):
            sc = ins.getScalar(k)
            if sc is None:
                continue
            v = to_int(sc)
            if PEC[0] <= v < PCC[1]:
                f = fm.getFunctionContaining(ins.getAddress())
                calls.append((va, v, ins.toString(), f.getName() if f else "?"))
            elif 0x60000 <= v < 0x72000:
                scal.append((va, v, ins.toString()))
            print(f"instructions scanned: {n}")
    print(f"calls/scalars into pecodes VA range: {len(calls)}")
    seen = {}
    for va, v, s, f in calls:
        seen.setdefault(f, []).append((va, v, s))
    print(f"distinct containing functions: {len(seen)}")
    for f, v in sorted(seen.items(), key=lambda kv: -len(kv[1]))[:25]:
        print(f"  {f}  targets={len(v)}  first={v[0][2]}")
    print(f"scalars in RVA range 0x60000..0x72000: {len(scal)}")
    for va, v, s in scal[:40]:
        print(f"  {va:08X}  {s}")


def decompile(api, va, count=1):
    from ghidra.app.decompiler import DecompInterface
    from ghidra.util.task import ConsoleTaskMonitor

    prog = api.getCurrentProgram()
    fm = prog.getFunctionManager()
    addr = api.toAddr(va)
    f = fm.getFunctionContaining(addr)
    if f is None:
        print(f"no function containing 0x{va:X}")
        return
    ifc = DecompInterface()
    ifc.openProgram(prog)
    mon = ConsoleTaskMonitor()
    got = 0
    for fn in fm.getFunctions(True):
        if fn.getEntryPoint().getOffset() < f.getEntryPoint().getOffset():
            continue
        res = ifc.decompileFunction(fn, 120, mon)
        if not res.decompileCompleted():
            print(
                f"// {fn.getName()} @ {fn.getEntryPoint()} decompile failed: {res.getErrorMessage()}"
            )
            continue
        print(
            f"// ==== {fn.getName()} @ {fn.getEntryPoint()} ({fn.getBody().getNumAddresses()} bytes)"
        )
        print(res.getDecompiledFunction().getC())
        got += 1
        if got >= count:
            break


def xrefs(api, va):
    prog = api.getCurrentProgram()
    a = api.toAddr(va)
    for r in prog.getReferenceManager().getReferencesTo(a):
        f = prog.getFunctionManager().getFunctionContaining(r.getFromAddress())
        print(f"{r.getFromAddress()}  {r.getReferenceType()}  in {f.getName() if f else '?'}")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "scan"
    with pyghidra.open_program(
        EXE,
        project_location=LOC,
        project_name="TJ",
        analyze=(os.environ.get("ANALYZE", "0") == "1"),
        program_name="TJ-WT",
    ) as api:
        if mode == "scan":
            scan(api)
        elif mode == "decompile":
            decompile(api, int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 1)
        elif mode == "xrefs":
            xrefs(api, int(sys.argv[2], 16))
        else:
            print("unknown mode")


main()
