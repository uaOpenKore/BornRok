#pragma once
#include <optional>
#include <vector>

#include "core/Types.hpp"
#include "formats/Image.hpp"

namespace uaro {

// PNG (and, since it wraps stb_image, JPEG/other) decoder -> top-down RGBA8. Used for high-colour
// texture/sprite assets that a GRF editor / roBrowser ship as PNG instead of the RO BMP/TGA/SPR.
class Png {
public:
    static std::optional<Image> decode(const std::vector<u8>& bytes);
};

}  // namespace uaro
