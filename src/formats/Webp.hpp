#pragma once
#include <optional>
#include <vector>

#include "core/Types.hpp"
#include "formats/Image.hpp"

namespace uaro {

// WebP (RIFF/WEBP) decoder -> top-down RGBA8, via libwebp. Lets HD content ship as .webp (smaller
// than PNG at the same quality, alpha supported). Returns nullopt if the build has no libwebp
// (CLIENT_WITH_WEBP undefined — e.g. the offline core tools) or the data isn't a valid WebP.
class Webp {
public:
    static std::optional<Image> decode(const std::vector<u8>& bytes);
    // True iff this build compiled the libwebp decoder (CLIENT_WITH_WEBP). Logged at startup so a
    // content maker can tell from the runtime log whether .webp overrides will be honoured at all.
    static bool supported();
};

}  // namespace uaro
