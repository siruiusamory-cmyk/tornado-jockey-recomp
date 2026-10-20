# Status

Updated 2026-10-18, at Stage 14E and its follow-ups. Direction: `port/VISION.md` is the design
plan; an item becomes a stage only through a stage contract.

Tags: MEASURED, RUNTIME-PROVEN, INFERRED, UNVERIFIED, BLOCKED. Details and addresses are in
`port/GAME.md`, `port/API_MAP.md`, `port/REPLAY.md`, `port/RENDERER.md` and
`port/STATE_CENSUS.md`.

## Stage 1, accepted

The repaired image renders its title screen from the game directory with no shim, wrapper or
registry hook. RUNTIME-PROVEN: PID 30400, title "Tornado Jockey", screenshot sha256
`4B31C0F50D347A43E7984EC8195B6E5078EFD406896233DC80D48AC269216062`. Gameplay untested.

## Stage 2, census recorded

`python tools\d3d8_census.py --check`, 2026-10-17: 0 proven sites, 1 candidate, 5691 unresolved
(54 resolved before the corrections, all invalidated), 13 distinct methods, 1 Direct3DCreate8
call, 1 CreateDevice call, self-check PASS.

- Vtable order from pinned Wine 10.0 `include/d3d8.h` (sha256 `413DD32B...6B022`); offsets =
  index * 4; a header hash mismatch aborts.
- Gate: 19.3 s total - Stage 1 frame 0.0 s, unwrap determinism 1.9 s (two builds, both
  `9D2A7C67...D55E1`), census oracle 17.0 s, vulture 0.4 s. `check.ps1` 1.6 s.
- Open findings: the device pointer is created at 0x5070BA in `FUN_00506fd0` and kept at
  `this+0xA8`; Present/EndScene are unresolved because the receiver comes through a renderer
  accessor (`[0x9fee08]` +0x1C -> `[0x6d09b8]+0xEC`); the engine wraps D3D8 in a class whose
  offsets mirror the device, so Stage 3 must hook the device object, not matching call sites.

## Stage 3, proxy built, P3 blocked

- P1: `port\proxy\d3d8_proxy.cpp`, `.def`, `tools\build-proxy.ps1`. `dumpbin` reports
  `14C machine (x86)`, one export `Direct3DCreate8`; all 113 slots wrapped.
- P2: staged copy 2903 files, `links=0`, same relative-path sha256, plus `d3d8.dll` (130,048
  bytes).
- P3 BLOCKED: the staged copy shows "Product is improperly installed" within 1 s (PID 21348;
  control PID 5096 loaded the system d3d8). The SKU check binds the module directory to
  `HKLM\...\InstalledSKUs\WT025771`; no elevated token was available.
- P4/P5 code done: `tools\proxy_log.py`, 3 tests / 11 subtests; a header-only log stops with
  `ValueError: the log holds no call line`.

## Stage 3 result

Two guarded drop-in runs (PIDs 41148, 8940), both `identical_to_before=True`, 2903 files hashed.

- Logs 288 and 213 call lines, 161 (slot, call site) pairs, identical sets. Screenshots black
  (mean 3/255).
- CreateDevice 0x5070BA CONFIRMED (`call 0 15 CreateDevice 005070BD 1`); GetBackBuffer 0x507129
  CONFIRMED (`call 1 16 GetBackBuffer 0050712C 1`); Present 0x4211F8 CONFIRMED
  (`call 1 15 Present 004211FB 271`); candidate 0x44BC93 NOT SEEN.
- Open: the runner dismissed 0 dialogs where an interactive start reports seven script errors;
  the frame is not a menu frame, so visual equivalence is UNVERIFIED.

## Stage 4 capture

- Opt-in `TJ_CAPTURE_AT`/`TJ_CAPTURE_OUT`; frame from `GetBackBuffer(0)` + `GetDesc` +
  `CreateImageSurface` + `CopyRects` + `LockRect` in the Present thunk, written by
  `port\proxy\bmp.h`.
- `GetFrontBuffer` is unusable on this windowed device (`width=600 height=450 format=22
  windowed=1`): it copies the desktop, and a console covered the client rect.
- Two runs (PIDs 36184, 39724): 600x450, 115,928 distinct colours, mean luminance 116.77,
  sha256 `0C6D01C254FF0E69EC4EB84E83A8C57FB5F3335E42307D11A6FAB8FBF3933DB8`, byte identical.
- The seven Script Error boxes are the game's own `Internet Explorer_TridentDlgFrame` windows
  from the live WildTangent storefront page (`line 0, char 0, code 0`). A second Present site,
  0x4DE64D, takes over after the start-up screen.
- `TJ_SILENT_SCRIPT_ERRORS=1` closes the class in a 10 ms poll. A CBT hook saw none of 59
  creations; `IWebBrowser2::Silent` is unreachable from another apartment (9 failures).
- BMP writer test: 12 subtests; RED "did not compile", GREEN `1 passed, 12 subtests passed`.

## Stage 5, oracle gate and usage surface

- Frame oracle: `gate.ps1 -Oracle` launches one guarded run, captures at Present 500, compares
  to the golden; `-Bless` writes it. Measured: bless `0C6D01C2...93DB8`, `-Oracle` PASS, the
  n=250 splash FAIL.
- `TJ_D3D8_ARGS=1` writes `# args <iface> <slot> <name> <v0..v5> <count>`, deduplicated. Two
  runs (PIDs 25212, 44932) captured the golden, 116 argument sets each, identical as sets.
- Menu Present site 0x4DE64A (return 0x4DE64D), 661 calls in run 14; start-up site 0x4211F8,
  344. 0x4DE64A is in `tools/fixtures/d3d8-anchors.json` under `runtime_sites`.

## Reproduction

```
pwsh -NoProfile -File tools\check.ps1      # 60 s budget
pwsh -NoProfile -File tools\gate.ps1       # 10 min budget, writes the evidence directory
pwsh -NoProfile -File tools\gate.ps1 -Oracle
pwsh -NoProfile -File tools\gate.ps1 -Oracle -Bless
python -B tools\d3d8_census.py --check
python -B tools\d3d8_census.py --refresh
```

Acceptance history: Stage 1 reconciliation, lean verification bar, census and API map, capture.
No push or merge. Open: Stage 2 criteria 2-4, gameplay untested, frame comparison.

## Stage 6, input and time

- J1: DirectInput 8 only. No `GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState`,
  `mouse_event`, joystick or XInput import. Two tables and the DirectInput version (0x800) are
  in `port/API_MAP.md`.
- J2, two runs (PIDs 36232, 44696): 28,554,181 and 26,363,452 `QueryPerformanceCounter` calls at
  0x5271DD (57,444.3 and 51,289.0 per frame); every other time API is start-up only;
  `timeGetTime` never called. Keyboard acquired/polled 2.0 per frame, state never read; mouse
  never acquired. Cursor stays 0,0 with `[0x9FEFC4+0x54]` = 1.
- Verdict: the sim advances on measured elapsed time, not a fixed step. A replay must
  virtualise the clock and advance it on every call.
- J3: `port\REPLAY.md` records keying (Present ordinal), events (mouse deltas and button edges)
  and the per-call clock.
- BLOCKED: the game window never owns the foreground, so DirectInput refuses the acquire. The
  `-Focus` attempt gave `foreground_pid=17896`, `foreground_is_game=False`. No replay event was
  delivered; no gameplay capture or second golden.
- Gates: check 3.6 s; gate 20.1 s; `-Oracle` 94.9 s at the golden.

## Stage 6B, in-process replay

- K1 recorded: four writers of `[0x9FEFC4+0x54]`; the only clearing path is
  `0x508AF0(arg != 0)` -> `0x4EE740`, and 0x508AF0 has no direct caller.
- Blocker 1: `WM_ACTIVATE(WA_CLICKACTIVE)` and `WM_ACTIVATEAPP(TRUE)` left the byte at 01, the
  cursor at 0,0, and `GetDeviceState` was never called.
- Blocker 2: the per-call clock hangs start-up. The pump 0x4A33B0 adds 16.7 ms per QPC call
  against a 0.5 s window and re-enters at 0x404FE0 forever.
- Control: the menu golden still matched; 22/22 io rows. `TJ_REPLAY` was a trap until 06C.

## Stage 6C, the clock fixed

- Rule c: real counter until the first Present, then a seed from the last real read and one
  1/60 s quantum per time-API call. Three runs (replay2/3/4), seeds 2991175174382 /
  2991806983563 / 2992125947861, all captured the golden `0C6D01C2...93DB8`.
- The gate stayed `inact=01` before and after both activate messages.
- L1: `GetForegroundWindow` 901 calls at 0x0048E8F5, `IsIconic` 902 at 0x00508D7F and 1 at
  0x00509752, `GetFocus` 0. 12 io rows, no additions.
- L2 STOP: 0x492301 is the only dispatcher of the activate slot; its five callers push 0 or
  re-apply a saved byte only 0x4922F0 writes, so no non-zero argument exists and 0x4EE740 is
  unreachable. The contract's L2 condition is false.
- The one ungated reader is the joystick loop 0x4EE531; no device attached, so it never runs.
- L4 NOT RUN: gameplay unreachable.

## Stage 6D, window messages

- Keyboard: no `GetDeviceState` site exists. After Poll 0x4EE963, `jge` 0x4EE968 skips Acquire
  and tail-jumps to the joystick reader.
- Synthetic joystick: `EnumDevices` reports one device, `CreateDevice` uses the proxy's GUID,
  `GetDeviceState` serves DIJOYSTATE2 twice a frame. POV presses and each of six buttons left
  the Present-500 capture byte-identical to the menu golden.
- Window messages: `WM_MOUSEMOVE` + `WM_LBUTTONDOWN`/`UP` opens New Game, `WM_CHAR` types a name,
  a click answers the restart prompt, CONTINUE/PLAY NOW starts the level.
- Auto-pause: 0x48E8D0 pauses when `GetForegroundWindow()` is not its own window; replay reports
  its own window and the storm timer runs (`05:59:98`, `05:41:80`).
- Clock: a 1/60000 s quantum gives 1,000,000 ticks over 1000 queries: 3,864,122 at Present 3700,
  3,964,122 at 3800.
- Three gameplay runs at Present 3700 differ: `9DD70E7A...AB98`, `566F1AAD...79FE`,
  `0F86DE7E...0C4D`; 66-75 % of pixels, mean delta 10. Nothing blessed.
- Level argument sets: 1195 distinct against the menu's 116; 1079 level-only.

## Stage 6E, virtual from process start

- E1: the full table is in `port/REPLAY.md`. `srand` has three sites; two seed from `time()`
  0x5C3231 (which reads `GetSystemTimeAsFileTime`), the third seeds 1. The random sequence was a
  function of the real clock.
- E2: there is no save. No registry writer is imported; the only `GENERIC_WRITE` opens are
  `%TEMP%\log.txt` and `%TEMP%\log.html`. The 06D note is refuted; replay redirects those two.
- E3: `clock_start(3000000000000)`, FILETIME base 127805472000000000 (2006-01-01). A 3600000000
  seed exited at 3 s; the counter's order of magnitude is load-bearing.
- E4 NOT MET: six runs. e4a differs from e4b in 18,937/270,000 px (7.01 %, mean 1.6); e4c hung
  in start-up (41 lines, 712 s CPU, PID 25440); e5a/e5b/e6a agree with e4b. No golden blessed.
  The first 800 virtual values match across runs, so the clock is not the source.
- The menu oracle is environment-sensitive: a control capture is `7C28ACAB...8AA7` against the
  golden, 513 px (0.19 %), the cursor sprite at another y. The pre-repair proxy produced the
  same frame.
- Control: 43 io keys against 06D's 25; none removed. Gates: check green, `-Oracle` menu FAIL.

## Stage 6F, offline boot fetch and fixed cursor

- F1: `CreateThread` 0x452F1B starts 0x4523C0, which calls FUN_004522F0 once and waits in
  `GetMessageA` 0x45240F. The control is created on the main thread (`tid=3652`).
- F2: no wininet/urlmon/ws2_32 import. `CoCreateInstance` 0x45083A creates CLSID_WebBrowser
  {8856F961-340A-11D0-A96B-00C04FD705A2}; replay answers `REGDB_E_CLASSNOTREG` (0x80040154).
- F3: `GetCursorPos` 0x506F0A is the only desktop read; 0 calls in every menu run. The 22x25
  sprite sits at (302,216) while window origins differ; the blessed session drew it at (2,2).
  The draw site stays BLOCKED. `kCursorX/kCursorY` = 0,0; two menu runs captured
  `7C28ACAB...8AA7`.
- F4: three hung runs all created the IE control; 0 of 11 replay runs with the offline answer
  hung. INFERRED: the boot-fetch path.
- F5: five replay runs at Present 3700 are byte-identical at
  `E7595A2D43CFA9AA16650CF9C2F4703339E8FC69A6BF4E0F0E5BB24E5B7BEEF9`, blessed as the gameplay
  golden. `-Oracle` then printed gameplay ok / menu FAIL.

## Stage 7, the tick and the tables

- SC1: frame driver `FUN_00508F50` (clock 0x508F6B, limiter 0x508F91..0x508FB7, step 0x508FDC);
  step driver `FUN_0048B7B0`; substep body `FUN_0048E8D0` with eight services, the fifth the
  scene pass `FUN_00491340`; tornado advance `FUN_0041F650`; render thunk 0x5091D0 -> Present
  0x4DE64A. Message pump `FUN_00508040`; 0x4A33B0 pumps a step chain, not windows.
- SC2: 1 substep for 16,892 Presents; `[sim+0x1C]` = Present + 1; 300 steps over 3401..3701;
  `[sim+4]` advanced 4.99992 s against 5.0 s; `[sim+8]` = 0x3C8888C6; `[sim+0x18]` = 0.05 s in
  play.
- SC3: TORNADO 0x2C58 bytes, vtable 0x658EF8, factory 0x40DE90; handle at
  `[0x6D09D4]+0x1E70..+0x1E78`; position `+0x28/+0x2C/+0x30`; pair `+0xF20/+0xF24`; previous
  `+0xF28/+0xF2C`; speed `+0xF30`; step copy `+0xDAC`; wind `+0x261C`. Strength/life on the game
  object `+0xE58/+0xE5C/+0xE60` (life 99.34 -> 94.61 -> -0.093). Pair diverged in 2 of 3 runs.
- SC4: 5000-slot array at `[scene+0xEC94]`, allocator `FUN_0048F8D0` (`+8` high water, `+0xC`
  free stack, `+0x10` count), spawn `FUN_00491680`, element header `+0` vtable, `+8`
  generation, `+0xC` slot, `+0x10` version. Counts 1, 572, 573, 1015, 1018 at Presents 1, 716,
  746, 2609, 5663. Debris table BLOCKED.
- SC5: probe implemented (`port/proxy/probe_list.h`), read-only, replay only. 11-term pair gave
  5,133 identical rows; 32-term pair differed in heap pointers and one pair.
- Deviations: indexed term form `A+B@C+D.F`; 0x4A33B0 is not the message pump; 2048-byte value
  buffer; the tornado has no Fujita field.

## Stage 7B, the tracked pair

- D1 MET: 20 terms / 6 runs, 5 differed (Present 3791 or 3101); 32 terms / 3 runs, 0 differed;
  8 terms / 6 runs, 5 differed (2694, 3011, 3116). Only the pair and one-frame `+0xF30` differ.
- D2 NOT MET: the pair is written before the tick (0x41FC79/0x41FC7F copies it); a 695,573-
  instruction sweep finds no 0xF20 write except the constructor (0x41C276/0x41C6D3) and
  0x4C7B91. The writer uses a computed base.
- D3 STOP: rand state, step count/delta, clock, identity, position, counts and the limiter's
  `Sleep(0)` are excluded. Swallowing the yield left 5 of 6 differing and slowed every run;
  reverted.
- B1 MET: debris is an in-object ring (head `+0xFA8`, kinds `[+0xFAC+4i]`, cursor `+0xFDC`,
  count `+0xFE0`, capacity 6); add 0x41AD30, drain 0x41B380, remove 0x41AB40.
- Oracle with the 8-term probe captured `CE429D58...E32078` against the golden, so the drawn
  frame is also intermittent.

## Stage 7C, the writer and the real mouse

- `TJ_WATCH` arms two 4-byte write watchpoints (DR7 `00DD0105`), re-armed every 64 Presents.
- Writer `FUN_00410F80` stores at 0x410FA6/0x410FB4 into `this+0x254/+0x258`, where this is
  tornado+0xCCC. Chain: 0x48BFD0 -> `FUN_00491C70` -> 0x49189B -> setter; one thread.
- Values are the normalised pointer: lParam 0x0187012B (299, 391) gives 299/600 = `3EFF258C`,
  391/450 = `3F5E6F81`.
- The differing input is the OS `WM_MOUSEMOVE` (five dropped in one run). `IoPeekMessage` drops
  every move the replay did not post. A click is not filtered.
- Runner: `-Focus` is opt-in; foreground stayed on the terminal in 19 of 20 samples.

## Stage 8, steering

- The joystick POV moves nothing. A mouse move changes the pair but not the position. A
  left-button press aims at the pair's world point; a stale pair follows the pair.
- Channel: pair `+0xF20/+0xF24`, aimed by a left-button press.
- Keyboard: W/A/S/D and the four arrows; 5 client px per Present (300 px/s), press every 6
  Presents. Five fixtures held 3560..3680, x/z at 3640 against `BFDF33CC / C3BCBA78`:

| Fixture | Key | x | z |
| :--- | :--- | :--- | :--- |
| `replay-k2-w.txt` | W | `4195BE71` (18.72) | `C3B4E6CF` (-361.8) |
| `replay-k2-s.txt` | S | `3EB61F0A` (0.36) | `C3C4A5C6` (-393.3) |
| `replay-k2-d.txt` | D | `4207B26B` (33.92) | `C3BC2BA5` (-376.3) |
| `replay-k2-a.txt` | A | `40F9CBEA` (7.80) | `C3B4C731` (-361.7) |
| `replay-k2-wd.txt` | W+D | `41C71CC7` (24.89) | `C3B62887` (-364.2) |

- Three W runs are row-identical over Presents 0..3699 (18,495 rows each).
- `faithful=1` disables everything; `TJ_FAITHFUL=1` overrides and the gate sets it for both
  oracle captures.
- Deviations: a press is required, not just a move; the config lives at the proxy's own
  `port\proxy\d3d8.conf`, not the game directory.

## Stage 9, fixed step

- The step driver is called from exactly two sites, 0x508F86 and 0x508FDC. Both `E8 rel32` are
  retargeted to `StepHook`, which calls the driver N times with `0x3C888889`; naked asm because
  the driver takes `this` in `ecx`.
- Live: N = floor(acc / (1/60)), remainder carried, burst capped at 5. Replay: one step per
  call. `faithful=1` installs nothing.
- SC1: 509 of 509 rows read `3C8888C6`; live, the same argument had 446 distinct values over
  525 calls (min 0.016667 s, mean 0.0370 s, max 4.0499 s).
- SC2: `# fixed step on sites=2 seed=0 replay=0`; replay `proxy=61 sim=61` at Present 60 and
  3661 at 3660.
- SC3: one replay to Present 36,000 (180,000 probe rows, 132 s wall clock), 600 samples, max
  difference 0.
- SC4: five runs, 25,900 rows each, `max_diff=0`, hash
  `29F320D962757AEC785FDF7995C794CD770E46129C1F515D14D38730481E3EBD`; the perturbed script
  diverged at frame 3301 (`F20 3EFF258C` against `3E2AAAAB`).
- SC5: `faithful=1` oracle matched the gameplay golden; the menu matched this run and stays
  held.
- Bugs fixed: `probe_resolve` double-dereferenced `A+B`; restoring page protection between the
  two writes faulted the second one.
- Determinism: the PRNG state 0x6C8910 advanced differently in 1 run of 3 (Present 2837,
  `00640055` against `952FE48D`), no other row differing through 3700. UNVERIFIED as to cause.

## Stage 10, d3d8 to d3d9 translation

- `port/third_party/d3d8to9/` at `255338f698c8270b537f0a91a13f795f4f988250` (BSD-2).
  `tools/build-d3d8to9.ps1` builds `d3d8to9.dll` (305,152 bytes, sha256
  `72EA44CAD5BD5B695D58E5A7B0E879821B609191F24DF901334E7DDAD3A93F40`), exporting
  `Direct3DCreate8`, `ValidateVertexShader`, `ValidatePixelShader`, `DebugSetMute`.
- `faithful=1` loads the system d3d8; `faithful=0` loads d3d8to9. `backend.h` routes it and the
  log names the module.
- Menu: byte-identical in both backends (`F6DE0320177A12834ED1DCBD7C11D19CB38D87E0601FA79820BAD8C63C60541E`).
- Gameplay at Present 3700:

| Run | Capture | Differing pixels | Max channel | Mean |
| :--- | :--- | :--- | :--- | :--- |
| faithful=1 | `E7595A2D...BEEF9` | golden | - | - |
| faithful=0, step=1 | `4B037468...8E95` | 142,720 | 220 | 4.23 |
| faithful=0, step=0 | `7E06AA3D...05CE3` | 118,078 | 59 | 0.64 |

- With `step=0`, 95 % of differing pixels are one LSB (112,499 at 1, 5,034 at 2, 357 at 3),
  spread evenly. INFERRED: fixed-function lighting rounding. With `step=1` the step changes the
  world.
- Window modes: windowed scale 2 = 1200x900 at 352,46; borderless fit = 1920x1032; fullscreen
  fit = 1920x1080. The game resizes once after CreateDevice; the plan is re-asserted for the
  first ten frames.
- Pointer survives scale: 7,568 probe rows, 0 differing. The HUD re-lays out for the larger
  client: 65,811 of 270,000 pixels differ, the known UI-math limit.
- Amendment A: `TJ_BACKGROUND=1` for scripted runs; window at virtual screen bottom + 64,
  measured at 64,1144; focus calls answered; `SW_SHOWNOACTIVATE`; background runs print
  `screenshot=skipped background=1`.
- Residual: the game can still take the foreground at start-up in some runs (four of seven
  probed); the activator was not one of the hooked calls and the cause is UNVERIFIED.

## Stage 10B, background runs

- F1: four activators measured: the frame's `WS_VISIBLE` creation, the engine's own `SetFocus`
  (0x50974C, site 0x0050802A), the embedded browser's Script Error frames (MSHTML
  `ShowWindow`/`SetWindowPos`), and the replay's `WM_ACTIVATE(2)` probe. Two earlier attempts
  were rejected by a logging fault (class name read by atom), not by the engine.
- F2: background mode creates the frame without `WS_VISIBLE` and with `WS_EX_NOACTIVATE`,
  answers focus calls, scans 17 modules every 250 ms for 120 s, and answers the WebBrowser
  CLSID offline. Rendered frames unchanged.
- F3: 20 consecutive runs, 1,744 foreground samples, 0 game, no `foreground_change` row. Build
  sha256 `473A3548C4963CE41E953466AB439588DC88F5418B51A0522040B8992FFC578`. A start-up fault
  in `patch_import` (MAP: `d3d8.dll+0x5620`) was bounded by `SizeOfImage` and `__try/__except`.
- F4: oracle menu `0C6D01C2...93DB8` and gameplay `E7595A2D...BEEF9`, both ok, 107.7 s; step
  gate five runs, hash `29F320D9...EBD`, 163.8 s.

## Stage 11, state census

- SC1: roots `[0x6D09D4]` (game, 0x1FB0), `[0x6D15E0]` (scene, vtable 0x6587B8), `[0x6D09C0]`
  (sim, 0x667A04), `[scene+0xEC94]` 5000-slot array, 0x6C8910 rand. `obj+8` is a generation, not
  a class id (tornado reads 3, class id 9).
- SC2: 34 vtables (30 registered + 4 extras); 18 hold a live slot; 17 hold all 1,013 at Present
  3700; 15 of 30 never appear. Three classes use a second vtable; a first-store decoder read 552
  of 1,013 as "no class" before the extras.
- SC3: per-class fields; TORNADO's 590 changed dwords, most once at the level load (~2640).
- SC4: debris ring count 0 -> 1 at 3720, head constant, cursor 1, capacity 6. No other ring.
- SC5: snapshot 1,017 blobs, 4,930,548 bytes; 7,293 of 9,600 rows equal; the first differing row
  is 0x6C8910; 27 extra rand calls at Present 3656 from 0x4AD35D and 0x4B00AD..0x4B150C.
  Copying the writable image moved the divergence to 3060 and ran 15x slower; reverted.
- SC6: 2.27 % of writable image plus private heap. Exclusions named.
- The identity check on restore turns a freed-and-reused blob into a counted skip.

## Stage 12, the spawner

- The extra callers are `FUN_004ad340` (rand 0x4AD358) and `FUN_004afea0` (20 rand sites), called
  from `FUN_004b1650` at 0x4B181E/0x4B16A2, which is called from `FUN_004b1720`. Deciders:
  `[spawner+0x408]` timer, `[spawner+0x40C]` burst flag, `[owner+0x60]` vs `[owner+0x140]`.
- Three heap classes outside the table: spawner 0x66B8E0 / 0x43C, owner 0x66B778 / 0x448,
  child 0x66B738 / 0x9C. Four spawners exist.
- TORNADO heap: 39 pointers, 8,299,202 bytes, min 27 max 333,335.
- Attempt 1 (whole objects) killed the game at restore; attempt 2 (scalars) restored the timers
  but the children population differs: 19,200 rows, 14,503 equal, 4,697 differ; first row
  0x6C8910.
- SC5: both goldens reproduced, five step runs `rows=72636 max_diff=0`, hash
  `B4D43713558840627698E2FDB132645DA82DC03265CFBBDC143EC201D035322D`.

## Stage 12B, the rand divergence

- R1: 10 probe runs and 40 watch runs (362,886 writes to 0x6C8910) produced 0 divergent.
  Across every log with the fixture, the fixed-step class is 1 divergent in 51.
- R2: at Present 2836 the shared state is `8F24577A`; run 1 reaches `00640055` after 1 call, run
  2 `952FE48D` after 153. The delta is 152 = 8 x 19: eight spawner iterations (18 child draws +
  1 refill).
- R3 NOT MET: the input is not named. Excluded: writer thread, seed, step, dropped mouse moves.
  BLOCKED.
- R4/R5: no fix; 0x6C8910 stays out of the step gate.

## Stage 12C, 150 runs

- 150 launches, 4,912 s, 147 verdicts, 0 divergent; every verdict `00640055` at Present 2837
  with 10 calls. 0 of 200 instrumented against 1 in 51 bare (under 2 % at 95 %).
- `GetMessageA` wrapped and never entered (0 rows, 5 runs). The pump delivered 20 messages per
  run: 16 scripted, 3 at Present 0, one foreign `WM_TIMER`.
- The burst is one-shot byte `[spawner+0x40C]`, set at 0x4AD218 (callers 0x42230D and
  0x4AD2E7), consumed by the loop 0x4B1788 with `[owner+0x138]` iterations, cleared 0x4B17AA.

## Stage 12D, 200 runs

- 200 launches, 5,474 s, 199 verdicts, 0 divergent; 0 of 399 instrumented (under 0.75 % at
  95 %). Cost: bare 38.0 s, light 43.0 s, watch 27 s extra.
- The arm row: 0x4AD2E7 fires in every run (14 entries by 2800, up to 838); 0x42230D reads 0 in
  all 199 rows.
- A dead launch is recorded: ended at Present 660, state still `43B71B80`, no crash entry.
- No fix; 0x6C8910 stays out of the step gate.

## Stage 13, the save file

- `port\SAVE_FORMAT.md` is the format: 64-byte header `TJSAVE01`, 8 sections, records with
  patch lists. `save_test.cpp` RED then GREEN: `ok save format: header, class map, 2 blocks,
  2 patches, references resolved`.
- Save at 3400: `records=1494 blocks=474 patch=86589 free=6 bytes=73127324`, sha256
  `3F9A11037B1E1B0FA7D775E2DF0797EAE6E5198CEC81F9B62CDED06D24AC0AFD`.
  `refs unresolvable=1147657 nameonly=66660 unknown=0 overflow=19325 blockbytes=67100728`.
- Loader fires at Present 2609 (`# load applied frame=2609 high=1015 spawners=4 records=1494
  paired=1016 mismatched=5 blocks=461 allocated=5 rand=B579466C`). `faithful=1` refuses.
- SC4 NOT MET: run C ends within one Present of the load. Strike 1 (classifying 410,418 dwords)
  hung; fixed by opaque blocks and a region map. Strike 2 (saver addresses) fixed by block id 0
  (66,660 dwords), still ends. The block rule over-takes 8x: 468 blocks, 66,991,292 bytes.
- SC5 PARTIAL: oracle gameplay ok, step gate `rows=84320 max_diff=0` hash `71A70BBB...BBE6`;
  the menu hash was wrong reproducibly (`C325772E...969A`), not this stage's diff.

## Stage 13B, menu golden retaken

- The engine draws its software cursor only after a `WM_MOUSEMOVE`; fixed-cursor mode posts one
  at Present 1 and drops foreign moves.
- Five captures, all `0C6D01C2...33DB8`; 11 menu captures of one hash. Old and new golden are
  byte-identical (`differing_pixels 0 of 270000`).
- Five oracle runs, all exit 0, totals 107.4 / 105.3 / 104.9 / 104.8 / 104.7 s. Step gate five
  runs `rows=84320 max_diff=0`, hash `71A70BBB...BBE6`.

## Stage 13C, the block rule narrowed

- A block is kept only for the debris ring head `+0xFA8`. At 3400 the ring is empty:
  `records=1020 blocks=0 patch=86545 free=6 bytes=6016588`.
- Excluded: other-object 6,085 blocks / 3,030,116,276 upper bound; TORNADO heap 20 / 9,479,136;
  over-ceiling 10; ring-over-cap 0.
- The load still ends the process at 3400, 3 of 3. Bisect: no bodies survives to 4542; scene
  root alone survives 3 of 3; actor set 2 of 2 ends; spawner set 2 of 2 ends.
- Gates: oracle exit 0, both frames; step `rows=84320 max_diff=0`, hash `71A70BBB...BBE6`.

## Stage 13D, the keep-local map

- Two fresh processes, raw dumps: Present 3400 differs 72,137 dwords (20 classes); Present 2609
  differs 49,087 (21). Classification at 3400: 36,668 private, 1,505 object, 101 image, 33,863
  other.
- The union is 21 classes, 1,861 offsets in `port/proxy/keep_local.h`; loader keeps its own
  value at each (`# load keep-local classes=21 offsets=1861 patches=231417`).
- One aligned load survived 600 Presents; five others did not. A second save of the same frame
  ended at the load frame 6 of 6, three with no body write. Knobs flip on their own
  (`cap=252`: four survivals, one death; `bytes=3248`: two survivals, one death).
- First differing row: `spawn0.burst 3401`; spawner records pair by scan index, and address
  order differs across processes (`spawn3.timer` holds `spawn2.timer`'s `40CDDE76`). Rand follows
  at 3610.
- The bisect stopped at record 256, class 16, size 5,024; byte prefix 3,248 kills, but the
  result flips. The save file is not reproducible (86,511 vs 113,675 patches).
- Gates: oracle exit 0, both frames; step `rows=84320 max_diff=0`, hash `71A70BBB...BBE6`.

## Stage 14, the renderer boundary

- B1 MET: the census over 1005 menu presents and 3826 level presents; spot checks agree with
  `port/D3D8_USAGE.md` (SetRenderState 58,658 / 58.37 per frame, SetTextureStageState 76,103 /
  75.72, SetTransform 7,303 / 7.27, DrawIndexedPrimitive 5,262 / 5.24, Present 1005 / 1.00).
  Resource-interface calls stay UNVERIFIED at this stage.
- B2 MET: `render.h` + `render_legacy.h` + adapters; the method set is exactly the census's
  device set.
- B3 MET: the legacy backend forwards every call. Faithful menu `0C6D01C2...93DB8`, gameplay
  `E7595A2D...EEF9`; translated menu `F6DE0320...541E`; step five runs `rows=84320
  max_diff=0` hash `71A70BBB...BBE6`; coverage `device_calls=98390 present=500` (menu) and
  39,374,336 (gameplay).
- B4 NOT MET: `render=d3d12` fell back with `# render backend=d3d12 not built, using legacy`.
  The real device answers a 52-dword D3DCAPS8 (`00000001 00000000 00020000 E00A0000 ...`), and
  neither run has a `CreateVertexShader`/`SetVertexShaderConstant` call.
- B5/B6 NOT MET: depend on B4.
- Deviation: d3d8's `Clear` keeps the rectangle arguments; the six-argument form fixed a crash.

## Stage 14B, resource interfaces and D3D12

- W1 MET: texture, surface, vertex/index buffer and cube adapters with complete vtables; legacy
  output unchanged.
- W2 MET: interface-2..6 census over menu (503 presents) and level (4,249). Vertex-buffer
  `Lock`/`Unlock` 5.99/frame menu, 32.59 level; surface `GetDesc` 0.58/2.55; `LockRect` 0.35/1.62.
- W3 MET: `render_d3d12.h` device, flip-model swap chain, present, read-back. No provider is
  loaded. Capture 600x450, 810,054 bytes,
  `3ABDB67658B1DCEE58C335047BEC2AE7DD7B13EC5C0B17D895CC63430D938937`.
- W4 NOT MET: 820 draws, one PSO, but the 2D UI path never started; the run dies in 2-14 s in
  `D3D12Core.dll`. The only live frame is black and white (269,979/270,000 differ, mean 375.88).
- W5 NOT MET: no level frame.
- W6 MET: legacy defaults and both gates green.

## Stage 14C, the crash named and fixed

- X1 MET with the debug layer BLOCKED: `D3D12GetDebugInterface` = `887A002D`, no
  `d3d12sdklayers.dll`. Three faults fixed: `CopyRects` overran a mip block; a cross-format copy
  pulled 64 rows of 512 bytes from an 8,192-byte block; `UnlockRect` on a released surface
  (`exit_code=0xC0000409`) from double deletion and uninitialised fields.
- Three runs: `device_calls=57319 present=500 draws=820 skipped=0 pso=1 upload=4`, no fatal.
- X2: the first divergence was row 264, `R_GetType` answering 0 instead of 3; fixed. UI calls
  then matched legacy to Present 500 (`DrawPrimitiveUP 134`, `SetTexture 2971`,
  `SetTextureStageState 16085`), with ten textures created later.
- X3 recorded: stage 0 MODULATE/SELECTARG1/SELECTARG2/DISABLE with TEXTURE/DIFFUSE/CURRENT;
  stage 1 disabled; four draws use `MODULATEALPHA_ADDCOLOR` and a projected texture transform.
  Not modelled: `TEXTUREFACTOR`, fog, stage 1/2 combines, texture transform.
- X4 NOT MET: five samples one colour, `pixels_above_16 268,371 of 270,000`, mean 370.10;
  every vertex transformed to the same clip position.
- X5 NOT MET: the level run exited after 3 s at `Tornado.exe+0x65B5`.
- X6 MET: both oracle frames and the step gate green.

## Stage 14D, the menu frame

- G1 MET: five defects (viewport DWORDs into floats; primitive count read as vertices; target
  change did not reset the viewport; `SampleDesc.Count = 0`; `SetMaterial` wrote 64 bytes per
  16-byte row). CPU reference matched clip positions on all 1,116 sampled indexed draws, max abs
  diff 0.002461.
- G2 MET as a measurement: five runs, `> 16` on 142,091 / 142,091 / 142,088 / 142,281 / 142,091
  of 270,000 (spread 193), mean 60.2656 / 60.2311 / 60.2924 / 60.3505 / 60.2589, 143,091 pixels
  drawn, 75,348 colours. Missing background plate and button panel.
- G3 NOT MET: stage 1 uses `TEXCOORDINDEX 0x30000`, `TEXTURETRANSFORMFLAGS 0x103` and
  `MODULATEALPHA_ADDCOLOR`; 126,909 pixels missing.
- G4 NOT MET: the replay reaches Present 700 and stalls (unresponsive, full core, no fault).
- G5 NOT MET, blocked by G4.
- G6 MET: both oracle frames and the step gate green.

## Stage 14E, the frame completed

- E1 MET: two texture stages, stage cascade, TEXCOORDINDEX, projected transform, TEXTUREFACTOR
  and fog, selected by a constant buffer; ten state pairs in the level, two in the menu.
- E2 MET: TEXTUREFACTOR set by the game and used by no draw; fog 34/36/37 set (`FF82A4A8`, 170,
  350) on 931,684 level draws; MODULATE2X on 2,184; stage 1 on 3,276; `MODULATEALPHA_ADDCOLOR`
  served by no draw.
- E3 MET for the picture, not byte-exact: five menu runs all `D3FF062F...97F`, 24,507 pixels
  above 16 of 270,000, mean 19.4911, spread 0. Nothing blessed.
- E4 MET: the stall reproduces at Present 700 / 10,090 draws; five samples in ntdll's heap path,
  returning at `0xA7D79` (caller `0xA7FA1`, then `0x8EA3E`, `0x94C0A`). The index-block overrun
  is the corruption; with it fixed the replay reaches 4000 (1,221,109 draws) and 6000.
- E5 NOT MET: five level runs at 3700 are the loading overlay (248,317 pixels above 16, mean
  79.1960), while legacy is in play. Device calls 21,364,473 vs 21,369,778. Why the load takes
  more Presents is OPEN.
- E6 MET: both oracle frames and the step gate green.

### Stage 14E follow-ups

- First: the state map corrected (`D3DRS_CLIPPING` 136, `ALPHATESTENABLE` 15,
  `SPECULARMATERIALSOURCE` 146); the level never sets 136. Clipping added to `pso_key`. A/B:
  HEAD `F6FC709E...647BD7`, this build `4C8FFDB1...FD4831`, old clipping `00BD969C...DAA42`
  (mean 13.24 from the 14E frame instead of 76.69).
- Descriptor pairs 1..8,190; 8,191 unused; textures 16,383 down; `TJ_D3D12_PAIRS=2` gives 543
  restarts and a byte-identical menu. The old 1,023-slot ceiling gave every later texture slot
  1,025; it is now 8,192 slots wide.
- TEXCOORDINDEX, D3DTA, SPECULAR, alpha MODULATE2X and fog applied once. 48 checks GREEN;
  `--legacy` RED with exactly four failures.
- Second: fog modes named; stalled GPU fails the frame; retired textures wait on the fence of the
  submission that could read them (256-entry list); every alpha comparison (the game sets
  ALPHAFUNC 5 only, and the menu's 4,156 draw rows are `atest=0/8/0`, so no measured pixel
  moved); stage cascade; alpha blend op; watchdog release stores; descriptor arithmetic 4,095 pairs. Menu `D3FF062F...97F`;
  level `4C8FFDB1...FD4831` with `draws=963840 skipped=0 flush=0 srv=0/0 stage1=3276 projected=51243 fog=931684 mod2x=2184 modarg=0`. Step gate RED on the
  spawner row split (`spawn0.burst` at row 5461, `003F9000` against `1100EC00`), reproduced on
  the pre-repair proxy; the step counting itself is intact.
- Third: `RetireQueue` binds a retirement to its own submission's fence; upload timeout fails the
  frame; thread model measured (`thread=26492 other=0`); shutdown drains with one bounded wait;
  alpha table parsed; fog selection fixed (`fog_selected_mode` + `fog_applies`); step-gate A/B
  kept its artifact (20,347 rows differ from the repaired build, all spawner rows from frame
  780); menu `D3FF062F...97F`; the level frame was not re-measured.
- Fourth: CRLF restored byte-for-byte; shutdown is transfer-aware; the failing draw checks
  `frameFailed` after both binds; fog selection follows d3d8 (table mode wins); the thread
  assertion replaced the counter; source-order guards. Menu `D3FF062F...97F`. Step gate RED
  twice: run 1 `FB251AB3...AE635`, run 2 `14C3C766...AA8C1F`, split at row 5461 with a third
  value `5626A300`; runs 3-5 did not run.
- Later follow-up: the failed draw's guard sits between the binds, before `IASetVertexBuffers`
  and the barrier.
- Later follow-up: `RetireQueue::release_pending_on_complete(complete, xferPending)` clears the flag on a
  completed wait and answers with the wait alone. RED: 131 checks, 4 failures; GREEN: 131 checks,
  0.

Split record: C2-C6 map to F1-F5.

- F1 landed: shutdown releases the pending bucket only after a completed wait; that wait clears
  the transfer flag. A failed wait keeps every pending resource and slot.
- F2 landed: `frameFailed` is checked between stage 0 and stage 1, before any barrier or upload
  the second bind could record.
- F3 landed: the guard's refusal count and frame-failed flag are atomic; `own_thread` covers
  every retire and slot mutation, including `pump_retired` and both drains.
- F4 landed: each submit site is tied to its own wait and signalled value; the draw test checks
  `IASetVertexBuffers` and the inter-bind barrier; a device-free second-thread case is refused,
  counted and fails the frame.
- F5 is this record. The step-gate RED stays the pre-existing spawner scan-order class; retained
  A/B: row 5461 `spawn0.burst 780`, `003F9000` against `1100EC00`, also on the pre-repair proxy.
- E5, the load lag is falsified. The game's own level-object count `[scene+0xEC98]` steps
  `0x23C` -> `0x3F7` at Present 2609 in five legacy and five D3D12 runs; `[sim+0x1C]` reads
  `0xE75` (3,701) at Present 3700 on both. Paired runs are row-identical on every value that is
  not a heap address. No D3D12 run needs a later Present than a legacy run.
- Cause of the frame, measured: the game sets `D3DTS_TEXTURE0..7` (states 16..23, identity) at
  Present 745. `textureMatrix[4][16]` indexed states 20..23 past its end, onto `proj`, so the
  projection read identity at every draw and all 3D geometry fell outside the clip volume. The
  frame was the clear colour `0x82A4A8` plus the unfogged HUD.
- Fixed: `textureMatrix[8][16]` (`port/proxy/render/render_d3d12.h:233`). At Present 3700 `proj`
  reads `1.732051, 2.309401, 1.001335`; before the fix the same row read identity.
  `test_texture_matrix_covers_every_texture_state` RED `4 not greater than or equal to 8`, GREEN
  after.
- Level frame at Present 3700 on D3D12: five runs, all
  `6496F24A7826C6D1993514E0141E45CF26E0E95B72CAB438A9BAF0AC1E00CBAC`, spread 0; against the
  legacy golden `E7595A2D...BEEF9` 170,916 pixels above 16 of 270,000, mean abs diff 72.5136.
  This build before the fix: 210,562 and 118.6255. At 14E: 248,317 and 79.1960. Nothing blessed.
- Gates: check.ps1 32 tests GREEN 3.8 s; oracle menu `0C6D01C2...93DB8` and gameplay
  `E7595A2D...BEEF9` match, 144.9 s; step gate GREEN 5/5 `rows=84320 max_diff=0` sha
  `71A70BBB...BBE6`. One earlier gate attempt missed the menu capture inside proxy-run's 60 s
  default and one step-gate attempt was truncated (run 1 `rows=1358`); both passed on re-run.
- Not explained: three D3D12 runs during this measurement, on two builds, ended in an access
  violation inside `render::expand_indices` (d3d8.dll `+0xDE40`) in the menu at 10-29 s; the ten
  measured runs and the five later runs did not.
- Fog depth: the level sets FOGCOLOR `FF82A4A8`, FOGSTART 170, FOGEND 350, and the backend took
  the distance from projected `z/w`, 0.99 at the first fogged vertex, so every fogged draw kept
  its own colour; the distance is clip `w`, the eye distance d3d8 ramps over
  (`port/proxy/render/render_d3d12_impl3.h`; `test_fog_follows_eye_distance`, now the per-pixel
  form below). Level at Present 3700, five runs:
  `113C4E7069F24624AF89A6A3D4C1834180172991A7150527222BA85D0BD7DB6F`, spread 0, above 16
  `170,916 -> 37,295` of 270,000, mean `72.5136 -> 18.2527`. Remaining: the tornado plume and
  dust (14,055 above 16 in a 120x280 column), the 2D HUD text sprites (7,816 in the bottom bar),
  and a small fog residual over the world (left 4,123, right 5,260 of 67,200 per half).
- Per-pixel table fog: d3d8 computes the table-fog factor per pixel from that pixel's w, so the
  shader now carries one over w with `noperspective` and rebuilds w in the pixel stage. As a
  cause it is falsified: five runs at Present 3700 give `9A8F72BC3C2AA74DA71ED09539B47FEF7262D00E6E93F5F3DE31E36EF69FFC3D`,
  above 16 `37,293` against `37,295`, mean `18.2099` against `18.2527`, spread 0. Kept because the
  per-pixel evaluation is the d3d8 form; the residual does not come from fog interpolation.
- Access violation, `render::expand_indices`: not reproduced in eight D3D12 menu runs at Present
  500 after the fog build (one reached Present 300 in 90 s, six reached Present 100 in 35 s, one
  stalled at Present 100, all capped by the runner), against the earlier 3 in 18. The
  Application log holds no `Tornado.exe` fault in that window; the last entries are 2026-10-18
  10:50, `0xc0000409` in `d3d8.dll` `+0x20BFF`. Next measurement: a LocalDumps entry for
  `Tornado.exe` so a fault address lands on the proxy's linker map, plus one guard log row at the
  expansion carrying `startIndex`, `count`, the index-buffer length and `format`;
  `render::expand_indices` reads only `indexList[0..t+2]` (INFERRED from
  `render_d3d12_impl3.h:344-357`) and the adapter does not check the source length.
- Draw mask: `TJ_SKIP_DRAW=<frame>:<first>[-<last>]` drops the same game draw in both backends,
  and `TJ_D3D12_ROWS=3699` logs the draws that produce the Present 3700 capture. Alignment:
  masking draw 1 moves `46,164` legacy pixels and `45,686` D3D12 pixels; draw 830 moves `141`
  against `921`, draw 792 `52` against `235`, draws 700-845 `86,763` against `92,506` above 16.
  Semi-transparent draws cover more pixels on d3d12; the draw that owns the plume is not named.
- The ordinal masks compared different draws: at Present 3700 the legacy frame is 828 calls, the
  D3D12 frame 831, three 76-index smoke draws more from Present 3481 on, so draw 830 exists on
  D3D12 only. Aligned by count, format and size, 819 pairs agree on blend, alpha test, z, the stage
  ops and the filters; the only differing fields are ADDRESSU/V (legacy table 1, D3D12 table 0,
  static sampler wrap) and TEXTUREFACTOR, which no draw selects.
- Index guard: `index_overruns(startIndex, vertices, length, format)` in `fixed_function.h`,
  checked in `DrawIndexedPrimitive`, logs `# d3d12 index guard` once and leaves the draw
  unchanged; `trace_exception` prints `expand list= verts= start= count= length= format=` next
  to the fault address. `check.ps1` runs `test_index_guard` and a RED mirror that fails the three
  overrun checks. No access violation has occurred since, so the guard row is untriggered.
- Draw state at Present 3700: `TJ_DRAW_STATE=<present>:<first>[-<last>]` logs what each draw sees
  from the backend's own getters: render states 19/20/171/27/15/24/25/7/14/23/60, stage 0-1
  colour and alpha ops with arguments, TCI/TTF, address and filter, the texture's format and size,
  FVF and stride, and the first three vertex diffuse alphas.
- MIPFILTER: 187 of the 828 draws set `D3DTSS_MIPFILTER` 0, which disables mipmapping; the path
  sampled the mip chain. A second static sampler at `s1` clamps `MaxLOD` to 0 for that stage
  (`mip_filter_flag`, `test_mipfilter_none_samples_the_base_level` RED then GREEN). At Present
  3700, five runs `C457B4E6A2E74B1E5E37376421EB6EF3AEF8593DF2B8DEF91B9C32282C5689B6`, spread 0:
  above 16 `37,293 -> 36,754` of 270,000, mean `18.2099 -> 18.1325`.
- Falsified: `GetAvailableTextureMem` (legacy `4,292,870,144`, D3D12 `128 MB`) changes neither
  pixels nor draw counts; the address default (legacy wrap, D3D12 table 0, static sampler wrap) and
  `TEXTUREFACTOR` (selected by no draw) are inert here.
- Open lead: masking the plume draw (legacy 789, D3D12 792) changes 964 pixels on legacy and 407
  on D3D12; `frame_probe.py diff` over the frame gives ratio `0.080,0.058,0.119`, corr `0.253`.

### Tornado region at Present 3700

- Mask harness: the saturated plume sleeves are D3D12 draws 713-728 against legacy 710-725; the base
  cloud is D3D12 791-800 against legacy 788-797 (ordinal offset -3). Each pair agrees on primitive,
  count, start, FVF, stride, blend, alpha test, z, both stage ops, texture format and size, the fog
  states (enabled, linear, 170, 350) and the stage-0 texture matrix.
- Masking the sleeve pair leaves 10,481 pixels above 16 in the 120x280 column, the cloud pair
  10,031, from 14,096. The shell draws 729-732 move 2,464 against 2,997 and the 58 billboard draws
  733-790 move 2,094 against 2,102, so no single range owns the difference.
- Directional light: `D3DLIGHT8` type 3 took the point branch, so its direction was read as a
  position. `light_uses_position` (`port/proxy/render/fixed_function.h`) selects the branch;
  `test_light_type` RED (unknown symbol) then GREEN. Five runs at Present 3700
  `176C6BF13132E940ED5A97255C58979207D59DFFC2EF297EDA91C9DCBEE1B245`, spread 0: above 16
  `36,754 -> 34,383` of 270,000, mean `8.4197 -> 8.1221`; the column box `14,096 -> 13,990`, mean
  `18.1268 -> 17.9460`.
- Falsified: making the last world matrix the current one changes no pixel (same frame hash). The
  game creates `X8R8G8B8` render-target textures on legacy and `A8R8G8B8` on D3D12 for the same
  calls, so the 512x256 composites read format 22 against 21, but an A8 target samples no more
  alpha than an X8 one, so the format does not explain a denser plume.
- The draw state log now carries fog, the stage-0 texture matrix and every texture creation.

### Query answers at Present 3700

- `TJ_QUERY_LOG=1` and `TJ_RES_LOG=1` record every IDirect3D8/IDirect3DDevice8 query and every
  resource creation with the Present ordinal, the game call site, the arguments, the HRESULT and
  the returned dwords (`# d3d8 query`, `# d3d8 create`).
- First differing answer: `GetAdapterIdentifier` at Present 0 from 0x5083F3. Legacy (d3d8to9 on
  the Intel adapter) answers vendor 0x8086, device 0x9BC4, subsystem 0x143D1025, revision 5, WHQL
  1; D3D12 answers zeros. DXGI adapter 0 here is NVIDIA 0x10DE/0x1F95, so the backends render on
  different physical adapters and the identifier cannot match honestly.
- Later differing answers: 25 of the 52 GetDeviceCaps dwords (Caps2 200A0000 against E00A0000,
  DevCaps 0819AE50 against 001BBEF0, TextureOpCaps 03FFFFFF against 03FEFFFF,
  MaxVertexShaderConst 00000060 against 00000100, and 21 more); display mode 1920x1080@144 against
  600x450@60 for GetAdapterDisplayMode and GetDisplayMode; mode count 44 against 12 and every mode
  entry; multisample 2/4/8 accepted for an A8 surface on legacy, refused on D3D12;
  CheckDeviceFormat A8-as-texture 0x8876086A against S_OK, and backbuffer formats 23/24/25 S_OK
  on D3D12 against 0x8876086A; GetAvailableTextureMem FFE00000 against 08000000; GetInfo(4)
  S_FALSE against E_NOTIMPL. Failure HRESULTs differ for the same FAILED outcome (0x8876086A
  against 0x80004005).
- Resource requests: legacy creates 883 textures (A8 353, X8 345, DXT1 105, DXT3 80); D3D12
  creates 689 (A8 498, X8 6, DXT1 105, DXT3 80). First divergent request is the second creation at
  Present 0 from 0x454D8C: 32x32, 6 levels, usage 0, pool 1, format 00000016 against 00000015.
  The last query before it is DeviceGetDeviceCaps from 0x454AF1, whose blob differs.
- Falsified, one run each at Present 3700 with CreateTexture=689 and draws=831 unchanged: the
  adapter identifier answered from DXGI adapter 0 (10DE/1F95), the display-mode answers from
  EnumDisplaySettings (1920x1080@144), and the whole 52-dword caps blob replaced by the legacy
  device values. The first divergent request kept format 00000015 in all three.
- Five runs of this build: legacy 883 textures, 828 draws,
  `E7595A2D43CFA9AA16650CF9C2F4703339E8FC69A6BF4E0F0E5BB24E5B7BEEF9`; D3D12 689 textures, 831
  draws, five identical
  `176C6BF13132E940ED5A97255C58979207D59DFFC2EF297EDA91C9DCBEE1B245`. Against the legacy frame
  234,455 differing pixels, 34,383 above 16 of 270,000, mean abs diff 17.6171 (three-panel
  `tj207-r4-3700.png` under evidence).
- One D3D12 run of the set did not reach Present 3700 inside 360 s (558 textures when stopped); it
  is not a 3700 measurement.
- The format at the divergent request is a constant at each loader call site (0x53421E asks for
  00000017, 0x534245 for 00000015) and the created level is accepted when its format reads 0015 or
  0016, so the input that selects the X8 branch is still unnamed.

### Format-check answers (round 5)

- The game asks its format checks through the `IDirect3D8` that `IDirect3DDevice8::GetDirect3D`
  returns. That root is now the proxy's root, so the calls are logged on both backends
  (`# d3d8 query ... c=0045473D`): 7,741 each, and the legacy frame stays
  `E7595A2D43CFA9AA16650CF9C2F4703339E8FC69A6BF4E0F0E5BB24E5B7BEEF9`.
- The texture format is chosen by `FUN_004549b8` (0x4549b8): `FUN_00454ce2` forwards the request with
  rtype 3 and the caller's pool, `FUN_00454d32` creates at 0x454D8C. Outside pool 3 it calls
  `FUN_004546bb` (0x4546bb), which walks the 0x24-byte entries at 0x662b38 up to `[0x6b5d7c]` and
  asks `CheckDeviceFormat(adapter=caps.AdapterOrdinal, devtype=caps.DeviceType, adapterFormat=display
  format, usage, rtype, candidate)` at 0x45473A; a candidate equal to the request that passes that
  key is kept, else the cheapest supported entry by the rank at 0x4545d8. Pool 3 defaults to 0x15.
- Differing answers, measured on both backends: CheckDeviceFormat `(0x15,0,3,0x15)` `0x8876086A`
  against `S_OK`, and `(0x19,0,3,0x19)`, `(0x1A,0,3,0x1A)` the same; CheckDeviceType back
  0x17/0x18/0x19 for windowed 0 and 1 the same; every key both fail returns `0x8876086A` against
  `0x80004005`.
- Those nine keys now answer as legacy does and every failing check returns `0x8876086A`. The checks
  D3D12 answers narrower than legacy stay narrower: multi-sample types above 1, DXT2, DXT4, UYVY, A8,
  the depth formats and 0x3F as texture, because the backend creates none of them.
- Five runs at Present 3700: 883 textures (A8 353, X8 345, DXT1 105, DXT3 80) against 689 (A8 498,
  X8 6), and the request at 0x454D8C now asks 0x16 as legacy does. Draws 831 against 828. Frame hash
  unchanged, 234,455 differing pixels, 34,383 above 16 of 270,000, mean abs diff 17.6171
  (`tj207-r5-3700.png` under evidence).
- CreateDevice takes the same arguments on both backends: adapter 0, HAL, behavior 0x40, 600x450
  format 0x16, one back buffer, no multi-sample, auto depth-stencil 0x4B. The reported descriptions
  differ: back buffer usage 1 against 0, size 0 against 1,080,000, multi-sample 0 against 1;
  depth-stencil format 0x4B against 0x50, usage 2 against 0, pool 0 against 2.

### Round 6 (TJ-207)

- Draw counts at Present 3700 on this build: legacy 842, D3D12 845. Both include 14
  `DrawPrimitiveUP` calls the old `# draws` counter missed because the UP overload of
  `state_draw` did not advance it (`port/proxy/d3d8_proxy.cpp`). Aligned, D3D12 carries the
  three extra 76-index smoke draws from Present 3481.
- Draw attribution: `TJ_DRAW_ID=<path>` with `TJ_D3D12_ROWS=3699` rasterizes the captured
  frame's triangles into one draw ordinal per pixel (A8 texture alpha sampled, z tested) and
  writes `TJID <w> <h> <n>` plus 4-byte ordinals. The captured frame is unchanged
  `176C6BF13132E940ED5A97255C58979207D59DFFC2EF297EDA91C9DCBEE1B245`.
- Groups carrying the 34,383 above-16 pixels: draws 2-4 `2,939` (8.5%, 32/64 px additive and
  alpha-tested dust), 728-732 `15,017` (43.7%, plume particles, 32-256 px), 785-794 `8,216`
  (23.9%, 64x64/64x32 billboards), 800-802 `4,197` (12.2%, full-screen UP overlays 1024/512),
  830-832 `913` (2.7%, 256 px sprites). Total 31,282 (91.0%); the rest is under 1% per group.
- Largest group lead draw 728 against legacy 725: blend ONE/ONE, alpha test off, z test on,
  z write off, table fog 170/350, both stage ops and args, TEXCOORDINDEX 2, FVF 0x112, stride
  32, DXT1 32x32 texture and viewport agree. First differences: TEXTUREFACTOR `FFD8D868`
  against `FF27D869`, and ADDRESSU/V `1/1` against `0/0`; the draw selects neither (colour
  args texture/texture, alpha args diffuse/current) and the static sampler is wrap, so both
  are inert here. No backend fix follows; the remaining inputs are the vertex clip positions
  and the texture bytes.
- `render::expand_indices`: 11 D3D12 runs, 0 access violations. Three runs stalled in the menu
  after `code=E06D7363` (C++ exception, KERNELBASE RaiseException, D3D12Core stack); the index
  trace read `list=0 verts=0 length=0`, so the guard stayed untriggered. By inspection the
  cause is unchanged: `DrawIndexedPrimitive` passes `primitive_vertices(primitive, count)` to
  `expand_indices` without clamping to the index buffer length, and the guard only logs. Not
  fixed: no reproduction.
- Gates: `check.ps1` GREEN 7.2 s; the Oracle gate stalled in the menu capture with no game
  process for over 20 minutes and was stopped; the Step gate was not run. The last Oracle
  record is `gate-20261019-102215.txt`.

### Round 7 (TJ-207)

- Gate on the round-6 tree, before any change: `check.ps1` GREEN, 49 tests, 3.3 s; the Oracle gate
  matched both goldens (menu `0C6D01C2...93DB8`, gameplay `E7595A2D...BEEF9`) in 135.6 s; the Step
  gate GREEN 5/5, rows 84,320, max_diff 0,
  `EC41A0C4C877B51D68EE2578FFC91205DC98AA2382F030CFF8E99B252BB823C9`, 217.2 s. The round-6 menu
  stall did not reproduce: both capture steps reached their capture and stopped the game PID they
  recorded.
- `DrawIndexedPrimitive` clamps the vertex count to the bound index buffer's length before
  `expand_indices`, and drops a draw whose start index is at or past its end
  (`clamp_index_count`, `port/proxy/render/fixed_function.h`). The test puts the end of a 32-bit
  index buffer against a `PAGE_NOACCESS` page, so an over-read faults; the RED build fails the three
  clamp checks. Gates after the change: `check.ps1` GREEN; Oracle 148.6 s, both goldens; Step GREEN
  5/5, rows 84,320, `EC41A0C4...BB823C9`, 190.3 s.
- 20 D3D12 runs at Present 3700 with the clamp: 18 reached the capture, all `176C6BF1...245`, 0
  access violations. Two hit the 200 s cap while analyses ran on the same box (the replay's last
  event at frame 3308 had fired); both retried on an idle box and captured in 67 and 76 s.
  `E06D7363` is logged in every run at replay frame 0, before any draw, with the index trace zeroed
  and a KERNELBASE/D3D12Core/NVIDIA driver stack; it is first-chance and no run stalled on it, so
  the round-6 link to `expand_indices` is not supported.
- Draw inputs on both backends (`TJ_INPUT_DUMP=<present>:<first>[-<last>]`): 46 dump pairs at
  Present 3700, 45 aligned by the +3 D3D12 ordinal and matching on primitive, count, start, stride,
  FVF and the index list. The lead pair (legacy 725, D3D12 728, 192 vertices) matches byte for byte
  on eight sampled vertices, the index list, FVF 0x112, stride 32, the fog states and the stage
  inputs; its view matrix agrees within 1.5e-5 and its world matrix within 3e-5. Nine other pairs
  match byte for byte; the rest differ in the last float digits, the animated values the game
  recomputes.
- The lead draw's DXT1 32x32 level 0 differs in 6 of 512 bytes (2 of 64 blocks, 21 of 1,024 decoded
  texels, peak channel delta 16); the DXT3 64x32 of the 76-vertex draws differs in 98 of 2,048
  texels, peak 14. Both buffers hold the game's own writes. A texture lock that ignored its
  rectangle was tried as the cause and changed neither the bytes nor the frame, and was reverted.
- Masking the group (legacy 725-729, D3D12 728-732) leaves 34,287 of the 34,383 above-16 pixels.
  The group's own contribution (full minus masked, per backend) differs above 16 on 5,556 of its
  153,516 covered pixels, peak 120, 4,465 of them inside the box x 260-350, y 20-330. The frame
  difference is carried by what the group composites over; the draw-id attribution counts cover,
  not cause.
- Frame hashes unchanged on this round's builds: D3D12 `176C6BF1...245`, legacy
  `E7595A2D...BEEF9`, 34,383 pixels above 16 of 270,000, mean abs diff 17.6171.

### Round 8 (TJ-207)

- Adapter facts on this box: DXGI enumerates one adapter, index 0, vendor 10DE, device 1F95,
  LUID 00000000000133C0, "NVIDIA GeForce GTX 1650 Ti"; D3D9 enumerates one, vendor 10DE, device
  1F95, `\\.\DISPLAY14`. `Win32_VideoController` lists an Intel UHD that neither API exposes, so
  the round-7 "legacy on Intel, D3D12 on NVIDIA" boundary was wrong: both backends already ran on
  the NVIDIA adapter.
- `TJ_ADAPTER=<hex vendor id>` pins the DXGI adapter for D3D12 and the matching D3D9 ordinal for
  legacy (`port/proxy/render/render_d3d12_impl1.h`, `port/proxy/d3d8_proxy.cpp`); unset keeps the
  old choice. Legacy with 10DE reproduces the golden `E7595A2D...BEEF9`; D3D12 with 10DE reproduces
  `176C6BF1...245`. Same-GPU pair: 234,455 differing pixels, 34,383 above 16 of 270,000, mean abs
  diff 17.6171, so the GPU is not the cause.
- First divergence at Present 3700 by masked draws, aligned ordinals 1:1 this early: draws 1 only
  317 above 16 (below the 344 that 1 % of 34,383 is); draws 1-2 1,403; full frame 34,383. Draw 2 is
  `DrawIndexedPrimitive`, 4,872 vertices, start 873, stride 32, FVF 0x242, DXT1 64x64 and 128x128,
  linear fog 102/370, blend off, z test and write on, cull off.
- Draw 2 inputs: index list, eight sampled vertices, FVF, stride, fog and stage ops agree; view and
  proj differ in the last digits (view translation 155.154846 against 155.154831, proj 1.73205054
  against 1.73205078); the DXT1 level 0 differs in 6 of 2,048 and 29 of 2,048 bytes, peak byte
  delta 169 and 223. `TEXTUREFACTOR` and `ADDRESSU/V` differ and are inert for this draw's stage
  args.
- Hypothesis, falsifier recorded before the run: the draw-2 difference is carried by the texture
  path, not the draw translation. Forcing the colour stage to SELECTARG2 on both backends leaves
  317 above 16 and mean abs diff 0.3089 (from 1,403 and 1.2755), below the threshold: GREEN. The
  texture bytes the game writes differ between backends, so the difference is not a D3D12 draw
  translation bug and no backend fix follows.
- Gates on this build: `check.ps1` GREEN 49 tests 3.0 s; Oracle 157.0 s, both goldens; Step 5/5,
  rows 84,320, max_diff 0, `14C3C766E0BB81EA7C977581934255ECB41BC45BF522C3596EFEA4DBF2AA8C1F`,
  399.5 s.
- Instruments: `TJ_INPUT_DUMP` emits no row for `DrawPrimitiveUP` (828 rows for 842 draws), and
  the `# state tss` and `# state tex` lines carry no frame or draw number, so an ordinal comparison
  must add the UP gap back.

### Round 9 (TJ-207)

- Instrument: `TJ_TEX_HISTORY=<present>` logs every texture creation, texture and surface lock
  (level, rect, flags, returned pitch and pointer, the level hash before the game writes), unlock
  (the hash after), copy and update, every wrapped resource call and every surface description;
  `TJ_TEX_BYTES=<present>` dumps DXT1 level bytes on lock and unlock. The frames stay `176C6BF1...245`
  on D3D12 and the legacy golden with the instruments on.
- The two draw-2 textures are 64x64 DXT1 with 7 levels and 128x128 with 8, created at Present 735,
  usage 0, pool 1. Both backends create them at the same Present and run the same sequence per
  level: CopyRects from an X8 source fails `8876086C`, the source level is locked read-only
  (`00000810`) and the destination (`00000800`) at the same pitch, and the game's CPU conversion
  writes the destination before unlock.
- First differing event, 64x64 level 0: before the write both levels hash `19DBC3365EA56383` (all
  zero); after it D3D12 `1F138A6109BCC732` against legacy `6076A5F7245DD4C5`. 128x128 level 0: pre
  `1D00C08067BE8383`, post `D3FF895E37744AA9` against `600C8542BE5D2CAB`; levels 1-3 differ the
  same way, 4-7 match. Bytes differ in 6 of 2,048 (one block) and 175 of 8,192 (36 blocks); the
  decoded block colours differ by one green step.
- The only proxy answer that differs inside the write window is the surface description's
  MultiSampleType, D3D12 `1` against legacy `0`; every other field, the pitch, the flags, the
  source level bytes and the failed copy match.
- Falsified: 16-byte-aligned level buffers leave both texture hashes and the frame unchanged.
  Falsified: answering MultiSampleType `0` leaves both unchanged. The x87 control word differs:
  `027F` on D3D12 against `007F` on legacy at all 3,028 locks of the run, and the legacy value
  comes from the real d3d8 device creation. `_controlfp(_PC_24, _MCW_PC)` in the D3D12 device
  creation makes both draw-2 texture hashes legacy's (`6076A5F7...`, `600C8542...`); the draw-2
  view, projection and world matrices become byte-equal.
- Five runs at Present 3700, spread 0:
  `857A27F3CB0ED01811A623A4143C3466CF63F5C62723E5972E6275EB1560C144` against legacy
  `E7595A2D...BEEF9`: 233,261 differing pixels, 34,395 above 16 of 270,000, mean 17.5700. Round 8:
  234,455, 34,383, 17.6171. Masking draws 1-2 no longer isolates the rest: above 16 is 34,784 after
  draw 1 and 34,467 after draw 2 against 34,395 full. Three-panel `tj207-r9-3700.png` under
  evidence.
- Gates on this build: `check.ps1` GREEN, 49 tests, 2.7 s; the Oracle gate matched both goldens in
  128.4 s with game=0 in every sample; the Step gate 5/5, rows 84,320, max_diff 0,
  `9FE081918E3C5D6F543CC2326C39B5515625AA7C4DBA4C89CD85325BEAE172EC`, 259.9 s.

### Round 10 (TJ-207)

- Attribution: `TJ_DRAW_ID=<path>` with `TJ_D3D12_ROWS=3699` writes one last-writer draw ordinal per
  Present 3700 pixel (z tested, A8 alpha sampled for blended draws). Joined to the legacy golden,
  34,395 pixels above 16: the top 10 draws cover 29,457 (85.6%) - 725 13,831 (40.2%), 790 4,849
  (14.1%), 799 3,545 (10.3%), 791 2,401, 2 1,469, 4 1,160, 782 695, 729 613, 829 505, 726 389.
  Draw 725 is the plume (576 verts, DXT1 32x32, ONE/ONE, z write off, fog 170/350); 790 a DXT3
  256x256 sprite (SRCALPHA/INVSRCALPHA); 799 a full-screen UP overlay (A8R8G8B8 1024x256); 2 the
  opaque world (DXT1 64x64 + 128x128, MODULATE2X).
- Stage switches on both backends, above 16 / top-10 mask: baseline 34,395 / 29,457; texture off
  5,841 / 2,686; fog off 45,083 / 29,056; light off 20,078 / 14,419; blend off 2,329 / 223. Blend
  off removes most of the mask (99.2%), texture off 90.9%, light off 51.0%; blend off hides the
  composited source-colour error, it does not name a wrong blend equation. The causal stage is the
  lit-vertex colour path below.
- `TJ_ONLY_DRAW=3700:<draw>` isolates one draw: 725 alone has 0 pixels above 16 (max channel 13)
  though 154,167 differ; 790 alone has 7,878 (max 75). With texture off the 790 diff rises to
  12,915, with fog off it is 8,391, so its own vertex colour is the difference.
- Cause, measured: d3d8's material-source defaults are DIFFUSE=COLOR1, SPECULAR=COLOR2,
  AMBIENT=MATERIAL, COLORVERTEX=TRUE (`# input rs ... 141=1 145=1 146=2 147=0` on the legacy
  device at draw 790); D3D12 initialized diffuse and specular to MATERIAL and COLORVERTEX to 0, so
  a lit draw without normals took the material colour instead of the vertex colour. With the
  defaults corrected, isolated 790 has 0 pixels above 16 (max 5) and the level frame at Present
  3700 is 21,905 above 16 of 270,000, mean 13.7853 (from 34,395, 17.5700); five runs identical
  `95E782CEE63677303094FCCD90997E790597F8F7C7952AF852BDFBE8856B82E8`, spread 0
  (`tj207-r10-3700.png`, `tj207-r10-3700-attribution.png` under evidence).
- The game's CreateDevice behaviour is `00000040`, without `D3DCREATE_FPU_PRESERVE` (0x2), so the
  precision set still runs; the guard is in place and the frame hash is unchanged.
- Instruments: `TJ_ONLY_DRAW=<present>:<index>`, `TJ_STAGE_OFF=texture|fog|light|blend`; the D3D12
  draw row carries texture formats, fog, base-level mip sampling and lighting.
- Gates: `check.ps1` GREEN 49 tests 3.4 s; Oracle both goldens in 125.7 s with game=0 in every
  sample; Step gate GREEN 5/5, `rows=84320 max_diff=0`
  `14C3C766E0BB81EA7C977581934255ECB41BC45BF522C3596EFEA4DBF2AA8C1F`, 240.3 s.

## Open

- The in-level frame on D3D12 renders the level and is not accepted; the load lag it was read as
  is explained (a clipped world, not a slower load).
- The menu frame on D3D12 is complete but not byte-exact; not accepted.
- The tornado column and base cloud keep 13,990 pixels above 16 in their box. The group's own
  inputs match (vertices, indices, state, fog); its texture bytes now match too, so the remaining
  difference is renderer output for those draws.
- The step gate is not stable: on this build run 1 `FB251AB3000A850C327D9B3DD3DD497949368BE6E28DC22839691F73F26AE635`
  and run 2 `EC41A0C4C877B51D68EE2578FFC91205DC98AA2382F030CFF8E99B252BB823C9` split at row 5461
  (`spawn0.burst 780`, `1100EC00` against `5626A300`), the pre-existing spawner scan-order class;
  the earlier five-run green set was `71A70BBB242996B817F87AB3D69B69757519CA28285CE93F6B914C9A9EB9BBE6`
  with `rows=84320 max_diff=0`.
- The rand divergence is 1 in 51 bare, 0 of 399 instrumented; the input behind it is not named.
- Save/load: the keep-local map survives once; the body writes still kill the load
  non-deterministically; the save file is not reproducible.
- Stage 15 waits on M1 in the roadmap.
