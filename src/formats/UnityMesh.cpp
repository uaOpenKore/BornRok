#include "formats/UnityMesh.hpp"

#include <cmath>
#include <cstring>

#include "core/Log.hpp"

namespace uaro {

namespace {

struct LeReader {
    const u8* p;
    usize n;
    usize at = 0;

    bool ok(usize need) const { return at + need <= n; }
    u8 u8v() { return p[at++]; }
    u32 u32v() {
        const u32 v = p[at] | (static_cast<u32>(p[at + 1]) << 8) |
                      (static_cast<u32>(p[at + 2]) << 16) | (static_cast<u32>(p[at + 3]) << 24);
        at += 4;
        return v;
    }
    i32 i32v() { return static_cast<i32>(u32v()); }
    u64 u64v() {
        u64 v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | p[at + i];
        at += 8;
        return v;
    }
    float f32v() {
        float f;
        const u32 v = u32v();
        std::memcpy(&f, &v, 4);
        return f;
    }
    void align4() { at = (at + 3) & ~usize{3}; }
    bool skip(usize k) {
        if (!ok(k)) return false;
        at += k;
        return true;
    }
    std::optional<std::string> alignedString() {
        if (!ok(4)) return std::nullopt;
        const u32 len = u32v();
        if (len > 4096 || !ok(len)) return std::nullopt;
        std::string s(reinterpret_cast<const char*>(p + at), len);
        at += len;
        align4();
        return s;
    }
};

float halfToFloat(u16 h) {
    const u32 sign = (h >> 15) & 1, exp = (h >> 10) & 0x1f, man = h & 0x3ff;
    u32 bits;
    if (exp == 0)
        bits = (sign << 31);  // flush subnormals (fine for mesh data)
    else if (exp == 31)
        bits = (sign << 31) | 0x7f800000 | (man << 13);
    else
        bits = (sign << 31) | ((exp - 15 + 127) << 23) | (man << 13);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// Bytes per component of a Unity VertexFormat (2019+): 0 f32, 1 f16, 2 unorm8, 3 snorm8,
// 4 unorm16, 5 snorm16, 6 u8, 7 s8, 8 u16, 9 s16, 10 u32, 11 s32.
usize formatSize(u8 f) {
    switch (f) {
        case 0: return 4;
        case 1: return 2;
        case 2: case 3: case 6: case 7: return 1;
        case 4: case 5: case 8: case 9: return 2;
        case 10: case 11: return 4;
        default: return 0;
    }
}

float readComponent(const u8* p, u8 fmt) {
    switch (fmt) {
        case 0: {
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        case 1: {
            u16 h;
            std::memcpy(&h, p, 2);
            return halfToFloat(h);
        }
        case 2: return p[0] / 255.0f;
        case 3: return static_cast<i8>(p[0]) / 127.0f;
        case 4: {
            u16 v;
            std::memcpy(&v, p, 2);
            return v / 65535.0f;
        }
        case 6: return p[0];
        case 8: {
            u16 v;
            std::memcpy(&v, p, 2);
            return v;
        }
        case 10: {
            u32 v;
            std::memcpy(&v, p, 4);
            return static_cast<float>(v);
        }
        default: return 0.0f;
    }
}

}  // namespace

std::optional<UnityMesh> parseUnityMesh(
    const std::vector<u8>& objectBytes,
    const std::function<std::optional<std::vector<u8>>(const std::string&, u64, u32)>& resRead) {
    LeReader r{objectBytes.data(), objectBytes.size()};
    UnityMesh m;
    auto name = r.alignedString();
    if (!name) return std::nullopt;
    m.name = *name;

    // SubMeshes (48 B each: 6 u32 + AABB 6 f32).
    if (!r.ok(4)) return std::nullopt;
    const u32 subCount = r.u32v();
    struct Sub {
        u32 firstByte, indexCount;
    };
    std::vector<Sub> subs(subCount);
    for (Sub& s : subs) {
        if (!r.ok(48)) return std::nullopt;
        s.firstByte = r.u32v();
        s.indexCount = r.u32v();
        r.skip(48 - 8);
    }

    // BlendShapeData: vertices (40 B) + shapes (12 B) + channels (str + 12) + fullWeights.
    if (!r.ok(4)) return std::nullopt;
    if (!r.skip(static_cast<usize>(r.u32v()) * 40)) return std::nullopt;
    if (!r.ok(4)) return std::nullopt;
    const u32 shapeCount = r.u32v();
    for (u32 i = 0; i < shapeCount; ++i) {
        if (!r.skip(8 + 2)) return std::nullopt;  // firstVertex, vertexCount, 2 bools
        r.align4();
    }
    if (!r.ok(4)) return std::nullopt;
    const u32 chanCount = r.u32v();
    for (u32 i = 0; i < chanCount; ++i) {
        if (!r.alignedString()) return std::nullopt;
        if (!r.skip(12)) return std::nullopt;  // nameHash, frameIndex, frameCount
    }
    if (!r.ok(4)) return std::nullopt;
    if (!r.skip(static_cast<usize>(r.u32v()) * 4)) return std::nullopt;  // fullWeights

    // BindPoses, bone hashes, root hash, bones AABB, variable bone counts.
    if (!r.ok(4)) return std::nullopt;
    const u32 bindCount = r.u32v();
    m.bindPoses.resize(bindCount);
    for (auto& bp : m.bindPoses) {
        if (!r.ok(64)) return std::nullopt;
        // Unity serializes Matrix4x4 row-by-row; our Mat4 is column-major. Transpose on read
        // or the translation lands in the 4th ROW and reads back as zero (verified on the real
        // poring: world(defaultPose) exactly equals transpose(inv(bindPose)) per bone).
        std::array<float, 16> raw;
        for (float& f : raw) f = r.f32v();
        for (int rw = 0; rw < 4; ++rw)
            for (int c = 0; c < 4; ++c) bp[static_cast<usize>(c) * 4 + rw] = raw[rw * 4 + c];
    }
    if (!r.ok(4)) return std::nullopt;
    {
        const u32 nh = r.u32v();  // m_BoneNameHashes: the mesh-bone -> avatar-node key
        if (!r.ok(static_cast<usize>(nh) * 4)) return std::nullopt;
        m.boneNameHashes.resize(nh);
        for (u32 i = 0; i < nh; ++i) m.boneNameHashes[i] = r.u32v();
    }
    if (!r.skip(4)) return std::nullopt;                                 // root bone hash
    if (!r.ok(4)) return std::nullopt;
    if (!r.skip(static_cast<usize>(r.u32v()) * 24)) return std::nullopt;  // bones AABB
    if (!r.ok(4)) return std::nullopt;
    if (!r.skip(static_cast<usize>(r.u32v()) * 4)) return std::nullopt;  // variable bone weights

    if (!r.ok(4 + 4)) return std::nullopt;
    const u8 meshCompression = r.u8v();
    r.skip(3);  // isReadable/keepVertices/keepIndices
    r.align4();
    if (meshCompression != 0) {
        log::warn("UnityMesh '{}': compressed mesh not supported", m.name);
        return std::nullopt;
    }
    const i32 indexFormat = r.i32v();  // 0 = u16, 1 = u32

    // Index buffer.
    if (!r.ok(4)) return std::nullopt;
    const u32 ibSize = r.u32v();
    if (!r.ok(ibSize)) return std::nullopt;
    const u8* ib = r.p + r.at;
    r.skip(ibSize);
    r.align4();

    // VertexData.
    if (!r.ok(4 + 4)) return std::nullopt;
    m.vertexCount = r.u32v();
    const u32 nChan = r.u32v();
    struct Chan {
        u8 stream, offset, format, dim;
    };
    std::vector<Chan> chans(nChan);
    for (Chan& c : chans) {
        if (!r.ok(4)) return std::nullopt;
        c.stream = r.u8v();
        c.offset = r.u8v();
        c.format = r.u8v();
        c.dim = r.u8v() & 0x0f;
    }
    if (!r.ok(4)) return std::nullopt;
    const u32 vdSize = r.u32v();
    std::vector<u8> vdata;
    if (vdSize > 0) {
        if (!r.ok(vdSize)) return std::nullopt;
        vdata.assign(r.p + r.at, r.p + r.at + vdSize);
        r.skip(vdSize);
    }
    r.align4();

    // Trailing fields we only need StreamData from: CompressedMesh is absent when
    // meshCompression == 0? No — it is ALWAYS serialized; with compression 0 all its 12
    // packed vectors are empty. Each PackedBitVector: u32 numItems + float range/start (only
    // for float vectors) + byte array + align + bitSize u8 + align.
    auto skipPackedFloats = [&]() -> bool {  // m_NumItems, m_Range, m_Start, m_Data, m_BitSize
        if (!r.ok(12)) return false;
        r.skip(12);
        if (!r.ok(4)) return false;
        const u32 bytes = r.u32v();
        if (!r.skip(bytes)) return false;
        r.align4();
        if (!r.ok(1)) return false;
        r.u8v();
        r.align4();
        return true;
    };
    auto skipPackedInts = [&]() -> bool {  // m_NumItems, m_Data, m_BitSize
        if (!r.ok(4)) return false;
        r.skip(4);
        if (!r.ok(4)) return false;
        const u32 bytes = r.u32v();
        if (!r.skip(bytes)) return false;
        r.align4();
        if (!r.ok(1)) return false;
        r.u8v();
        r.align4();
        return true;
    };
    // CompressedMesh: Vertices(f) UV(f) Normals(f) Tangents(f) Weights(i) NormalSigns(i)
    // TangentSigns(i) FloatColors(f) BoneIndices(i) Triangles(i) UVInfo u32.
    if (!skipPackedFloats() || !skipPackedFloats() || !skipPackedFloats() ||
        !skipPackedFloats() || !skipPackedInts() || !skipPackedInts() || !skipPackedInts() ||
        !skipPackedFloats() || !skipPackedInts() || !skipPackedInts())
        return std::nullopt;
    if (!r.skip(4)) return std::nullopt;  // UVInfo

    if (!r.skip(24)) return std::nullopt;  // m_LocalAABB
    if (!r.skip(4)) return std::nullopt;   // m_MeshUsageFlags
    // m_BakedConvexCollisionMesh + m_BakedTriangleCollisionMesh (byte arrays).
    for (int i = 0; i < 2; ++i) {
        if (!r.ok(4)) return std::nullopt;
        const u32 sz = r.u32v();
        if (!r.skip(sz)) return std::nullopt;
        r.align4();
    }
    if (!r.skip(8)) return std::nullopt;  // m_MeshMetrics[2]
    r.align4();
    // m_StreamData: the vertex bytes live in the .resS when vdSize == 0.
    if (r.ok(16)) {
        const u64 off = r.u64v();
        const u32 size = r.u32v();
        auto path = r.alignedString();
        if (path && !path->empty() && size > 0 && vdata.empty()) {
            std::string p = *path;
            if (const auto slash = p.rfind('/'); slash != std::string::npos)
                p = p.substr(slash + 1);
            auto bytes = resRead(p, off, size);
            if (!bytes) {
                log::warn("UnityMesh '{}': .resS read failed", m.name);
                return std::nullopt;
            }
            vdata = std::move(*bytes);
        }
    }

    // Decode indices (concatenate all submeshes — they share the vertex buffer).
    for (const Sub& s : subs) {
        if (indexFormat == 0) {
            const usize base = s.firstByte;
            for (u32 i = 0; i < s.indexCount; ++i) {
                u16 v;
                if (base + i * 2 + 2 > ibSize) return std::nullopt;
                std::memcpy(&v, ib + base + i * 2, 2);
                m.indices.push_back(v);
            }
        } else {
            const usize base = s.firstByte;
            for (u32 i = 0; i < s.indexCount; ++i) {
                u32 v;
                if (base + i * 4 + 4 > ibSize) return std::nullopt;
                std::memcpy(&v, ib + base + i * 4, 4);
                m.indices.push_back(v);
            }
        }
    }

    // Streams: stride = sum of channel sizes per stream; stream k starts after stream k-1,
    // 16-aligned (Unity VertexData layout).
    usize streamStride[4] = {0, 0, 0, 0};
    for (const Chan& c : chans)
        if (c.dim && c.stream < 4) {
            const usize end = c.offset + formatSize(c.format) * c.dim;
            if (end > streamStride[c.stream]) streamStride[c.stream] = end;
        }
    usize streamStart[4] = {0, 0, 0, 0};
    {
        usize at = 0;
        for (int s = 0; s < 4; ++s) {
            streamStart[s] = at;
            at += streamStride[s] * m.vertexCount;
            at = (at + 15) & ~usize{15};
        }
    }
    auto readChannel = [&](u32 chan, u32 wantDim, std::vector<float>& out) {
        if (chan >= chans.size() || chans[chan].dim == 0) return;
        const Chan& c = chans[chan];
        const usize csz = formatSize(c.format);
        if (!csz || c.stream >= 4) return;
        out.resize(static_cast<usize>(m.vertexCount) * wantDim);
        for (u32 v = 0; v < m.vertexCount; ++v) {
            const usize base = streamStart[c.stream] + streamStride[c.stream] * v + c.offset;
            for (u32 d = 0; d < wantDim; ++d) {
                if (d >= c.dim || base + csz * (d + 1) > vdata.size()) {
                    out[static_cast<usize>(v) * wantDim + d] = 0.0f;
                    continue;
                }
                out[static_cast<usize>(v) * wantDim + d] =
                    readComponent(vdata.data() + base + csz * d, c.format);
            }
        }
    };
    readChannel(0, 3, m.positions);   // kShaderChannelVertex
    readChannel(1, 3, m.normals);     // kShaderChannelNormal
    readChannel(4, 2, m.uv0);         // kShaderChannelTexCoord0
    readChannel(12, 4, m.boneWeights);  // kShaderChannelBlendWeight
    if (chans.size() > 13 && chans[13].dim) {  // kShaderChannelBlendIndices (integer)
        const Chan& c = chans[13];
        const usize csz = formatSize(c.format);
        m.boneIndices.resize(static_cast<usize>(m.vertexCount) * 4, 0);
        for (u32 v = 0; v < m.vertexCount && csz; ++v) {
            const usize base = streamStart[c.stream] + streamStride[c.stream] * v + c.offset;
            for (u32 d = 0; d < 4 && d < c.dim; ++d) {
                if (base + csz * (d + 1) > vdata.size()) continue;
                u32 idx = 0;
                std::memcpy(&idx, vdata.data() + base + csz * d, csz);
                m.boneIndices[static_cast<usize>(v) * 4 + d] = idx;
            }
        }
    }
    return m;
}

}  // namespace uaro
