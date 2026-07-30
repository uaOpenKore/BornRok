#pragma once
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Decoded image: tightly packed RGBA8, top-down (row 0 = top). The common output
// of all texture decoders (BMP now; TGA/JPEG later), ready to upload as a texture.
struct Image {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba;  // width * height * 4 bytes

    bool valid() const { return width > 0 && height > 0 && rgba.size() == static_cast<usize>(width) * height * 4; }
};

} // namespace uaro
