#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

struct RsmFace {
    u16 vertIdx[3] = {0, 0, 0};
    u16 tvertIdx[3] = {0, 0, 0};
    u16 texId = 0;
    u16 padding = 0;
    i32 twoSide = 0;
    i32 smoothGroup = 0;
};

struct RsmTVertex {
    u32 color = 0xffffffff;
    f32 u = 0;
    f32 v = 0;
};

struct RsmPosKey {
    i32 frame = 0;
    f32 p[3] = {0, 0, 0};
};

struct RsmRotKey {
    i32 frame = 0;
    f32 q[4] = {0, 0, 0, 1};  // quaternion
};

// One node (mesh) in a model's hierarchy.
struct RsmNode {
    std::string name;
    std::string parentName;
    std::vector<i32> textureIndices;  // indices into the model's texture list
    f32 mat3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // 3x3 offset/orientation matrix
    f32 offset[3] = {0, 0, 0};                   // translation
    f32 pos[3] = {0, 0, 0};
    f32 rotAngle = 0;
    f32 rotAxis[3] = {0, 0, 0};
    f32 scale[3] = {1, 1, 1};
    std::vector<std::array<f32, 3>> vertices;
    std::vector<RsmTVertex> tvertices;
    std::vector<RsmFace> faces;
    std::vector<RsmPosKey> posKeys;
    std::vector<RsmRotKey> rotKeys;
};

// RSM (Resource Model): a 3D model (buildings and map objects). Targets the
// version 1.x family (1.1–1.5). Animation is keyframed per node.
class Rsm {
public:
    static std::optional<Rsm> parse(const std::vector<u8>& bytes);

    u16 version() const { return version_; }
    i32 animLength() const { return animLength_; }
    i32 shadeType() const { return shadeType_; }
    u8 alpha() const { return alpha_; }
    const std::vector<std::string>& textures() const { return textures_; }
    const std::string& mainNode() const { return mainNode_; }
    const std::vector<RsmNode>& nodes() const { return nodes_; }

private:
    u16 version_ = 0;
    i32 animLength_ = 0;
    i32 shadeType_ = 0;
    u8 alpha_ = 255;
    std::vector<std::string> textures_;
    std::string mainNode_;
    std::vector<RsmNode> nodes_;
};

} // namespace uaro
