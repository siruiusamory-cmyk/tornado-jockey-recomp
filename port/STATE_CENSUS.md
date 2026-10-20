# State census

Object model measured, not assumed: roots, actor array and class table, changing fields, the
in-object ring, the heap an actor owns, and a snapshot/restore proof inside one process.
Addresses are VAs of the repaired image (base 0x400000, sha256 `9D2A7C67...D55E1`).

Instruments (`port/proxy`): `TJ_CENSUS=<end frame>`, `TJ_SNAPSHOT=<frame>`, `TJ_PROBE`,
`tools\snapshot-gate.ps1`. The frozen fixtures are `tools\fixtures\replay-menu-level1.txt` and
`replay-snapshot.txt`. Nothing is written into the game directory.

## 1. Roots

Measured at Present 3700 in the `f3700` run unless named otherwise. Heap addresses differ per
run; the structure repeats.

| Root | Reached by | Measured | Citation |
| :--- | :--- | :--- | :--- |
| game object | `[0x6D09D4]`, handle `+0x1E70/+0x1E74/+0x1E78` | `0x14146F58`, handle `3/2/1` | GAME 10.3, RUNTIME-PROVEN |
| its class | first dword | vtable `0x656A40`, size `0x1FB0` | allocation 0x404F1B pushes 0x1FB0 |
| actor array | `[scene+0xEC94]` | heap address, 5000 slots | GAME 10.4 |
| slot layout | 4-byte pointer, 0..4999 | stride 4, no flag | destroy clears at 0x48F97B/0x48F9A4 |
| live flag | slot pointer != 0 | 1,015 ever used, 1,013 live at 3700 | MEASURED |
| class pointer | first dword the slot points at | vtable -> class, 34 in play | RUNTIME-PROVEN |
| slot high water | `[scene+0xEC98]` | 1015 at 3700, 1016 at 6000 | RUNTIME-PROVEN |
| free stack | `[scene+0xEC9C]`, count `[scene+0xECA0]` | heap base + 2 entries at 3700 | GAME 10.4 |
| scene | `[0x6D15E0]` | `0x134B1260`, vtable `0x6587B8` | RUNTIME-PROVEN |
| scene extent | highest named field `+0xECA0` | copy window `0xECA4` | INFERRED |
| sim object | `[0x6D09C0]` | `0x03E46E58`, vtable `0x667A04` | GAME 10.1/10.2 |
| sim extent | fields `+0x4..+0x30` | copy window `0x200` | GAME 10.2 |
| step counter | `[sim+0x1C]`, 0x48E97A | `0xB88` = 2952 at Present 2951 | RUNTIME-PROVEN |
| sim clock | `[sim+4]` | `0x4244CC35` = 49.19942 s | RUNTIME-PROVEN |
| timestep | `[sim+8]`, `[sim+0x30]` | `0x3C888889`, 1 substep | row-identical |
| rand state | `0x6C8910` | `0x19A0D549` at Present 2951 | RUNTIME-PROVEN |
| class table | `0x6D13E0 + 4*id` | 30 entries, ids 0..29 | `# census class` |
| class size | `push <imm32>; call 0x45446B` | 27 decoded, 30 at runtime | `tools/test_census.py` |
| class vtable | `mov [reg],<imm32>` in the constructor | 34 in play (30 + 4 extras) | MEASURED |
| identity triple | `obj+8/+0xC/+0x10`, `FUN_00491080` | tornado `3/2/1` | RUNTIME-PROVEN |

The class table is filled by the image's start-up code, so it is read at the first Present, not
at DLL load. `obj+8` is a generation, not a class id: the tornado reads 3 while its class id is 9.

## 2. Class census

18 vtables hold a live slot; 17 hold all 1,013 slots at Present 3700. 15 of the 30 registered
classes never appear. Counts are the `# census count` rows of `f3700` and `f6000`.

| Vtable | ID | Size | 720 | 2580 | 2640 | 3700 | 6000 | Samples | Changed |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 00669338 | -1 | 0CCC | 103 | 104 | 543 | 543 | 542 | 89 | 31 |
| 0065A7A0 | 16 | 13A0 | 401 | 401 | 403 | 405 | 405 | 89 | 80 |
| 0065E000 | 20 | 0ED8 | 16 | 16 | 16 | 16 | 16 | 89 | 24 |
| 0065BDE8 | 11 | E618 | 14 | 14 | 14 | 14 | 14 | 89 | 603 |
| 00658A00 | 26 | 0E3C | 18 | 18 | 9 | 9 | 9 | 89 | 30 |
| 0065DBF0 | -1 | 1900 | 3 | 3 | 3 | 8 | 8 | 89 | 49 |
| 0065C700 | 12 | 0D08 | 3 | 3 | 4 | 4 | 5 | 89 | 31 |
| 0065E5C0 | 24 | 0F20 | 3 | 3 | 3 | 3 | 3 | 89 | 25 |
| 0066CA98 | 2 | 1D88 | 2 | 2 | 2 | 2 | 2 | 89 | 24 |
| 00659C38 | 10 | 0D0C | 2 | 2 | 2 | 2 | 2 | 89 | 26 |
| 0067BB10 | 5 | 0D18 | 1 | 1 | 1 | 1 | 1 | 89 | 24 |
| 00658300 | 7 | 4294 | 1 | 1 | 1 | 1 | 1 | 89 | 96 |
| 006578C8 | 8 | 1308 | 1 | 1 | 1 | 1 | 1 | 89 | 54 |
| 00658EF8 | 9 | 2C58 | 1 | 1 | 1 | 1 | 1 | 89 | 590 |
| 006575B8 | 15 | 0CD0 | 1 | 1 | 1 | 1 | 1 | 89 | 45 |
| 0065AE20 | 17 | 0FC4 | 0 | 0 | 0 | 1 | 1 | 41 | 45 |
| 0065B8E0 | -1 | 1C10 | 1 | 1 | 1 | 1 | 1 | 89 | 35 |
| 00659F70 | 25 | 0CE8 | 0 | 0 | 0 | 0 | 0 | 11 | 22 |
| 006692FC | 0 | 0CCC | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 00669F78 | 1 | 0DA8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0066C6F8 | 3 | 0E84 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0067C6F8 | 4 | 0D28 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 00657BB8 | 6 | 0DDC | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0065D270 | 13 | 0CF8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0065A390 | 14 | 0EE4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0065DBA0 | 18 | 1900 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0065CC48 | 19 | 1194 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0065D580 | 21 | 1120 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0065E2A8 | 22 | 13A4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0065C358 | 23 | 0EE8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0065B198 | 27 | 0F44 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0065BBAC | 28 | 1C10 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0065B470 | 29 | 1158 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 00656A40 | -1 | 1FB0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

- Three classes use a second vtable: `0x669338` for class 0 (`0x48F280`, size 0xCCC),
  `0x65B8E0` for class 28 (`0x48F340`, 0x1C10), `0x65DBF0` for class 18 (`0x40E2B0`,
  allocation 0x40E2CB, 0x1900). A first-store decoder read 552 of 1,013 slots as "no class"
  before those extras were added.
- `0x669338` is the most common object in the level (543 of 1,013 at Present 3700, 103 in the
  menu). Two classes hold 948 of the 1,013 live slots.
- Class names are game-file content and are not reproduced. Only TORNADO is named; the rest
  carry their registered id and vtable. The name record is at `0x6D15E8 + 0x1E*id`.

## 3. Fields

`changed` is the number of dwords that differed at any sample. Rows carry
`# census field id=<id> off=<hex> changes=<n> kind=<k> target=<t> first=<hex> last=<hex>`.

Type is INFERRED from the bits, values are RUNTIME-PROVEN. Most large counts are one event: the
level load at Present ~2640 re-initialises each object in place.

TORNADO instance (slot 2, vtable 0x658EF8):

| Offset | Changes | Kind | First | Last |
| :--- | :--- | :--- | :--- | :--- |
| 0x0028 | 4 | float | BFA2A305 | BFDF33E2 |
| 0x002C | 5 | float | BFBB3D08 | BD85F4A6 |
| 0x0030 | 3 | int (negative float) | C3A5FCB3 | C3BCBA79 |
| 0x0DAC | 1 | float, per-step delta copy | 00000000 | 3C888889 |
| 0x0F20 | 1 | float, pair x | 00000000 | 3EFF258C |
| 0x0F24 | 1 | float, pair z | 00000000 | 3F5E6F81 |
| 0x0F28 | 1 | float, previous pair x | 00000000 | 3EFF258C |
| 0x0F2C | 1 | float, previous pair z | 00000000 | 3F5E6F81 |
| 0x0FA8 | 1 | ptr -> heap, debris head | 00000000 | 18255A80 |
| 0x0FAC | 1 | int, debris kind | FFFFFFFF | 00000000 |
| 0x0FE0 | 1 | int, debris count | 00000000 | 00000001 |
| 0x25E8 | 1 | float, height | 42AA0000 | 42DC0000 |
| 0x261C | 1 | float, wind speed | 00000000 | 42480000 |

The pair changes once, at the frame-3300 input burst: 0 to `3EFF258C/3F5E6F81` (299/600,
391/450). Strength and life are on the game object: `+0xE5C` moves every Present (rewritten from
`rand()`), `+0xE60` decays.

## 4. Rings and heaps

Debris ring (tornado `+0xF60..+0xFE4`) over 6,000 Presents:

| Field | Offset | Measured |
| :--- | :--- | :--- |
| id-triple block | +0xF60, stride 0xC, 6 entries | `0xF60 + 6*0xC = 0xFA8`, capacity 6 |
| list head | +0xFA8 | 0 while count 0, then `0x1831B7C0` (per run), constant 3720..6000 |
| kind words | +0xFAC + 4i | 0 at every sample with count 1 |
| cursor | +0xFDC | 1 at every sample, 720..6000 |
| count | +0xFE0 | 0 to Present 3660, 1 from 3720 |

The 0x14-byte head block (Present 3720) is `000003FE 00000219 00000001 00000000 00000000`;
none is a pointer, so it is a leaf, not a linked list. Its word meanings are UNVERIFIED.

No other ring: the counter test (rises 1..600 per 60-Present sample, at least 20 times) flags
nothing in any class. Limit: a counter slower than every third sample, or jumping over 600,
would not be flagged. No actor owns a class object (`owned=0` at Present 2950, 1,017 blobs);
the TORNADO has 49 pointer fields into plain heap blocks of unknown size.

## 5. Snapshot/restore proof

One process, `tools\snapshot-gate.ps1`, replay, `faithful=0`, `step=1`:

```
# snapshot root name=game addr=14298D80 size=1FB0 class=-1
# snapshot root name=sim addr=01F3CB30 size=0200 class=-1
# snapshot root name=scene addr=134B1930 size=ECA4 class=-1
# snapshot take frame=2950 blobs=1017 bytes=4930548 image=3567864 actors=1006 owned=0 heap=213839872 overflow=0
# snapshot restore frame=3550 blobs=1017 bytes=4930548 skipped=0 reused=0
rows compared=9600 equal=7293 differ=2307
  6C8910 differs on 596 row(s)
  6D09D4+E5C differs on 595 row(s)
  6D09D4+E60 differs on 498 row(s)
  6D15E0+EC94@6D09D4+1E74.28/.2C/.30 differ on 206 rows each
frame hash at 3550 pass1=E769836D pass2=5072361B
snapshot differs: first row 6C8910 2951 pass1=19A0D549 pass2=3808785B
# snapshot frame frame=2950 hash=583F5F2D pass=first
```

- Restores exactly: `[sim+0x1C]`, `[sim+4]`, `[sim+8]`, `[scene+0xEC98]`, the debris cursor
  and count, and the whole tracked pair. 7,293 of 9,600 rows; those terms are 600/600. Every
  captured region was still the same object at restore (`skipped=0 reused=0`).
- Does not restore: the rand state `0x6C8910`, the strength/life fields it feeds, the position
  they drive, and the frame hash.
- The missing consumer is a non-actor subsystem: a write watchpoint on 0x6C8910 shows 10 calls
  per Present in both passes and 27 extra calls at Present 3656 (the pass-2 twin of 3056) from
  `0x4AD35D` and `0x4B00AD..0x4B150C`.
- Copying the whole writable image section moved the divergence to Present 3060 and made the
  run 15x slower (4,041 Presents in 240 s). Reverted.
- The identity check (`blob.object`: first dword is the vtable) turns a freed-and-reused blob
  into a counted skip. Without it the process ended on the next scene pass (`reused=1`, two runs).

## 6. Coverage

| Quantity | Bytes |
| :--- | :--- |
| snapshotted | 4,930,548 |
| actor and object bodies | 4,841,316 (98.2 %) |
| actor array (5000 x 4) | 20,000 |
| scene / game / sim windows | 60,580 / 8,112 / 512 |
| seven named globals | 28 |
| image writable data (.data + .data1) | 3,567,864 |
| private committed read-write heap | 213,839,872 |
| denominator (image + heap) | 217,407,736 |

Coverage is 2.27 %. The class-object heap is captured completely (`skipped=0`); the denominator
is dominated by memory a save must not contain:

- D3D resources: re-creatable from assets, and a saved pointer is meaningless in the next
  process.
- Sound buffers: same argument, and the driver picks the format.
- Engine subsystem objects (renderer, UI `[0x6D09BC]`, script VM `[0x7E0AA8]`, the IE worker):
  not reachable from an actor; at least one advances `rand()`, so a save must name it or
  re-seed the stream.
- Non-class heap blocks actors point at (49 fields on the TORNADO alone): counted, not copied,
  because their size is not in the class table.

## 7. What the save stage needs

1. A save is the seven globals, the game object (0x1FB0), the scene (0xECA4), the sim (0x200),
   the array (20,000 bytes plus high water and free stack), one record per live actor with its
   class id and class-sized body, and the ring's 0x14-byte node. That is 4.93 MB for a level.
2. A versioned class map, because the class table is built at run time and 3 of the 34 vtables
   are second vtables.
3. The rand state and a decision about its consumer: serialise the non-actor subsystem, or
   re-seed on load and accept a different frame.
4. Re-create objects, not memcpy them; address restore on a live process is the freed-and-reused
   hazard above.
5. Smallest first step: serialise 7.1 to a file and load it at Present 0, where no actor has
   been freed yet.

## 8. Reproduce

```
pwsh -NoProfile -File tools\build-proxy.ps1
$env:TJ_REPLAY='C:\TornadoJockey\tools\fixtures\replay-menu-level1.txt'
$env:TJ_FIXED_CURSOR='1'; $env:TJ_CENSUS='3700'
pwsh -NoProfile -File tools\proxy-run.ps1 -Name c -CaptureAt 3700 -TimeoutSeconds 180
pwsh -NoProfile -File tools\snapshot-gate.ps1 -Take 2950
$env:TJ_SNAPSHOT='2950'; $env:TJ_WATCH='6C8910'; $env:TJ_CENSUS='4150'
pwsh -NoProfile -File tools\proxy-run.ps1 -Name rngw -CaptureAt 4150 -TimeoutSeconds 180
```

## 9. Stage 12: the spawner behind the extra rand calls

| Step | Where | Pattern | Decides |
| :--- | :--- | :--- | :--- |
| per-frame update | `FUN_004b1720`, slot `+0x60` of vtable `0x66B8E0` | `cmp byte [edi+0x40C],bl` 0x4B1780; `fld [edi+0x408]; fsub [sim+8]` 0x4B17F8 | burst flag, spawn timer |
| spawn loop | 0x4B1815..0x4B1834 | `call 0x4B1650` 0x4B1817, `call 0x4AD340` 0x4B181E | repeats while the timer is positive |
| spawn gate | `FUN_004b1650` | `cmp [eax+0x60],ecx; jge` 0x4B1676 | `[owner+0x60]` against `[owner+0x140]` |
| child build | `FUN_004afea0`, 20 rand sites, first 0x4B00A8 | `[owner+0x70]`, float range `[owner+0x90]` | one child |
| timer refill | `FUN_004ad340`, rand 0x4AD358 | `fld [esi+0x80]; fcomp [esi+0x7c]` 0x4AD348 | adds to `[owner+0x408]` |

Both consumers have one caller: `FUN_004ad340` from 0x4B181E, `FUN_004afea0` from 0x4B16A2 in
`FUN_004b1650`, called from 0x4B179A and 0x4B1817 in `FUN_004b1720`.

Three heap classes, outside the class table and unreachable from actors:

| Object | Vtable | Size | Size source |
| :--- | :--- | :--- | :--- |
| spawner | 0x66B8E0 | 0x43C | `push 0x43c` 0x4AFD57 |
| owner | 0x66B778 | 0x448 | `push 0x448` 0x4AFE47 |
| child | 0x66B738 | 0x9C | `push 0x9c` 0x4B167F |

The owner is `[spawner+0x28]`; children are a doubly-linked list at `[spawner+0x424]`/`+0x420`
with links `+0x94/+0x98`; the owner's container is `+0x434`. Four spawners exist in the level
(`# spawn scan count=4`). Before inclusion, the timers differed on 600 of 600 rows each, and
`spawn0` never leaves 0.

Restoring whole spawner objects killed the game at the restore: the child list points at nodes
freed as children expire. The retained blobs are field ranges: `+0x404..+0x41F`, `+0x428..+0x433`,
and on the owner `+0x60`, `+0x7C..+0x83`, `+0x138..+0x14B`, each guarded by the owning vtable.

The timers then stay identical past the take frame (`spawn2.timer` first differs at 2995,
`spawn3.timer` at 3024, against 2951 for all before). The children's population is still
different, so the rand draws differ from the first compared Present.

TORNADO heap blocks, sized with a guarded `HeapSize`:
`# snapshot tornado pointers=39 sized=39 bytes=8299202 min=27 max=333335`. They stay out of the
snapshot: the spawner pair decides the rand stream, and a block freed between take and restore
is the recorded hazard.

## 10. Stage 13: the save census

File shape at Present 3400: `# save write frame=3400 records=1494 blocks=474 patch=86589
free=6 bytes=73127324`.

| Quantity | Measured | Census expectation |
| :--- | :--- | :--- |
| records | 1,494 (3 roots, 1,013 actors, 8 spawner/owner, 0 children, 470 blocks) | matches 7.1 |
| block bytes | 67,100,728 over 474 blocks; 468 of 64 KB..1 MB hold 66,991,292 | 9.5: 39 blocks / 8,299,202, 8x over |
| patches | 86,589 | new |
| block id 0 | 66,660 dwords | "pointer-shaped, unresolvable" |
| unknown vtable | 0 | every live slot decodes |

- A block body is a leaf. Classifying block dwords as references added 410,418 pointer tests;
  the save then hung (`AppHangB1`, 2026-10-18 07:12:50). Bodies are opaque now, and the pointer
  test is a region map.
- The block rule over-takes by 8x: a load wrote 67 MB of engine heap over the loader's blocks
  and ended within one Present at 2609.

## 11. Stage 13C: the rule narrowed

A block is kept only for a field this census names as a block owner: the debris ring head
`+0xFA8`. At Present 3400 the ring is empty, so a save carries no block bytes:
`# save write frame=3400 records=1020 blocks=0 patch=86545 free=6 bytes=6016588`.

| Excluded class | Count | Bytes | Reason |
| :--- | :--- | :--- | :--- |
| other-object private pointers | 6,085 | 3,030,116,276 (HeapSize sum, upper bound) | not a named block owner |
| TORNADO heap pointers | 20 | 9,479,136 | not simulation state; the bytes kill the load |
| over the 1 MB ceiling | 10 | - | not a block size |
| ring field over a cap | 0 | 0 | none at this frame |

A load with no body writes and those 19-20 block bytes kept survived to Present 4519; with the
bytes copied it ended at the load frame. The C1 file applies at 3400 and ends the process in the
next tick, 3 of 3.

Bisect: no body writes survives to 4542; the scene root body alone survives
3 of 3; the actor set alone ends it 2 of 2 and the spawner set 2 of 2. The first bad field is
not named; the next step is a record cap, then a byte-prefix bisect.

## 12. Stage 13D: the process-local field map

Raw dumps of two fresh processes at the same Present, compared by `tools/field_map.py`
(`port/SAVE_FORMAT.md` section 8):

| Present | Records | Differing dwords | Classes | Map union |
| :--- | :--- | :--- | :--- | :--- |
| 3400 | 1020 | 72,137 | 20 | 21 classes, 1,861 offsets |
| 2609 | 1020 | 49,087 | 21 | same |

- Two common classes carry the differences: class 16 (0x65A7A0, 0x13A0) and `0x669338`
  (0xCCC) hold 37,493 and 30,162 of the 72,137. A 0xCCC object differs at 157-212 offsets.
- Of the 72,137, 36,668 are `private`, 1,505 `object`, 101 `image`, 33,863 `other`. A save must
  treat every differing dword as process-local, not only pointer-shaped ones.
- Keeping the map's offsets lifted one load 600 Presents once, not reproducibly: a second save
  of the same frame ended at the load frame 6 of 6, three with no body write. Knobs flip on
  their own (`cap=252`: four survivals, one death; `bytes=3248`: two survivals, one death).
- The save file is not reproducible: 86,511 patches in one save and 113,675 in another, because
  the region scan stops at 1,024 regions.
- Named divergences: spawner records pair by scan index (address order is not stable), and the
  rand row differs from Present 3610 (the non-actor consumer).
