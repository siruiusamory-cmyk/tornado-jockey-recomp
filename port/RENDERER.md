# Renderer abstraction and its two backends

The game still calls d3d8. Since Stage 14 every call lands on the port's interface first, and a
backend decides what reaches the GPU:

```
game -> d3d8.dll (proxy adapters) -> render::Device -> backend
                                                      legacy: d3d8 provider (system / d3d8to9)
                                                      d3d12:  our own ID3D12Device, no provider
```

| File | Role |
| :--- | :--- |
| `port/proxy/render/render.h` | the interface; its method set is the measured device and resource call set |
| `port/proxy/render/render_legacy.h` | backend 1: one forward per call to the provider's vtable slot, provider HRESULT returned |
| `port/proxy/render/render_d3d12.h` + `_impl1/2/3` | backend 2: device, flip-model swap chain, root signature, PSO cache, upload ring, read-back |
| `port/proxy/render/shaders/tj.hlsl` | pass-through vertex stage (transform and lighting run on the CPU) and one pixel stage selected by a constant buffer |
| `port/proxy/d3d8_proxy.cpp` | adapters `A_*`, slot table `kAdapters`, `ResWrap` vtables, fake `IDirect3D8` and 97-slot device table on d3d12 |

`faithful` selects the legacy provider (`faithful=1` system d3d8, `faithful=0` vendored
d3d8to9); it no longer selects a backend.

## Settings

`render=legacy|d3d12` in `port/proxy/d3d8.conf`, overridable with `TJ_RENDER`. Absent means
`legacy`, which stays the default while the D3D12 frame is not accepted.

## What a run reports

Every 100 Presents:

```
# render backend=d3d12 device_calls=65176 present=500 draws=820 skipped=0 pso=1 upload=0
# render textures uploaded=0 missing=0
```

`device_calls` counts adapter calls; `draws`, `skipped`, `pso`, `upload` are the backend's.
`TJ_RENDER_TRACE=1` writes one `# call <iface> <slot> <site>` per adapter call and the HRESULT of
every structured device call.

## Fixed-function set emulated by D3D12

Measured from the census, not the API: FVFs `XYZ|DIFFUSE|TEX1`, `XYZRHW|DIFFUSE|TEX1`,
`XYZ|DIFFUSE|UV`, `XYZ|NORMAL|TEX1`; triangle list, strip and fan (strips and fans expanded on
the CPU to preserve winding); two texture stages with colour and alpha op, D3DTA arguments,
`TEXCOORDINDEX`, `TEXTURETRANSFORMFLAGS` with `PROJECTED`, `TEXTUREFACTOR` and fog; alpha test
with every `D3DCMPFUNC` on 8-bit operands; alpha blend with D3D8 factors and ops; depth test
and write, cull and fill mode; `Clear`, `SetViewport`; D3D8 lighting for point, spot and
directional lights with material sources. Transforms (`SetTransform` 2, 3, 256+n, 16..19) are
kept verbatim; the game reads them back (`GetTransform` 203,070 times in the level).

## Resource bar

- The game's resource objects are ours: `ResWrap` plus a complete vtable per interface. A
  method outside the model forwards through `raw_call` when there is a provider.
- A wrapper must never reach the provider as its own object (d3d8's `CopyRects` faults), and
  `Surface::GetDevice` returns our device wrapper because the game calls it directly.
- Census: vertex-buffer `Lock`/`Unlock` 5.99/frame menu, 32.59/frame level; surface
  `LockRect`/`UnlockRect` 0.35/1.62; texture `LockRect`/`UnlockRect` 0.05/0.14. Surface upload,
  not texture upload, carries texture data.

## Legacy bar

The menu capture is byte-identical to the Stage 10 translated capture (`F6DE0320...541E`,
cursor not pinned), both oracle goldens match (`0C6D01C2...93DB8`, `E7595A2D...EEF9`), and the
step gate's five runs are identical.

## Stage 14C: the d3d12 path survives

Three runs to Present 500 with no fatal:
`device_calls=57319 present=500 draws=820 skipped=0 pso=1 upload=4`.

- `Surface12` initialises `face`, `level`, `backbuffer`; its destructor drops the level's
  reference; `Release` only decrements; a level surface starts at two references.
- `CopyRects` refuses a format mismatch (`0x8876086C`), requires the rectangle to fit, counts
  block rows, uses the destination pitch, and clamps the read-back.
- `bind_texture` never uploads a render-target texture from its CPU copy.
- `R_GetType` answers from the interface the game holds: 0 sent the loader to `GetLevelDesc`
  where the provider's 3 (`D3DRTYPE_TEXTURE`) takes `GetLevelCount`.
- The D3D12 debug layer is not installed (`D3D12GetDebugInterface` = `887A002D`); the backend's
  own diagnosis rows are gated on `TJ_D3D12_DEBUG=1`.
- The 2D UI path matches legacy counts (`DrawPrimitiveUP 134`, `SetTexture 2971`,
  `SetTextureStageState 16085`, the state-block calls); ten textures are created later.
- The frame was one colour (`3ABDB676...938937`, `pixels_above_16 268,371 of 270,000`,
  mean 370.10): every vertex transformed to the same clip position.

## Stage 14D: the frame arrives

Five defects, found by printing each draw's inputs and transforms beside its output:

1. `SetViewport` copied `D3DVIEWPORT8` DWORDs into `float viewport[6]`; 256x256 became 1.4e-43
   and every draw was clipped while `Clear` still painted.
2. d3d8 draw calls pass a primitive count; read as vertices it drew a third of every list,
   dropped calls under three primitives, and overran the index block.
3. A render-target change did not reset the viewport; d3d8 resets it to the new target.
4. The staging buffer had `SampleDesc.Count = 0`, so every upload was refused and every texture
   sampled black.
5. `SetMaterial` wrote 64 bytes per 16-byte row; material sources now default to d3d8's
   MATERIAL, and a lit vertex takes its diffuse from the material.

The CPU reference of the legacy transform matches the backend's clip positions on all 1,116
sampled indexed draws of one menu run, max abs diff 0.002461.

Frame: `> 16` on 142,091 of 270,000, mean abs diff 60.27, 143,091 pixels drawn, 75,348 colours.
Missing: the background plate and button panel, which use stage 1 with a projected texture
coordinate and `MODULATEALPHA_ADDCOLOR` (126,909 pixels; 1,218 stage-1 calls in the level
census). The level on d3d12 reached replay frame 708 / Present 700 and stalled (unresponsive,
full core, no fault row).

## Stage 14E: the frame completed

Two defects owned the missing half:

- A strip or fan expands to `vertices - 2` triangles, but the index block was sized by vertices:
  the second triangle of every 4-vertex strip lost its last index and the block was overrun.
  The block is `triangles * 3`; `outCount` drives upload, view and `DrawInstanced`. Pixels drawn:
  143,091 -> 268,000 of 270,000; the menu's 1,592 draws report `stage1=0 modadd=0`.
- Two texture descriptors were shared by the whole frame. A descriptor heap is read at execution,
  not recording, so every draw sampled the last texture copied. Each draw now takes its own pair
  from a ring (1..1023); texture views grow down from 2047. Before the ring the frame held 1,084
  colours, textures smeared across it.

The pixel stage serves two texture stages in d3d8 cascade order. A `D3DTOP_DISABLE` at stage 0
stops the cascade.

Each served stage carries colour and alpha op with D3DTA arguments (DIFFUSE,
CURRENT, TEXTURE, TFACTOR, SPECULAR with its white default, and the read modifiers), the
coordinate set and generated source, `TEXTURETRANSFORMFLAGS` with `PROJECTED`, `TEXTUREFACTOR`
and fog. The lighting-derived specular term is not modelled: the measured SPECULAR arguments are
alpha arguments on a vertex format with no specular component (23,840 alpha-argument draws).
Measured: ten distinct state pairs
in the level, two in the menu; `modarg=0` in both runs.

Three D3D12 validity rules, each caught as `# d3d12 pso fail hr=80070057`: an alpha blend factor
may not be SRC/DEST_COLOR (mapped to its alpha counterpart); `DepthWriteMask=ALL` is refused
while depth test is off; the alpha blend op follows the colour one, MIN/MAX included. Before:
5,625 level draws with no PSO. After: `skipped=0`.

`D3DRS_CLIPPING` is 136 and maps to `DepthClipEnable`; Stage 14E read 146
(`D3DRS_SPECULARMATERIALSOURCE`), which the level sets once. The level never calls
`SetRenderState(136)`, and D3D8 clips by default.

The stall: `port/proxy/stall.h` takes one bounded sample of the presenting thread (registers,
16 frame words, 24 live-stack words, each resolved to its module) into preallocated storage.

On the Stage 14D index block it reproduced at Present 700 / 10,090 draws; five samples 3-5 s
apart sit in ntdll's heap path, entering the game at `0xA7D79` (caller `0xA7FA1`, then
`0x8EA3E`, `0x94C0A`). The heap corruption is the index-block overrun. With the block fixed the
same replay reaches Present 4000, 1,221,109 draws at 4000, and 6000, with no stall row.

Fog: only `D3DFOG_LINEAR` is emulated, applied once on projected depth. EXP and EXP2 take
`D3DRS_FOGDENSITY`, have no measured caller, and are unsupported: `render::fog_applies` refuses
their fog bit, the draw is counted `fogmode_unsupported=`, and the first one logs
`# d3d12 fog mode unsupported`. Selection is d3d8's: `FOGTABLEMODE` when not NONE, else
`FOGVERTEXMODE`.

Menu frame: five samples, all `D3FF062F...97F`, 24,507 pixels above 16, mean abs diff 19.4911,
spread 0. The residual is on high-contrast edges and sky gradient bands. Nothing was blessed.

Level at Present 3700 is still the loading overlay on d3d12 (`248,317` pixels above 16, mean
79.1960, five identical samples) while legacy is in level play. Both backends make the same
number of device calls there (0.02 % apart), the level loads and runs to 6000, and no stall row
appears. Why the load takes more Presents on d3d12 is OPEN.

## Descriptor partition

The 16,384-descriptor heap holds descriptor 0 as the white texel, 4,095 draw pairs in 1..8,190,
and 8,192 texture views in 16,383 down to 8,192 with a free list. Descriptor 8,191 is unused;
the last pair base is 8,189.

A recorded list holds 4,095 draws; the next submits, waits and restarts. The census row counts
restarts (`flush=`) and SRV reuse/exhaustion (`srv=`). The level frame is `flush=0 srv=0/0`;
with `TJ_D3D12_PAIRS=2` the menu runs 543 restarts and its capture is byte-identical.

## Texture lifetime

`render::RetireQueue` holds both the resource and its descriptor slot. A retirement during
recording goes to a pending bucket and takes the fence that list's submission signals (at
`Present`, the draw-list restart and the capture copy); a retirement afterwards takes the last
signalled value. A transfer's staging buffer takes the transfer's own fence.

Both buckets are bounded at 256; an entry past the bound is kept for the process and counted in
`# d3d12 retire full dropped=`. Shutdown makes one bounded wait and releases the pending bucket
when that wait completed (`RetireQueue::release_pending_on_complete`); a failed wait keeps the
pending resources and slots, counted in `# d3d12 retire shutdown kept=`.

## A failed draw

`bind_texture` uploads before it copies the descriptor, so a timeout is raised inside the draw.
The draw checks `frameFailed` after each bind, between stage 0 and stage 1. From the failing
bind on it records no second bind, no vertex fetch, no `DrawInstanced`, no barrier, returns
`E_FAIL` and is counted once in `skipped`.

`bind_texture` keeps the CPU copy dirty, so a later frame retries. `Clear`, `CopyRects`,
`Present`, `flush_draws` and `reset_frame` refuse to record or reset after `frameFailed`.

Landing record: C2-C6 map to F1-F5. F1 is the shutdown predicate, F2 the inter-bind guard, F3 the
atomic thread guard with owner checks on every retire and slot mutation, F4 the intent tests, F5
these records. The step-gate RED is the pre-existing spawner scan-order class; the step counting
is intact.

## One owning thread

The lifetime state has no lock: `Device12::retireQueue`, `srvSlots`, `listOpen`, `xferPending`
and the draw pair allocator's `pairUsed`.

- Creation: `CreateTexture`/`CreateCubeTexture` -> `make_texture` -> `srv_alloc` and
  `CreateCommittedResource`; the buffer and surface creates.
- Release: any wrapper's `Release` -> `R_Release` -> `Texture12::~Texture12` -> `retire`;
  `pump_retired`/`drain_retired` -> `srv_free`.
- `submit_pending` is called only by `Present`, `flush_draws` and `CopyRects`, after each
  executes the one in-order queue; `retire_at` only by `upload_texture` from `bind_texture`
  from `draw_triangles`.

All of those are game calls through its renderer object (device at `this+0xA8`) and the proxy's
adapters. The proxy creates no thread; the backend executes every list on the calling thread and
waits inside that call. The game's worker thread (`CreateThread` 0x452F1B) records no d3d8 call
in any retained run.

No lock means no lock order and nothing held while calling out. The only blocking operation is
one bounded wait, capped at 5,000 ms, taken with no lock held and no callback into the game, on
one in-order queue whose submissions come from the same thread.

`Device12::init` records the creating thread. `retire`, `retire_at`, `srv_alloc`, `srv_free` and
`submit_pending` call `own_thread`; on any other thread they write
`# d3d12 thread violation tid= owner= at=` once, fail the frame, and refuse the mutation.
