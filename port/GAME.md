# Tornado Jockey (2006, Windows x86): intake and identity

Stage 0 intake and Stage 1 gate. Game files are referenced by path only; no game bytes or file
text appear here. Every value was measured by the stated command.

## 1. Source copy and hashes

Source archive: `C:\Dev\Tornado-Jockey_Win_EN_Setup.zip`. It contains
`tornadojockey-setup.exe` (NSIS) and a third-party file named "Tornado Jockey Crack.exe". The
crack was never extracted into the tree, never run, and its bytes are not quoted.

| Record | Bytes | sha256 |
| :--- | :--- | :--- |
| installer archive, outside the repository | 31,114,425 | `2E1E347483406B6EC9968B61F46A0050508F495FFF33D0C89E76441863D37E9F` |
| installer as extracted, statically | 30,229,152 | `D4C018489F19A96EF3A7A7A600B6EBBAD319593FE892392456ACCFB289D76E88` |
| shipped stub `Tornado.exe` | 2,949,120 | `5C0781EA7ECCB94124735B07A9A65CDD12C2CED0F01C4AAA48B5082599DF3AB1` |
| signed launcher `Tornado-WT.exe` | 3,610,128 | `ECDD622BEC3C6E27D4CA2DA5FC7E6258DF4055A05F1E18A239F5F124E2F6C909` |
| derived image `Tornado.exe` | 2,949,120 | `D541C608C18607608BAB8B2824DEDEF7E65942CCE491F483F890279B8C0EBD8C` |
| repaired runnable image | 2,949,120 | `9D2A7C67B45A2E07893A11F9905A6FBD66A8E8F61091A8C3EA24DBF1182D55E1` |

Command: `Get-FileHash -Algorithm SHA256 -LiteralPath <path>; (Get-Item -LiteralPath <path>).Length`.
Install tree: 2903 files, unpacked with 7-Zip (`C:\Program Files\7-Zip\7z.exe`); the installer
was never executed.

Authenticode (`Get-AuthenticodeSignature`): `Tornado-WT.exe` and `GDF.dll` are Valid,
`CN=WildTangent Inc, OU=Product Development, O=WildTangent Inc, L=Redmond, S=Washington, C=US`;
`wtap.dll`, `fmod.dll` and the derived image are NotSigned. Defender on the derived image:
`Scanning <image> found no threats.`

## 2. Wrapper layout

Sections of the shipped stub (`python tools\pe_report.py`):

| Section | VSize | Raw off | Raw size | Entropy | Flags |
| :--- | :--- | :--- | :--- | :--- | :--- |
| .text | 0x254424 | 0x1000 | 2,445,312 | 0.00 | XR |
| .rdata | 0x56CC6 | 0x256000 | 356,352 | 0.00 | R |
| .data | 0x3667F8 | 0x2AD000 | 122,880 | 0.00 | RW |
| .data1 | 0x900 | 0x2CB000 | 4,096 | 0.00 | RW |
| .rsrc | 0x3E10 | 0x2CC000 | 16,384 | 4.15 | R |

Code and data are all zero bytes; the import directory resolves to 0 symbols, so the stub
cannot execute. Only `.rsrc` carries content.

Sections of `Tornado-WT.exe`: `.text` 405,504 bytes / 6.60, `.pecode` 45,056 / 8.00 (XRW),
`.pccode` 4,096 / 7.96 (XRW), `.rdata` 118,784 / 5.54, `.data` 16,384 / 3.94, `.phs` 4,096 /
0.29, `.rsrc` 57,344 / 4.49.

- Last section raw end 0xA0000 (655,360); certificate directory at file offset 0x370000
  (3,604,480), size 5,648.
- Overlay [655,360, 3,604,480) = 2,949,120 bytes, exactly the stub's size, entropy 6.49.
  Its first bytes parse as a complete PE with the stub's section table and timestamp.
- Authenticode covers every byte before the certificate table, so the overlay is covered by
  WildTangent Inc's valid signature. The extracted image is the vendor's bytes.
- `.pecode`/`.pccode` are encrypted, mapped RWX: the wrapper's own protected payload.

## 3. The real image

`llvm-readobj --file-headers --sections` + `python tools\pe_report.py`:

- Machine `IMAGE_FILE_MACHINE_I386`, PE32, Magic 0x10B
- TimeDateStamp 2006-07-14 21:22:54 (0x44B80B2E)
- Linker 7.10, Visual Studio .NET 2003 (7.1)
- ImageBase 0x400000, entry RVA 0x1C1839 (VA 0x5C1839)
- SectionAlignment/FileAlignment 4096/4096, SizeOfImage 0x619000, SizeOfHeaders 4096
- Subsystem `WINDOWS_GUI`, version 4.0
- BaseRelocationTable RVA 0, size 0: no relocation table, so the image loads at 0x400000 or
  not at all

| Section | VSize | Raw off | Raw size | Entropy | Flags |
| :--- | :--- | :--- | :--- | :--- | :--- |
| .text | 0x254424 | 0x1000 | 2,445,312 | 6.69 | XR |
| .rdata | 0x56CC6 | 0x256000 | 356,352 | 5.34 | R |
| .data | 0x3667F8 | 0x2AD000 | 122,880 | 2.15 | RW |
| .data1 | 0x900 | 0x2CB000 | 4,096 | 3.47 | RW |
| .rsrc | 0x3E10 | 0x2CC000 | 16,384 | 0.00 | R |

The image's own `.rsrc` is all zero; the stub's is populated. No overlay, no certificate.

## 4. Imports: 277 symbols over 14 DLLs

| DLL | Count | DLL | Count |
| :--- | :--- | :--- | :--- |
| KERNEL32.dll | 119 | OLEAUT32.dll | 11 |
| USER32.dll | 76 | AVIFIL32.dll | 8 |
| fmod.dll | 20 | ADVAPI32.dll | 6 |
| ole32.dll | 13 | MSVFW32.dll | 4 |
| GDI32.dll | 12 | VERSION.dll | 3 |
| SHELL32.dll | 2 | d3d8.dll | 1 |
| DINPUT8.dll | 1 | WINMM.dll | 1 |

- `d3d8.dll`: `Direct3DCreate8` (hint 0). One factory call, then COM vtable dispatch.
- `DINPUT8.dll`: `DirectInput8Create` (0).
- `fmod.dll` 3.74: 20 `_FSOUND_*`/`_FSOUND_Stream_*` entries.
- Codec/video: AVIFIL32 (8), MSVFW32 (4).
- No `wtap.dll`, no `GDF.dll`, no wininet/urlmon, no licensing host string.

## 5. Assets

Census over the game directory (2902 files):

| Extension | Count | Container | Tag |
| :--- | :--- | :--- | :--- |
| .wkz | 1436 | `WKZ` magic + u32 uncompressed size + zlib at offset 7 | MEASURED |
| .MDL | 1112 | plain binary, no printable magic | BLOCKED |
| .gif | 166 | plain | MEASURED |
| .jpg | 45 | plain | MEASURED |
| .SMS | 41 | plain binary | BLOCKED |
| .SMA | 27 | plain binary | BLOCKED |
| .html/.htm | 15 / 7 | launcher web layer | MEASURED |
| .SCN | 11 | plain binary | BLOCKED |
| .dll | 10 | game-supplied | MEASURED |
| .js | 9 | launcher web layer | MEASURED |
| .png, .AMS | 5, 4 | plain / plain binary | BLOCKED for AMS |
| .exe, .css, .wav, .ico | 4, 2, 2, 2 | mixed | MEASURED |
| .nsis, .dds, .avi, .xml | 1 each | mixed | MEASURED |

WKZ: all 1436 files carry the magic and a positive size; the zlib stream inflates to that size
(23,989 == 23,989 on a 14,914-byte file). WKZ is `magic(3) + u32 size + zlib`. MDL/SMS/SMA/
SCN/AMS readers are Stage 2 work.

## 6. Launcher web layer

`game.js` and `main.js` are the WildTangent "wire" browser layer: `window.external.*` bindings
(`GetDPCodeName`, `GetSKU`, `GetIsOwned`, `SetClientSize`) and
`rdr.wildtangent.com/wire2/...` URLs. Nothing in them starts the game process.

## 7. Stage 1 gate: what the real image does at startup

`pwsh -NoProfile -File tools\oracle-launch.ps1 [-Trace] [-Wait ms]`, launched with
`-WorkingDirectory` at the game directory.

- Unshimmed: the loader stops the process with "Entry Point Not Found" naming a KERNEL32
  by-name import. RUNTIME-PROVEN (screenshot sha256
  `6199449D3CE2BD1EBE675E6FD6532169434A60E3019C70E69FB7E78BDEE4798F`).
- Cause: exactly one import-by-name string in `.rdata` is scrambled. IAT slot VA 0x2561DC,
  name at RVA 0x2ACB34, hint 237, 26 bytes. The launcher's own table carries hint 237 for
  `FreeEnvironmentStringsA`.
- Shim: `oracle-launch.ps1` creates the child suspended, proves the slot with
  `ReadProcessMemory`, rewrites 24 bytes at VA 0x6ACB34 to `FreeEnvironmentStringsA` + NUL,
  restores protection and resumes. Memory only; no disk byte or registry key changes.
- After the shim the loader error is gone and the process dies at `0xC0000005`,
  `GAME+0x1FC256`, after 38 DLLs load.
- Cause: the instruction at VA 0x5FC256 is `mov [disp32], edx` with the impossible
  0x716AE9A8; the 38 following bytes do not decode, and code resumes at 0x5FC282. A sweep for
  absolute operands outside 0x400000..0xA19000 finds 59, so more than one window is plausible.
- The repair is not in this tree: the launcher has no plaintext copy of either damaged site
  and no 32-bit constants of them. Its only high-entropy payload is `.pecode`/`.pccode`, so
  the repair data is inside the encrypted wrapper payload.

## 7b. The launcher's protected payload

Ghidra 12.1.2 over the signed launcher: 138,893 instructions, 5,014 functions, 223 named
library functions.

- `.rdata` holds a pointer table at 0x70A94 (VA 0x470A94): 140 entries, 83 distinct targets,
  all inside 0x46415A..0x46D569.
- 1588 direct `call`/`jmp` sites in `.text` target 0x464000..0x470000. The encrypted region is
  a module of about 83 functions with a plaintext dispatch table.
- No plaintext copy of the region exists outside the overlay (40-byte signatures, 0 hits).
- No local transform found: entropy flat at 7.996/7.958; no zlib/lzma/bz2 stream; no repeating
  key (lag 1 0.00358 against 0.00391 uniform); single-byte XOR/subtract 0.0854/0.0864 against
  0.0820 baseline; RC4 with ten candidate keys 0.128..0.142 against 0.133 baseline.
- No decryptor anchor: one operand in 0x60000..0x72000 at 0x45D9A5 (`ADD EAX,0x60000`, a
  null-guard), and the only `VirtualProtect` site (0x437507) is `__resetstkoflw`.
- The entry point reads its own headers (`GetModuleHandleA`, `MZ`, `PE`, magic, resource count)
  then runs the CRT init chain. No decryption loop.
- `.phs` is a product descriptor: GUID `{CD83F69C-8826-46FA-B955-88970BEFE38E}`, a product name
  and an exe name. Not key material.
- The game image contains no repair table: the two damaged RVAs appear 0 times as 32-bit values.

Conclusion: the payload is protected code, its decryption is not locally reproducible with
these instruments, and it is not a repair patch set. The damage windows are prefix-preserving
overwrites, so the correct bytes are not recoverable by a cipher that acts on them.

## 7c. The crack payload and the recovered repair set

The third-party crack was read statically and never executed. Identity (not distributed):
1,119,218 bytes, sha256
`53A3C14E0B2D5DF13FE187871E13341D2CE550CFBB05466DAE3FA148238A266E`, NotSigned, Defender
found no threats.

It is a RAR self-extractor; its overlay starts at 0x1D200 and holds one
member, `Tornado-WT.exe`, 6,393,856 bytes, sha256
`E2B151DB0DD4D8DD954FD499D8F6C9438BEB871FEF05ECCEE400F79AF18C17F0`, read into a scratch
directory. That member is a memory image of a working process from the same build.

The working image differs from the shipped overlay in 341 runs, 5201 bytes, none longer than
16 bytes, outside the loader-filled IAT (0x256000..0x256488):

| Site | Extent | Working image holds |
| :--- | :--- | :--- |
| import-name slot | run at 0x2ACB3A, 16 bytes | `FreeEnvironmentStringsA` |
| code window | run at 0x1FC25B, 16 bytes | valid code, 44 bytes from 0x5FC256 decode |
| `.data` | 122,880 bytes from 0x2AD000 | the launcher-loaded windows the shipped copy lacks |
| `.data1` | 2,304 bytes at file 0x2CB000 | second segment |
| `.rsrc` | 16,384 bytes at 0x2CC000 | equal to the shipped stub's `.rsrc` |

The `.data`/`.data1` rows were missed by the first tool version (291 runs, 4452 bytes); that
omission was the fault at 0x489A12. The `.rsrc` equality makes the resources recoverable
without the crack.

`tools/unwrap.py` applies those rules to the overlay and refuses to write when a rule fails
(run longer than one 16-byte window, or a stub `.rsrc` mismatch). Output: 2,949,120 bytes,
sha256 `9D2A7C67B45A2E07893A11F9905A6FBD66A8E8F61091A8C3EA24DBF1182D55E1`; two consecutive
runs agree.

The repaired image, launched with no shim, no wrapper and no registry hook (PID
30400), reached the title screen "Tornado Jockey". RUNTIME-PROVEN (screenshot sha256
`4B31C0F50D347A43E7984EC8195B6E5078EFD406896233DC80D48AC269216062`). Gameplay is untested.

The SKU gate reads HKLM: `RegOpenKeyExA` 0x54FBB7 on
`SOFTWARE\WildTangent\InstalledSKUs`, `RegEnumKeyExA` 0x54FC27 per subkey, then
`InstallDirectory` 0x67D2F8. The image contains no `HKEY_CURRENT_USER` reference. The minimal
registration writes the 14 REG_SZ values under
`HKLM\SOFTWARE\WildTangent\InstalledSKUs\WT025771` (product code `tornadojockey`, display name
`Tornado Jockey`, runtime `Tornado.exe`, `UseLeaderboard=True`, `InstallDirectory` at the game
directory).

## 8. HKCU / machine state

Nothing written. `HKCU\Software\WildTangent` does not exist before or after all runs; the shim
writes no registry and takes no HKLM action.

## 9. Reproduce

```
cd C:\TornadoJockey
python tools\pe_report.py <shipped stub> <signed launcher> <derived image>
python tools\imports.py
llvm-readobj --file-headers --sections --coff-imports <path>
llvm-objdump -d --no-show-raw-insn --print-imm-hex --x86-asm-syntax=intel `
    --start-address=0x5FC250 --stop-address=0x5FC290 <path>
pwsh -NoProfile -File tools\oracle-launch.ps1 -Trace -Wait 20000
```

## 10. Simulation

Addresses are VAs of the repaired image (base 0x400000). The static half is Ghidra 12.1.2 and
capstone; the runtime half is `TJ_PROBE` inside the replay (`port\REPLAY.md`).

### 10.1 The tick

One frame is `FUN_00508F50` (542 bytes). Body in order: input pre-step `call 0x4EE940` at
0x508F5E; frame delta from the limiter's clock `call [eax+0x30]` at 0x508F6B; on the countdown
path a sim step `call 0x48B7B0` at 0x508F86; the limiter target `1.0f/[obj-0x38]` (0x508F91,
0x508F94) compared at 0x508F9E with `Sleep(0)` 0x508FB7 per spin; the normal step at 0x508FDC.

- Step driver `FUN_0048B7B0` (57 bytes): `mov eax,[0x6D09C0]`, two pre-step virtuals
  ([0x6D09B8]+0x68, [0x6D09D8]+0x50), then `jmp 0x48E8D0` with the delta still on the stack.
- Stepper `FUN_0048E8D0` (299 bytes, `ret 4`): running gate `[sim+0x2A]`; pause path
  (`GetForegroundWindow` 0x48E8EF against virtual +0x98, `call 0x48C790` 0x48E914); clamp and
  scale 0x48E919..0x48E952; substep loop 0x48E960..0x48E9F0 with eight services: 0x48E993
  `[0x6D2700]+8`, 0x48E99E 0x48A8B0, 0x48E9A9 0x48B150, 0x48E9BA `[0x6D09D4]+0x4C` (input),
  0x48E9C3 0x498C10, 0x48E9CE 0x491340 (scene pass), 0x48E9D9 0x48DD40 (UI), 0x48E9E6
  `[0x7E0AA8]+0x70` (script VM).
- Scene pass `FUN_00491340` (794 bytes) walks `[scene+0xEC94]` 0..4999 and calls each actor's
  virtual +0x164. Tornado advance `FUN_0041F650` (1693 bytes) is `0x658EF8+0x198`.
- Rendering: thunk 0x5091D0 -> `FUN_004DE1C0`, one Present at 0x4DE64A per entry.
- Message pump `FUN_00508040` (344 bytes): PeekMessageA, TranslateMessage, DispatchMessageA,
  WM_QUIT, per-frame virtual `[vtable+0x40]`. Start-up pump 0x4A33B0 walks a step chain against
  the 0.5 s window 0x656B60; it contains no message API.
- Dispatch of 0x508F50/0x5091D0 is data-only (vtable slots 0x661C18/0x661CF8,
  0x678E40/0x678F20); the loop is BLOCKED statically.

### 10.2 Timestep

Variable frame delta, fixed substep count, no remainder accumulator.

| Offset in `[0x6D09C0]` | Type | Meaning | Instruction |
| :--- | :--- | :--- | :--- |
| +0x18 | float | max frame delta | 0x48E919, clamp 0x48E937 |
| +0x2C | float | time scale | 0x48E947 |
| +0x30 | int | substeps this frame | 0x48E942 |
| +0x10 | float | scaled frame delta | 0x48E952 |
| +0x14 | float | summed delta | 0x48E968 |
| +0x1C | int | substep counter | 0x48E97A |
| +0xC | float | step multiplier | 0x48E981 |
| +0x8 | float | per-step delta every consumer reads | 0x48E984 |
| +0x4 | float | simulation clock, seconds | 0x48E987/0x48E98A |
| +0x28 | bool | paused, zeroes the delta | 0x48E963 |

Measured with `TJ_PROBE` (three replay runs):

- `[sim+0x30]` = 1 for all 16,892 logged Presents; `[sim+0x2C]` = 1.0.
- `[sim+0x1C]` = Present + 1; over Presents 3401..3701 the delta is 300 steps for 300 Presents.
- `[sim+0x8]` = 0x3C8888C6 (0.01666678 s); `[sim+0x10]` equals it.
- `[sim+0x4]`: 56.6827 s to 61.6826 s over 300 Presents, delta 4.99992 s against 5.00000 s
  (truncation drift 7.6e-5 s); 216.6579 s at Present 13,000 against 216.6833 s.
- `[sim+0x18]` = -1.0f before the level and 0.05 s in level play (20 Hz maximum).

### 10.3 Tornado state

TORNADO class, 0x2C58 bytes, factory 0x40DE90 (`push 0x2C58`, allocator 0x45446B, ctor
0x41C0B0), vtable 0x658EF8. It is reached through a three-dword handle on
`[0x6D09D4]+0x1E70/+0x1E74/+0x1E78`, resolved by `FUN_004908F0`:
`obj = *(*(scene+0xEC94)+index*4)`, with `index < 0x1388`, `obj+8 == handle[+0x1E70]`,
`obj+0xC == index`, `obj+0x10 == handle[+0x1E78]`. `FUN_00402290` is the accessor.

| Offset | Type | Meaning | Writer |
| :--- | :--- | :--- | :--- |
| +0x8/+0xC/+0x10 | int | generation / slot / version | `FUN_00491080` insert |
| +0x28/+0x2C/+0x30 | float | position x, y, z | tick 0x41F650 |
| +0xDAC | float | copy of `[sim+8]` | 0x41F6F5 |
| +0xF20/+0xF24 | float | tracked pair (x, z) | tick 0x41F650 |
| +0xF28/+0xF2C | float | previous pair | tick 0x41F650 |
| +0xF30 | float | speed = sqrt(dx²+dz²)/dt | 0x41FC95, 0x41FC9B |
| +0x25E8 | float | height property | tick 0x41F650 |
| +0x261C | float | wind-speed property | setter 0x410B56, ctor zero 0x41CB3C |

Strength and life are on the game object, not the tornado: `+0xE58` dword, 0 all run; `+0xE5C`
float rewritten from `rand()` at 0x41F6B0; `+0xE60` float initialised 100.0f and decaying. The
property descriptors sit at 0x6ADB08, 0x6ADB40 and 0x6ADB78. No Fujita/rating field exists.

Measured at Present 3700: handle `id1=3, index=2, id2=1`, vtable 0x658EF8, `+0xDAC`=0x3C8888C6,
position `BFDF33CC/BD85F4A6/C3BCBA78`, pair `3EFF258C/3F5E6F81`, `+0x261C`=50.0. `+0xE5C`
moved through 39448498, BA20034D, B9D5972E while `+0xE60` fell 99.34 -> 94.61 -> -0.093 by
Present 6000.

### 10.4 Object and debris tables

- Slot table: pointers at `[scene+0xEC94]`, hard capacity 5000 (`cmp eax,0x1388` 0x48F8F1).
- Allocator `FUN_0048F8D0` (60 bytes): `+8` high-water, `+0xC` free stack, `+0x10` free count;
  pops a free slot or bumps the high-water, `-1` when full. Live at `scene+0xEC90`.
- Spawn `FUN_00491680` takes a class id, calls the factory at `0x6D13E0+id*4`, inserts through
  `FUN_00491080`. Update: scene pass per actor virtual +0x164; tornado 0x41F650.
- Destroy: push 0x48F955 (count 0x48F965) in `FUN_0048F910`, reached from `FUN_00490300`
  (0x49032D..0x490352); slot clear 0x48F97B/0x48F9A4.
- Debris: in-object ring on the tornado. Head `+0xFA8`, kinds `[+0xFAC+4i]` (0x41B15D),
  cursor `+0xFDC`, count `+0xFE0`; id triple at `+0xF60` stride 0xC; node 0x14 bytes, capacity
  6 by layout. Add 0x41AD30, drain 0x41B380, remove 0x41AB40. Probe: `+0xFE0` 0 to Present
  3692 then 1; `+0xFDC` 1 from Present 716.
- High-water count `[scene+0xEC98]`: 1 at Present 1, 572 at 716, 573 at 746, 1015 at 2609,
  1018 at 5663. The array base and scene pointer differ between runs (heap layout).

### 10.5 Reproducing this section

```
pwsh -NoProfile -File tools\build-proxy.ps1
$env:TJ_REPLAY='C:\TornadoJockey\tools\fixtures\replay-menu-level1.txt'
$env:TJ_FIXED_CURSOR='1'
$env:TJ_PROBE='6D09C0+1C,6D09C0+4,6D15E0+EC94,6D09D4+1E74,6D15E0+EC94@6D09D4+1E74.0'
pwsh -NoProfile -File tools\proxy-run.ps1 -Name probe -CaptureAt 3700 -TimeoutSeconds 150
```

Rows read `# probe 6D15E0+EC94@6D09D4+1E74.0 3700 00658EF8`.

### 10.6 Determinism: the tracked pair

| Round | Terms | Runs | First differing Present |
| :--- | :--- | :--- | :--- |
| 1 | 20 | 6 | 3791 in four runs, 3101 in one; one matched |
| 2 | 32 | 3 | none (5,382-5,410 Presents identical) |
| 3 | 8 | 6 | 2694, 3011 or 3116; one matched |

Only the pair and the one-frame `+0xF30` differ. Row-identical: rand state 0x6C8910,
`[sim+0x1C]`, `[sim+0x8]`, `[sim+0x4]`, position, identity triple, `+0x25E8`, `+0x261C`,
`+0xE5C/+0xE60`, actor and debris counts.

The image sweep (695,573 instructions) finds writes to displacement 0xF20 only in the TORNADO
constructor (0x41C276/0x41C6D3) and another class (0x4C7B91). The tick reads the pair at
0x41FC59/0x41FC5F and writes only the previous pair at 0x41FC79/0x41FC7F. The writer uses a
computed base; the source is not identified.

Excluded: rand state, step count/delta, identity, position, counts, and the limiter's
`Sleep(0)` yield (swallowing it left 5 of 6 runs differing and slowed every run; reverted). The
outcome tracks per-frame cost, so the trigger is timing-sensitive.

### 10.7 The pair is the normalised mouse position

The writer is `FUN_00410F80`, storing at 0x410FA6 (`mov [esi+0x254],eax`) and 0x410FB4
(`mov [esi+0x258],ecx`). Its `this` is tornado+0xCCC, so no instruction writes `[reg+0xF20]`.

Caller chain: 0x48BFD0 splits x and y out of lParam and calls `FUN_00491C70`; that calls
`FUN_004917C0` at 0x491D70; dispatch 0x49189B (`call [edx*4+0x6B9FB8]`, table 0x6B9FB8)
reaches the setter. One thread, the frame thread.

The dwords are x/width and y/height: lParam 0x0187012B (299, 391) gives 299/600 = `3EFF258C`,
391/450 = `3F5E6F81`. The differing input is the physical mouse: replay now drops every
`WM_MOUSEMOVE` it did not post (`IoPeekMessage`).
