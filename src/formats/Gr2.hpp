#pragma once
// RAD Game Tools Granny (.gr2) model reader for Ragnarok Online's 3D monster models
// (WoE guardians, Emperium, Guild Standard, Treasure Box, Dragon). Format reference:
// rdw-archive/RagnarokFileFormats GR2.MD. All 21 RO .gr2 files are Granny version 6,
// little-endian, 32-bit pointers (magic B8 67 B0 CA F8 6D B1 0F ...).
//
// Layout: a 352-byte header + 6 "sections". Heavy sections (vertices, bone bindings,
// animation tracks) are Oodle0-compressed (mode 1); textures/curves are stored raw
// (mode 0). After decompression the sections form a pointer-linked object tree whose
// root is a granny_file_info -> meshes/skeletons/textures.
//
// This module decodes to a plain mesh (positions/normals/uv/indices + texture names)
// that the renderer can upload; it is engine-agnostic (std + core types only) so it is
// unit-testable in core against real .gr2 bytes.
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// One physical section: a (possibly compressed) buffer plus its pointer-relocation table.
struct Gr2Section {
    u32 compression = 0;    // 0 = raw, 1 = Oodle0
    u32 dataOffset = 0;     // byte position of the section data in the file
    u32 compressedSize = 0;
    u32 decompressedSize = 0;
    u32 alignment = 0;
    u32 stop0 = 0, stop1 = 0;        // Oodle0 decode stops (16-bit / 8-bit boundaries)
    u32 relocOffset = 0, relocCount = 0;     // pointer fixups (within-file -> absolute)
    u32 marshalOffset = 0, marshalCount = 0;  // mixed-marshalling (endianness) fixups
    std::vector<u8> data;   // decompressed (or raw) bytes, length == decompressedSize
};

// The parsed file header (granny_file_magic + granny_file_header), pre-decompression.
struct Gr2Header {
    u32 headerSize = 0;     // == 352
    u32 version = 0;        // == 6 for RO
    u32 fileSize = 0;
    u32 sectionOffset = 0;  // == 56
    u32 sectionCount = 0;   // == 6
    u32 rootTypeSection = 0, rootTypeOffset = 0;  // granny_reference to the root type def
    u32 rootObjSection = 0, rootObjOffset = 0;    // granny_reference to the root object
};

// A decoded mesh: triangle list, one diffuse texture name. (Skeleton/animation come later;
// the base/idle pose is enough to draw the model standing on the map.)
struct Gr2Mesh {
    std::vector<f32> positions;  // x,y,z per vertex
    std::vector<f32> normals;    // x,y,z per vertex (may be empty)
    std::vector<f32> uvs;        // u,v per vertex (may be empty)
    std::vector<u16> indices;    // triangle indices (3 per face)
    std::string texture;         // diffuse texture name (resolve under data/texture/...)
    int texIndex = -1;           // index into Gr2Model::textures if the pixels are EMBEDDED (else -1)
    // Skinning (#71 animation): 4 bone indices + 4 weights per vertex, referencing Gr2Model::bones.
    // Empty on a rigid (non-skinned) mesh — then `rigidBone` names the single bone it is attached to
    // (its parent in the skeleton), or -1 if the mesh is not bound to the skeleton at all.
    std::vector<u8> boneIndices;   // 4 per vertex (into Gr2Model::bones via the mesh's BoneBinding table)
    std::vector<f32> boneWeights;  // 4 per vertex (sum ~= 1)
    std::vector<i32> boneBindings; // this mesh's bone table: local bone slot -> skeleton bone index
    int rigidBone = -1;            // rigid mesh: the single skeleton bone it rides (else -1)
};

// One skeleton bone (Granny). Transforms are raw 4x4 (row-major, 16 f32) so this header stays engine-
// agnostic; the renderer converts to its Mat4. `local` = bind-pose local transform (relative to parent);
// `invBind` = inverse world-space bind matrix (for `skin = world[b] * invBind[b]`).
struct Gr2Bone {
    std::string name;
    int parent = -1;   // index into Gr2Model::bones, -1 = root
    f32 local[16];     // local bind transform (T*R*S from granny_transform)
    f32 invBind[16];   // InverseWorld4x4 (ready-made inverse bind matrix)
};

// A texture whose pixel data is EMBEDDED in the .gr2 (granny_texture), not an external file. RO's
// guild-flag / guardian models carry their skin this way -- FromFileName is only the artist's authoring
// path. Decoded here to straight RGBA8 (top-down) so the renderer can upload it directly. Empty rgba =
// the texture could not be decoded (unsupported encoding) -> fall back to the name-based lookup.
struct Gr2Texture {
    std::string name;            // basename of FromFileName (for logs / fallback lookup)
    u32 width = 0, height = 0;
    std::vector<u8> rgba;        // width*height*4, row-major top-down (empty if undecoded)
};

struct Gr2Model {
    std::vector<Gr2Mesh> meshes;
    std::vector<Gr2Texture> textures;  // embedded textures (index via Gr2Mesh::texIndex)
    std::vector<Gr2Bone> bones;        // skeleton (empty on a static prop like the guild flag)
};

// Low-level: parse the header + section table and decompress every section. Returns the
// header and the (decompressed) sections, or nullopt if the magic/version is wrong or a
// section fails to decode. Verifiable without any renderer.
struct Gr2Parsed {
    Gr2Header header;
    std::vector<Gr2Section> sections;
};
std::optional<Gr2Parsed> gr2ParseSections(const std::vector<u8>& bytes);

// High-level: parse + walk the object tree to the base-pose meshes. nullopt on failure.
std::optional<Gr2Model> gr2Load(const std::vector<u8>& bytes);

// --- Skeletal animation (#71) ---------------------------------------------------------------------
// One animated bone track: keyframed position (3/knot) and orientation (quat 4/knot) curves. A curve
// with a single knot is constant. Knots are times (seconds); controls are the flattened values.
struct Gr2AnimCurve {
    std::vector<f32> knots;     // K times
    std::vector<f32> controls;  // K * dim values (dim = 3 position, 4 quaternion)
    int dim = 0;
    bool empty() const { return knots.empty() || controls.empty(); }
};
struct Gr2AnimTrack {
    std::string bone;      // bone name (matched to Gr2Model::bones by name)
    Gr2AnimCurve position;  // dim 3
    Gr2AnimCurve orientation;  // dim 4 (quaternion x,y,z,w)
};
struct Gr2Animation {
    f32 duration = 0.0f;   // clip length (seconds)
    f32 timeStep = 0.0f;   // authoring step (~1/30)
    std::vector<Gr2AnimTrack> tracks;
};
// Parse an animation-only .gr2 (data/model/3dmob_bone/<action>.gr2) — these carry the skeleton + a
// granny_animation but gr2Load bails on them (mesh-focused). nullopt if the file has no animation.
std::optional<Gr2Animation> gr2LoadAnimation(const std::vector<u8>& bytes);

// Sample `anim` at `time` (seconds; wrapped into [0,duration)) into per-bone skinning matrices for the
// skeleton `bones`. outSkin is resized to bones.size()*16 (row-major, column-vector: skin=world*invBind).
// A bone is animated when a track matches its name (position lerp, orientation slerp); bones without a
// track keep their bind-pose local. Returns false if `bones` is empty. CPU skinning then does
// v' = Σ weight_i · skin[boneIdx_i] · v. `world` (optional, size bones*16) receives the world matrices.
bool gr2SamplePose(const std::vector<Gr2Bone>& bones, const Gr2Animation& anim, f32 time,
                   std::vector<f32>& outSkin, std::vector<f32>* world = nullptr);

// Oodle0 (Granny bitstream) decompressor: expand `compressed` (compressedSize bytes) into
// `decompressedSize` bytes. stop0/stop1 are the section's two decode stops. Returns false on
// malformed input. Exposed for unit testing against a known section.
bool gr2DecompressOodle0(const u8* compressed, usize compressedSize, u32 stop0, u32 stop1,
                         std::vector<u8>& out, usize decompressedSize);

}  // namespace uaro
