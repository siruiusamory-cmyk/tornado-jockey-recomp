# Roadmap

The long-range design is [`port/VISION.md`](../port/VISION.md);
[`port/TIERS-REVIEW.md`](../port/TIERS-REVIEW.md) gives a verdict, cost and prerequisite per
item. This file is the delivery register.

Two fields, never merged: **Status** says where the work is; **Delivery** says what is proven.

## Phases

| Phase | Goal | State |
| :--- | :--- | :--- |
| P0/P1 | identity, census, proxy, gate; clock, input, replay, census, save | done with remainders, stages 1-13D |
| P2 | one abstraction, D3D12 backend accepted | active, stages 14-14E and follow-ups |
| P3-P5 | modernize without changing the core; a new simulation around recovered data; long-term sandbox | planned, P4 rows split at its boundary |

Faithful mode is the original executable's own simulation; the new simulation is a separate
mode on the same data, and the original stays the oracle permanently.

## Milestones

| Milestone | Exit bar | Rows |
| :--- | :--- | :--- |
| M1, D3D12 renderer accepted | 14E counts merged (F1-F3, F5), E5 level frame met, menu frame accepted | TJ-201..206 |
| M2, Tier 1 playable modern build | resolution, input, rendering and save/settings rows Validated | TJ-301..311 |

## Work register

| Rows | Phase | State and delivery |
| :--- | :--- | :--- |
| TJ-001, 003, 101, 102, 103, 105, 106, 201 | P0/P1 | done, validated |
| TJ-104, 108, 202, 203, 204 | P0/P1/P2 | done, implemented |
| TJ-002, 107 | P0/P1 | cancelled / superseded (107 folded into TJ-309) |
| TJ-004 | P0 | public repository: in progress, delivery not started |
| TJ-205, 206 | P2 | F3/F5 records, then E5 level frame and load lag; todo |
| TJ-301..310 | P3 | uncapped rate, replay recording, controller, remap, resolutions, rendering, camera, HUD, saves, settings; todo |
| TJ-311, 312 | P3/P5 | subtitles paused; photo mode, sharing, ghosts, boards, stats todo |
| TJ-400, 401, 501 | P4/P5 | asset readers todo; new simulation and Tier 3 modes paused |

## Decisions

| ID | Question | Answer |
| :--- | :--- | :--- |
| D1 | static recomp, or a new simulation around recovered data | new simulation |
| D2 | may the port write the game's own structures | yes |
| D3 | approve the phases, milestones and order | approved |

Further: the menu-frame review happens after TJ-205 and is never inferred; asset recovery may
run read-only once M1 is met; subtitles wait for the audio map from TJ-310. A row changes state
only in a commit that records the date and what moved it.
