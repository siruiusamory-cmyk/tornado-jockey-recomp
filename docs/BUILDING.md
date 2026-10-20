# Building and running

Windows x86. Only the census refresh and the gate need the game.

## Prerequisites

| Tool | Used for | Notes |
| :--- | :--- | :--- |
| Visual Studio 2022 x86 tools, Windows SDK 10 (`dxc.exe`) | proxy, d3d8to9, shaders to DXIL | `vswhere` finds VS; bytecode is embedded |
| Python 3.12 + `ruff`, PowerShell 7, Ghidra 12.1.4 | tools, tests, scripts, the D3D8 census | Ghidra only for `--refresh` |

## Checks, build and game copy

```powershell
pwsh -NoProfile -File tools/check.ps1        # ruff check, format check, unit tests; about 2 s
pwsh -NoProfile -File tools/build-proxy.ps1  # the proxy DLL, about 5 s from clean
python tools/unwrap.py                       # recovers a runnable image from the intake copy
python -B tools/d3d8_census.py --check       # the Stage 2 oracle
python -B tools/ghidra_symbols.py            # names port/symbols.csv in the TJG project
```

`check.ps1` reads only the `Tornado.exe` headers and its budget is 60 s. `build-proxy.ps1` writes the 32-bit
`d3d8.dll` with one export, `Direct3DCreate8`, into the build directory; it builds vendored
d3d8to9, compiles `port/proxy/render/shaders/tj.hlsl` for `vs_6_0` and `ps_6_0`, embeds both
blobs as `d3d12_shaders.h`, and links against the pinned vtable table.

The whole-file hash is not reproducible (linker time and debug path); the `.text` section is:
two builds give size 205,312 and sha256 `FB83ACFC6C37FB1B842A2550B0468956803261BF41ADDA1204C78C910A67B110`.
`--refresh` re-runs the Ghidra export over your `Tornado.exe` (about 2 min).

## Stage gate

```powershell
pwsh -NoProfile -File tools/gate.ps1            # oracles, unwrap determinism, vulture
pwsh -NoProfile -File tools/gate.ps1 -Oracle    # launches the game, compares frames
pwsh -NoProfile -File tools/gate.ps1 -Step      # fixed-step replay gate
```

Without a switch the gate launches nothing. `-Oracle` needs the game directory and both
goldens; `-Step` needs the replay fixture. It writes a record into the evidence directory.

## What needs what

| Command | Game | Goldens | Writes |
| :--- | :--- | :--- | :--- |
| `check.ps1`, `build-proxy.ps1`, `d3d8_census.py --check` | headers only for `check.ps1` | no | build directory (build only) |
| `ghidra_symbols.py` | `Tornado.exe`, the TJG project | no | the Ghidra project (build only) |
| `d3d8_census.py --refresh`, `unwrap.py` | `Tornado.exe`, the intake copy | no | the call-site table, the intake copy |
| `proxy-run.ps1`, `gate.ps1` | the game directory | `-Oracle` only | the drop-in, evidence directory |

Keep 4 GB free. `proxy-run.ps1` refuses if the game runs or a `d3d8.dll` is already in the game
directory, hashes every file before and after, and removes the drop-in in a `finally` block.
