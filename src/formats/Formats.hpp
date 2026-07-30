#pragma once
// STUB — implemented in v2 (asset formats).
//
// Parsers for the RO asset formats, all reading from core::ByteReader over Vfs
// blobs. Targeted for v2:
//   - SPR / ACT     : sprites and animation/action data
//   - GND / GAT / RSW: ground mesh, walkability, world/map definition
//   - RSM           : 3D models (.rsm / .rsm2)
//   - PAL           : palettes (cf. Palettes.grf)
//   - BMP / TGA     : textures, plus modern large-texture formats (goal 3.4)
//
// Each format gets its own header/translation unit under formats/. This file is
// the umbrella include and a placeholder for shared format enums.
#include "core/Types.hpp"

namespace uaro::formats {

enum class Kind { Unknown, Spr, Act, Gnd, Gat, Rsw, Rsm, Pal, Bmp, Tga };

} // namespace uaro::formats
