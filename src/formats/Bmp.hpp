#pragma once
#include <optional>
#include <vector>

#include "core/Types.hpp"
#include "formats/Image.hpp"

namespace uaro {

// Windows BMP decoder for the variants RO uses for textures: 8-bit indexed
// (palette) and 24/32-bit, uncompressed (BI_RGB). Returns top-down RGBA8.
class Bmp {
public:
    static std::optional<Image> decode(const std::vector<u8>& bytes);
};

} // namespace uaro
