#pragma once
#include <vector>

#include "core/Types.hpp"
#include "formats/Rsm.hpp"

namespace uaro {

struct ModelVertex {
    f32 x, y, z;  // node-local position
    f32 u, v;     // texture coords
    u32 color;    // vertex colour (as stored in the RSM tvertex)
    f32 nx, ny, nz;  // node-local geometric normal (accumulated from faces) — for lighting (#107)
    f32 tx, ty, tz;  // node-local tangent (from the UV gradient) — for normal mapping (#107)
};

struct ModelBatch {
    i32 textureId = -1;  // index into rsm.textures()
    u32 indexStart = 0;
    u32 indexCount = 0;
};

// Triangulated geometry for one RSM node, in node-local space.
struct ModelNodeMesh {
    u32 nodeIndex = 0;  // index into rsm.nodes() — use it for the node transform
    std::vector<ModelVertex> vertices;
    std::vector<u32> indices;
    std::vector<ModelBatch> batches;
};

// Per-node triangle meshes for an RSM, batched by model texture. Geometry only,
// in each node's local space; the node hierarchy transforms (from rsm.nodes())
// are composed by the renderer (they need GPU verification). Pairs with
// GroundMesh to complete the static map geometry.
struct ModelMesh {
    std::vector<ModelNodeMesh> nodes;

    static ModelMesh build(const Rsm& rsm);

    usize totalIndices() const {
        usize n = 0;
        for (const auto& nm : nodes) n += nm.indices.size();
        return n;
    }
};

} // namespace uaro
