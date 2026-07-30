#pragma once
// Runtime renderer for RO's Granny (.gr2) 3D monster/prop models (WoE guardians, Emperium, Guild
// Standard/flags, Treasure Box, Dragon). Parses the model with formats/Gr2 (gr2Load) on first use,
// uploads each mesh to a bgfx buffer + its diffuse texture, and draws it at a world transform using
// the same vs_model/fs_model program the RSM ModelRenderer uses. Static pose for now (skeleton
// animation is a later step). Models are cached by their VFS path and freed on destroy().
#include <bgfx/bgfx.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Types.hpp"
#include "formats/Gr2.hpp"  // Gr2Bone, Gr2Animation (skeletal animation, #71)

namespace uaro {

class Application;

// RO monster "actions" mapped to skeletal animation clips (3dmob_bone/<group>_<motion>.gr2).
enum class Gr2Action { Idle = 0, Walk = 1, Attack = 2, Hurt = 3, Dead = 4 };

class Gr2Models {
public:
    bool init(Application& app);
    void destroy();
    bool ready() const { return bgfx::isValid(program_); }

    // Draw the model at `vfsPath` (e.g. "data/model/3dmob/guildflag90_1.gr2") with the given
    // column-major world matrix into `view`. Lazily loads+caches; a model that fails to parse is
    // cached as empty so it is not retried every frame. No-op if not ready. `emblemTex` (optional):
    // if valid, replaces the diffuse on the "emblem" mesh (a guild flag's cloth) with the owning
    // guild's server-sent emblem (#5).
    void draw(Application& app, bgfx::ViewId view, const std::string& vfsPath, const float world[16],
              bgfx::TextureHandle emblemTex = BGFX_INVALID_HANDLE);

    // Draw `vfsPath` skeletally animated: sample the clip for `action` at `timeSec` (seconds; looped
    // for Walk/Idle), CPU-skin each mesh into a per-frame dynamic vertex buffer, and draw. Falls back
    // to the static bind pose (== draw()) when the model has no skeleton or the clip can't be found.
    // Clip files live at data/model/3dmob_bone/<group>_<motion>.gr2 (group = the model's trailing _N).
    void drawAnimated(Application& app, bgfx::ViewId view, const std::string& vfsPath, Gr2Action action,
                      float timeSec, const float world[16],
                      bgfx::TextureHandle emblemTex = BGFX_INVALID_HANDLE);

private:
    struct Mesh {
        bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        u32 idxCount = 0;
        bgfx::TextureHandle tex = BGFX_INVALID_HANDLE;  // owned via ownedTex_
        bool emblem = false;  // this mesh is the guild-flag cloth (gr2 texture name "emblem")
        // --- skeletal animation (#71): CPU-side skinning source, kept so the mesh can be re-posed ---
        bool skinned = false;                 // has per-vertex bone weights (else rigid/static)
        int rigidBone = -1;                   // rigid mesh: single skeleton bone it rides (else -1)
        std::vector<u8> baseVerts;            // the un-posed interleaved GpuVertex bytes (bind pose)
        std::vector<f32> posNative;           // native (Z-up) positions, 3/vertex (pre axis-swap)
        std::vector<f32> nrmNative;           // native normals, 3/vertex (may be empty)
        std::vector<u8> boneIdx;              // 4/vertex, index into `bindings`
        std::vector<f32> boneWeight;          // 4/vertex
        std::vector<i32> bindings;            // local bone slot -> skeleton bone index
        bgfx::DynamicVertexBufferHandle dvbh = BGFX_INVALID_HANDLE;  // per-frame skinned buffer
    };
    struct Model {
        std::vector<Mesh> meshes;
        std::vector<Gr2Bone> bones;  // skeleton (empty on a static prop like the guild flag)
        bool tried = false;
    };
    const Model& get(Application& app, const std::string& vfsPath);
    // Load+cache the animation clip for (modelPath, action); nullptr if none exists.
    const Gr2Animation* clipFor(Application& app, const std::string& modelPath, Gr2Action action);

    std::unordered_map<std::string, Model> cache_;
    std::unordered_map<std::string, std::optional<Gr2Animation>> clips_;  // clip vfs path -> parsed
    std::vector<bgfx::TextureHandle> ownedTex_;
    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;     // s_tex
    bgfx::UniformHandle nrmSampler_ = BGFX_INVALID_HANDLE;  // s_nrm
    bgfx::UniformHandle nrmParams_ = BGFX_INVALID_HANDLE;   // u_nrmParams
    bgfx::UniformHandle lightDir_ = BGFX_INVALID_HANDLE;    // u_lightDir
    bgfx::UniformHandle lightColor_ = BGFX_INVALID_HANDLE;  // u_lightColor
    bgfx::UniformHandle ambient_ = BGFX_INVALID_HANDLE;     // u_ambient
    bgfx::UniformHandle fade_ = BGFX_INVALID_HANDLE;        // u_fade
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flatNrm_ = BGFX_INVALID_HANDLE;
};

}  // namespace uaro
