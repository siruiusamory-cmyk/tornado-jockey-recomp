# Project status

Last updated 2026-10-18, at Stage 14E with its follow-ups.

This page is a pointer; it keeps no numbers of its own.

| What | Where |
| :--- | :--- |
| Measurement record: per stage, what ran, what it printed, what stayed open | [`port/STATUS.md`](../port/STATUS.md) |
| Delivery register: phases, milestones, work items | [`docs/ROADMAP.md`](ROADMAP.md) |
| D3D8 call-site census and corrected totals | [`port/API_MAP.md`](../port/API_MAP.md) |

| Stage | State | Record section |
| :--- | :--- | :--- |
| 1, intake and a runnable original | Accepted | "Stage 1, accepted" |
| 2, the static map | Criterion 1 measured; 2-4 open | "Stage 2, census recorded" |
| 3, the proxy | Built and loading; anchors confirmed | "Stage 3 result" |
| 4, capture | Two runs byte-identical | "Stage 4 capture" |
| 5, the oracle gate | Menu golden compared | "Stage 5" |
| 6 to 6F, input and time | Replay and virtual clock measured | "Stage 6" to "Stage 6F" |
| 7 to 7C, the simulation | Tick, tornado, object tables, pair writer named | "Stage 7" to "Stage 7C" |
| 8, keyboard steering | Steering on the measured pointer channel | "Stage 8" |
| 9, fixed step | 1/60 s step under `faithful=0` | "Stage 9" |
| 10 and 10B, translation and windows | d3d8 to d3d9, background runs | "Stage 10", "Stage 10B" |
| 11, state census | Snapshot and restore in process; rand stream excluded | "Stage 11" |
| 12 to 12D, divergence | Bounded, not eliminated; input not named | "Stage 12" to "Stage 12D" |
| 13 to 13D, save and load | Save file v1 and the aligned load | "Stage 13" to "Stage 13D" |
| 14 to 14E, the renderer | One abstraction, D3D12 backend, menu frame | "Stage 14" to "Stage 14E" |

## Open

- The in-level frame on D3D12 renders the level and is not accepted; the level-load lag was a
  clipped world, not a slower load.
- The menu frame on D3D12 is complete but not byte-exact; not accepted.
- The step gate is red on a quarantined spawner row split; the step counting is intact.
- The rand divergence is 1 in 51 bare, 0 of 399 instrumented; the input is not named.
- Save/load survives once, not reproducibly; the save file is not reproducible.
- Stage 15 waits on M1 in the roadmap.
