#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// One keyframe of one STR layer. roBrowser interpolates a `type 0` source keyframe toward the
// next `type 1` morph target as `value = from + to*delta` (delta = currentFrame - from.frame),
// so a `type 1` record's fields hold per-frame SLOPES, not absolute endpoints (Str/StrEffect.js).
struct StrKeyframe {
    i32 frame = 0;     // keyframe index along the 0..maxKey timeline
    u32 type = 0;      // 0 = source keyframe, 1 = morph target (slopes)
    f32 pos[2] = {0, 0};
    f32 uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    f32 xy[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // 4 quad-corner offsets: x0..x3 in [0..3], y0..y3 in [4..7]
    f32 aniframe = 0;  // texture index (float; floored at render)
    u32 anitype = 0;   // 0 none / 1 normal / 2 stop-at-end / 3 repeat / 4 reverse
    f32 delay = 0;     // per-frame advance step for the texture index
    f32 angle = 0;     // DEGREES (file stores 1024-per-revolution; parser divides by 1024/360)
    f32 color[4] = {1, 1, 1, 1};  // RGBA 0..1 (parser divides the 0..255 file values by 255)
    u32 srcAlpha = 5;  // D3DBLEND source factor (5 = SRC_ALPHA)
    u32 destAlpha = 6;  // D3DBLEND dest factor (6 = INV_SRC_ALPHA)
};

struct StrLayer {
    std::vector<std::string> textures;  // bare filenames; live under data/texture/effect/
    std::vector<StrKeyframe> keys;
};

// STR (effect): RO's keyframed billboard-effect format, data/texture/effect/*.str. 36-byte header
// ("STRM", version 0x94, fps, maxKey frames, layer count, 16 reserved), then per-layer textures +
// keyframes. Used for level-up (angel/joblvup), warp, skill effects, sakura petals, etc.
class Str {
public:
    static std::optional<Str> parse(const std::vector<u8>& bytes);
    // Build a Str straight from prepared layers (e.g. converted from an EVFF/.ezv effect) so the
    // StrEffect renderer can play non-STR effects through the same path.
    static Str build(u32 fps, u32 maxKey, std::vector<StrLayer> layers);

    u32 fps() const { return fps_; }
    u32 maxKey() const { return maxKey_; }
    const std::vector<StrLayer>& layers() const { return layers_; }

private:
    u32 fps_ = 0;
    u32 maxKey_ = 0;
    std::vector<StrLayer> layers_;
};

}  // namespace uaro
