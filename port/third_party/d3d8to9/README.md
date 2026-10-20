# d3d8to9, vendored (TJ-STAGE-10)

Upstream: https://github.com/crosire/d3d8to9

- **Pinned commit**: `255338f698c8270b537f0a91a13f795f4f988250` (2026-09-24,
  "Fix build with Visual Studio 2026").
- **Licence**: BSD 2-clause, `LICENSE.md` in this directory, copyright (C) 2015 Patrick
  Mours. The upstream README states the same ("BSD 2-clause license").
- **Files**: `source/` and `res/` are copied unmodified from the commit above. This
  README is the port's, and the only file added here.
- **Build**: `tools/build-d3d8to9.ps1` compiles the eleven sources with the same Visual
  Studio x86 toolchain the proxy builds with (`cl /LD /O2 /MT /EHsc /W3 /std:c++17
  /DD3D8TO9NOLOG`, `/DEF:res/d3d8.def`, `d3d9.lib user32.lib gdi32.lib shell32.lib`) into
  `build/proxy/d3d8to9.dll`. It is not renamed to `d3d8.dll`: the game still loads this
  repo's proxy from `game\d3d8.dll`, and the proxy picks the backend at load
  (`port/proxy/backend.h`).
- **Runtime dependencies**: the system `d3d9.dll`, and `d3dx9_43.dll` (DirectX end-user
  runtime) for its shader disassembly/assembly path. This machine has
  `C:\Windows\SysWOW64\d3dx9_43.dll` (DirectX end-user runtime installed), so no
  redistributable is installed by this repo and none is needed. The game creates no vertex
  or pixel shader (`port\D3D8_USAGE.md`: no `CreateVertexShader`/`CreatePixelShader` call
  site), so that path is not reached by this title; the dependency is recorded because the
  library would load it in a title that used shaders.
