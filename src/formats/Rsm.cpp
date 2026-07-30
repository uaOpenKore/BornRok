#include "formats/Rsm.hpp"

#include <cstring>
#include <stdexcept>

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"

namespace uaro {

namespace {
constexpr u32 kMaxGeom = 5'000'000;   // verts/tverts/faces per node
constexpr u32 kMaxNodes = 100'000;
constexpr u32 kMaxTex = 4096;
constexpr u32 kMaxKeys = 1'000'000;

bool overCap(u32 n, u32 cap) { return n > cap; }

// RSM string: fixed 40-byte NUL-terminated up to v2.1; length-prefixed (u32 len + bytes) from
// v2.2 (RSM2). korangar ragnarok-formats/model.rs ModelString<40>.
std::string readRsmString(ByteReader& r, u16 version, usize fixedLen = 40) {
    if (version >= 0x202) {
        const u32 len = r.u32le();
        if (len > 1'000'000u) throw std::out_of_range("RSM2 string too long");
        std::string s(len, '\0');
        for (u32 i = 0; i < len; ++i) s[i] = static_cast<char>(r.u8v());
        if (auto z = s.find('\0'); z != std::string::npos) s.resize(z);
        return s;
    }
    return r.read_cstring(fixedLen);
}

// One node. `version` selects the classic (<=2.1) or RSM2 (>=2.2 / >=2.3) layout. For RSM2.3 the
// per-node texture NAMES are appended to the model's global texture list and the node's texIds are
// remapped into it, so the mesh builder (texId -> node.textureIndices -> model.textures_) is unchanged.
bool parseNode(ByteReader& r, u16 version, RsmNode& n, std::vector<std::string>& modelTextures) {
    n.name = readRsmString(r, version);
    n.parentName = readRsmString(r, version);

    if (version >= 0x203) {  // RSM2.3: per-node texture NAMES -> fold into the global list
        const u32 texNameCount = r.u32le();
        if (overCap(texNameCount, kMaxTex)) return false;
        n.textureIndices.resize(texNameCount);
        for (auto& t : n.textureIndices) {
            const std::string tn = readRsmString(r, version);
            t = static_cast<i32>(modelTextures.size());
            modelTextures.push_back(tn);
        }
    } else {                 // classic + RSM2.2: indices into the global texture list
        const u32 texCount = r.u32le();
        if (overCap(texCount, kMaxTex)) return false;
        n.textureIndices.resize(texCount);
        for (auto& t : n.textureIndices) t = r.i32le();
    }

    for (int i = 0; i < 9; ++i) n.mat3[i] = r.f32le();       // offset matrix (3x3)
    if (version < 0x202) {                                    // classic: translation1 (offset)
        for (int i = 0; i < 3; ++i) n.offset[i] = r.f32le();
    } else {
        n.offset[0] = n.offset[1] = n.offset[2] = 0.0f;
    }
    for (int i = 0; i < 3; ++i) n.pos[i] = r.f32le();        // translation2 (position)
    if (version < 0x202) {                                    // classic: rot angle/axis + scale
        n.rotAngle = r.f32le();
        for (int i = 0; i < 3; ++i) n.rotAxis[i] = r.f32le();
        for (int i = 0; i < 3; ++i) n.scale[i] = r.f32le();
    } else {                                                  // RSM2: driven by the offset matrix + keyframes
        n.rotAngle = 0.0f;
        n.rotAxis[0] = n.rotAxis[1] = 0.0f; n.rotAxis[2] = 1.0f;
        n.scale[0] = n.scale[1] = n.scale[2] = 1.0f;
    }

    const u32 vertCount = r.u32le();
    if (overCap(vertCount, kMaxGeom)) return false;
    n.vertices.resize(vertCount);
    for (auto& v : n.vertices) {
        v[0] = r.f32le();
        v[1] = r.f32le();
        v[2] = r.f32le();
    }

    const u32 tvertCount = r.u32le();
    if (overCap(tvertCount, kMaxGeom)) return false;
    n.tvertices.resize(tvertCount);
    for (auto& tv : n.tvertices) {
        if (version >= 0x102) tv.color = r.u32le();
        tv.u = r.f32le();
        tv.v = r.f32le();
    }

    const u32 faceCount = r.u32le();
    if (overCap(faceCount, kMaxGeom)) return false;
    n.faces.resize(faceCount);
    for (auto& f : n.faces) {
        // RSM2.2+ prefixes each face with a byte length; the fields below are 24 bytes, any
        // surplus (extra smooth groups) is skipped. korangar FaceData.
        u32 faceLen = 0;
        if (version >= 0x202) faceLen = r.u32le();
        for (int i = 0; i < 3; ++i) f.vertIdx[i] = r.read<u16>();
        for (int i = 0; i < 3; ++i) f.tvertIdx[i] = r.read<u16>();
        f.texId = r.read<u16>();
        f.padding = r.read<u16>();
        f.twoSide = r.i32le();
        if (version >= 0x102) f.smoothGroup = r.i32le();
        if (version >= 0x202)
            for (u32 s = 24; s + 4 <= faceLen; s += 4) (void)r.i32le();  // extra smooth groups
    }

    // Scale key frames: classic reads position keys at >=1.5; RSM2 reads scale keys at >=1.6
    // (frame + vec3 + reserved). We advance past them (no scale-anim field to store).
    if (version >= 0x200) {
        if (version >= 0x106) {
            const u32 scaleCount = r.u32le();
            if (overCap(scaleCount, kMaxKeys)) return false;
            for (u32 i = 0; i < scaleCount; ++i) { (void)r.i32le(); r.f32le(); r.f32le(); r.f32le(); r.f32le(); }
        }
    } else if (version >= 0x105) {
        const u32 posCount = r.u32le();
        if (overCap(posCount, kMaxKeys)) return false;
        n.posKeys.resize(posCount);
        for (auto& k : n.posKeys) {
            k.frame = r.i32le();
            k.p[0] = r.f32le();
            k.p[1] = r.f32le();
            k.p[2] = r.f32le();
        }
    }
    const u32 rotCount = r.u32le();
    if (overCap(rotCount, kMaxKeys)) return false;
    n.rotKeys.resize(rotCount);
    for (auto& k : n.rotKeys) {
        k.frame = r.i32le();
        for (int i = 0; i < 4; ++i) k.q[i] = r.f32le();
    }
    if (version >= 0x202) {  // RSM2: translation key frames (frame + vec3 + reserved)
        const u32 transCount = r.u32le();
        if (overCap(transCount, kMaxKeys)) return false;
        n.posKeys.resize(transCount);
        for (auto& k : n.posKeys) {
            k.frame = r.i32le();
            k.p[0] = r.f32le();
            k.p[1] = r.f32le();
            k.p[2] = r.f32le();
            r.f32le();  // reserved
        }
    }
    if (version >= 0x203) {  // RSM2.3: texture animation key frames -> advance past
        const u32 texKfCount = r.u32le();
        if (overCap(texKfCount, kMaxKeys)) return false;
        for (u32 i = 0; i < texKfCount; ++i) {
            (void)r.u32le();                   // texture index
            const u32 opCount = r.u32le();     // operation-type entries
            if (overCap(opCount, kMaxKeys)) return false;
            for (u32 o = 0; o < opCount; ++o) {
                (void)r.u32le();               // operation type
                const u32 frameCount = r.u32le();
                if (overCap(frameCount, kMaxKeys)) return false;
                for (u32 fr = 0; fr < frameCount; ++fr) { (void)r.i32le(); r.f32le(); }
            }
        }
    }
    return true;
}
} // namespace

std::optional<Rsm> Rsm::parse(const std::vector<u8>& bytes) {
    if (bytes.size() < 32) {
        log::error("RSM: too small");
        return std::nullopt;
    }
    try {
        ByteReader r(bytes);
        char magic[4];
        r.read_bytes(magic, 4);
        if (std::memcmp(magic, "GRSM", 4) != 0) {
            log::error("RSM: bad signature");
            return std::nullopt;
        }
        const u8 major = r.u8v();
        const u8 minor = r.u8v();

        Rsm m;
        m.version_ = static_cast<u16>(major) * 0x100 + minor;
        m.animLength_ = r.i32le();
        m.shadeType_ = r.i32le();
        if (m.version_ >= 0x104) m.alpha_ = r.u8v();
        if (m.version_ < 0x202) r.skip(16);  // reserved (classic)
        else r.f32le();                      // frames_per_second (RSM2 >= 2.2)

        if (m.version_ < 0x203) {            // global texture list (folded per-node from RSM2.3)
            const u32 texCount = r.u32le();
            if (overCap(texCount, kMaxTex)) {
                log::error("RSM: implausible texture count {}", texCount);
                return std::nullopt;
            }
            m.textures_.reserve(texCount);
            for (u32 i = 0; i < texCount; ++i) m.textures_.push_back(readRsmString(r, m.version_));
        }

        if (m.version_ < 0x202) {            // classic: one root node name
            m.mainNode_ = readRsmString(r, m.version_);
        } else {                             // RSM2: a list of root node names
            const u32 rootCount = r.u32le();
            if (overCap(rootCount, kMaxNodes)) {
                log::error("RSM: implausible root-node count {}", rootCount);
                return std::nullopt;
            }
            for (u32 i = 0; i < rootCount; ++i) {
                std::string rn = readRsmString(r, m.version_);
                if (i == 0) m.mainNode_ = rn;
            }
        }

        const u32 nodeCount = r.u32le();
        if (overCap(nodeCount, kMaxNodes)) {
            log::error("RSM: implausible node count {}", nodeCount);
            return std::nullopt;
        }
        m.nodes_.resize(nodeCount);
        for (auto& n : m.nodes_) {
            if (!parseNode(r, m.version_, n, m.textures_)) {
                log::error("RSM: implausible node data (version {:#x})", m.version_);
                return std::nullopt;
            }
        }
        return m;
    } catch (const std::out_of_range&) {
        log::error("RSM: truncated/corrupt file");
        return std::nullopt;
    }
}

} // namespace uaro
