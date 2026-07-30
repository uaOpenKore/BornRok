#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "formats/UnityAnimClip.hpp"
#include "formats/UnityAvatar.hpp"
#include "formats/UnityMesh.hpp"
#include "formats/UnityTexture.hpp"

namespace uaro {

// A fully-assembled RoM mob model, built from ONE .unity3d bundle (they are
// self-contained: mesh + textures + avatar skeleton + animation clips; the only
// external is the shader bundle, which we ignore — we light with our own shader).
// Pure data: the render layer uploads it to bgfx.
struct RomBoneTrack {
    i32 avatarNode = -1;  // index into skeleton parents
    // Per-frame local TRS (frameCount entries each; empty channel = use skeleton default).
    std::vector<float> t;  // 3 per frame
    std::vector<float> q;  // 4 per frame
    std::vector<float> s;  // 3 per frame
};

struct RomClip {
    std::string name;  // "wait"/"walk"/"attack"/"hit"/"die"/...
    float sampleRate = 30;
    float duration = 0;
    u32 frameCount = 0;
    std::vector<RomBoneTrack> tracks;
};

// A named prefab attach point (players: CP_1..CP_11 / EP_1..EP_11 hang under the model
// root; CP_4 is the head connect point on the p1 rig — verified on novice_m, y 1.662).
struct RomAttachPoint {
    std::string name;
    float t[3] = {0, 0, 0};  // local translation under the prefab root
};

struct RomModel {
    std::string name;
    std::string wantName;   // set by the loader BEFORE appending: lowercased mob/bundle name;
                            // a mesh whose name matches it wins outright (bundles may carry
                            // variant meshes with richer rigs: poring.unity3d also ships
                            // Poring_001c1 749v/11b vs the real Poring 471v/6b)
    std::string forceTexName;  // when set (the --view dyes tab), this skin wins outright
    i64 mainTexPathId = 0;  // _MainTex PPtr from the model's Material: picks the diffuse
                            // among the family textures the _ext bundle ships (poring_ext
                            // carries the WHOLE poring family; "largest" picked Archangeling)
    UnityMesh mesh;                    // geometry + skin weights + bind poses + bone hashes
    // Accessory meshes from the same bundle (dokebi's club, weapons riding in the model):
    // small rigs skinned against the same avatar. Variant/effect meshes are filtered out at
    // load (see appendRomBundle).
    struct SubMesh {
        UnityMesh mesh;
        std::vector<i32> boneToNode;   // resolved in finalizeRomModel
    };
    std::vector<SubMesh> extras;
    UnityAvatarSkeleton skeleton;      // node parents + name hashes
    std::vector<i32> meshBoneToNode;   // mesh bone i -> skeleton node (via hash), -1 if unmatched
    std::vector<UnityTexture2D> textures;
    std::map<std::string, RomClip> clips;
    std::vector<RomAttachPoint> attachPoints;  // named prefab Transforms (see above)
};

// Assemble from raw .unity3d bytes. Returns nullopt if the bundle has no mesh/skeleton.
// Self-contained bundles (minibosses) carry everything; regular mobs split content:
// <name>.unity3d = mesh+skeleton, <name>_ext.unity3d = textures,
// art/public/animation/body/<name>/<clip>.unity3d = one clip each. Use appendRomBundle
// for the extra bundles, then finalizeRomModel once.
std::optional<RomModel> loadRomModel(const std::vector<u8>& bundleBytes);
bool appendRomBundle(RomModel& m, const std::vector<u8>& bundleBytes);  // merge objects in
void finalizeRomModel(RomModel& m);  // resolve mesh bones + clip tracks by name hash

}  // namespace uaro
