# Tornado Jockey Recomp

**The 2006 game, recovered and still running, with its Direct3D 8 layer replaced by a proxy and a native renderer.**

![License](https://img.shields.io/badge/license-GPL--2.0--or--later-blue.svg) ![Platform](https://img.shields.io/badge/platform-Windows%20x86-blue.svg)

`Tornado.exe` runs unmodified on disk. A `d3d8.dll` beside it carries every port addition:
keyboard steering, a fixed simulation step, a replay, a translation to Direct3D 9, and a
native Direct3D 12 backend that draws the title screen. Game files are referenced by path and
never tracked, quoted or distributed; measurements are in [`port/STATUS.md`](port/STATUS.md).

Listed on [recomp.fyi](https://recomp.fyi) from [.recomp.json](.recomp.json).

| Area | State |
| :--- | :--- |
| Proxy | exports `Direct3DCreate8`, wraps all 113 vtable slots, logs and captures on demand |
| Control | keyboard steering on the game's own pointer channel; fixed 1/60 s step under `faithful=0` |
| Replay | frame-keyed input, virtual clock from process start, offline boot fetch |
| Legacy renderer | every drawing call translated d3d8 -> d3d9; window modes and present scaling |
| D3D12 renderer | device, present, textures, draws, fixed-function state; menu frame accepted by eye, not byte-exact |
| Saves | save file v1 and an aligned load; restore still non-deterministic |
`faithful=1` turns every addition off; `TJ_FAITHFUL=1` overrides the file for one run.

## Proxy and deterministic control

- `port/proxy/d3d8.conf` is read once at load: `faithful=0` (default) enables additions,
  `faithful=1` restores 2006 behaviour.
- W/A/S/D or the arrow keys steer while the window has the foreground; two keys give a
  diagonal, release returns to neutral. Space and the other keys do nothing.
- The keyboard moves a virtual pointer from the screen centre and re-aims with a left-button
  press, the same channel as the mouse; the mouse still works while the keyboard is idle.
- The simulation runs on a fixed 1/60 s step. A replay feeds the step directly, so the same
  script reaches the same state; `tools/gate.ps1 -Step` runs one script five times. Scripted
  runs use an off-desktop window and answer focus calls, logging `foreground_samples=<n> game=0`.

## Renderer

[`port/RENDERER.md`](port/RENDERER.md) describes one abstraction with two backends:
`render=legacy` forwards to Direct3D 9, `render=d3d12` selects the native backend. Device and
present, textures, draws, render states and the fixed-function set are implemented; legacy
shaders and state are translated to HLSL and compiled to DXIL embedded in the proxy.

The title screen is drawn by Direct3D 12 and is complete but not byte-exact; the in-level frame
is drawn but differs from legacy. `render=legacy` stays the default.

## Tools

| Tool | Does |
| :--- | :--- |
| `tools/check.ps1` | lint, format check and unit tests, about 2 s, image headers only |
| `tools/proxy-run.ps1`, `tools/gate.ps1` | one observed run with the drop-in, hashed before and after; the stage gates |
| `tools/d3d8_census.py`, `tools/unwrap.py` | the D3D8 call-site census; deterministic image recovery |
| `tools/check_symbols.py`, `tools/ghidra_symbols.py` | the checked address table `port/symbols.csv`; its Ghidra import |

## Build and run

Visual Studio 2022 x86 tools and the Windows SDK build the proxy; your own copy of the game
supplies `Tornado.exe`. See [`docs/BUILDING.md`](docs/BUILDING.md) and
[`docs/INSTALLING.md`](docs/INSTALLING.md).

```powershell
pwsh -NoProfile -File tools/check.ps1        # cheap checks
pwsh -NoProfile -File tools/build-proxy.ps1  # the proxy DLL
```

## Status

Stages 1 to 14E are recorded. [`port/STATUS.md`](port/STATUS.md) is the measurement record,
[`docs/STATUS.md`](docs/STATUS.md) the summary and [`docs/ROADMAP.md`](docs/ROADMAP.md) the
register.

| Work | State |
| :--- | :--- |
| Stage 1, the original running as its own process | Accepted |
| Stages 2 to 13D, census, proxy, control, save and load | Measured and recorded |
| Stage 14 to 14E, the renderer and the D3D12 menu frame | Menu frame accepted by eye |
| The in-level frame on D3D12 | Not met |
| Stages 15 and on, the roadmap's P3 to P5 | Planned |

Open an issue with the game version, the hash of the `Tornado.exe` you ran, the settings file
and the run log. Never attach a game file: the hash identifies the copy. GPL-2.0-or-later, see
[`LICENSE`](LICENSE); third-party code and licences in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
