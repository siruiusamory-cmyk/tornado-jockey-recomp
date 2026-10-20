# Replay: frame-keyed input, virtual clock

Opt-in, replay only: `TJ_REPLAY=<path>` drives input and time from a script. Without it nothing
changes. The script parser is `port/proxy/replay_script.h`; the fixture is
`tools/fixtures/replay-menu-level1.txt`.

## What the game reads

| Device | Created at | Format | Read |
| :--- | :--- | :--- | :--- |
| Keyboard | `CreateDevice` 0x4EE244, `GUID_SysKeyboard` 0x68EC20 | 256-byte `DIDATAFORMAT` 0x68E43C | acquired, polled 2/frame, `GetDeviceState` never called |
| Mouse | `CreateDevice` 0x4EE698, `GUID_SysMouse` 0x68EC30 | 16-byte `DIMOUSESTATE` 0x68E84C | never acquired or read |
| Joystick | `EnumDevices`/`CreateDevice` 0x4EE290 | 272-byte `DIJOYSTATE2` | loop 0x4EE531, no device attached in harness |

- The mouse reader 0x4EE780 returns at 0x4EE78B while `[0x9FEFC4+0x54]` is 1. Only
  0x4EE740 clears it, reached from the true branch of 0x508AF0, which has no direct caller.
- The UI cursor is `[0x9FEFC4]+0x58/+0x5C`, scale `[manager+0x74]` = 4.0f, buttons `+0x70`.
  Written only by that mouse reader; it stayed 0,0 for a whole run.
- Keyboard `Acquire` returns 0x80070005 (`DIERR_OTHERAPPHASPRIO`) on 1947 of 1948 calls in
  harness runs, so the window does not own input priority.
- No keyboard `GetDeviceState` site exists. The keyboard cannot be served because nothing reads it.

## What the replay drives

Mouse only, through the wrapped mouse `GetDeviceState`: `mouse <dx> <dy>` deltas and
`button <0..3> <0|1>` edges. Events are keyed to the Present ordinal and applied when Present n
runs, so the consuming frame is n+1. That offset is constant and independent of frame rate.

Window messages are a separate replay channel: `frame <n> message <msg> <wparam> <lparam>`.
`WM_MOUSEMOVE` plus `WM_LBUTTONDOWN`/`WM_LBUTTONUP` opens New Game, `WM_CHAR` types a name, and
`CONTINUE`/`PLAY NOW` reach the level.

## Clock

Measured: the recurring time read is `QueryPerformanceCounter` at 0x5271DD (31,158.7 calls per
frame in one early run, 61,549.1 in the next; later runs 57,444.3 and 51,289.0); `timeGetTime` is
never called and every other time API is start-up only. In replay mode every time source is
virtual from the first wrapped call, from frozen constants in `replay_script.h`.

| API | Replay value |
| :--- | :--- |
| `QueryPerformanceFrequency` | 60,000,000 |
| `QueryPerformanceCounter` | +1000 ticks per call |
| `timeGetTime`, `GetTickCount` | +16 ms per call |
| `GetSystemTimeAsFileTime` | fixed base, +166666 per call |
| `GetLocalTime` | from the frozen FILETIME base 127805472000000000 (2006-01-01) |
| `GetForegroundWindow` | the game's own window |

- Per call, not per frame: the limiter 0x508F50 accumulates 1,000,000 ticks over the 1000
  queries it makes per Present. A clock that moves once per Present returns a zero delta and
  the game hangs in the limiter's spin.
- The seed is 3000000000000 (the real counter's order of magnitude, measured 3,035,962,213,312
  in the oracle run). At 3600000000 the game exited at 3 s before its first Present.
- `TJ_CLOCK_TRACE=1` logs the first 800 values with site and thread; each io flush prints
  `# io clockdigest <fnv64> reads=<n>`.

## Opt-in surface

| Switch | Effect |
| :--- | :--- |
| `TJ_REPLAY=<path>` | the replay, input and clock |
| `TJ_CAPTURE_AT`, `TJ_CAPTURE_OUT` | capture the n-th Present as BMP |
| `TJ_IO_LOG=1` | input and time counts |
| `TJ_D3D8_ARGS=1` | argument sets |
| `TJ_PROBE=<terms>` | read-only dword probe, grammar below |
| `TJ_WATCH=<term>` | hardware write watchpoint |
| `TJ_ARM=1` | count entries to the two spawner arm call sites |
| `TJ_CLOCK_TRACE=1` | first 800 virtual time values |
| `TJ_FIXED_CURSOR=1` | serve one fixed cursor point |
| `TJ_SILENT_SCRIPT_ERRORS=1` | close the Script Error dialog class |
| `TJ_FAITHFUL=1` | override `faithful` in the file |

## Stage 06B-06D: the gate, then the measured channels

- Blocker 1, 06B: `WM_ACTIVATE` and `WM_ACTIVATEAPP` leave `[0x9FEFC4+0x54]` at 01, and the
  mouse reader never runs. The only clearing path is unreachable by an allowed message.
- Blocker 2, 06B: a per-call clock from the first call hangs start-up. The start-up pump
  0x4A33B0 adds 16.7 ms per QPC call to a 0.5 s window and re-enters forever. 06C fixed it with
  the real clock until the first Present, then the seed; 06E made it virtual from the first call.
- 06C: three replay runs reached the menu at Present 500 with seeds 2991175174382 /
  2991806983563 / 2992125947861 and byte-identical captures at `0C6D01C2...93DB8`. The input
  gate still did not open.
- 06D: the DirectInput keyboard has no reader; a synthetic joystick is served twice a frame and
  moves nothing; window messages reach the engine. The in-level update 0x48E8D0 pauses when
  `GetForegroundWindow()` is not its own window, so replay reports its own window.
- 06D clock: 32 QPC queries per frame on the loader. A 1/60000 s quantum advances the limiter
  1,000,000 ticks over 1000 calls: `# io QueryPerformanceCounter 005271DD` 3,864,122 at Present
  3700 and 3,964,122 at 3800.
- Three gameplay runs at Present 3700 differ (`9DD70E7A...AB98`,
  `566F1AAD...79FE`, `0F86DE7E...0C4D`), 198,454/270,000 (73.5 %) and 180,066/270,000 (66.7 %)
  against the first, mean channel delta 10. Nothing was blessed. Level argument sets: 1195
  distinct against the menu's 116.

## Stage 06E: every nondeterminism source

Static `tools/imports.py` / `tools/find_iat_calls.py` plus one `TJ_IO_LOG=1` replay run.

| API | Sites | Calls per run | Replay answer |
| :--- | :--- | :--- | :--- |
| `QueryPerformanceCounter` | 0x453F75, 0x5271D7, 0x5D3660 | 0x5271DD 4,206,902 (~1000/Present), 0x453F7B 4, 0x5D3666 1 | +1000 ticks/call |
| `QueryPerformanceFrequency` | 0x45406F, 0x52716C | 1 + 1 | 60,000,000 |
| `timeGetTime` | 0x453F8A, 0x5273C2 | 0 | virtual, unused |
| `GetTickCount` | 0x5D3654 | 0x5D365A 1 | +16 ms |
| `GetSystemTimeAsFileTime` | 0x5C323A, 0x5D3638 | 1 + 1 | fixed base |
| `GetLocalTime` | 0x5C2877, 0x5C28E7 | 1 + 1 | fixed base |
| `Sleep` | 0x4A7EB6, 0x508FB7 | 0x508FB7 4,033,782 | not virtualised |
| `GetForegroundWindow` | 0x48E8F5 | 4,001 | own window |
| `IsIconic` | 0x508D7F, 0x509752 | 4,002 + 1 | constant |
| `rand` / `srand` | rand 0x5C03EB (~170 sites), srand 0x5C03E1 (3) | static link, state 0x6C8910 | by seed |
| `time()` | 0x5C3231, reads 0x5C323A | - | by the FILETIME base |
| `CreateFileA` | 0x465656, 0x54F65F, 0x5D6166 | 2,736 + 5 | reads pass, writes redirected |

- `srand` is called three times: twice from `time()` (0x48B93F, 0x5BDEF8) and once with 1
  (0x5BE35D). The random sequence was a function of the real clock.
- No save exists: the image imports no registry writer, and the only `GENERIC_WRITE` opens are
  `%TEMP%\log.txt` and `%TEMP%\log.html`, never read back. Those two writes are redirected to
  scratch (`# io redirect`).
- Six runs at Present 3700: e4a differs from e4b in 18,937/270,000 px (7.01 %, mean delta 1.6);
  e4c hung in start-up (41 lines, 712 s CPU, stopped by PID); e5a/e5b/e6a agree with e4b. No
  golden was blessed. The first 800 virtual values match row for row across runs, so the clock
  is not the divergence source.
- The menu oracle is environment-sensitive: a control capture is `7C28ACAB...8AA7` against the
  golden `0C6D01C2...93DB8`, 513 px (0.19 %), the cursor sprite at another y. The pre-repair
  proxy produced the same `7C28ACAB...8AA7`.

## Stage 06F: boot fetch offline, fixed cursor

- Worker thread: `CreateThread` 0x452F1B starts 0x4523C0, which calls FUN_004522F0 once and then
  waits in `GetMessageA` 0x45240F. The WebBrowser control itself is created on the main thread.
- The image imports no `wininet`, `urlmon` or `ws2_32`. The fetch is the embedded IE control:
  `CoCreateInstance` 0x45083A, CLSID_WebBrowser {8856F961-340A-11D0-A96B-00C04FD705A2}.
  Replay answers that CLSID with `REGDB_E_CLASSNOTREG` (0x80040154), logging
  `# io offline CoCreateInstance WebBrowser tid=<n> hr=80040154`.
- Cursor: the only desktop-pointer read is the `WM_MOUSEMOVE` branch of FUN_00506C20
  (`GetCursorPos` 0x506F0A, `ScreenToClient` 0x506F26, `SetCursorPosition` 0x506F41). Measured:
  0 `GetCursorPos` calls in every menu run.
- The 22x25 arrow sits at client (302,216) while window origins differ (29,52 / 55,78 / 107,130 /
  159,182 / 211,234); the blessed session drew it at (2,2). The draw site stays BLOCKED.
- `replay::kCursorX/kCursorY` = 0,0. Two menu runs capture `7C28ACAB...8AA7` twice, not the
  golden; nothing was re-blessed.
- Start-up hangs: 3 of 3 hung runs created the IE control; 0 of 11 replay runs with the offline
  answer hung. INFERRED: the hang is in the boot-fetch path replay no longer enters.
- Five gameplay runs at Present 3700 are byte-identical at
  `E7595A2D43CFA9AA16650CF9C2F4703339E8FC69A6BF4E0F0E5BB24E5B7BEEF9`, blessed as the second
  named golden. `gate.ps1 -Oracle` compares both frames.

## Stage 07: the read-only probe

`TJ_PROBE=<term>,...` (at most 32 terms, value shorter than 2048 bytes), replay only.

| Term | Reads |
| :--- | :--- |
| `A` | dword at VA A |
| `A+B` | dword at `dword(A)+B` (one dereference) |
| `A+B@C+D.F` | array `dword(dword(A)+B)`, index `dword(dword(C)+D)`, dword at `dword(array+4*index)+F` |

Rows: `# probe armed n=<count> base=00400000`, then one
`# probe <term> <present> <value as %08X>` per term per Present, or `unreadable` (null, fault,
index above 4999, zero base/index). A refused list logs `# probe bad list`; without replay,
`# probe refused (replay only)`. Parser `port/proxy/probe_list.h`, tests
`port/proxy/probe_test.cpp`.

- Determinism: 11 terms / 5,133 rows identical; 32 terms / 7,397 rows differ only in heap
  pointers and the tornado pair (Present 3686 in one run, 4686 in another); a third 32-term
  pair / 4,764 rows differs in four heap pointers and the pair; 8 terms differ in 5 of 6 runs at
  Present 2694, 3011 or 3116.
- The probe does not move the frame: 32 terms armed still captures
  `E7595A2D43CFA9AA16650CF9C2F4703339E8FC69A6BF4E0F0E5BB24E5B7BEEF9`. Cost: 32 terms over
  7,397 Presents = 236,704 rows, with no measurable slowdown.
- 07B: a whole-image Ghidra sweep (695,573 instructions) finds no write to displacement 0xF20
  except the constructor (0x41C276/0x41C6D3) and another class (0x4C7B91). The writer uses a
  computed base. The 07B runs: 20 terms 5 of 6 differ; 32 terms 0 of 3; 8 terms 5 of 6.

## Stage 07C: the writer and the real mouse

- `TJ_WATCH=<term>` arms two 4-byte write watchpoints (DR0/DR1, DR7 `00DD0105`), re-armed every
  64 Presents. A vectored handler logs and continues.
- Writer: `FUN_00410F80` stores at 0x410FA6/0x410FB4 into `this+0x254/+0x258`, where this is
  tornado+0xCCC. Caller chain: 0x48BFD0 -> FUN_00491C70 -> 0x49189B -> setter, one thread.
- Values are the normalised pointer: lParam 0x0187012B (299,391) gives 299/600 = `3EFF258C`
  and 391/450 = `3F5E6F81`.
- The differing input is the OS `WM_MOUSEMOVE` for the physical pointer. `IoPeekMessage` drops
  every move the replay did not post (FIFO of 8), replay only. A click is not filtered.

## Stage 08: steering channel and faithful switch

- A held joystick POV (18000) moves nothing. A mouse move changes the pair but not the position. A
  left-button press aims the tornado at the pair's world point; a stale pair follows the pair.
- Chosen channel: pair `+0xF20/+0xF24`, aimed by a left-button press.
- Keyboard: W/A/S/D and the four arrow keys, per-axis, diagonal on two keys. The pointer moves
  5 client px per Present (300 px/s at 60 Hz) and a press is re-posted every 6 Presents.
- Five fixtures held from Present 3560 to 3680, x/z at 3640 against the control's
  `BFDF33CC / C3BCBA78`:

| Fixture | Key | x at 3640 | z at 3640 |
| :--- | :--- | :--- | :--- |
| `replay-k2-w.txt` | W | `4195BE71` (18.72) | `C3B4E6CF` (-361.8) |
| `replay-k2-s.txt` | S | `3EB61F0A` (0.36) | `C3C4A5C6` (-393.3) |
| `replay-k2-d.txt` | D | `4207B26B` (33.92) | `C3BC2BA5` (-376.3) |
| `replay-k2-a.txt` | A | `40F9CBEA` (7.80) | `C3B4C731` (-361.7) |
| `replay-k2-wd.txt` | W+D | `41C71CC7` (24.89) | `C3B62887` (-364.2) |

Three W runs are row-identical over Presents 0..3699 (18,495 rows each). `faithful=1` installs
nothing; `TJ_FAITHFUL=1` overrides the file and the gate sets it for both oracle captures.

## Stage 09: the fixed step

- The step driver FUN_0048B7B0 is called from exactly two sites, 0x508F86 and 0x508FDC. Both
  `E8 rel32` are retargeted to `StepHook`, which calls the driver N times with the float
  `0x3C888889`. `StepHook` is naked asm because the driver takes `this` in `ecx`.
- Live: N = floor(acc / (1/60)), remainder carried, burst capped at 5 steps. Replay: one step
  per call. `faithful=1` installs nothing.
- One replay to Present 36,000: 600 samples, `proxy=61 sim=61` at frame 60 to `proxy=36001
  sim=36001` at 36,000, max difference 0. The extra step is the countdown path's warm-up.
- Step gate: five runs, 25,900 rows each, `max_diff=0`, one hash
  `29F320D962757AEC785FDF7995C794CD770E46129C1F515D14D38730481E3EBD`; the perturbed script
  diverges at frame 3301 (`F20 3EFF258C` against `3E2AAAAB`).
- The gate compares `[sim+0x1C]`, `[sim+0x4]`, `[sim+8]`, and the tornado's
  `+0xF20/+0xF24/+0xF28/+0xF30`. Two `faithful=1` runs are row-identical over 14,800 rows. The
  PRNG state 0x6C8910 is excluded by design (below).

## Stages 12B-12D: the rand divergence

- 12B: 50 instrumented runs (10 probe, 40 watch) produced 0 divergent. Across every log on disk
  with the same fixture, the fixed-step class is 1 divergent in 51.
- The one divergent pair: at Present 2836 the shared state is `8F24577A`; run 1 reaches
  `00640055` after 1 rand call, run 2 `952FE48D` after 153. That is 152 = 8 x 19 calls: eight
  spawner loop iterations, each one child build (18 draws) plus one timer refill (1 draw).
- Excluded: writer thread (40 of 40 one tid), seed (`43B71B80` at Present 1 everywhere), step
  (`[sim+8]` fixed), and dropped physical mouse moves as a sufficient trigger. Which input made
  run 2 take the eight-iteration path is not established.
- 12C: 150 runs with watch and message rows, 0 divergent, 4,912 s; 0 of 200 instrumented against
  1 in 51 bare (under 2 % at 95 %). `GetMessageA` was wrapped and never entered; the frame pump
  delivered 20 messages per run, all scripted or present at Present 0, one foreign `WM_TIMER`;
  nothing after it at the level boundary (2608) or at 2837.
- The burst is a one-shot byte `[spawner+0x40C]`, set at 0x4AD218 in FUN_004AD210, consumed by
  the loop at 0x4B1788 (`[owner+0x138]` iterations, cleared 0x4B17AA). Its two callers are the
  script-VM handler 0x42230D and FUN_004AD2C0 at 0x4AD2E7.
- 12D: 200 light runs, 0 divergent, 5,474 s; 0 of 399 instrumented against 1 in 51 bare (under
  0.75 % at 95 %). Light cost 38.0 s against bare 38.0-40.5 s; the watch cost 27 s a run.
- 12D arm row: 0x4AD2E7 fires in every run (14 entries by Present 2800, up to 838); 0x42230D
  reads 0 in all 199 verdict rows. In this fixture the spawner's own arm fires, not the script's.
- Consequence: 0x6C8910 stays out of `tools/step-gate.ps1`; no trigger was named, so no fix was
  made. The dead launch is recorded: Present 660, state still `43B71B80`, no crash entry.

## Known limits

- The script is a fixed timeline; it assumes the menu is up before its first event. The menu
  Present site 0x4DE64A takes over at about frame 355.
- The cursor starts at 0,0 with scale 4.0f. A different start would misplace a click by
  `delta * scale`; the cursor is logged each frame.
- The physical keyboard path is read, not run: OS input automation is forbidden here.
- No keyboard beyond the eight steering keys is bound. The joystick action map moves nothing.
