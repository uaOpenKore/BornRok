#include "formats/UnityAvatar.hpp"

#include <cstring>

namespace uaro {

// Layout verified on the real BasiliskAvatar (scratchpad/avatar_peek): aligned name ->
// u32 m_AvatarSize -> AvatarConstant: m_AvatarSkeleton (OffsetPtr inlined in the release
// data): u32 nodeCount + nodeCount x {parentId i32, axesId i32}, then m_ID: u32 count +
// count x u32 hashes. (AxesArray / poses / m_TOS follow; not needed for skinning v1.)
std::optional<UnityAvatarSkeleton> parseUnityAvatar(const std::vector<u8>& b) {
    usize at = 0;
    auto ok = [&](usize need) { return at + need <= b.size(); };
    auto u32v = [&] {
        u32 v;
        std::memcpy(&v, b.data() + at, 4);
        at += 4;
        return v;
    };
    if (!ok(4)) return std::nullopt;
    const u32 nameLen = u32v();
    if (nameLen > 4096 || !ok(nameLen)) return std::nullopt;
    UnityAvatarSkeleton sk;
    sk.name.assign(reinterpret_cast<const char*>(b.data() + at), nameLen);
    at = (at + nameLen + 3) & ~usize{3};

    if (!ok(8)) return std::nullopt;
    u32v();  // m_AvatarSize
    const u32 nodeCount = u32v();
    if (nodeCount == 0 || nodeCount > 4096 || !ok(static_cast<usize>(nodeCount) * 8))
        return std::nullopt;
    sk.parents.resize(nodeCount);
    for (u32 i = 0; i < nodeCount; ++i) {
        sk.parents[i] = static_cast<i32>(u32v());
        u32v();  // axesId (unused for skinning)
    }
    if (!ok(4)) return std::nullopt;
    const u32 idCount = u32v();
    if (idCount != nodeCount || !ok(static_cast<usize>(idCount) * 4)) return std::nullopt;
    sk.nameHashes.resize(idCount);
    for (u32 i = 0; i < idCount; ++i) sk.nameHashes[i] = u32v();

    // Tail (verified on real RoM avatars, scratchpad/avatar_tail + avatar_axes): u32 axesCount
    // (0 on every RoM model incl. players), then m_AvatarSkeletonPose and m_DefaultPose, each
    // u32 count + count x xform {t float3, q float4, s float3}. We keep m_DefaultPose — clips
    // only animate a subset of nodes and the rest must use these locals (pupa's root carries a
    // -90-degree X rotation there; identity laid it on its side).
    if (ok(4)) {
        const u32 axesCount = u32v();
        if (axesCount == 0) {
            for (int pass = 0; pass < 2 && ok(4); ++pass) {
                const u32 poseCount = u32v();
                if (poseCount != nodeCount || !ok(static_cast<usize>(poseCount) * 40)) break;
                if (pass == 1) {  // m_DefaultPose (second block)
                    sk.defaultPose.resize(poseCount);
                    for (u32 i = 0; i < poseCount; ++i)
                        std::memcpy(sk.defaultPose[i].data(), b.data() + at + i * 40, 40);
                }
                at += static_cast<usize>(poseCount) * 40;
            }
        }
    }
    return sk;
}

}  // namespace uaro
