#pragma once
#include <array>
#include <vector>

#include "core/Types.hpp"
#include "formats/Act.hpp"
#include "formats/Spr.hpp"

namespace uaro {

// One sprite image to blit when drawing a composed actor frame, in actor-local
// space (origin = the actor anchor, +x right, +y down). The pixels come from the
// sprite the quad's `part` refers to; the consumer (CPU compositor or GPU sprite
// batch) resolves and draws them. Geometry only — no rendering here, so this is
// built and tested offline.
struct ComposedQuad {
    int part = 0;       // which sprite owns the frame (0 = body, 1 = head, ...)
    int sprIndex = -1;  // frame index within that sprite
    bool indexed = true;
    float x = 0, y = 0;  // top-left in actor-local space
    float w = 0, h = 0;  // size (already scaled)
    bool mirror = false;
    u8 r = 255, g = 255, b = 255, a = 255;
};

// Anchor[0] (the attach point, e.g. body neck / head base) of one ACT frame, or
// {0,0} if the frame has none.
std::array<i32, 2> frameAnchor(const Action& act, int action, int frame);

// Append the layers of (action, frame) as quads, offset by (ox,oy) in local
// space and tagged with `part`. Sprite frame sizes come from `spr`.
//
// Frame interpolation (S.): when frameNext >= 0 and t in (0,1], each layer's transform
// (position/scale/alpha/colour) is linearly blended toward the matching layer of `frameNext`
// (matched by index AND same sprIndex, so only a transform-only change is smoothed -- a pose
// change with a different bitmap snaps). The drawn bitmap stays the CURRENT frame's (no ghosting).
// Defaults (frameNext<0) reproduce the old snap behaviour byte-for-byte.
void composeFrame(const Action& act, const Sprite& spr, int action, int frame, float ox, float oy,
                  int part, std::vector<ComposedQuad>& out, int frameNext = -1, float t = 0.0f);

} // namespace uaro
