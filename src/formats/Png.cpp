#include "formats/Png.hpp"

// This TU owns the stb_image implementation for the whole client (one definition only). Other
// callers include third_party/stb_image.h for the declarations and link against these symbols.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "third_party/stb_image.h"

namespace uaro {

std::optional<Image> Png::decode(const std::vector<u8>& bytes) {
    if (bytes.empty()) return std::nullopt;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h,
                                              &n, 4);  // force RGBA8
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return std::nullopt;
    }
    Image img;
    img.width = static_cast<u32>(w);
    img.height = static_cast<u32>(h);
    img.rgba.assign(px, px + static_cast<usize>(w) * h * 4);
    stbi_image_free(px);
    return img;
}

}  // namespace uaro
