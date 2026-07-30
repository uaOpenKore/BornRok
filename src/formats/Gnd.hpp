#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// A textured ground quad (one face). u/v are the texture coords of its 4 corners.
struct GndSurface {
    f32 u[4] = {0, 0, 0, 0};
    f32 v[4] = {0, 0, 0, 0};
    i16 textureId = -1;
    i16 lightmapId = -1;
    u8 color[4] = {255, 255, 255, 255};  // BGRA vertex colour
};

// One ground cell: corner heights and the surface index of its top/front/right
// faces (-1 = no face).
struct GndCube {
    f32 height[4] = {0, 0, 0, 0};
    i32 tileUp = -1;
    i32 tileFront = -1;
    i32 tileRight = -1;
};

// GND (Ground): the textured ground mesh of a map — texture list, lightmaps,
// surfaces, and the per-cell cubes. Targets version 1.7+ (the modern format).
class Gnd {
public:
    static std::optional<Gnd> parse(const std::vector<u8>& bytes);

    u32 width() const { return width_; }
    u32 height() const { return height_; }
    f32 zoom() const { return zoom_; }
    i32 lightmapCount() const { return lightmapCount_; }
    // Per-tile lightmap dimensions (usually 8x8) and the raw atlas: lightmapCount
    // tiles, each lmW*lmH brightness bytes (shadow map) then lmW*lmH*3 colour bytes
    // (the coloured light from torches/lamps). Empty if the map predates lightmaps.
    i32 lightmapW() const { return lmW_; }
    i32 lightmapH() const { return lmH_; }
    const std::vector<u8>& lightmap() const { return lightmap_; }
    const std::vector<std::string>& textures() const { return textures_; }
    const std::vector<GndSurface>& surfaces() const { return surfaces_; }
    const std::vector<GndCube>& cubes() const { return cubes_; }

private:
    u32 width_ = 0;
    u32 height_ = 0;
    f32 zoom_ = 0;
    i32 lightmapCount_ = 0;
    i32 lmW_ = 0, lmH_ = 0;
    std::vector<u8> lightmap_;  // raw lightmap atlas (count * lmW*lmH*4 bytes)
    std::vector<std::string> textures_;
    std::vector<GndSurface> surfaces_;
    std::vector<GndCube> cubes_;
};

} // namespace uaro
