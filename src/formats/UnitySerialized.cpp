#include "formats/UnitySerialized.hpp"

#include <cstring>

#include "core/Log.hpp"

namespace uaro {

namespace {

// Mixed-endian reader: the SerializedFile header is big-endian; the metadata that follows is
// little-endian when the header's endianess byte is 0 (the common case, incl. RoM).
struct Reader {
    const u8* p;
    usize n;
    usize at = 0;

    bool ok(usize need) const { return at + need <= n; }
    u8 u8v() { return p[at++]; }
    u32 u32be() {
        const u32 v = (static_cast<u32>(p[at]) << 24) | (static_cast<u32>(p[at + 1]) << 16) |
                      (static_cast<u32>(p[at + 2]) << 8) | p[at + 3];
        at += 4;
        return v;
    }
    u64 u64be() {
        u64 v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | p[at + i];
        at += 8;
        return v;
    }
    u32 u32le() {
        const u32 v = p[at] | (static_cast<u32>(p[at + 1]) << 8) |
                      (static_cast<u32>(p[at + 2]) << 16) | (static_cast<u32>(p[at + 3]) << 24);
        at += 4;
        return v;
    }
    u16 u16le() {
        const u16 v = static_cast<u16>(p[at] | (p[at + 1] << 8));
        at += 2;
        return v;
    }
    i64 i64le() {
        u64 v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | p[at + i];
        at += 8;
        return static_cast<i64>(v);
    }
    std::string cstr() {
        std::string s;
        while (at < n && p[at] != 0) s.push_back(static_cast<char>(p[at++]));
        if (at < n) ++at;
        return s;
    }
    void align4() { at = (at + 3) & ~usize{3}; }
};

}  // namespace

bool UnitySerializedFile::parse(const std::vector<u8>& bytes) {
    objects_.clear();
    bytes_ = bytes;
    Reader r{bytes.data(), bytes.size()};
    if (!r.ok(20)) return false;
    r.u32be();  // legacy metadataSize
    r.u32be();  // legacy fileSize
    formatVersion_ = r.u32be();
    r.u32be();  // legacy dataOffset
    if (formatVersion_ < 17 || formatVersion_ > 22) {
        log::warn("UnitySerialized: unsupported format version {}", formatVersion_);
        return false;
    }
    const u8 endianess = r.u8v();
    r.at += 3;  // reserved
    if (formatVersion_ >= 22) {
        if (!r.ok(28)) return false;
        r.u32be();  // metadataSize
        r.u64be();  // fileSize
        r.u64be();  // dataOffset
        r.u64be();  // unknown
    }
    if (endianess != 0) {
        log::warn("UnitySerialized: big-endian data not supported");
        return false;
    }

    unityVersion_ = r.cstr();
    if (!r.ok(5)) return false;
    r.u32le();  // target platform
    const bool typeTree = r.u8v() != 0;

    // Types: per entry classId + stripped + scriptTypeIndex + hashes (+ the type tree blob,
    // which we skip via its node/string sizes).
    if (!r.ok(4)) return false;
    const u32 typeCount = r.u32le();
    std::vector<i32> typeClass(typeCount);
    for (u32 i = 0; i < typeCount; ++i) {
        if (!r.ok(4 + 1 + 2)) return false;
        const i32 classId = static_cast<i32>(r.u32le());
        typeClass[i] = classId;
        r.u8v();                                     // isStripped
        const i16 scriptIdx = static_cast<i16>(r.u16le());
        if (classId == 114 || scriptIdx >= 0) {      // MonoBehaviour: script hash
            if (!r.ok(16)) return false;
            r.at += 16;
        }
        if (!r.ok(16)) return false;
        r.at += 16;  // old type hash
        if (typeTree) {
            if (!r.ok(8)) return false;
            const u32 nodeCount = r.u32le();
            const u32 stringSize = r.u32le();
            const usize blob = static_cast<usize>(nodeCount) * 32 + stringSize;  // v19+: 32 B/node
            if (!r.ok(blob)) return false;
            r.at += blob;
            if (formatVersion_ >= 21) {  // type dependencies
                if (!r.ok(4)) return false;
                const u32 deps = r.u32le();
                if (!r.ok(static_cast<usize>(deps) * 4)) return false;
                r.at += static_cast<usize>(deps) * 4;
            }
        }
    }

    // Object table.
    if (!r.ok(4)) return false;
    const u32 objCount = r.u32le();
    objects_.reserve(objCount);
    for (u32 i = 0; i < objCount; ++i) {
        r.align4();
        if (!r.ok(8 + 8 + 4 + 4)) return false;
        UnityObjectInfo o;
        o.pathId = r.i64le();
        o.byteStart = static_cast<u64>(r.i64le());  // v22: i64 start (relative to dataOffset)
        o.byteSize = r.u32le();
        const u32 typeIdx = r.u32le();
        o.classId = typeIdx < typeCount ? typeClass[typeIdx] : -1;
        objects_.push_back(o);
    }
    // Script types (count of {fileIndex i32, pathId i64 4-aligned}) then EXTERNALS — the
    // other SerializedFiles this one references; a PPtr with fileID > 0 points into
    // externals_[fileID-1]. Needed to pull a mob's full rig from the shared bundle.
    if (r.ok(4)) {
        const u32 scriptTypes = r.u32le();
        bool ok2 = true;
        for (u32 i = 0; i < scriptTypes && ok2; ++i) {
            if (!r.ok(4)) { ok2 = false; break; }
            r.u32le();
            r.align4();
            if (!r.ok(8)) { ok2 = false; break; }
            r.i64le();
        }
        if (ok2 && r.ok(4)) {
            const u32 ext = r.u32le();
            for (u32 i = 0; i < ext; ++i) {
                r.cstr();            // tempEmpty
                if (!r.ok(20)) break;
                r.at += 16;          // guid
                r.u32le();           // type
                externals_.push_back(r.cstr());  // e.g. "archive:/CAB-xxx/CAB-xxx"
            }
        }
    }

    // Resolve byteStart to absolute: v22 keeps dataOffset in the extended header; re-read it.
    if (formatVersion_ >= 22) {
        Reader h{bytes.data(), bytes.size()};
        h.at = 20 + 8;  // legacy header + endianess/reserved + metadataSize
        const u64 dataOffset = [&] {
            Reader hh{bytes.data(), bytes.size()};
            hh.at = 16 + 4;      // legacy 16 + endianess+reserved
            hh.u32be();          // metadataSize
            hh.u64be();          // fileSize
            return hh.u64be();   // dataOffset
        }();
        for (UnityObjectInfo& o : objects_) o.byteStart += dataOffset;
    }
    return true;
}

std::optional<std::vector<u8>> UnitySerializedFile::objectData(usize index) const {
    if (index >= objects_.size()) return std::nullopt;
    const UnityObjectInfo& o = objects_[index];
    if (o.byteStart + o.byteSize > bytes_.size()) return std::nullopt;
    return std::vector<u8>(bytes_.begin() + static_cast<std::ptrdiff_t>(o.byteStart),
                           bytes_.begin() + static_cast<std::ptrdiff_t>(o.byteStart + o.byteSize));
}

const char* unityClassName(i32 classId) {
    switch (classId) {
        case 1: return "GameObject";
        case 4: return "Transform";
        case 21: return "Material";
        case 23: return "MeshRenderer";
        case 28: return "Texture2D";
        case 33: return "MeshFilter";
        case 43: return "Mesh";
        case 48: return "Shader";
        case 74: return "AnimationClip";
        case 83: return "AudioClip";
        case 90: return "Avatar";
        case 91: return "Animator";
        case 95: return "AnimatorController";
        case 114: return "MonoBehaviour";
        case 115: return "MonoScript";
        case 128: return "Font";
        case 137: return "SkinnedMeshRenderer";
        case 142: return "AssetBundle";
        case 213: return "Sprite";
        default: return "Class";
    }
}

}  // namespace uaro
