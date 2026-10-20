# Code quality bar

The model is the two shipped reference ports: `hedge-dev/UnleashedRecomp` and
`goliathret/EdgeOfTimeRecomp`. Python tools are plain stdlib scripts, one job each.

## Eight rules

1. **No comment restates the code.** A comment says why or cites evidence: an address, a
   document, a measurement. `# loop over sections` is noise.
2. **No dead code, no commented-out code, no "for later" hooks.** Delete unused options,
   flags and abstractions.
3. **No catch-and-rethrow, no blanket `try`/`except`, no silent fallback.** An error fails
   loudly with the measured value that broke. A tool that cannot verify its input stops.
4. **No duplicated logic.** Reuse the helper; two copies of a rule drift into a wrong artifact.
5. **Names say what the thing is.** A guest address stays `sub_XXXXXXXX` until proven, then
   carries its proven name and the evidence.
6. **Every non-trivial function, tool or hook has a test or self-check that fails on a wrong
   result.** The check states intent, not implementation.
7. **No generated filler.** No banner comments, emoji, "robust", "comprehensive" or
   "seamless"; no empty README section.
8. **Formatting is mechanical.** Python: `ruff check tools`. C++: clang-format with the
   reference project's `.clang-format`, copied and cited.

## Lean verification

Neither suite grows to hundreds of tests, and no full suite runs for an hour.

- **L1, one golden oracle per stage.** Stage 1 is the menu frame; Stage 2 the census self-check.
- **L2, unit tests only where they earn it.** Parsers, byte transforms and RED-first
  regressions get one table-driven test.
- **L3/L4, two entry points.** `check.ps1` (60 s): ruff check, ruff format check, unit tests.
  `gate.ps1` (10 min): stage oracles, unwrap determinism (two hashes), `vulture
  --min-confidence 80 tools`, a record in the evidence directory.
- **L7, tests come from the spec or the bug report**, never from reading the new code.
- **L8, quiet output.** One line per step.
- **L9, formatters modify only locally; the gate only checks.**

## Applied here

- `tools/unwrap.py` aborts on a rule violation: each differing run is at most one launcher
  window and outside the IAT, and the stub `.rsrc` matches.
- The measurement tools (`pe_report.py`, `imports.py`, `find_iat_calls.py`,
  `oracle-launch.ps1`, `ghidra_probe.py`) read their input and print facts; none writes into
  the intake copy.
- Every stage freeze draws one independent adversarial review of the diff. Correctness and
  slop findings block; style nits do not.
- Python formatting is `ruff format` at line length 100 (`pyproject.toml`), the same config the
  check reads.
