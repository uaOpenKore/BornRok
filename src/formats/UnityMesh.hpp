#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Typed Unity Mesh (class 43) decode for the 2021.3 layout — the uncompressed path
// (m_MeshCompression == 0, which is what the RoM pack ships). Produces plain arrays
// ready for a bgfx vertex buffer; skinning data comes from VertexData channels 12/13.
struct UnityMesh {
    std::string name;
    u32 vertexCount = 0;
    std::vector<float> positions;      // 3 per vertex
    std::vector<float> normals;        // 3 per vertex (may be empty)
    std::vector<float> uv0;            // 2 per vertex (may be empty)
    std::vector<float> boneWeights;    // 4 per vertex (may be empty)
    std::vector<u32> boneIndices;      // 4 per vertex (may be empty)
    std::vector<u32> indices;          // triangle list
    std::vector<std::array<float, 16>> bindPoses;  // one 4x4 per bone (may be empty)
    std::vector<u32> boneNameHashes;   // per bone; match UnityAvatarSkeleton.nameHashes
};

// resRead: as in UnityTexture — resolves a StreamingInfo path into bytes when the vertex
// data lives in the bundle's .resS node.
std::optional<UnityMesh> parseUnityMesh(
    const std::vector<u8>& objectBytes,
    const std::function<std::optional<std::vector<u8>>(const std::string&, u64, u32)>& resRead);

}  // namespace uaro
