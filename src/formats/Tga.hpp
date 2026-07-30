#pragma once
#include <optional>
#include <vector>

#include "core/Types.hpp"
#include "formats/Image.hpp"

namespace uaro {

// TARGA (TGA) decoder for the variants RO uses (status icons etc.): uncompressed
// (image type 2) and RLE (type 10) true-color, 24-bit (BGR) or 32-bit (BGRA).
// Returns top-down RGBA8; a 24-bit magenta (255,0,255) background is keyed to
// transparent (classic RO convention). Ported from S.'s reference decoder.
class Tga {
public:
    static std::optional<Image> decode(const std::vector<u8>& bytes);
};

} // namespace uaro
