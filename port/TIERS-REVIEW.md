# Tier review

Written from the measured state of Stages 3-7C, 2026-10-17. Cost: S days, M weeks, L months,
XL a recomp or new subsystem. Tag M measured here, I inferred.

Basis: the original `Tornado.exe` on disk plus the `d3d8.dll` proxy. The frame driver 0x508F50
calls the step driver 0x48B7B0 once per Present with a variable delta and no accumulator
(GAME 10.1/10.2). The tornado is a 0x2C58-byte class (GAME 10.3); the object model is a
5000-slot array with 1 of its classes mapped (GAME 10.4); there is no save, settings store or
registry writer (Stage 6E); replay is deterministic (7C).

## T1: 62 items

Tier 1 has 28, Tier 2 has 19, Tier 3 has 15.

| # | Item | Verdict | Prerequisite |
| :--- | :--- | :--- | :--- |
| 1 | native Windows compatibility | KEEP, done | - |
| 2 | keyboard remap | NEEDS FOUNDATION I; no keyboard reader exists | input gate (BLOCKED) |
| 3 | controller | KEEP; real pad maps onto the joystick loop 0x4EE531 | - |
| 4 | mouse sensitivity | KEEP; scale the client point into the cursor pair | - |
| 5 | native 1440p/4K | BLOCKED on R; UI math assumes 600x450 | renderer |
| 6 | ultrawide | BLOCKED on R plus FOV/HUD layout | renderer + HUD map |
| 7 | windowed/borderless | KEEP, done; HUD clips above scale 1 | - |
| 8 | UI scaling | NEEDS FOUNDATION H; cursor sprite draw site unlocated | HUD/camera map |
| 9 | uncapped fps + fixed timing | KEEP, do first; uncapped needs Present decoupled from a data-only dispatcher | sim tick hook |
| 10 | camera zoom | NEEDS FOUNDATION C | camera map |
| 11 | see targets through buildings | NEEDS FOUNDATION R | renderer + object census |
| 12 | clearer target markers | NEEDS FOUNDATION H | HUD map |
| 13 | radar | NEEDS FOUNDATION H | HUD + camera |
| 14 | improved tornado rendering | BLOCKED on R | renderer |
| 15 | lighting | BLOCKED on R | renderer |
| 16 | particles | SPLIT from 14, BLOCKED on R; debris ring capacity 6, no velocity | renderer + debris model |
| 17 | original campaign preserved | KEEP, hard rule | - |
| 18 | Tornado Alley preserved | KEEP | - |
| 19 | saves | NEEDS FOUNDATION S, census done; 4.93 MB state, rand consumer unnamed | name it or accept a re-seed |
| 20 | settings | KEEP; one file, read at load | - |
| 21 | audio sliders | KEEP; 20 FSOUND calls | audio map |
| 22 | contrast | BLOCKED on R | renderer |
| 23 | screen shake | NEEDS FOUNDATION C | camera map |
| 24 | motion reduction | NEEDS FOUNDATION C | camera map |
| 25 | subtitles | INFEASIBLE (unknown); voice content unmeasured | audio/content census |
| 26 | replay: record inputs and events | KEEP, mostly done | - |
| 27 | free-camera playback | NEEDS FOUNDATION C+R | camera + renderer |
| 28 | replay as a debug/perf tool | KEEP, done | - |
| 29 | progressive structural destruction | INFEASIBLE on proxy | recomp/new sim |
| 30 | material-specific breakage | INFEASIBLE | 29 |
| 31 | persistent wreckage changes navigation | INFEASIBLE | 29 |
| 32 | debris physics | INFEASIBLE; ring carries no position or velocity | sim ownership |
| 33 | vehicle physics | INFEASIBLE | recomp |
| 34 | chain reactions | INFEASIBLE | 29 |
| 35 | Arcade weather profile | MOVE to 2b | sim hook |
| 36 | Dynamic Storm energy budget | MOVE to 2b; no storm state, three floats and no Fujita field | new state + renderer |
| 37 | environment reactions | MOVE to 2b | 29 |
| 38 | responsive enemies | MOVE to 2b | recomp |
| 39 | emergency vehicles | MOVE to 2b | recomp |
| 40 | civilian/traffic sim | MOVE to 2b | recomp/new sim |
| 41 | new abilities | MOVE to 2b | sim ownership |
| 42 | precision debris grab/orbit/launch | MOVE to 2b | 32 |
| 43 | boost | SPLIT from 41, KEEP early | sim hook |
| 44 | sharper control | KEEP | 2/3 |
| 45 | wide-circulation mode | MOVE to 2b | recomp |
| 46 | more maps | INFEASIBLE now; level format unnamed | format recovery |
| 47 | more objectives | MOVE to 2b; script VM exists, language unknown | VM map |
| 48 | Endless/Survival/Sandbox | MOVE to 3b | 19/29 |
| 49 | Scenario Creator with shareable files | MOVE to 3b | 19, 46 |
| 50 | map editor and mod interface | MOVE to 3b | 46 |
| 51 | elaborate weather evolution | MOVE to 3b | 36 |
| 52 | persistent environmental damage | MOVE to 3b | 31 |
| 53 | photo mode | KEEP late 3 | 10, 27 |
| 54 | replay sharing | KEEP late 3 | 26 |
| 55 | ghost runs | KEEP late 3 | 26, 53 |
| 56 | daily challenges | MOVE to 3b | 49 |
| 57 | local leaderboards | KEEP late 3 | 19 |
| 58 | online leaderboards with validation | INFEASIBLE now | service + policy |
| 59 | progression: credits unlock | MOVE to 3b | 19 |
| 60 | stats | KEEP late 3 | 28 |
| 61 | classic campaign kept | KEEP, hard rule | - |
| 62 | expanded campaign | MOVE to 3b | 29/46 |

## T2: what the plan was missing

| Item | Why it matters | Status |
| :--- | :--- | :--- |
| class/field census | the object model was 1 of 5000 classes mapped | DONE, Stage 11: 34 vtables, 18 live, sizes, counts, rings |
| faithful switch as a mechanism | the ground rules demand it | proposed: one `faithful` bit read by the proxy and every new system |
| proxy boundary as the shipping layer | decides what "port" means | the shipped vehicle is the original exe plus our DLL |
| state-serializer format | "reliable saves" hid "there is no save" | measured; must version class ids |
| renderer seam at the engine's device wrapper | the game's own wrapper mirrors the device | done; the adapter sits on it |
| determinism as a gate | cheapest regression net | promote the probe/frame comparison into every tier gate |

## T3: the foundation question

- **P, proxy only.** IAT, device, window, time and input boundaries: items 1, 3, 4, 7, 9,
  17-20, 26, 28, 44, 53-55, 57, 60. Cannot change the simulation, object model or content.
- **S, a state layer over the original.** Read and write the game's structures through the
  census: saves (19), stats (60), progression (59), a HUD from real state (12, 13). Needs the
  census and a policy decision on writing game memory.
- **R, a new renderer.** 5, 6, 8, 11, 14-16, 22, 27, 53. Independent of S.
- **X, recomp or new simulation.** 29-42, 45-52, 56, 58-59, 61-62.

When Tier 2 starts, the project must choose X: a static recomp or a reimplementation of the
simulation around the recovered data. Until then every Tier 2 item is a plan.

The first blocker is #29 (and #32): the debris ring has no position or velocity, and a building
has no component state. The cheaper earlier decision is S: saves need permission for the port
to write the game's own structures.

## T4: proposed contents

- Tier 1, route P or R: 9; 26; 3/2/4; 7; 5/6 behind the renderer abstraction; 14-16; 10/23/24;
  12/13; 19/20/21/22; 8; 53/54/55 later.
- Tier 2, route S then X: 35/36; 43/44; 47; then 29/30/31, 32/33, 34, 37, 38-40, 41/42, 45, 46.
- Tier 3, route X: 48, 49/50, 51/52, 56-60, 62.

First three stages:

1. Renderer boundary: the engine's device wrapper replaced by one abstraction with a D3D12
   backend, no legacy call on that path. Bar: the menu frame hash unchanged, `d3d8.dll` absent
   from the process.
2. Fixed timestep + replay gate: 1/60 per step regardless of Present rate. Bar: a 10-minute
   replay with `[sim+0x1C]` equal to the accumulated step count, and five identical probe runs.
3. State census + save/restore: save at Present N, restore, continue 1000 frames, probe rows
   identical to an uninterrupted run.

## Stage 14 status

Foundation R is half built: the abstraction and legacy backend exist and are proven (B1-B3),
the D3D12 backend does not yet.

- Waiting on R alone: 5, 11, 14, 15, 16, 22, 27 (27 also needs the camera map).
- 8 and 13 wait on the HUD/camera map; 6 waits on R and H.
- T2's renderer seam is done: the adapter sits on the d3d8 device the game holds.
- T4 stage 1's bar is met only for its first half: 100 % of the menu's device calls are served
  by the abstraction (`# render backend=legacy device_calls=98390 present=500`) and both oracle
  frames are unchanged, but the D3D12 half and the module check are not.
