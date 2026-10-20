# Installing and playing

You need your own legally owned copy of Tornado Jockey (2006, Windows x86). No game file,
save, asset or executable is distributed here, and nothing downloads one. The port is one
file, `d3d8.dll`, a proxy with the game's own export; the game loads it instead of the system
DLL and every addition happens in-process.

`tools/unwrap.py` recovers `Tornado.exe` from your installer; no patched executable is shipped.

## Install
1. Install the game from your own copy, or use the copy on this machine.
2. Build the proxy ([BUILDING](BUILDING.md)) or take it from a release.
3. Copy `d3d8.dll` next to `Tornado.exe`.
4. Create the settings folder below if you want to change anything.
5. Run `Tornado.exe`. To uninstall, delete the DLL.

## Settings file
The DLL resolves `..\port\proxy\d3d8.conf` from the folder holding the DLL, so the file sits
outside the game folder on purpose: a scripted run hash-checks the game folder. Create
`<folder with Tornado.exe>\..\port\proxy\d3d8.conf`. If that folder does not exist, the proxy
runs on built-in defaults (additions on, windowed, scale 1, fixed step, d3d9 translation) and
does not create the file; only the environment variables work until it exists.

One `key=value` line per key, read once at load. Unknown keys and comments are ignored.

| Key | Values | Effect |
| :--- | :--- | :--- |
| `faithful` | `0`, `1` | `1` turns every addition off |
| `window` | `windowed`, `borderless`, `fullscreen-desktop` | window placement |
| `scale` | `1`..`8`, `fit` | present scale; the game still draws 600x450, aspect kept |
| `step` | `0`, `1` | `1` fixed 1/60 s step; `0` original variable step |
| `render` | `legacy`, `d3d12` | d3d8 to d3d9 translation, or native D3D12 |

`TJ_FAITHFUL`, `TJ_WINDOW`, `TJ_SCALE`, `TJ_STEP` and `TJ_RENDER` override the file for one run.

`TJ_GAME_DIR=self` lets a copy of the game outside the registered install start; the launch tools
set it for every run, and unset leaves every answer untouched.

## Controls and problems
Keyboard steering is on under `faithful=0`: W/A/S/D or the arrow keys steer while the window
has the foreground, two keys give a diagonal, release returns to neutral. The game aims at the
point under the mouse when the left button goes down, so the keyboard moves a virtual pointer
from the screen centre and re-aims. The mouse still works; Space and the other keys do nothing.

- **Script Error dialogs.** The engine embeds a browser and fetches a storefront page; a
  scripted run closes them with `TJ_SILENT_SCRIPT_ERRORS=1`.
- **Nothing renders, or wrong mode.** Set `faithful=1` to check the original behaviour.
- **A run must not take the foreground.** Scripted runs use an off-desktop window and answer
  focus calls; `tools/proxy-run.ps1 -Foreground` opts out.
- **Collect a log.** `TJ_D3D8_LOG=<path>` writes the call log, `TJ_CAPTURE_AT`/`TJ_CAPTURE_OUT`
  a BMP, `TJ_IO_LOG=1` input and time rows; the full list is [`port/REPLAY.md`](../port/REPLAY.md).

Game files are never written to; a scripted run hashes the game directory before and after.
