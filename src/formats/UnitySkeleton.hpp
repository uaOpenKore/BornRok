#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "formats/UnitySerialized.hpp"

namespace uaro {

// Skeleton extraction from a SerializedFile: GameObject names + Transform hierarchy with
// local TRS. Bone order matches SkinnedMeshRenderer.m_Bones (PPtr pathIds), which is how
// the mesh's bone indices/bind poses line up.
struct UnityBone {
    std::string name;
    i32 parent = -1;          // index into bones, -1 = root
    float rot[4] = {0, 0, 0, 1};  // local quaternion (x,y,z,w)
    float pos[3] = {0, 0, 0};
    float scale[3] = {1, 1, 1};
    i64 transformPathId = 0;  // the Transform object's pathId (for matching m_Bones)
};

struct UnitySkeleton {
    std::vector<UnityBone> bones;  // parents come before children
};

// Builds the skeleton from all Transform+GameObject objects in the file. Returns nullopt
// when the file has no transforms.
std::optional<UnitySkeleton> buildUnitySkeleton(const UnitySerializedFile& sf);

}  // namespace uaro
