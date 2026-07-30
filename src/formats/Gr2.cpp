#include "formats/Gr2.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>

#include "formats/Gr2Oodle.hpp"

namespace uaro {
namespace {

inline u16 rd16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
inline u32 rd32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

// Granny v6, little-endian, 32-bit-pointer magic (the only variant RO ships).
const u8 kMagic[16] = {0xB8, 0x67, 0xB0, 0xCA, 0xF8, 0x6D, 0xB1, 0x0F,
                       0x84, 0x72, 0x8C, 0x7E, 0x5E, 0x19, 0x00, 0x1E};

constexpr u32 kHeaderStart = 32;   // granny_file_header begins after the 32-byte magic struct
constexpr u32 kSectionSize = 44;   // one granny_file_section descriptor

}  // namespace

std::optional<Gr2Parsed> gr2ParseSections(const std::vector<u8>& bytes) {
    if (bytes.size() < 88) return std::nullopt;
    const u8* p = bytes.data();
    if (std::memcmp(p, kMagic, 16) != 0) return std::nullopt;  // not a (RO) Granny file

    Gr2Parsed out;
    Gr2Header& h = out.header;
    h.headerSize = rd32(p + 16);
    h.version = rd32(p + kHeaderStart + 0);
    h.fileSize = rd32(p + kHeaderStart + 4);
    h.sectionOffset = rd32(p + kHeaderStart + 12);
    h.sectionCount = rd32(p + kHeaderStart + 16);
    // granny_file_header root references (32-bit): RootObjectTypeDefinition @+20 (sec,off), RootObject
    // @+28 (sec,off); +36 is the FileTypeTag (0x8000000f) — NOT the root offset (that bug read the tag
    // as an offset and the whole tree walk failed).
    h.rootTypeSection = rd32(p + kHeaderStart + 20);
    h.rootTypeOffset = rd32(p + kHeaderStart + 24);
    h.rootObjSection = rd32(p + kHeaderStart + 28);
    h.rootObjOffset = rd32(p + kHeaderStart + 32);
    if (h.version != 6) return std::nullopt;                  // only v6 is understood
    if (h.sectionCount == 0 || h.sectionCount > 64) return std::nullopt;
    if (h.fileSize != bytes.size()) return std::nullopt;      // truncated / wrong file

    const u32 secArr = kHeaderStart + h.sectionOffset;        // == 88 for RO files
    if (secArr + h.sectionCount * kSectionSize > bytes.size()) return std::nullopt;

    out.sections.reserve(h.sectionCount);
    for (u32 i = 0; i < h.sectionCount; ++i) {
        const u8* s = p + secArr + i * kSectionSize;
        Gr2Section sec;
        sec.compression = rd32(s + 0);
        sec.dataOffset = rd32(s + 4);
        sec.compressedSize = rd32(s + 8);
        sec.decompressedSize = rd32(s + 12);
        sec.alignment = rd32(s + 16);
        sec.stop0 = rd32(s + 20);
        sec.stop1 = rd32(s + 24);
        sec.relocOffset = rd32(s + 28);
        sec.relocCount = rd32(s + 32);
        sec.marshalOffset = rd32(s + 36);
        sec.marshalCount = rd32(s + 40);
        // Bounds-check the section's compressed payload before touching it.
        if (sec.compressedSize != 0 &&
            static_cast<usize>(sec.dataOffset) + sec.compressedSize > bytes.size())
            return std::nullopt;
        if (sec.compression == 0) {
            // Raw section (textures / some animation curves): copy as-is.
            sec.data.assign(p + sec.dataOffset, p + sec.dataOffset + sec.compressedSize);
        } else if (sec.compression == 1) {
            // Oodle0-compressed (vertices / bones / animation tracks). Best-effort: a failed
            // decode leaves data empty so callers can still inspect descriptors (the renderer
            // treats an empty heavy section as "model not decodable" and falls back to a sprite).
            if (!gr2DecompressOodle0(p + sec.dataOffset, sec.compressedSize, sec.stop0, sec.stop1,
                                     sec.data, sec.decompressedSize))
                sec.data.clear();
        }
        out.sections.push_back(std::move(sec));
    }
    return out;
}

// --- Oodle0 (Granny bitstream) decompression -------------------------------------------------
// RO's heavy .gr2 sections use Granny's ancient "Oodle0" codec (arithmetic + Huffman LZ). The
// algorithm is being ported from the open-source gr2-web / opengr2 readers; until that lands this
// returns false (the section stays undecoded and the model falls back to its sprite). Wiring the
// header/section parse first keeps that work isolated and lets the rest be verified offline.
bool gr2DecompressOodle0(const u8* compressed, usize compressedSize, u32 stop0, u32 stop1,
                         std::vector<u8>& out, usize decompressedSize) {
    // The verbatim open-source Oodle0/Oodle1 decoder (Gr2Oodle.cpp, ported from nwn2mdk / opengr2)
    // is wired below, but VERIFIED offline against a real RO file (dragon.gr2 section 0,
    // 23708 -> 51660) it MIS-DECODES: the range coder desyncs from the first symbol -> garbage +
    // an out-of-bounds back-reference (AddressSanitizer heap-overflow). This is the known
    // gr2-community failure -- the C Oodle0 readers fall over on RO's specific bitstream. So the
    // call is held disabled until a working decoder is in (port gr2-web's, which reportedly handles
    // RO, or pre-convert the 21 models offline with a working tool). Returning false leaves the
    // heavy sections undecoded -> gr2Load yields nullopt -> the monster keeps its sprite.
    (void)compressed;
    (void)compressedSize;
    (void)stop0;
    (void)stop1;
    (void)decompressedSize;
    out.clear();
    return false;
#if 0  // re-enable once the decoder is correct on RO files; memory-safe wrapper:
    std::vector<u8> cbuf(compressedSize + 4, 0);
    std::memcpy(cbuf.data(), compressed, compressedSize);
    out.assign(decompressedSize, 0);
    gr2_decompress(static_cast<u32>(compressedSize), cbuf.data(), stop0, stop1,
                   static_cast<u32>(decompressedSize), out.data());
    return true;
#endif
}

// --- object-tree walk (Granny v6, 32-bit) ----------------------------------------------------
// After decompression each section is a flat byte blob; cross-section pointers exist ONLY as the
// per-section relocation table (FromOffset in this section -> {ToSection, ToOffset}). So we never
// dereference a raw pointer: a "pointer field" is resolved by looking it up in that table. Struct
// member offsets are hard-coded for the Granny v6 32-bit layout RO ships (ref: rdw-archive
// RagnarokFileFormats GR2.MD + the granny_data_type_definition member enum).
namespace {

// A location in the decompressed image: which section + byte offset within it.
struct Loc {
    u32 sec = 0, off = 0;
    bool valid = false;
};

// granny_member_type sizes (bytes) for the vertex-layout types we read. 0 => pointer/complex (4 on 32-bit).
u32 memberSize(u32 type) {
    switch (type) {
        case 10: return 4;  // Real32
        case 21: return 2;  // Real16
        case 11: case 12: case 13: case 14: return 1;  // Int8/UInt8/BinormalInt8/NormalUInt8
        case 15: case 16: case 17: case 18: return 2;  // Int16/UInt16/BinormalInt16/NormalUInt16
        case 19: case 20: return 4;  // Int32/UInt32
        default: return 4;           // String/Reference/etc. (a pointer on 32-bit)
    }
}
constexpr u32 kMemberDefSize = 32;  // granny_data_type_definition on 32-bit (Type,Name*,Ref*,ArrayWidth,Extra[3],pad)

// Compose a granny_transform (Position[3] + Orientation quat[4] + ScaleShear[3][3]) into a row-major
// column-vector 4x4 (v' = M*v; translation in the +3 column). Used for a bone's local bind transform.
void grannyTransformToMatrix(const f32 pos[3], const f32 q[4], const f32 ss[9], f32 m[16]) {
    // quaternion (x,y,z,w) -> 3x3 rotation
    const f32 x = q[0], y = q[1], z = q[2], w = q[3];
    const f32 r[9] = {
        1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
        2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
        2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)};
    // rs = r * scaleShear (3x3 * 3x3)
    f32 rs[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            rs[i * 3 + j] = r[i * 3 + 0] * ss[0 * 3 + j] + r[i * 3 + 1] * ss[1 * 3 + j] +
                            r[i * 3 + 2] * ss[2 * 3 + j];
    m[0] = rs[0]; m[1] = rs[1]; m[2] = rs[2]; m[3] = pos[0];
    m[4] = rs[3]; m[5] = rs[4]; m[6] = rs[5]; m[7] = pos[1];
    m[8] = rs[6]; m[9] = rs[7]; m[10] = rs[8]; m[11] = pos[2];
    m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 1;
}

class Tree {
public:
    Tree(const std::vector<u8>& file, const Gr2Parsed& p) : file_(file), p_(p) {
        // Build the reloc lookup per section from the file's relocation arrays (12-byte entries).
        reloc_.resize(p.sections.size());
        for (u32 s = 0; s < p.sections.size(); ++s) {
            const Gr2Section& sec = p.sections[s];
            for (u32 i = 0; i < sec.relocCount; ++i) {
                const usize e = static_cast<usize>(sec.relocOffset) + i * 12u;
                if (e + 12 > file.size()) break;
                const u32 from = rd32(&file[e]), toSec = rd32(&file[e + 4]), toOff = rd32(&file[e + 8]);
                if (toSec < p.sections.size()) reloc_[s][from] = {toSec, toOff};
            }
        }
    }

    const u8* ptr(const Loc& l, u32 need) const {
        if (!l.valid || l.sec >= p_.sections.size()) return nullptr;
        const auto& d = p_.sections[l.sec].data;
        if (static_cast<usize>(l.off) + need > d.size()) return nullptr;
        return d.data() + l.off;
    }
    u32 u32At(const Loc& l, u32 memberOff) const {
        const u8* q = ptr({l.sec, l.off + memberOff, true}, 4);
        return q ? rd32(q) : 0;
    }
    f32 f32At(const Loc& l, u32 memberOff) const {
        const u8* q = ptr({l.sec, l.off + memberOff, true}, 4);
        u32 v = q ? rd32(q) : 0; f32 f; std::memcpy(&f, &v, 4); return f;
    }
    // Resolve the pointer stored at (l + memberOff) via the reloc table -> target Loc (invalid if null).
    Loc follow(const Loc& l, u32 memberOff) const {
        if (l.sec >= reloc_.size()) return {};
        auto it = reloc_[l.sec].find(l.off + memberOff);
        if (it == reloc_[l.sec].end()) return {};
        return {it->second.first, it->second.second, true};
    }
    std::string cstr(const Loc& l) const {
        if (!l.valid || l.sec >= p_.sections.size()) return {};
        const auto& d = p_.sections[l.sec].data;
        std::string s;
        for (usize i = l.off; i < d.size() && d[i]; ++i) s.push_back(static_cast<char>(d[i]));
        return s;
    }

private:
    const std::vector<u8>& file_;
    const Gr2Parsed& p_;
    std::vector<std::unordered_map<u32, std::pair<u32, u32>>> reloc_;
};

// Parse a vertex-layout type def (member array, End-terminated) -> total stride + component offsets.
struct VLayout {
    u32 stride = 0;
    int posOff = -1, normOff = -1, uvOff = -1;      // byte offset within a vertex (Real32 components only)
    u32 posType = 0, normType = 0, uvType = 0;
    int bwOff = -1, biOff = -1;                      // BoneWeights / BoneIndices byte offsets (skinning)
    u32 bwType = 0, biType = 0;                      // typically NormalUInt8(14) weights, UInt8(12) indices
};
VLayout parseVertexLayout(const Tree& t, Loc typeLoc) {
    VLayout v;
    u32 off = 0;
    for (u32 guard = 0; guard < 64 && typeLoc.valid; ++guard) {
        const u32 memberType = t.u32At(typeLoc, 0);
        if (memberType == 0) break;  // EndMember
        const Loc nameLoc = t.follow(typeLoc, 4);
        const std::string name = t.cstr(nameLoc);
        const u32 arrayWidth = t.u32At(typeLoc, 12);
        const u32 w = arrayWidth ? arrayWidth : 1;
        const u32 sz = memberSize(memberType) * w;
        if (name == "Position" || name == "Vertices") { v.posOff = static_cast<int>(off); v.posType = memberType; }
        else if (name == "Normal") { v.normOff = static_cast<int>(off); v.normType = memberType; }
        else if (name.rfind("TextureCoordinate", 0) == 0) { v.uvOff = static_cast<int>(off); v.uvType = memberType; }
        else if (name == "BoneWeights") { v.bwOff = static_cast<int>(off); v.bwType = memberType; }
        else if (name == "BoneIndices") { v.biOff = static_cast<int>(off); v.biType = memberType; }
        off += sz;
        typeLoc.off += kMemberDefSize;
    }
    v.stride = off;
    return v;
}

// --- embedded granny_texture extraction ------------------------------------------------------
// RO's flag/guardian models store their skin INSIDE the .gr2 (granny_texture with pixel bytes),
// not as an external file -- FromFileName is only the artist's authoring path (no GRF match, S.).
// granny_texture (v6, 32-bit): 0 FromFileName*, 4 TextureType, 8 Width, 12 Height, 16 Encoding,
// 20 SubFormat, 24 granny_pixel_layout (36B: BytesPerPixel + 4*ShiftForComponent + 4*BitsForComponent),
// 60 ImageCount, 64 Images* (granny_texture_image[]). image: 0 MIPLevelCount, 4 MIPLevels*.
// mip: 0 Stride, 4 PixelByteCount, 8 PixelBytes*. Encoding 1=Raw, 2=S3TC(DXT). We pick the decoder
// from PixelByteCount vs W*H (robust against the exact SubFormat enum, which varies by Granny build).

inline u8 rescale(u32 v, u32 fromBits) { return static_cast<u8>(fromBits ? (v * 255u) / ((1u << fromBits) - 1u) : 0); }

// Decode a BC1/DXT1 block (8B) -> 16 RGBA8 pixels (r,g,b,a). c0<=c1 => 1-bit alpha (index 3 transparent).
void dxt1Block(const u8* b, u8 out[16][4]) {
    const u16 c0 = static_cast<u16>(b[0] | (b[1] << 8)), c1 = static_cast<u16>(b[2] | (b[3] << 8));
    u8 col[4][4];
    auto expand = [](u16 c, u8* p) {
        p[0] = rescale((c >> 11) & 0x1f, 5); p[1] = rescale((c >> 5) & 0x3f, 6);
        p[2] = rescale(c & 0x1f, 5);         p[3] = 255;
    };
    expand(c0, col[0]); expand(c1, col[1]);
    if (c0 > c1) {
        for (int k = 0; k < 3; ++k) { col[2][k] = static_cast<u8>((2 * col[0][k] + col[1][k]) / 3);
                                      col[3][k] = static_cast<u8>((col[0][k] + 2 * col[1][k]) / 3); }
        col[2][3] = col[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) { col[2][k] = static_cast<u8>((col[0][k] + col[1][k]) / 2); col[3][k] = 0; }
        col[2][3] = 255; col[3][3] = 0;  // index 3 = transparent black
    }
    const u32 bits = static_cast<u32>(b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24));
    for (int i = 0; i < 16; ++i) { const int s = (bits >> (i * 2)) & 3; std::memcpy(out[i], col[s], 4); }
}

// Decode a BC3/DXT5 block (16B): 8B alpha (2 endpoints + 16*3-bit) then an 8B DXT1-style colour block.
void dxt5Block(const u8* b, u8 out[16][4]) {
    dxt1Block(b + 8, out);  // colour is always 4-colour mode in BC3; overwrite alpha below
    u8 a[8];
    a[0] = b[0]; a[1] = b[1];
    if (a[0] > a[1]) { for (int k = 1; k <= 6; ++k) a[k + 1] = static_cast<u8>(((7 - k) * a[0] + k * a[1]) / 7); }
    else { for (int k = 1; k <= 4; ++k) a[k + 1] = static_cast<u8>(((5 - k) * a[0] + k * a[1]) / 5); a[6] = 0; a[7] = 255; }
    u64 idx = 0; for (int k = 0; k < 6; ++k) idx |= static_cast<u64>(b[2 + k]) << (8 * k);
    for (int i = 0; i < 16; ++i) out[i][3] = a[(idx >> (i * 3)) & 7];
}

// Extract + decode one granny_texture to RGBA8 (top-down). Empty rgba on unsupported/invalid.
Gr2Texture extractTexture(const Tree& t, const Loc& tex) {
    Gr2Texture out;
    const auto slash = [](std::string s) {
        const auto p = s.find_last_of("/\\"); if (p != std::string::npos) s = s.substr(p + 1);
        const auto d = s.find_last_of('.'); if (d != std::string::npos) s = s.substr(0, d); return s;
    };
    out.name = slash(t.cstr(t.follow(tex, 0)));
    const u32 w = t.u32At(tex, 8), h = t.u32At(tex, 12);
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return out;
    const Loc imgArr = t.follow(tex, 64);
    if (!imgArr.valid) return out;
    const Loc img0 = imgArr;                 // Images[0] (array of granny_texture_image, first element)
    const Loc mipArr = t.follow(img0, 4);    // MIPLevels*
    if (!mipArr.valid) return out;
    const u32 pbc = t.u32At(mipArr, 4);      // MIPLevels[0].PixelByteCount
    const Loc pix = t.follow(mipArr, 8);     // MIPLevels[0].PixelBytes
    if (!pix.valid || pbc == 0) return out;
    const u8* src = t.ptr(pix, pbc);
    if (!src) return out;

    out.width = w; out.height = h;
    out.rgba.assign(static_cast<usize>(w) * h * 4, 0);
    const u32 bw = (w + 3) / 4, bh = (h + 3) / 4;
#ifdef GR2_DEBUG
    std::fprintf(stderr, "  [gr2tex] '%s' %ux%u enc=%u sub=%u bpp=%u pbc=%u  (dxt1=%u dxt5=%u raw32=%u raw24=%u)\n",
                 out.name.c_str(), w, h, t.u32At(tex, 16), t.u32At(tex, 20), t.u32At(tex, 24), pbc,
                 bw * bh * 8, bw * bh * 16, w * h * 4, w * h * 3);
#endif
    auto placeBlock = [&](u32 bx, u32 by, const u8 tile[16][4]) {
        for (u32 ry = 0; ry < 4; ++ry) for (u32 rx = 0; rx < 4; ++rx) {
            const u32 px = bx * 4 + rx, py = by * 4 + ry;
            if (px >= w || py >= h) continue;
            std::memcpy(&out.rgba[(static_cast<usize>(py) * w + px) * 4], tile[ry * 4 + rx], 4);
        }
    };
    if (pbc == static_cast<usize>(bw) * bh * 8) {                   // BC1 / DXT1
        for (u32 by = 0; by < bh; ++by) for (u32 bx = 0; bx < bw; ++bx) {
            u8 tile[16][4]; dxt1Block(src + (static_cast<usize>(by) * bw + bx) * 8, tile); placeBlock(bx, by, tile);
        }
    } else if (pbc == static_cast<usize>(bw) * bh * 16) {          // BC3 / DXT5
        for (u32 by = 0; by < bh; ++by) for (u32 bx = 0; bx < bw; ++bx) {
            u8 tile[16][4]; dxt5Block(src + (static_cast<usize>(by) * bw + bx) * 16, tile); placeBlock(bx, by, tile);
        }
    } else if (pbc == static_cast<usize>(w) * h * 4) {             // raw 32-bit, assume BGRA -> RGBA
        for (usize i = 0; i < static_cast<usize>(w) * h; ++i) {
            out.rgba[i * 4 + 0] = src[i * 4 + 2]; out.rgba[i * 4 + 1] = src[i * 4 + 1];
            out.rgba[i * 4 + 2] = src[i * 4 + 0]; out.rgba[i * 4 + 3] = src[i * 4 + 3];
        }
    } else if (pbc == static_cast<usize>(w) * h * 3) {             // raw 24-bit BGR -> RGBA (opaque)
        for (usize i = 0; i < static_cast<usize>(w) * h; ++i) {
            out.rgba[i * 4 + 0] = src[i * 3 + 2]; out.rgba[i * 4 + 1] = src[i * 3 + 1];
            out.rgba[i * 4 + 2] = src[i * 3 + 0]; out.rgba[i * 4 + 3] = 255;
        }
    } else {
        out.rgba.clear();  // unrecognised layout -> caller falls back to the name lookup
    }
    return out;
}

}  // namespace

std::optional<Gr2Model> gr2Load(const std::vector<u8>& bytes) {
    auto parsed = gr2ParseSections(bytes);
    if (!parsed) return std::nullopt;
    // Every heavy section must have decoded (data non-empty where a payload exists), else the tree
    // walk would read zeros. A section with compressedSize>0 but empty data = failed Oodle0 -> bail.
    for (const Gr2Section& s : parsed->sections)
        if (s.compressedSize > 0 && s.data.empty()) return std::nullopt;

    const Gr2Header& h = parsed->header;
    Tree t(bytes, *parsed);
    const Loc root{h.rootObjSection, h.rootObjOffset, true};  // granny_file_info

    // granny_file_info (32-bit), verified on RO's guildflag: 4 leading pointers (ArtToolInfo, ExporterInfo,
    // +1, FromFileName) then count/array** pairs from +16: Texture(16,20), Material(24,28), Skeleton(32,36),
    // VertexData(40,44), TriTopology(48,52), Mesh(56,60), Model(64,68), TrackGroup(72,76), Animation(80,84).
    const u32 meshCount = t.u32At(root, 56);
    const Loc meshArr = t.follow(root, 60);  // granny_mesh*[]
    if (!meshArr.valid || meshCount == 0 || meshCount > 4096) return std::nullopt;

    // Texture list (file_info.Textures @+20, count @+16). granny_texture.FromFileName @0 is the artist's
    // authoring path ("D:\\...\\flag01.tif") — strip to the basename (no dir, no extension) so the renderer
    // resolves it under data/texture/. Assigned per mesh by index (clamped) as a first pass; the exact
    // per-mesh material->texture mapping is refined once the model renders (S. iterates visually).
    auto basename = [](std::string s) {
        const auto slash = s.find_last_of("/\\");
        if (slash != std::string::npos) s = s.substr(slash + 1);
        const auto dot = s.find_last_of('.');
        if (dot != std::string::npos) s = s.substr(0, dot);
        return s;
    };
    Gr2Model model;

    // Skeleton (#71 animation). file_info: SkeletonCount@32, Skeletons@36 (granny_skeleton**). granny_skeleton:
    // Name@0, BoneCount@4, Bones@8. granny_bone: stride 156; Name@0, ParentIndex@4, LocalTransform@8
    // (granny_transform: Position@+4[3], Orientation@+16[4] quat, ScaleShear@+32[9]), InverseWorld4x4@76[16].
    // Verified on aguardian90_8 (43-bone biped). Static props (guild flag) have no skeleton -> bones empty.
    {
        const u32 skelCount = t.u32At(root, 32);
        const Loc skelArr = t.follow(root, 36);
        if (skelArr.valid && skelCount > 0) {
            const Loc sk = t.follow(skelArr, 0);  // one skeleton per RO model
            const u32 boneCount = sk.valid ? t.u32At(sk, 4) : 0;
            const Loc bones = t.follow(sk, 8);
            constexpr u32 kBoneStride = 156;
            for (u32 b = 0; bones.valid && b < boneCount && b < 256; ++b) {
                const Loc bone{bones.sec, bones.off + b * kBoneStride, true};
                Gr2Bone gb;
                gb.name = t.cstr(t.follow(bone, 0));
                gb.parent = static_cast<int>(t.u32At(bone, 4));
                const u32 xf = 8;  // LocalTransform base
                f32 pos[3] = {t.f32At(bone, xf + 4), t.f32At(bone, xf + 8), t.f32At(bone, xf + 12)};
                f32 q[4] = {t.f32At(bone, xf + 16), t.f32At(bone, xf + 20), t.f32At(bone, xf + 24),
                            t.f32At(bone, xf + 28)};
                f32 ss[9];
                for (int k = 0; k < 9; ++k) ss[k] = t.f32At(bone, xf + 32 + k * 4);
                grannyTransformToMatrix(pos, q, ss, gb.local);
                // InverseWorld4x4 is stored column-major relative to our row-major/column-vector local
                // (grannyTransformToMatrix). Transpose on read so `skin = world * invBind` == identity at
                // the bind pose (verified: transposed -> avg identity error 0.0000 across all 43 bones).
                f32 raw[16];
                for (int k = 0; k < 16; ++k) raw[k] = t.f32At(bone, 76 + k * 4);
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c) gb.invBind[r * 4 + c] = raw[c * 4 + r];
                model.bones.push_back(std::move(gb));
            }
        }
    }

    std::vector<std::string> texNames;
    std::vector<Loc> texLocs;  // file location of each granny_texture (to match a mesh's material -> texture)
    {
        const u32 texCount = t.u32At(root, 16);
        const Loc texArr = t.follow(root, 20);
        for (u32 i = 0; texArr.valid && i < texCount && i < 64; ++i) {
            const Loc tex = t.follow(texArr, i * 4);
            if (!tex.valid) continue;
            texNames.push_back(basename(t.cstr(t.follow(tex, 0))));  // FromFileName @0
            texLocs.push_back(tex);
            // Also pull the EMBEDDED pixels (RO flag/guardian skins live inside the .gr2). Parallel-
            // indexed with texNames; an undecoded texture keeps empty rgba (renderer falls back to name).
            model.textures.push_back(extractTexture(t, tex));
        }
    }

    // Resolve a mesh's diffuse texture through its material binding (the CORRECT mapping) rather than
    // guessing by mesh index. granny_mesh: MaterialBindingCount@20, MaterialBindings@24. binding@0 =
    // granny_material*. granny_material.Texture@12 -> granny_texture. Match that back to our texture list.
    // Returns -1 if the model has no material bindings (fall back to the index guess).
    auto meshTexIndex = [&](const Loc& meshPtr) -> int {
        const u32 mbCount = t.u32At(meshPtr, 20);
        const Loc mbArr = t.follow(meshPtr, 24);
        if (!mbArr.valid || mbCount == 0) return -1;
        const Loc mb = t.follow(mbArr, 0);         // MaterialBindings[0]
        const Loc mat = mb.valid ? t.follow(mb, 0) : Loc{};   // -> granny_material
        // granny_material: Name@0, MapCount@4, Maps@8, Texture@12. A leaf "texture material" has
        // Texture@12 set; a composite material instead lists Maps (granny_material_map: Usage*@0,
        // Material*@4) each pointing at a leaf material. Try the direct texture, else walk the maps.
        Loc tex = mat.valid ? t.follow(mat, 12) : Loc{};
        if (mat.valid && !tex.valid) {
            const u32 mapCount = t.u32At(mat, 4);
            const Loc maps = t.follow(mat, 8);     // granny_material_map[] (8 bytes each on 32-bit)
            for (u32 mi = 0; maps.valid && mi < mapCount && mi < 16; ++mi) {
                const Loc leaf = t.follow({maps.sec, maps.off + mi * 8, true}, 4);  // map[mi].Material
                const Loc lt = leaf.valid ? t.follow(leaf, 12) : Loc{};
                if (lt.valid) { tex = lt; break; }
            }
        }
        if (!tex.valid) return -1;
        for (usize i = 0; i < texLocs.size(); ++i)
            if (texLocs[i].sec == tex.sec && texLocs[i].off == tex.off) return static_cast<int>(i);
        return -1;
    };
    bool anyBinding = false;  // did any mesh resolve a real material->texture binding?

    for (u32 m = 0; m < meshCount; ++m) {
        const Loc meshPtr = t.follow(meshArr, m * 4);  // each array slot is a pointer -> granny_mesh
        if (!meshPtr.valid) continue;
        // granny_mesh: 0 Name*,4 PrimaryVertexData*,8 MorphCount,12..,16 PrimaryTopology*,...
        const Loc vd = t.follow(meshPtr, 4);
        const Loc topo = t.follow(meshPtr, 16);
        if (!vd.valid || !topo.valid) continue;

        // granny_vertex_data: 0 VertexType*,4 VertexCount,8 Vertices(u8*).
        const Loc vtype = t.follow(vd, 0);
        const u32 vcount = t.u32At(vd, 4);
        const Loc verts = t.follow(vd, 8);
        if (!verts.valid || vcount == 0 || vcount > 1u << 20) continue;
        const VLayout vl = parseVertexLayout(t, vtype);
        if (vl.stride == 0 || vl.posOff < 0) continue;

        Gr2Mesh mesh;
        mesh.positions.reserve(vcount * 3);
        const bool haveN = vl.normOff >= 0 && vl.normType == 10;  // Real32 normals only for now
        const bool haveUV = vl.uvOff >= 0 && vl.uvType == 10;
        const bool haveSkin = vl.bwOff >= 0 && vl.biOff >= 0;  // skinned mesh (#71 animation)
        for (u32 i = 0; i < vcount; ++i) {
            const Loc vpos{verts.sec, verts.off + i * vl.stride, true};
            mesh.positions.push_back(t.f32At(vpos, static_cast<u32>(vl.posOff) + 0));
            mesh.positions.push_back(t.f32At(vpos, static_cast<u32>(vl.posOff) + 4));
            mesh.positions.push_back(t.f32At(vpos, static_cast<u32>(vl.posOff) + 8));
            if (haveN) {
                mesh.normals.push_back(t.f32At(vpos, static_cast<u32>(vl.normOff) + 0));
                mesh.normals.push_back(t.f32At(vpos, static_cast<u32>(vl.normOff) + 4));
                mesh.normals.push_back(t.f32At(vpos, static_cast<u32>(vl.normOff) + 8));
            }
            if (haveUV) {
                mesh.uvs.push_back(t.f32At(vpos, static_cast<u32>(vl.uvOff) + 0));
                mesh.uvs.push_back(t.f32At(vpos, static_cast<u32>(vl.uvOff) + 4));
            }
            if (haveSkin) {
                // t.ptr(l,need) returns the vertex START (l.off) with `need` bytes guaranteed, so index the
                // components at their layout offsets. BoneIndices: UInt8×4. BoneWeights: NormalUInt8×4
                // (byte/255) or Real32×4.
                const u8* bi = t.ptr(vpos, static_cast<u32>(vl.biOff) + 4);
                for (int k = 0; k < 4; ++k) mesh.boneIndices.push_back(bi ? bi[vl.biOff + k] : 0);
                if (vl.bwType == 10) {  // Real32 weights
                    for (int k = 0; k < 4; ++k)
                        mesh.boneWeights.push_back(t.f32At(vpos, static_cast<u32>(vl.bwOff) + k * 4));
                } else {  // NormalUInt8 (14) -> byte/255
                    const u8* bw = t.ptr(vpos, static_cast<u32>(vl.bwOff) + 4);
                    for (int k = 0; k < 4; ++k) mesh.boneWeights.push_back(bw ? bw[vl.bwOff + k] / 255.0f : 0.0f);
                }
            }
        }

        // granny_tri_topology: 0 GroupCount,4 Groups*,8 IndexCount,12 Indices(int32*),16 Index16Count,20 Indices16*.
        const u32 idx32Count = t.u32At(topo, 8);
        const Loc idx32 = t.follow(topo, 12);
        const u32 idx16Count = t.u32At(topo, 16);
        const Loc idx16 = t.follow(topo, 20);
        if (idx16.valid && idx16Count > 0) {
            const u8* q = t.ptr(idx16, idx16Count * 2);
            if (q) for (u32 i = 0; i < idx16Count; ++i) mesh.indices.push_back(rd16(q + i * 2));
        } else if (idx32.valid && idx32Count > 0) {
            const u8* q = t.ptr(idx32, idx32Count * 4);
            if (q) for (u32 i = 0; i < idx32Count; ++i) {
                const u32 v = rd32(q + i * 4);
                mesh.indices.push_back(static_cast<u16>(v));  // RO models fit in 16-bit
            }
        }
        if (mesh.positions.empty() || mesh.indices.empty()) continue;

        // Bone bindings (#71 animation): the vertex BoneIndices reference this mesh's LOCAL bone table,
        // not the skeleton directly, so we must map local slot -> skeleton bone index. granny_mesh:
        // BoneBindingCount@28, BoneBindings@32 (granny_bone_binding stride 36, BoneName@0). Verified on
        // aguardian (body mesh: 40 bindings Pelvis/Spine/Spine1/Neck/Head/...). A mesh with exactly one
        // binding is RIGID (no per-vertex weights) -- it rides that single bone (rigidBone).
        {
            const u32 bbCount = t.u32At(meshPtr, 28);
            const Loc bbArr = t.follow(meshPtr, 32);
            constexpr u32 kBindStride = 36;
            for (u32 bi = 0; bbArr.valid && bi < bbCount && bi < 256; ++bi) {
                const std::string bn = t.cstr(t.follow({bbArr.sec, bbArr.off + bi * kBindStride, true}, 0));
                int skelIdx = -1;
                for (usize s = 0; s < model.bones.size(); ++s)
                    if (model.bones[s].name == bn) { skelIdx = static_cast<int>(s); break; }
                mesh.boneBindings.push_back(skelIdx);
            }
            if (bbCount == 1 && !mesh.boneBindings.empty()) mesh.rigidBone = mesh.boneBindings[0];
        }

        // Diffuse texture: follow the mesh's material binding when it resolves (correct mapping).
        if (!texNames.empty()) {
            int ti = meshTexIndex(meshPtr);
            if (ti >= 0) {
                anyBinding = true;
                mesh.texture = texNames[ti];
                if (static_cast<usize>(ti) < model.textures.size() && !model.textures[ti].rgba.empty())
                    mesh.texIndex = ti;
            } else {  // provisional: mesh-order index; corrected by the size-pairing pass below if needed
                const usize gi = model.meshes.size() < texNames.size() ? model.meshes.size() : texNames.size() - 1;
                mesh.texture = texNames[gi];
                if (gi < model.textures.size() && !model.textures[gi].rgba.empty()) mesh.texIndex = static_cast<int>(gi);
            }
        }
        model.meshes.push_back(std::move(mesh));
    }
    if (model.meshes.empty()) return std::nullopt;

    // Material bindings didn't resolve (granny_material layout not yet mapped) -> the mesh-order guess can
    // put the wrong skin on a mesh (guildflag: the 288-vert cloth got the 16x16 emblem, the 8-vert quad got
    // the 256x256 flag atlas, S.: "текстура легла неверно"). Re-pair by size: the mesh with the most
    // vertices wears the largest texture, next-largest next, etc. Matches how RO builds these props (the
    // body mesh carries the main skin) and fixes the swap. Skipped when a real binding was found.
    if (!anyBinding && model.textures.size() >= 2 && model.meshes.size() >= 2) {
        std::vector<usize> mo(model.meshes.size()), to(model.textures.size());
        for (usize i = 0; i < mo.size(); ++i) mo[i] = i;
        for (usize i = 0; i < to.size(); ++i) to[i] = i;
        std::sort(mo.begin(), mo.end(), [&](usize a, usize b) {
            return model.meshes[a].positions.size() > model.meshes[b].positions.size();
        });
        std::sort(to.begin(), to.end(), [&](usize a, usize b) {
            return static_cast<usize>(model.textures[a].width) * model.textures[a].height >
                   static_cast<usize>(model.textures[b].width) * model.textures[b].height;
        });
        for (usize r = 0; r < mo.size() && r < to.size(); ++r) {
            Gr2Mesh& me = model.meshes[mo[r]];
            const usize ti = to[r];
            me.texture = texNames[ti];
            me.texIndex = !model.textures[ti].rgba.empty() ? static_cast<int>(ti) : -1;
        }
    }
    return model;
}

// --- Skeletal animation (#71) ---------------------------------------------------------------------
namespace {

// Read one inline granny_curve2 (DaK32fC32f layout) at track-relative offsets. A curve slot is the
// 16-byte {KnotCount(int), Knots*(ptr), ControlCount(int), Controls*(ptr)} quad. dim = controls/knots
// (3 = position xyz, 4 = orientation quaternion, 9 = scaleshear). A 1-knot curve is a constant value.
Gr2AnimCurve readCurve(const Tree& t, const Loc& track, u32 kcOff, u32 kOff, u32 ccOff, u32 cOff) {
    Gr2AnimCurve c;
    const u32 knotCount = t.u32At(track, kcOff);
    const u32 ctrlCount = t.u32At(track, ccOff);
    if (knotCount == 0 || knotCount > 4096 || ctrlCount < knotCount || ctrlCount > 65536) return c;
    if (ctrlCount % knotCount != 0) return c;             // controls must be a whole multiple of knots
    const Loc knots = t.follow(track, kOff);
    const Loc ctrls = t.follow(track, cOff);
    const u8* kp = t.ptr(knots, knotCount * 4);
    const u8* cp = t.ptr(ctrls, ctrlCount * 4);
    if (!kp || !cp) return c;
    c.dim = static_cast<int>(ctrlCount / knotCount);
    if (c.dim != 3 && c.dim != 4 && c.dim != 9) return {};  // not a transform curve we understand
    c.knots.resize(knotCount);
    c.controls.resize(ctrlCount);
    for (u32 i = 0; i < knotCount; ++i) { u32 v = rd32(kp + i * 4); std::memcpy(&c.knots[i], &v, 4); }
    for (u32 i = 0; i < ctrlCount; ++i) { u32 v = rd32(cp + i * 4); std::memcpy(&c.controls[i], &v, 4); }
    return c;
}

}  // namespace

// Parse an animation-only .gr2 (3dmob_bone/<action>.gr2). Structure (Granny v6 32-bit, verified on
// 8_move.gr2): file_info.AnimationCount@80 / Animations@84 (granny_animation**) -> anim.Duration@4,
// TimeStep@8, TrackGroups@20 (-> granny_track_group directly). track_group.TransformTrackCount@12,
// TransformTracks@16 (granny_transform_track[], stride 64). Each track: Name@0 (bone), then 3 inline
// curve slots at (8,12,16,20),(28,32,36,40),(48,52,56,60) with 4-byte header fields between them.
// Curves are classified by dim (3=position, 4=orientation) rather than slot order, so a track that
// lists them in another order still resolves.
std::optional<Gr2Animation> gr2LoadAnimation(const std::vector<u8>& bytes) {
    auto parsed = gr2ParseSections(bytes);
    if (!parsed) return std::nullopt;
    for (const Gr2Section& s : parsed->sections)
        if (s.compressedSize > 0 && s.data.empty()) return std::nullopt;  // undecoded heavy section

    const Gr2Header& h = parsed->header;
    Tree t(bytes, *parsed);
    const Loc root{h.rootObjSection, h.rootObjOffset, true};  // granny_file_info

    const u32 animCount = t.u32At(root, 80);
    const Loc animArr = t.follow(root, 84);
    if (!animArr.valid || animCount == 0) return std::nullopt;
    const Loc anim = t.follow(animArr, 0);   // Animations[0] (granny_animation**)
    if (!anim.valid) return std::nullopt;

    Gr2Animation out;
    out.duration = t.f32At(anim, 4);
    out.timeStep = t.f32At(anim, 8);

    // TrackGroups@20 -> the (single) granny_track_group directly. Guard: if the count field there is not
    // sane, fall back to treating it as an array-of-pointers (**) and dereference the first element.
    Loc tg = t.follow(anim, 20);
    if (tg.valid) {
        u32 tc = t.u32At(tg, 12);
        if (tc == 0 || tc > 512) { const Loc tg2 = t.follow(tg, 0); if (tg2.valid) tg = tg2; }
    }
    if (!tg.valid) return std::nullopt;

    const u32 trackCount = t.u32At(tg, 12);   // TransformTrackCount
    const Loc tracks = t.follow(tg, 16);      // TransformTracks (stride 64)
    if (!tracks.valid || trackCount == 0 || trackCount > 512) return std::nullopt;
    constexpr u32 kTrackStride = 64;

    out.tracks.reserve(trackCount);
    for (u32 i = 0; i < trackCount; ++i) {
        const Loc track{tracks.sec, tracks.off + i * kTrackStride, true};
        Gr2AnimTrack at;
        at.bone = t.cstr(t.follow(track, 0));
        if (at.bone.empty()) continue;
        // Read the 3 curve slots and classify each by dim.
        const Gr2AnimCurve slots[3] = {
            readCurve(t, track, 8, 12, 16, 20),
            readCurve(t, track, 28, 32, 36, 40),
            readCurve(t, track, 48, 52, 56, 60),
        };
        for (const Gr2AnimCurve& c : slots) {
            if (c.dim == 3 && at.position.empty()) at.position = c;
            else if (c.dim == 4 && at.orientation.empty()) at.orientation = c;
        }
        out.tracks.push_back(std::move(at));
    }
    if (out.tracks.empty()) return std::nullopt;
    return out;
}

// --- pose sampler + CPU skinning helpers ----------------------------------------------------------
namespace {

// Row-major 4x4 multiply: out = a * b (column-vector convention, v' = a*(b*v)).
void mat4mul(const f32 a[16], const f32 b[16], f32 out[16]) {
    f32 r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] +
                           a[i * 4 + 2] * b[2 * 4 + j] + a[i * 4 + 3] * b[3 * 4 + j];
    std::memcpy(out, r, sizeof r);
}

// Find the two knots bracketing t and return (index i, blend u in [0,1]) for [i, i+1]. A single-knot
// curve (constant) returns u=0. t is assumed already wrapped into the curve's range.
void bracket(const std::vector<f32>& knots, f32 t, int& i, f32& u) {
    i = 0; u = 0.0f;
    if (knots.size() < 2) return;
    if (t <= knots.front()) { i = 0; u = 0; return; }
    if (t >= knots.back()) { i = static_cast<int>(knots.size()) - 2; u = 1; return; }
    int lo = 0, hi = static_cast<int>(knots.size()) - 1;
    while (lo + 1 < hi) { int mid = (lo + hi) / 2; (knots[mid] <= t ? lo : hi) = mid; }
    i = lo;
    const f32 span = knots[i + 1] - knots[i];
    u = span > 1e-9f ? (t - knots[i]) / span : 0.0f;
}

// Normalised quaternion slerp (shortest path). qa/qb are (x,y,z,w).
void slerp(const f32 qa[4], const f32 qb[4], f32 u, f32 out[4]) {
    f32 b[4] = {qb[0], qb[1], qb[2], qb[3]};
    f32 dot = qa[0] * b[0] + qa[1] * b[1] + qa[2] * b[2] + qa[3] * b[3];
    if (dot < 0) { for (int k = 0; k < 4; ++k) b[k] = -b[k]; dot = -dot; }
    f32 s0, s1;
    if (dot > 0.9995f) { s0 = 1 - u; s1 = u; }  // near-parallel: linear (avoids div by ~0)
    else {
        const f32 th = std::acos(dot), st = std::sin(th);
        s0 = std::sin((1 - u) * th) / st; s1 = std::sin(u * th) / st;
    }
    f32 len = 0;
    for (int k = 0; k < 4; ++k) { out[k] = s0 * qa[k] + s1 * b[k]; len += out[k] * out[k]; }
    len = std::sqrt(len);
    if (len > 1e-9f) for (int k = 0; k < 4; ++k) out[k] /= len;
}

// Sample a position (dim 3) curve at t -> pos[3]. Returns false if the curve can't supply a position.
bool samplePosition(const Gr2AnimCurve& c, f32 t, f32 pos[3]) {
    if (c.dim != 3 || c.knots.empty()) return false;
    int i; f32 u; bracket(c.knots, t, i, u);
    const f32* a = &c.controls[i * 3];
    if (c.knots.size() < 2) { pos[0] = a[0]; pos[1] = a[1]; pos[2] = a[2]; return true; }
    const f32* b = &c.controls[(i + 1) * 3];
    for (int k = 0; k < 3; ++k) pos[k] = a[k] + (b[k] - a[k]) * u;
    return true;
}

// Sample an orientation (dim 4) curve at t -> quat[4]. Returns false if not an orientation curve.
bool sampleOrientation(const Gr2AnimCurve& c, f32 t, f32 q[4]) {
    if (c.dim != 4 || c.knots.empty()) return false;
    int i; f32 u; bracket(c.knots, t, i, u);
    const f32* a = &c.controls[i * 4];
    if (c.knots.size() < 2) { for (int k = 0; k < 4; ++k) q[k] = a[k]; return true; }
    slerp(a, &c.controls[(i + 1) * 4], u, q);
    return true;
}

}  // namespace

bool gr2SamplePose(const std::vector<Gr2Bone>& bones, const Gr2Animation& anim, f32 time,
                   std::vector<f32>& outSkin, std::vector<f32>* worldOut) {
    const usize n = bones.size();
    if (n == 0) return false;

    // Wrap time into the clip.
    f32 t = time;
    if (anim.duration > 1e-6f) { t = std::fmod(t, anim.duration); if (t < 0) t += anim.duration; }

    // Bone name -> animation track (case-sensitive; granny names match exactly).
    std::unordered_map<std::string, const Gr2AnimTrack*> byName;
    byName.reserve(anim.tracks.size() * 2);
    for (const Gr2AnimTrack& tr : anim.tracks) byName[tr.bone] = &tr;

    const f32 kIdentityScale[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::vector<f32> world(n * 16);
    outSkin.assign(n * 16, 0.0f);

    for (usize b = 0; b < n; ++b) {
        const Gr2Bone& bone = bones[b];
        f32 local[16];
        auto it = byName.find(bone.name);
        const Gr2AnimTrack* tr = it != byName.end() ? it->second : nullptr;
        f32 q[4];
        if (tr && sampleOrientation(tr->orientation, t, q)) {
            // Animated rotation. Position from the track if it has one, else the bind translation.
            f32 pos[3];
            if (!samplePosition(tr->position, t, pos)) {
                pos[0] = bone.local[3]; pos[1] = bone.local[7]; pos[2] = bone.local[11];
            }
            grannyTransformToMatrix(pos, q, kIdentityScale, local);
        } else {
            std::memcpy(local, bone.local, sizeof local);  // no orientation track -> keep bind local
        }
        // world = parentWorld * local (bones are stored parent-before-child in RO skeletons).
        if (bone.parent >= 0 && static_cast<usize>(bone.parent) < b)
            mat4mul(&world[bone.parent * 16], local, &world[b * 16]);
        else
            std::memcpy(&world[b * 16], local, sizeof local);
        // skin = world * invBind.
        mat4mul(&world[b * 16], bone.invBind, &outSkin[b * 16]);
    }
    if (worldOut) *worldOut = world;
    return true;
}

}  // namespace uaro
