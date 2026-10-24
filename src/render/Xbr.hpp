#pragma once
#include <cstdint>
#include <vector>

namespace uaro::xbr {

// xBR 4x pixel-art upscaler (#xbr), level 2 (Hyllian's scaler), ported from kwebmn/ragnarok-graphics
// (src/core/xbr.cpp). Each pixel's corners follow the edges through them, so diagonals and 1:2 slopes
// come out smooth while right angles, 1px strokes and letter/box corners stay sharp (where a neural
// upscaler would melt them). Input `argb` is w*h pixels, 0xAARRGGBB, straight alpha; edges are clamped
// (the edge pixels repeat). Returns a 4w x 4h buffer, same format. Empty input -> empty result.
std::vector<uint32_t> scale4(const uint32_t* argb, int w, int h);

}  // namespace uaro::xbr
