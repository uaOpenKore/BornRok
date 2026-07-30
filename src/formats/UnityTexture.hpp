#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Typed Texture2D decode (Unity 2021.3 layout) from a SerializedFile object payload.
// The pixel data either sits inline or in the bundle's .resS node — the caller passes a
// resolver for the latter. Compressed mobile formats (ASTC/ETC2) are returned AS-IS:
// bgfx uploads those blocks directly, no CPU decode needed.
struct UnityTexture2D {
    std::string name;
    i64 pathId = 0;  // object id within its serialized file (Material PPtrs point at this)
    i32 width = 0;
    i32 height = 0;
    i32 format = 0;    // Unity TextureFormat enum
    i32 mipCount = 1;
    std::vector<u8> data;  // block-compressed or raw pixels, mip 0 first

    // Unity TextureFormat values we expect from the RoM pack.
    static constexpr i32 kRGBA32 = 4, kARGB32 = 5, kRGB24 = 3, kAlpha8 = 1;
    static constexpr i32 kETC_RGB4 = 34, kETC2_RGB = 45, kETC2_RGBA1 = 46, kETC2_RGBA8 = 47;
    static constexpr i32 kASTC_4x4 = 48, kASTC_5x5 = 49, kASTC_6x6 = 50, kASTC_8x8 = 51,
                         kASTC_10x10 = 52, kASTC_12x12 = 53;
    static constexpr i32 kASTC_RGBA_4x4 = 54, kASTC_RGBA_5x5 = 55, kASTC_RGBA_6x6 = 56,
                         kASTC_RGBA_8x8 = 57, kASTC_RGBA_10x10 = 58, kASTC_RGBA_12x12 = 59;
};

// resRead(path, offset, size): bytes from the named .resS/.resource node (the path in the
// StreamingInfo may carry an "archive:/CAB-xxx/" prefix — the callee strips it).
std::optional<UnityTexture2D> parseUnityTexture2D(
    const std::vector<u8>& objectBytes,
    const std::function<std::optional<std::vector<u8>>(const std::string&, u64, u32)>& resRead);

}  // namespace uaro
