#pragma once
#include <bgfx/bgfx.h>

#include <vector>

#include "core/math/Math.hpp"
#include "formats/Rsm.hpp"   // RsmRotKey — animated node rotation keyframes (spinning airship propellers etc.)
#include "world/MapData.hpp"

namespace uaro {

class Application;

// Renders the RSM model objects of a map (buildings, props, trees). Each unique
// model is baked once — its node hierarchy transforms are composed and the
// vertices flattened into a single model-space buffer (validated offline) — then
// drawn once per RSW placement with that placement's world transform. Textures
// come from data/texture/. Shares view 0 with the ground (MapRenderer sets the
// camera); this only sets per-placement model matrices.
class ModelRenderer {
public:
    bool load(Application& app, const MapData& map);
    void destroy();
    bool ready() const { return ready_; }

    // camPos/playerPos are world-space; placements between the two (near the
    // camera->player line) fade to semi-transparent so they don't hide the player.
    // forceFade < 1 fades EVERY model to that opacity (Camera Lock x-ray), instead of only the
    // placements crossing the camera->player line. (#104)
    void render(const MapData& map, const Vec3& camPos, const Vec3& playerPos,
                double time, float forceFade = 1.0f) const;

private:
    struct Batch {
        bgfx::TextureHandle tex = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle nrm = BGFX_INVALID_HANDLE;  // optional _n.png normal map (#107)
        u32 indexStart = 0;
        u32 indexCount = 0;
    };
    // A node that carries rotation keyframes (rotKeys) — e.g. an airship propeller. Its geometry is
    // baked in the node's REST space (scale/offset/mat3 only, NOT the animated rotation) into its own
    // buffer, and drawn each frame with matrix = place * basePart * animRot(time). Kept out of the
    // static bake so the rest of the model stays a single static draw.
    struct AnimNode {
        bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        std::vector<Batch> batches;
        Mat4 basePart = Mat4::identity();  // T(-modelCenter) * worldOf(parent) * T(node.pos)
        std::vector<RsmRotKey> rotKeys;    // keyframed quaternions (frame-sorted)
        i32 animLength = 0;                // model animation length in frames (loop period)
    };
    struct Model {
        bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        std::vector<Batch> batches;
        std::vector<AnimNode> animNodes;  // rotating sub-parts drawn per-frame (usually empty)
        f32 radiusXZ = 0.0f;  // model-space horizontal bounding-circle radius (camera-occlude fade)
        bool ok = false;
    };

    std::vector<Model> models_;                  // parallel to map.rsmCache
    std::vector<bgfx::TextureHandle> textures_;  // owned; deduplicated per name
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fade_ = BGFX_INVALID_HANDLE;  // u_fade.x: per-draw alpha (camera-occlude fade)
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    // Normal mapping (#107): a second sampler + the RSW-sun lighting uniforms.
    bgfx::UniformHandle nrmSampler_ = BGFX_INVALID_HANDLE;   // s_nrm
    bgfx::UniformHandle nrmParams_ = BGFX_INVALID_HANDLE;    // u_nrmParams: x=has-map, y=strength
    bgfx::UniformHandle lightDir_ = BGFX_INVALID_HANDLE;     // u_lightDir.xyz
    bgfx::UniformHandle lightColor_ = BGFX_INVALID_HANDLE;   // u_lightColor.rgb
    bgfx::UniformHandle ambient_ = BGFX_INVALID_HANDLE;      // u_ambient.rgb
    bgfx::TextureHandle flatNrm_ = BGFX_INVALID_HANDLE;      // 1x1 (128,128,255) = flat normal
    f32 lightDirV_[4] = {0.0f, 1.0f, 0.0f, 0.0f};            // computed from RSW lat/long
    f32 lightColorV_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    f32 ambientV_[4] = {0.4f, 0.4f, 0.4f, 1.0f};
    bool ready_ = false;
};

} // namespace uaro
