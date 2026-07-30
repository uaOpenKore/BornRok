#pragma once
#include <bgfx/bgfx.h>

#include <memory>
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "core/math/Math.hpp"
#include "game/CharacterActor.hpp"  // Anim enum
#include "world/RomModel.hpp"

namespace uaro {

class Application;

// A skinned RoM 3D mob (ROeM Mobs mode): uploads a RomModel to bgfx and plays its
// clips — the bone palette (worldOf(node) * bindPose) is rebuilt on the CPU each
// draw from the baked animation tracks and fed to vs_romskin.
class RomActor {
public:
    ~RomActor() { destroy(); }

    // Uploads geometry + the main texture. `model` is kept (tracks drive the palette).
    bool load(Application& app, RomModel model);
    // Attach a rigid extra mesh (player head/hair bundles: unskinned, own texture) to the
    // prefab attach point `pointName` (e.g. "CP_4" = head on the p1 rig). The anchor node is
    // the skeleton node nearest to that point in the default pose, so the part follows the
    // animation. `scale` is applied around the point (RoM heads are chibi-scaled ~2x).
    // anchorAtNode: place the part at the resolved NODE's default position instead of the
    // CP point itself — hand-held weapons track the palm exactly; the head keeps the CP.
    bool attachPart(Application& app, const RomModel& part, const char* pointName, float scale,
                    bool anchorAtNode = false, const float tint[3] = nullptr, float yLift = 0.0f);
    // Attach a SKINNED part (its own skeleton — e.g. the female player hairstyles are bound
    // meshes, not rigid): skin it against THIS model's node worlds by matching bone name
    // hashes (the shared Bip001 rig resolves across bundles), with its own texture. Falls
    // back to attachPart when the part has no bones. Returns false if nothing matched.
    bool attachSkinnedPart(Application& app, const RomModel& part, const char* pointName = "CP_1",
                           const float tint[3] = nullptr, float yLift = 0.0f);
    void destroy();
    bool ready() const { return bgfx::isValid(vbh_); }
    // Extra render scale on top of the authored size (reskin variants: the female/male
    // thief bugs are 2x the base model). Call once after load().
    void scaleBy(float k) { scale_ *= k; }

    // Draw at world `pos` facing RO direction `dir` (0 = south, clockwise), playing `anim`
    // (clip picked by name: wait/walk/attack/hit/die), `animTime` seconds into it.
    void render(bgfx::ViewId view, const Vec3& pos, u8 dir, Anim anim, double animTime);

    // Rendered wait-pose height in world units (authored height x clamp x reskin scale) —
    // the --view browser fits each model to its list cell with it.
    float height() const { return height_ * scale_; }
    // The assembled model (texture inventory etc.) — the --view dyes tab lists its skins.
    const RomModel& model() const { return model_; }

    // Shared program/uniforms (created once, freed at shutdown).
    static bool initShared(Application& app);
    static void shutdownShared();
    // Per-draw light uniforms (bgfx keeps them until submit); call before render()s.
    static void setLight(const float dir[4], const float ambient[4], const float color[4]);

private:
    const RomClip* clipFor(Anim anim) const;

    struct Part {  // rigid attachment (head/hair), rendered off the anchor node's world
        bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle tex = BGFX_INVALID_HANDLE;
        i32 node = -1;
        Mat4 rest = Mat4::identity();  // inv(defaultWorld[node]) * T(point) * S(scale)
    };

    struct Extra {  // accessory mesh from the same bundle (dokebi's club), skinned
        bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        usize src = 0;  // index into model_.extras (bindposes + boneToNode); ignored when standalone
        // Standalone skinned attachment (female hair): its bones are NOT the body's rig (0
        // hash matches), so it's drawn in its own authored rest pose — inverse(bindPose) puts
        // each vertex where the artist placed it in character space — and rigidly rides the
        // head joint's animation delta (nodeWorld_[anchorNode] * anchorRestInv).
        bool standalone = false;
        std::vector<std::array<float, 16>> restPose;  // per-bone inverse(bindPose)
        i32 anchorNode = -1;                          // body head joint the hair rides
        Mat4 anchorRestInv = Mat4::identity();        // inverse(defaultWorld[anchorNode])
        float yLift = 0.0f;                           // world-Y offset (lower skinned hair to scalp)
        bgfx::TextureHandle tex = BGFX_INVALID_HANDLE;
    };

    RomModel model_;
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle tex_ = BGFX_INVALID_HANDLE;
    std::vector<Part> parts_;
    std::vector<Extra> extras_;
    std::vector<Mat4> nodeWorld_;  // node worlds of the last built palette (parts render)
    std::vector<float> palette_;   // scratch: 70 x 16 floats for u_bones
    float height_ = 1.5f;          // authored wait-pose height (world units), for previews
    float yLift_ = 0.0f;           // -min skinned Y (wait pose): stands the model on the cell
    float scale_ = 1.0f;           // outlier clamp only: RoM metres map 1:1 onto RO units
    void buildPalette(const RomClip* clip, u32 frame, std::vector<Mat4>& out);
};

}  // namespace uaro
