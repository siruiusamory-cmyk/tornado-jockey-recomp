# D3D8 usage: start-up and the title screen

Measured from the running game. Scope: start-up and the title screen; gameplay usage was
unmeasured until the replay existed.

Two runs with `TJ_D3D8_ARGS=1` and the Stage 4 capture on (run 14, run 15). Both reached the
menu at Present 500, captured the Stage 4 golden hash, left the game directory byte-identical
and removed the drop-in. Argument sets: 116 in each run, identical as sets.

Counts differ because the runs end after the capture.

## Slots called

| IF | Slot | Method | Run 14 | Run 15 |
| :--- | :--- | :--- | :--- | :--- |
| 0 | 4 | GetAdapterCount | 1 | 1 |
| 0 | 5 | GetAdapterIdentifier | 4 | 4 |
| 0 | 6 | GetAdapterModeCount | 1 | 1 |
| 0 | 7 | EnumAdapterModes | 44 | 44 |
| 0 | 8 | GetAdapterDisplayMode | 6 | 6 |
| 0 | 9 | CheckDeviceType | 420 | 420 |
| 0 | 10 | CheckDeviceFormat | 422 | 422 |
| 0 | 11 | CheckDeviceMultiSampleType | 2688 | 2688 |
| 0 | 12 | CheckDepthStencilMatch | 168 | 168 |
| 0 | 13 | GetDeviceCaps | 1 | 1 |
| 0 | 15 | CreateDevice | 1 | 1 |
| 1 | 1 | AddRef | 3 | 3 |
| 1 | 2 | Release | 2 | 2 |
| 1 | 4 | GetAvailableTextureMem | 2 | 2 |
| 1 | 6 | GetDirect3D | 37 | 37 |
| 1 | 7 | GetDeviceCaps | 85 | 85 |
| 1 | 8 | GetDisplayMode | 33 | 33 |
| 1 | 12 | ShowCursor | 1 | 1 |
| 1 | 14 | Reset | 2 | 2 |
| 1 | 15 | Present | 1005 | 1046 |
| 1 | 16 | GetBackBuffer | 18 | 18 |
| 1 | 20 | CreateTexture | 32 | 32 |
| 1 | 22 | CreateCubeTexture | 2 | 2 |
| 1 | 23 | CreateVertexBuffer | 26 | 26 |
| 1 | 24 | CreateIndexBuffer | 22 | 22 |
| 1 | 26 | CreateDepthStencilSurface | 1 | 1 |
| 1 | 29 | UpdateTexture | 1 | 1 |
| 1 | 31 | SetRenderTarget | 2 | 2 |
| 1 | 32 | GetRenderTarget | 1 | 1 |
| 1 | 33 | GetDepthStencilSurface | 4 | 4 |
| 1 | 34 | BeginScene | 1006 | 1047 |
| 1 | 35 | EndScene | 1006 | 1047 |
| 1 | 36 | Clear | 1667 | 1753 |
| 1 | 37 | SetTransform | 7303 | 7578 |
| 1 | 40 | SetViewport | 2 | 2 |
| 1 | 42 | SetMaterial | 5262 | 5508 |
| 1 | 50 | SetRenderState | 58658 | 61580 |
| 1 | 52 | BeginStateBlock | 1 | 1 |
| 1 | 53 | EndStateBlock | 1 | 1 |
| 1 | 54 | ApplyStateBlock | 1 | 1 |
| 1 | 55 | CaptureStateBlock | 1 | 1 |
| 1 | 61 | SetTexture | 12761 | 13384 |
| 1 | 63 | SetTextureStageState | 76103 | 79841 |
| 1 | 64 | ValidateDevice | 8 | 8 |
| 1 | 65 | GetInfo | 1 | 1 |
| 1 | 70 | DrawPrimitive | 344 | 340 |
| 1 | 71 | DrawIndexedPrimitive | 5262 | 5508 |
| 1 | 72 | DrawPrimitiveUP | 631 | 676 |
| 1 | 76 | SetVertexShader | 6237 | 6524 |
| 1 | 83 | SetStreamSource | 5606 | 5848 |
| 1 | 85 | SetIndices | 5262 | 5508 |

## Argument sets

Names come from the pinned Wine 10.0 `d3d8types.h`; unnamed values print as hex.

### CreateDevice (slot 15)

| Windowed | Width | Height | Format | Swap | Interval | 14 | 15 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| TRUE | 600 | 450 | D3DFMT_X8R8G8B8 | DISCARD | 0 | 1 | 1 |

### CreateTexture (slot 20)

| W | H | Levels | Usage | Format | Pool | 14 | 15 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 256 | 256 | 1 | 0 | A8R8G8B8 | MANAGED | 8 | 8 |
| 256 | 256 | 1 | 0 | X8R8G8B8 | MANAGED | 6 | 6 |
| 32 | 32 | 6 | 0 | X8R8G8B8 | MANAGED | 4 | 4 |
| 32 | 32 | 6 | 0 | A8R8G8B8 | MANAGED | 2 | 2 |
| 32 | 32 | 6 | 0 | 0x31545844 | MANAGED | 2 | 2 |
| 64 | 64 | 1 | 0 | A8R8G8B8 | MANAGED | 2 | 2 |
| 64 | 64 | 1 | 0 | X8R8G8B8 | MANAGED | 2 | 2 |
| 16 | 16 | 4 | 0 | A8R8G8B8 | MANAGED | 1 | 1 |
| 32 | 32 | 6 | 0 | 0x33545844 | MANAGED | 1 | 1 |
| 128 | 64 | 8 | 0 | X8R8G8B8 | MANAGED | 1 | 1 |
| 128 | 64 | 8 | 0 | 0x31545844 | MANAGED | 1 | 1 |
| 256 | 256 | 1 | RENDERTARGET | X8R8G8B8 | DEFAULT | 1 | 1 |
| 1024 | 1024 | 1 | 0 | X8R8G8B8 | MANAGED | 1 | 1 |

### CreateVertexBuffer (slot 23)

| Length | Usage | FVF | Pool | 14 | 15 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 112 | WRITEONLY | TEX1\|DIFFUSE\|XYZRHW | MANAGED | 4 | 4 |
| 288 | 0 | TEX1\|DIFFUSE\|XYZ | MANAGED | 3 | 3 |
| 384 | 0 | TEX1\|DIFFUSE\|XYZ | MANAGED | 3 | 3 |
| 864 | 0 | TEX1\|DIFFUSE\|XYZ | MANAGED | 3 | 3 |
| 5376 | 0 | TEX1\|DIFFUSE\|XYZ | MANAGED | 3 | 3 |
| 96 | 0 | TEX1\|DIFFUSE\|XYZ | MANAGED | 2 | 2 |
| 60256 | WRITEONLY | TEX1\|NORMAL\|XYZ | MANAGED | 2 | 2 |
| 300160 | WRITEONLY | TEX1\|NORMAL\|XYZ | MANAGED | 2 | 2 |
| 80 | WRITEONLY | DIFFUSE\|XYZRHW | MANAGED | 1 | 1 |
| 96 | WRITEONLY | TEX1\|DIFFUSE\|XYZ | MANAGED | 1 | 1 |
| 60256 | 0 | TEX1\|NORMAL\|XYZ | SYSTEMMEM | 1 | 1 |
| 300160 | 0 | TEX1\|NORMAL\|XYZ | SYSTEMMEM | 1 | 1 |

### CreateIndexBuffer (slot 24)

| Length | Usage | Format | Pool | 14 | 15 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 36 | WRITEONLY | INDEX16 | MANAGED | 3 | 3 |
| 48 | WRITEONLY | INDEX16 | MANAGED | 3 | 3 |
| 108 | WRITEONLY | INDEX16 | MANAGED | 3 | 3 |
| 672 | WRITEONLY | INDEX16 | MANAGED | 3 | 3 |
| 12 | WRITEONLY | INDEX16 | MANAGED | 2 | 2 |
| 13824 | WRITEONLY | INDEX16 | MANAGED | 2 | 2 |
| 25842 | WRITEONLY | INDEX16 | MANAGED | 2 | 2 |
| 13824 | 0 | INDEX16 | SYSTEMMEM | 1 | 1 |
| 25842 | 0 | INDEX16 | SYSTEMMEM | 1 | 1 |
| 27648 | WRITEONLY | INDEX16 | MANAGED | 1 | 1 |
| 51684 | WRITEONLY | INDEX16 | MANAGED | 1 | 1 |

### Clear (slot 36)

| Flags | 14 | 15 |
| :--- | :--- | :--- |
| TARGET | 661 | 706 |
| ZBUFFER | 661 | 706 |
| ZBUFFER\|TARGET | 345 | 341 |

### SetTransform (slot 37)

| State | 14 | 15 |
| :--- | :--- | :--- |
| D3DTS_VIEW | 5950 | 6188 |
| D3DTS_PROJECTION | 1005 | 1046 |
| 0x00000100 | 348 | 344 |

SetViewport: 256x256, 2 calls each run.

### SetRenderState (slot 50)

| State | Value | 14 | 15 |
| :--- | :--- | :--- | :--- |
| LIGHTING | 0 | 6898 | 7230 |
| ALPHATESTENABLE | 0 | 6240 | 6527 |
| ZENABLE | 0 | 5893 | 6184 |
| ZWRITEENABLE | 0 | 5893 | 6184 |
| CULLMODE | 1 | 5893 | 6184 |
| STENCILENABLE | 0 | 5893 | 6184 |
| SRCBLEND | 5 | 5262 | 5508 |
| DESTBLEND | 6 | 5262 | 5508 |
| ALPHABLENDENABLE | 1 | 5262 | 5508 |
| FOGENABLE | 0 | 1923 | 2058 |
| MULTISAMPLEANTIALIAS | 0 | 1006 | 1047 |
| LIGHTING | 1 | 664 | 709 |
| FILLMODE | 3 | 661 | 706 |
| SRCBLEND | 2 | 634 | 679 |
| DESTBLEND | 1 | 634 | 679 |
| ALPHABLENDENABLE | 0 | 631 | 676 |
| SPECULARENABLE | 0 | 4 | 4 |
| CULLMODE | 3 | 3 | 3 |
| SPECULARENABLE | 1 | 1 | 1 |
| SPECULARMATERIALSOURCE | 0 | 1 | 1 |

### SetTexture (slot 61)

| Stage | Texture | 14 | 15 |
| :--- | :--- | :--- | :--- |
| 0 | non-null | 6237 | 6524 |
| 1 | null | 5893 | 6184 |
| 0 | null | 631 | 676 |

### SetTextureStageState (slot 63)

| Stage | Type | Value | 14 | 15 |
| :--- | :--- | :--- | :--- | :--- |
| 0 | ALPHAARG1 | 2 | 5897 | 6188 |
| 0 | COLOROP | 4 | 5896 | 6187 |
| 0 | COLORARG1 | 2 | 5896 | 6187 |
| 0 | COLORARG2 | 1 | 5896 | 6187 |
| 0 | ALPHAOP | 4 | 5896 | 6187 |
| 0 | ALPHAARG2 | 1 | 5896 | 6187 |
| 0 | MAGFILTER | 2 | 5896 | 6187 |
| 0 | MINFILTER | 2 | 5896 | 6187 |
| 0 | MIPFILTER | 2 | 5896 | 6187 |
| 1 | COLOROP | 1 | 5896 | 6187 |
| 0 | TEXTURETRANSFORMFLAGS | 0 | 5893 | 6184 |
| 1 | ALPHAOP | 1 | 5893 | 6184 |
| 0 | TEXCOORDINDEX | 0 | 5262 | 5508 |
| 1 | COLOROP | 0x12 | 4 | 4 |
| 1 | COLORARG1 | 1 | 4 | 4 |
| 1 | TEXCOORDINDEX | 0x00030000 | 4 | 4 |
| 2 | COLOROP | 0x12 | 4 | 4 |
| 2 | COLORARG1 | 1 | 4 | 4 |
| 2 | COLORARG2 | 4 | 4 | 4 |
| 0 | ALPHAARG1 | 0 | 3 | 3 |
| 1 | COLORARG1 | 2 | 3 | 3 |
| 1 | COLORARG2 | 1 | 3 | 3 |
| 1 | ALPHAOP | 4 | 3 | 3 |
| 1 | ALPHAARG1 | 0 | 3 | 3 |
| 1 | ALPHAARG2 | 1 | 3 | 3 |
| 1 | TEXCOORDINDEX | 0 | 3 | 3 |
| 1 | MAGFILTER | 2 | 3 | 3 |
| 1 | MINFILTER | 2 | 3 | 3 |
| 1 | MIPFILTER | 2 | 3 | 3 |
| 2 | COLOROP | 1 | 3 | 3 |
| 2 | COLORARG1 | 2 | 3 | 3 |
| 2 | COLORARG2 | 1 | 3 | 3 |
| 2 | ALPHAOP | 4 | 3 | 3 |
| 2 | ALPHAARG1 | 0 | 3 | 3 |
| 2 | ALPHAARG2 | 1 | 3 | 3 |
| 2 | TEXCOORDINDEX | 0 | 3 | 3 |
| 2 | MAGFILTER | 2 | 3 | 3 |
| 2 | MINFILTER | 2 | 3 | 3 |
| 2 | MIPFILTER | 2 | 3 | 3 |
| 1 | COLORARG2 | 2 | 2 | 2 |
| 1 | COLORARG2 | 4 | 2 | 2 |
| 1 | TEXCOORDINDEX | 0x00020002 | 2 | 2 |
| 1 | TEXTURETRANSFORMFLAGS | 0 | 2 | 2 |
| 1 | TEXTURETRANSFORMFLAGS | 0x103 | 2 | 2 |

### Draws and shader

| Call | Primitive / FVF | 14 | 15 |
| :--- | :--- | :--- | :--- |
| DrawPrimitive | D3DPT_TRIANGLEFAN | 344 | 340 |
| DrawIndexedPrimitive | D3DPT_TRIANGLELIST | 5262 | 5508 |
| DrawPrimitiveUP | D3DPT_TRIANGLESTRIP | 631 | 676 |
| SetVertexShader | TEX1\|DIFFUSE\|XYZ | 5606 | 5848 |
| SetVertexShader | TEX1\|DIFFUSE\|XYZRHW | 631 | 676 |

No shader handle is set; the FVF is the whole binding.

## The two Present sites

| Call site | Return address | 14 | 15 |
| :--- | :--- | :--- | :--- |
| 0x004211F8 | 0x004211FB | 344 | 340 |
| 0x004DE64A | 0x004DE64D | 661 | 706 |

The first serves the start-up screen, the second the menu. The census classifies neither, so the
runtime log is the evidence; `tools/proxy_log.py` carries 0x4DE64A as `PRESENT_MENU`.

## Where the names come from

The vendored `tools/vtable/d3d8.h` includes `d3d8types.h` but does not contain it. This decode
uses a local, untracked copy of the same pinned commit (`wine-mirror/wine`,
`b073859675060c9211fcbccfd90e4e87520dc2c2`): 39,286 bytes, sha256
`DB5E340DF3A9F3DC6CC49778CD8623E9637E76E591E2AB4F10ADE835CE9139F6`.

Offsets in that file: `D3DRENDERSTATETYPE` 677, `D3DTEXTURESTAGESTATETYPE` 860, `D3DFORMAT`
549, `D3DPOOL` 657, `D3DPRIMITIVETYPE` 666, `D3DSWAPEFFECT` 799, `D3DTRANSFORMSTATETYPE` 903,
`D3DUSAGE_*` 73, `D3DFVF_*` 42, `D3DCLEAR_*` 12.

## Gameplay column: first level, 3700 Presents

Two runs with `TJ_D3D8_ARGS=1 TJ_IO_LOG=1 TJ_REPLAY` on
`tools/fixtures/replay-menu-level1.txt`, capture at Present 3700. The first log presents 359
times from the start-up site and 3467 from the menu site; the second, 359 and 3513.

| IF | Slot | Method | Menu sets | Level sets | Level-only | args1 calls | args2 calls |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 0 | 15 | CreateDevice | 1 | 1 | 0 | 1 | 1 |
| 1 | 20 | CreateTexture | 13 | 74 | 61 | 883 | 881 |
| 1 | 23 | CreateVertexBuffer | 12 | 387 | 375 | 1222 | 1214 |
| 1 | 24 | CreateIndexBuffer | 11 | 299 | 288 | 1491 | 1483 |
| 1 | 36 | Clear | 3 | 3 | 0 | 7294 | 7388 |
| 1 | 37 | SetTransform | 3 | 11 | 8 | 1,252,624 | 1,238,567 |
| 1 | 40 | SetViewport | 1 | 1 | 0 | 2 | 2 |
| 1 | 50 | SetRenderState | 20 | 279 | 259 | 9,377,834 | 9,217,788 |
| 1 | 61 | SetTexture | 3 | 10 | 7 | 832,083 | 818,631 |
| 1 | 63 | SetTextureStageState | 44 | 123 | 79 | 8,266,476 | 8,241,812 |
| 1 | 70 | DrawPrimitive | 1 | 1 | 0 | 359 | 359 |
| 1 | 71 | DrawIndexedPrimitive | 1 | 1 | 0 | 1,049,691 | 1,062,379 |
| 1 | 72 | DrawPrimitiveUP | 1 | 1 | 0 | 24,793 | 25,409 |
| 1 | 76 | SetVertexShader | 2 | 4 | 2 | 467,240 | 466,077 |

- 1195 distinct argument sets against the menu's 116; 1079 are level-only.
- The level adds 74 texture shapes (61 new, 32x16 cubemaps to 1024x1024), 387 vertex-buffer and
  299 index-buffer shapes, with about 9.4 M `SetRenderState`, 0.83 M `SetTexture`, 1.25 M
  `SetTransform` and 1.05 M `DrawIndexedPrimitive` calls.
- Counts differ between the two runs (`CreateVertexBuffer (0xC0, 0, 0x142, 1)` 29 against 23;
  `DrawIndexedPrimitive` by 12,684), the same divergence the M3 captures show. Set membership is
  stable; counts are not.
- The level presents through 0x4DE64A; there is no third Present site.
