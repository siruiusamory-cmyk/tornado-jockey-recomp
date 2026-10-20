import os
import sys
from pathlib import Path

os.environ.setdefault("GHIDRA_INSTALL_DIR", r"C:\Users\hears\tools\ghidra_12.1.2_PUBLIC")
import check_symbols
import pyghidra

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "game" / "Tornado.exe"
PROJECT = ROOT / "build" / "ghidra" / "TJG"
CSV = ROOT / "port" / "symbols.csv"


def apply(api):
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.program.model.listing import CommentType
    from ghidra.program.model.symbol import SourceType

    program = api.getCurrentProgram()
    functions = program.getFunctionManager()
    symbols = program.getSymbolTable()
    listing = program.getListing()
    rows, _ = check_symbols.load_rows(CSV)
    applied = renamed = created = unchanged = 0
    transaction = program.startTransaction("symbols")
    try:
        for va, kind, name, ref, _ in rows:
            address = api.toAddr(va)
            applied += 1
            if kind == "FUNCTION":
                function = functions.getFunctionAt(address)
                if function is None:
                    if listing.getInstructionAt(address) is None:
                        api.disassemble(address)
                    if listing.getInstructionAt(address) is None:
                        raise ValueError(f"no instruction at 0x{va:06X}")
                    command = CreateFunctionCmd(address)
                    if not command.applyTo(program):
                        raise ValueError(
                            f"create function failed at 0x{va:06X}: {command.getStatusMsg()}"
                        )
                    function = functions.getFunctionAt(address)
                    function.setName(name, SourceType.USER_DEFINED)
                    created += 1
                elif function.getName() != name:
                    function.setName(name, SourceType.USER_DEFINED)
                    renamed += 1
                else:
                    unchanged += 1
            else:
                if any(symbol.getName() == name for symbol in symbols.getSymbols(address)):
                    unchanged += 1
                else:
                    symbols.createLabel(address, name, SourceType.USER_DEFINED)
                    created += 1
            listing.setComment(address, CommentType.PLATE, f"ref: {ref}")
        program.endTransaction(transaction, True)
    except Exception:
        program.endTransaction(transaction, False)
        raise
    print(f"applied {applied} renamed {renamed} created {created} unchanged {unchanged}")


def main():
    errors = check_symbols.check(ROOT, EXE)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    if not PROJECT.is_dir():
        print(f"missing Ghidra project {PROJECT}", file=sys.stderr)
        return 1
    with pyghidra.open_program(
        str(EXE),
        project_location=str(PROJECT),
        project_name="TJG",
        analyze=False,
        program_name="Tornado",
    ) as api:
        apply(api)
    return 0


if __name__ == "__main__":
    sys.exit(main())
