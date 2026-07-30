#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Unity SerializedFile reader (the "CAB-..." node inside a UnityFS bundle) — the object
// table layer: which objects (class IDs) live in the asset and where their raw bytes are.
// Targets format version 22 (Unity 2021.3, what RoM ships); typed decoding of the object
// payloads (Texture2D, Mesh, ...) is the layer above.
//
// Well-known class IDs (the ones we care about): 1 GameObject, 4 Transform, 21 Material,
// 23 MeshRenderer, 28 Texture2D, 33 MeshFilter, 43 Mesh, 74 AnimationClip, 83 AudioClip,
// 90 Avatar, 91 Animator, 95 AnimatorController, 114 MonoBehaviour, 137 SkinnedMeshRenderer,
// 142 AssetBundle, 213 Sprite.
struct UnityObjectInfo {
    i64 pathId = 0;    // unique object id within the file
    u64 byteStart = 0; // absolute offset of the object's data within the SerializedFile bytes
    u32 byteSize = 0;
    i32 classId = 0;   // resolved Unity class id (e.g. 28 = Texture2D)
};

class UnitySerializedFile {
public:
    bool parse(const std::vector<u8>& bytes);

    const std::string& unityVersion() const { return unityVersion_; }
    u32 formatVersion() const { return formatVersion_; }
    const std::vector<UnityObjectInfo>& objects() const { return objects_; }
    // Referenced SerializedFiles ("archive:/CAB-x/CAB-x"): PPtr fileID N>0 -> externals()[N-1].
    const std::vector<std::string>& externals() const { return externals_; }

    // Raw payload bytes of one object (by index into objects()).
    std::optional<std::vector<u8>> objectData(usize index) const;

private:
    std::string unityVersion_;
    u32 formatVersion_ = 0;
    std::vector<UnityObjectInfo> objects_;
    std::vector<std::string> externals_;
    std::vector<u8> bytes_;  // whole file kept for objectData()
};

const char* unityClassName(i32 classId);  // "Texture2D", "Mesh", ... or "Class<N>"

}  // namespace uaro
