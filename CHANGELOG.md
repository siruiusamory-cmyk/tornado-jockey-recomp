# Changelog

All notable changes. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
everything below is unreleased and pre-1.0.

## [Unreleased]

Stages 1 to 14E. The measurement record is [`port/STATUS.md`](port/STATUS.md).

### Added

- A 32-bit `d3d8.dll` proxy with the game's export, wrapping Direct3D 8, DirectInput and time
  in memory; one call log, BMP capture and argument log on demand.
- A guarded scripted runner that hashes the game directory before and after.
- Keyboard steering through the game's own pointer channel.
- A faithful switch (`faithful=1`) and one settings file.
- A fixed 1/60 s step under `faithful=0`, gated by a five-run replay comparison.
- A replay with a virtual clock from process start and an offline boot fetch.
- d3d8-to-d3d9 translation, window modes and present scaling.
- Background scripted runs that never take the foreground.
- A read-only memory probe that named the frame driver, tornado state and object tables.
- A state census, in-process snapshot/restore, save file v1 and an aligned load.
- A renderer abstraction with a legacy backend and a native D3D12 backend; the menu frame is
  drawn by D3D12.
- Two gates: `tools/check.ps1` and `tools/gate.ps1`.
- A checked address table, `port/symbols.csv`, and `tools/ghidra_symbols.py` to apply it.
- `TJ_ADAPTER=<vendor id in hex>` pins the D3D12 adapter and the matching D3D9 adapter ordinal for
  the legacy backend; unset keeps the default adapter.
- `.recomp.json` describes the project for recomp.fyi and other readers of that format.
- `TJ_GAME_DIR=self` lets a copy of the game outside the registered install run: the proxy
  answers the SKU install path with the exe's own folder and hides other processes' windows from
  the game's single-instance lookup; the launch tools set it for every run.

### Changed

- The simulation runs on a fixed step rather than the limiter's measured delta when
  `faithful=0`.
- `Tornado.exe` needs no registry shim once the port's image is recovered.

### Fixed

- The D3D8 state map: `D3DRS_CLIPPING` and `D3DRS_ALPHATESTENABLE` no longer collide with other
  states, so the level's alpha test and clipping are applied.
- D3D12 descriptor pairs are never reused while their list is pending.
- A released texture waits for the fence of the submission that could read it.
- A timed-out transfer or stalled GPU fails the frame instead of reusing live pairs.
- Fog: non-linear modes are named unsupported; d3d8's table-versus-vertex precedence is followed.
- D3D12 clamps an indexed draw to the bound index buffer and rejects a start index past it, so the
  index expansion cannot read outside the buffer.
- Fog on D3D12 ramps over eye distance (clip `w`) instead of projected depth, which had removed
  the level's fog entirely.
- Table fog is evaluated per pixel on D3D12, from the interpolated one over `w`, as d3d8 does.
- The menu golden is exact again with the cursor served from a fixed point.
- D3D12 keeps a matrix for all eight texture states; states 20..23 no longer overwrite the
  projection, so the level is drawn instead of clipped.
- MIPFILTER NONE samples the base level on D3D12, as d3d8's disabled mipmapping does; 187 of the
  level frame's 828 draws set it.
- A directional light is routed by its direction; D3DLIGHT8 type 3 had taken the point branch.
- The draw state log carries fog, the stage-0 texture matrix and every texture creation.
- The proxy logs every D3D8 query answer and resource creation with its Present ordinal, call site
  and HRESULT (`TJ_QUERY_LOG=1`, `TJ_RES_LOG=1`).
- The D3D12 format checks answer as the legacy device does at the nine keys that differed, so the
  game's own format selector builds the legacy texture set (883, X8 345).
- The proxy logs the `IDirect3D8` the device hands out and the create-device descriptions, so the
  game's format selector is visible on both backends.
- The proxy dumps what a draw consumes on either backend (`TJ_INPUT_DUMP=<present>:<first>[-<last>]`):
  eight indices, eight vertices, four transforms, ten render states and the stage textures'
  level-0 bytes.
- The proxy logs a texture's create, lock, pre- and post-write level hash, copy and update history on
  either backend (`TJ_TEX_HISTORY=<present>`), plus every surface description and DXT1 level bytes
  on lock and unlock (`TJ_TEX_BYTES=<present>`).
- A D3D12 device sets the x87 precision to single as d3d8 does, so the game's software DXT
  conversion writes the legacy texture bytes; the draw-2 level textures now hash equal to legacy's.
- `TJ_ONLY_DRAW=<present>:<index>` runs one draw alone and `TJ_STAGE_OFF=texture|fog|light|blend`
  neutralizes one stage on either backend; the D3D12 draw row names the texture formats, fog, base
  level mip sampling and lighting.
- D3D12 starts from d3d8's material-source defaults (diffuse COLOR1, specular COLOR2, COLORVERTEX
  TRUE), so a lit draw without normals uses its vertex colour; the isolated cloud sprite drops from
  7,878 to 0 pixels above 16 and the level frame from 34,395 to 21,905.
- The x87 precision is set only when CreateDevice's behaviour lacks `D3DCREATE_FPU_PRESERVE`; the
  game passes 0x40 and the frame is unchanged.

### Known limits

- The in-level D3D12 frame still differs from legacy (21,905 of 270,000 pixels above 16).
- D3D12 raises a first-chance `E06D7363` at replay frame 0 inside the graphics driver's device
  setup; every measured run still reached Present 3700, and no access violation has followed the
  index clamp.
- The step gate is red on a quarantined spawner row split while both oracle frames pass.
- The rand divergence is 1 in 51 bare; the input is not named, and a loaded save re-seeds the
  stream.
- No settings UI, no uncapped frame rate and no native resolution above the scaled 600x450 frame.
