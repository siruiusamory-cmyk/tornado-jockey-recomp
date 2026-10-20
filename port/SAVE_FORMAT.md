# Save file format v1

A save is the census's object state in one flat, versioned binary file, loaded into a new
process where the objects are that process's own. The file carries values and references, never
an address. `port/proxy/save.h` is the one copy of the constants and the resolver;
`port/proxy/save_test.cpp` (run by `tools/test_save.py`) round-trips a synthetic file.

Design input: `port/STATE_CENSUS.md` sections 6 and 7 (4.9 MB of object state, 98 % object
bodies), plus the spawner pair and the TORNADO's heap blocks (sections 9.2-9.5).

## 1. Header (64 bytes)

| Offset | Field | Value |
| :--- | :--- | :--- |
| 0x00 | `magic[8]` | `TJSAVE01` |
| 0x08 | `version` u32 | 1 |
| 0x0C | `header_bytes` u32 | 64 |
| 0x10 | `section_count` u32 | 8 |
| 0x14 | `record_count` u32 | records in the table |
| 0x18 | `class_count` u32 | class-map entries |
| 0x1C | `flags` u32 | bit 0: `faithful=0` |
| 0x20 | `image_id[32]` | COFF `TimeDateStamp`, `SizeOfImage`, `CheckSum`, `AddressOfEntryPoint`; a mismatch refuses the load |
| 0x40 | section directory | `section_count * {u32 tag; u32 offset; u32 bytes}` |

Reader rules: magic, version, header size, directory bounds and `offset + bytes <= file size`
are checked first. An absent, duplicated or out-of-order section refuses the file. The image id
is checked against the running image.

## 2. Sections

| Tag | Name | Payload |
| :--- | :--- | :--- |
| 1 | globals | `u32 image_address[7]` (0x6D09C0, 0x6D09D4, 0x6D15E0, 0x6C8910, 0x6D09B4, 0x6D09B8, 0x6D09D8), then `u32 state[7]`; `state[3]` is the rand state |
| 2 | class map | `{u32 id; u32 vtable; u32 size}`; a vtable with no entry refuses the load |
| 3 | roots | 3 records: game 0x1FB0, scene 0xECA4, sim 0x200 |
| 4 | array | `u32 slots` (5000), `u32 high_water`, `u32 free_count`, free indices, then `slots * u32 record_id` |
| 5 | actors | one record per live actor, in slot order |
| 6 | blocks | heap blocks the records point at, each with its byte length |
| 7 | spawners | spawners (0x66B8E0, 0x43C), owners (0x66B778, 0x448), children (0x66B738, 0x9C), child order |
| 8 | rand | `u32 state` (same as `globals.state[3]`) |

Every record (tags 3, 5, 6, 7):

```
u32 record_id     1..n, unique
u32 kind          1 actor, 2 root, 3 block, 4 spawner, 5 owner, 6 child
u32 class_id      class-map entry (kinds 4..6 use -1)
u32 size          body bytes after the patches
u32 patch_count
u32 patches[patch_count]   {u32 offset; u32 block_id; u32 block_offset}
u8  body[size]
```

## 3. References, never addresses

A body dword that pointed into captured heap is written as `{offset, block_id, block_offset}`
and the body carries zero there. On load, `value = address(block_id) + block_offset`, using the
loader's own address table.

A dword becomes a block record when it addresses a committed private read-write region and
`HeapSize` resolves (guarded).

A dword that addresses the image is kept as its value (image
addresses are the same in every process of the build). A private address whose size cannot be
carried (unresolvable, over the 1 MB block ceiling, or over 64 MB total) becomes a patch with
block id 0: the loader keeps its own value. That is the one form for "this was a pointer and it
is not in the file"; leaving the saver's address is what killed the first load at Present 2609.

Block bodies are opaque bytes: a heap block is a leaf (census 4). Classifying them added 410,418
pointer tests at Present 2700 and hung the save (`AppHangB1`, 2026-10-18 07:12:50). The pointer
test is a binary search over the process's private regions, collected once per save; `HeapSize`
is called only for a dword inside one.

**Block rule (Stage 13C): only a field the census names owns a block.** A block record is made
only for the debris ring head `+0xFA8`. Every other private pointer is block id 0.

Measured: a
file that kept the TORNADO's 19-20 blocks (9.0 MB) died at the load frame; without body writes
but with those bytes it survived to Present 4519. The engine and renderer heaps are excluded the
same way.

Writer rows at Present 3400:

```
# save block rule tornado=1411C238 kept=0 bytes=0
# save excluded other-object blocks=6085 bytes=3030116276 reason=not-a-tornado-field
# save excluded tornado-heap blocks=20 bytes=9479136 reason=census-9.5-not-simulation-state
# save excluded capped blocks=0 bytes=0 reason=ring-field-over-a-cap
# save excluded over-ceiling blocks=10 reason=size-over-1MB-not-a-block-here
# save refs unresolvable=1147701 nameonly=81017 unknown=0 overflow=0 blockbytes=0
# save write frame=3400 records=1020 blocks=0 patch=86545 free=6 bytes=6016588
```

The ring is empty at 3400, so the file is 6,016,588 bytes against Stage 13's 73,127,324. The
other-object byte sum is an upper bound (stale addresses alias). The reliable over-take is
Stage 13's: 474 blocks, 67,100,728 bytes, 468 of 64 KB..1 MB holding 66,991,292.

The load still ends the process: the file applies at Present 3400 and the process ends in the
next tick, 3 of 3.

Bisect (`TJ_LOAD_STOP`, `TJ_LOAD_MASK`, `TJ_LOAD_ROOT`, `TJ_LOAD_BYTES`):
no body writes survives to 4542; the scene root body alone survives 3 of 3; the actor set alone
ends it 2 of 2, the spawner set 2 of 2. The block bytes were one killer, now removed; the body
writes carry at least two more. Stage 13D names the cause in section 8.

## 4. Class map

The class table is built at run time (`census.h`: `0x6D13E0 + 4*id` is the factory,
`push imm32; call 0x45446B` gives the size, the constructor stores the vtable). The file carries
id, vtable and size; a load refuses a record whose vtable is not in the map. Three vtables are
second vtables of a registered class and appear with `id = 0xFFFFFFFF`.

## 5. Load order

Applied at the level's first Present in the new process; earlier it is refused (no spawner, no
array).

1. Check the class map against the running image.
2. Place blocks so `address(block_id)` exists before any patch resolves.
3. Write root and actor records over the objects the new process's own level load created; the
   slot and vtable must match (a mismatch is counted, not forced).
4. Resolve each record's patches into its body.
5. Write the array's slot ids, high water and free stack.
6. Write the rand state `[0x6C8910]`.

The objects are the new process's allocations, made through the image's own factory
(`0x6D13E0 + 4*id` -> `0x45446B` -> constructor). A block the new process does not have is
allocated through `0x45446B` and filled from its record.

## 6. Deliberately excluded

D3D resources, sound buffers, and the engine's subsystem objects (renderer, UI, script VM): they
are re-creatable and a saved pointer into them is meaningless in another process. A load
reproduces the simulation, not the audio mix or the device's resource table.

## 7. Versioning

`version` is bumped for any change to a record layout, a section payload or the patch form. A
reader rejects an unknown version without migration. The class map and image id make a file
portable between runs of the same build; nothing is keyed to a Present number except diagnostics.

## 8. Process-local fields and the keep-local map

A body carries dwords that only mean something in the process that wrote them: heap addresses,
engine handles, pointers into rebuilt tables. Writing them into a new process stopped the load
at its own frame.

Two fresh processes ran the same replay to the same Present with `TJ_SAVE_AT=<N> TJ_SAVE_RAW=1`.
Raw mode leaves body dwords at the saver's values, so `tools/field_map.py` can compare two dumps
dword by dword.

| Present | Records | Differing dwords | Classes |
| :--- | :--- | :--- | :--- |
| 3400 (save frame) | 1020 / 1020 | 72,137 | 20 |
| 2609 (load frame) | 1020 / 1020 | 49,087 | 21 |

A differing dword is classified as `private` (block id 0), `object` (another record), `image`
(value in 0x400000..0xA19000) or `other` (small integer, float, handle, padding). The
private-region scan stops at 1,024 regions (`kMaxRegions`), so a pointer past the cap lands in
`other`.

The map is the union of differing offsets over both frames and both pairs: 21 classes, 1,861
offsets, in `port/proxy/keep_local.h`, generated by `tools/field_map.py --emit-header`.

| Kind | Key | Size | Offsets |
| :--- | :--- | :--- | :--- |
| actor | 0x00000002 | 7560 | 64 |
| actor | 0x00000005 | 3352 | 80 |
| actor | 0x00000007 | 17044 | 54 |
| actor | 0x00000008 | 4872 | 104 |
| actor | 0x00000009 | 11352 | 137 |
| actor | 0x0000000A | 3340 | 60 |
| actor | 0x0000000B | 58904 | 80 |
| actor | 0x0000000C | 3336 | 81 |
| actor | 0x0000000F | 3280 | 69 |
| actor | 0x00000010 | 5024 | 273 |
| actor | 0x00000014 | 3800 | 81 |
| actor | 0x00000018 | 3872 | 60 |
| actor | 0x0000001A | 3644 | 93 |
| actor | 0xFFFFFFFF | 3276 | 212 |
| actor | 0xFFFFFFFF | 6400 | 125 |
| actor | 0xFFFFFFFF | 7184 | 92 |
| owner | 0x0066B778 | 1096 | 3 |
| root | 0x00656A40 | 8112 | 8 |
| root | 0x006587B8 | 60580 | 1 |
| root | 0x00667A04 | 512 | 13 |
| spawner | 0x0066B8E0 | 1084 | 171 |

The loader keeps its own value at every listed offset, as block id 0: `load_keep` appends the
class offsets to a record's patch list, and pass 1 captures this process's own dword before the
body is copied. Measured: `# load keep-local classes=21 offsets=1861 patches=231417`.

**One aligned run survived 600 Presents; the survival does not reproduce.** The 13C file
(6,016,180 bytes, `patch=86511`) applied at Present 3400 (`paired=1020 mismatched=0`,
`# load bodies actors=1009 cap=0`) survived to Present 4000. Without the map the same file at the same frame ended the process 3 of 3.

`TJ_LOAD_MASK=0` survived to 4821, `=4` to 4901, `=2` still stopped at 2609. A second save at
the same frame (6,342,148 bytes, `patch=113675`) ended at the load frame 2 of 2 with the map and
3 of 3 with `TJ_LOAD_MASK=0`; `TJ_LOAD_STOP=5` survived to 4494 on that file, at 3400 and 3500
alike.

At the natural
first-level Present (2609) the load still stalls (three runs, no Present after it). A body of
one run is not a body of another.

**First differing row after the aligned load:** `spawn0.burst 3401` (15,197 of 21,600 probe rows
equal, 6,403 differ). The four spawner records pair by scan index, and address order is not stable across
processes: the loaded run's `spawn3.timer` holds the uninterrupted run's `spawn2.timer`
(`40CDDE76` at Present 3400 and 3401). The rand row follows at Present 3610, the non-actor
consumer.

**The bisect stopped short of a field.** At `TJ_LOAD_MASK=2`, 252 actor bodies survive and 253
end the run; the 253rd is record id 256, class 16, size 5,024. The byte prefix at 3,248 bytes
kills.

It does not reproduce: `cap=252` survived four of four repeats after one earlier death;
`bytes=3248` survived two of two after one. The death is recorded as non-deterministic.

Two more blockers: the save file is not reproducible (86,511 patches versus 113,675 at the same
Present, because the region scan stops at 1,024 regions), and spawners pair by scan index. The
next step is to make the load deterministic, then pair spawners by the owner's container at
`[owner+0x434]`.
