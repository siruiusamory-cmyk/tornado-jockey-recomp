# Third-party notices

This repository contains two pieces of third-party code; each keeps its own licence.

| Path | What it is | Licence | Licence text |
| :--- | :--- | :--- | :--- |
| `tools/vtable/d3d8.h` | Wine 10.0 `include/d3d8.h`, the vtable order the census and proxy derive slot indices from | LGPL-2.1-or-later | [`LICENSES/LGPL-2.1.txt`](LICENSES/LGPL-2.1.txt) |
| `port/third_party/d3d8to9/` | crosire/d3d8to9 at `255338f698c8270b537f0a91a13f795f4f988250`, the d3d8 to d3d9 translation layer | BSD 2-clause | [`port/third_party/d3d8to9/LICENSE.md`](port/third_party/d3d8to9/LICENSE.md) |

The port's own source is GPL-2.0-or-later; see [`LICENSE`](LICENSE). Vendored files are kept
byte-identical to upstream, so a local modification is a vendor change.
