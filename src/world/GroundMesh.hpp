#pragma once
#include <vector>

#include "core/Types.hpp"
#include "formats/Gnd.hpp"

namespace uaro {

// One ground vertex, ready to upload to a bgfx vertex buffer.
struct GroundVertex {
    f32 x, y, z;       // position
    f32 nx, ny, nz;    // normal
    f32 u, v;          // texture coords
    u32 color;         // 0xAABBGGRR (bgfx Color0)
};

// A contiguous run of indices that all use one texture (index into gnd.textures()).
struct GroundBatch {
    i16 textureId = -1;
    u32 indexStart = 0;
    u32 indexCount = 0;
};

// Triangle mesh built from a GND: top tiles plus front/right wall tiles. Vertices
// are interleaved; indices are grouped into per-texture batches so the renderer
// binds each texture once. Geometry only — texture pixels come from the BMPs named
// in gnd.textures(), uploaded by the renderer (v3+).
struct GroundMesh {
    std::vector<GroundVertex> vertices;
    std::vector<u32> indices;
    std::vector<GroundBatch> batches;

    static GroundMesh build(const Gnd& gnd);

    // Build the per-pixel lightmap image for a map: a (width*lmW) x (height*lmH) RGBA
    // texture, each ground cell's lmW*lmH (usually 8x8) lightmap tile placed at its map
    // position, so the ground shader can sample it continuously (smooth shadows, no
    // per-tile banding). RGB = the cell's coloured light (torches/lamps), A = the baked
    // brightness/shadow. Cells with no lightmap get neutral (A=255, RGB=0). The ground
    // vertex shader derives the lightmap UV from the world XZ, so no vertex attribute is
    // needed. outW/outH receive the texture size (0 if the map has no lightmap data).
    static std::vector<u8> buildLightmap(const Gnd& gnd, int& outW, int& outH);
};

} // namespace uaro
