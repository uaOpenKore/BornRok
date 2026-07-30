#include "formats/Webp.hpp"

#if defined(CLIENT_WITH_WEBP)
#include <webp/decode.h>
#endif

#include "core/Log.hpp"

namespace uaro {

// NOTE: if this reports ABSENT after CMake found libwebp (configure prints "webp ... ENABLED"), it is
// MSBuild's incremental build skipping recompile of this TU because the .cpp timestamp didn't change
// when only the compile-DEFINE (CLIENT_WITH_WEBP) was added -- it relinks the .lib from stale .obj.
// A source edit (like this comment) bumps the timestamp and forces the recompile so the flag lands.
bool Webp::supported() {
#if defined(CLIENT_WITH_WEBP)
    return true;
#else
    return false;
#endif
}

std::optional<Image> Webp::decode(const std::vector<u8>& bytes) {
#if defined(CLIENT_WITH_WEBP)
    if (bytes.size() < 12) return std::nullopt;
    int w = 0, h = 0;
    if (!WebPGetInfo(bytes.data(), bytes.size(), &w, &h) || w <= 0 || h <= 0) {
        log::warn("Webp: bad header ({} bytes)", bytes.size());
        return std::nullopt;
    }
    // Always decode to straight (non-premultiplied) RGBA8, top-down — matches every other decoder.
    uint8_t* out = WebPDecodeRGBA(bytes.data(), bytes.size(), &w, &h);
    if (!out) {
        log::warn("Webp: decode failed ({}x{})", w, h);
        return std::nullopt;
    }
    Image img;
    img.width = static_cast<u32>(w);
    img.height = static_cast<u32>(h);
    img.rgba.assign(out, out + static_cast<usize>(w) * h * 4);
    WebPFree(out);
    return img;
#else
    (void)bytes;  // no libwebp in this build (e.g. offline core tools) -> WebP unsupported
    return std::nullopt;
#endif
}

}  // namespace uaro
