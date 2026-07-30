#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "formats/Str.hpp"

namespace uaro {

// One keyframe of one EVFF layer. EVFF stores ABSOLUTE values per key (unlike STR's source/slope
// pairs); the renderer interpolates linearly between consecutive keys. Screen-space authoring: pos
// is relative to a 640x480 canvas whose centre (320,240) is the effect origin.
struct EvffKeyframe {
    i32 frame = 0;      // keyframe position along the 0..maxKey timeline
    f32 aniframe = 0;   // starting texture index (float; floored at render)
    u32 anitype = 0;    // texture-advance mode
    f32 delay = 0;      // per-frame texture-index step
    f32 pos[2] = {0, 0};     // screen pos (320,240 = centre = effect origin)
    f32 uv[2] = {0, 0};      // texture uv offset
    f32 uvs[2] = {1, 1};     // texture uv scale
    f32 uv2[2] = {0, 0};
    f32 uvs2[2] = {1, 1};
    f32 scale[2] = {1, 1};   // billboard scale (informational; points already carry the sized quad)
    f32 angle[3] = {0, 0, 0};  // rotation x,y,z (z stored 1024-per-revolution)
    f32 color[4] = {1, 1, 1, 1};  // RGBA 0..1 (file 0..255 divided by 255)
    u32 srcBlend = 5;   // D3DBLEND source factor
    u32 dstBlend = 7;   // D3DBLEND dest factor
    f32 points[8] = {0, 0, 0, 0, 0, 0, 0, 0};   // 4 quad corners (x,y) x4, axis-aligned
    f32 rpoints[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // 4 quad corners pre-rotated by the file
};

struct EvffLayer {
    std::string texture;  // bare bmp filename; lives under data/texture/effect/
    i32 type = 0;         // layer draw type (0 = textured quad; other = container/coded)
    std::vector<EvffKeyframe> keys;
};

// EVFF (.ezv): RO's text keyframed billboard-effect format, data/texture/effect/*.ezv. Header line
// "EVFF0.<ver>", then `fps=`, `maxkey=`, `layernum=`, then per-layer `layer:<name> { ... }` blocks
// each with `texname=` and N `{ frame= ... }` keyframe blocks. Newer/renewal skill+craft effects
// (hammerfall, cross, guardian, rune craft, ...) that STR doesn't cover.
class Evff {
public:
    static std::optional<Evff> parse(const std::vector<u8>& bytes);

    u32 fps() const { return fps_; }
    u32 maxKey() const { return maxKey_; }
    const std::vector<EvffLayer>& layers() const { return layers_; }

    // Convert to a Str so the StrEffect renderer plays it: each EVFF absolute keyframe becomes a
    // STR source(type 0) + slope(type 1) pair at the same frame, reproducing EVFF's linear
    // interpolation. EVFF's 640x480 canvas centre (320,240) is shifted to STR's 320-centre.
    Str toStr() const;

private:
    u32 fps_ = 0;
    u32 maxKey_ = 0;
    std::vector<EvffLayer> layers_;
};

}  // namespace uaro
