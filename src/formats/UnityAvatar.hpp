#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Unity Avatar (class 90) — the OPTIMIZED-rig skeleton (RoM models strip bone GameObjects;
// the full hierarchy lives here). v1 extracts what skinning needs: the node tree (parent
// indices) and the per-node name hashes (m_ID) — the mesh's m_BoneNameHashes match these,
// which is how mesh bone i maps onto avatar node j. Bone name strings (m_TOS) and the
// default pose come later with the animation layer.
struct UnityAvatarSkeleton {
    std::string name;                 // e.g. "BasiliskAvatar"
    std::vector<i32> parents;         // per node, -1 = root
    std::vector<u32> nameHashes;      // per node (m_ID), crc32 of the transform path
    // m_DefaultPose: per-node local xform {t xyz, q xyzw, s xyz} (10 floats). Nodes a clip
    // does not animate MUST fall back to this — identity loses e.g. the pupa root's -90° X
    // rotation and lays the model on its side. Empty if the avatar had axes (unseen in RoM).
    std::vector<std::array<float, 10>> defaultPose;
};

std::optional<UnityAvatarSkeleton> parseUnityAvatar(const std::vector<u8>& objectBytes);

}  // namespace uaro
