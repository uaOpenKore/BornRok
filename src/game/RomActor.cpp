#include "game/RomActor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <cstring>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "render/Shader.hpp"
#include <bimg/bimg.h>
#include <bx/allocator.h>

namespace uaro {

namespace {

constexpr int kMaxBones = 70;  // must match u_bones[70] in vs_romskin


bgfx::ProgramHandle s_prog = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_tex = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_bones = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_lightDir = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_ambient = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_lightColor = BGFX_INVALID_HANDLE;

struct SkinVertex {
    f32 px, py, pz;
    f32 nx, ny, nz;
    f32 u, v;
    f32 w0, w1, w2, w3;
    f32 i0, i1, i2, i3;  // bone indices as floats (WEIGHT/INDICES attribs, float4 layout)
};

// Local TRS -> column-major Mat4 (matches Math.hpp's storage; translation in m[12..14]).
Mat4 trsMatrix(const float t[3], const float q[4], const float s[3]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    Mat4 m = Mat4::identity();
    m.m[0] = (1 - 2 * (yy + zz)) * s[0];
    m.m[1] = (2 * (xy + wz)) * s[0];
    m.m[2] = (2 * (xz - wy)) * s[0];
    m.m[4] = (2 * (xy - wz)) * s[1];
    m.m[5] = (1 - 2 * (xx + zz)) * s[1];
    m.m[6] = (2 * (yz + wx)) * s[1];
    m.m[8] = (2 * (xz + wy)) * s[2];
    m.m[9] = (2 * (yz - wx)) * s[2];
    m.m[10] = (1 - 2 * (xx + yy)) * s[2];
    m.m[12] = t[0];
    m.m[13] = t[1];
    m.m[14] = t[2];
    return m;
}

// Compose node worlds from local TRS matrices, independent of node order (the avatar
// node array does NOT guarantee parents-precede-children — assuming it scattered limbs).
static void composeWorlds(const std::vector<i32>& parents, const std::vector<Mat4>& local,
                          std::vector<Mat4>& world) {
    const usize n = parents.size();
    world.assign(n, Mat4::identity());
    std::vector<u8> done(n, 0);
    for (usize i = 0; i < n; ++i) {
        // Walk up to the first resolved ancestor, then unwind.
        usize stack[64];
        usize top = 0;
        i32 cur = static_cast<i32>(i);
        while (cur >= 0 && !done[static_cast<usize>(cur)] && top < 64) {
            stack[top++] = static_cast<usize>(cur);
            cur = parents[static_cast<usize>(cur)];
        }
        while (top > 0) {
            const usize k = stack[--top];
            const i32 pk = parents[k];
            world[k] = (pk >= 0 && static_cast<usize>(pk) < n) ? world[static_cast<usize>(pk)] * local[k]
                                                               : local[k];
            done[k] = 1;
        }
    }
}

// Pick the model's diffuse: the Material's _MainTex (by pathID) — the _ext bundle ships the
// whole family's textures (poring_ext = Drops/Marin/Poporing/... too; "largest" picked
// Archangeling). Fallbacks: name == model name (with pack-gap aliases), largest non-"_ol".
bgfx::TextureHandle uploadModelTexture(const RomModel& model, const float tint[3] = nullptr) {
    const UnityTexture2D* best = nullptr;
    if (!model.forceTexName.empty()) {  // explicit skin pick (the --view dyes tab)
        for (const auto& t : model.textures) {
            std::string low = t.name;
            for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<u8>(ch)));
            if (low == model.forceTexName) best = &t;
        }
    }
    for (const auto& t : model.textures)
        if (!best && model.mainTexPathId != 0 && t.pathId == model.mainTexPathId) best = &t;
    if (!best) {
        // Known aliases: mobs whose diffuse is missing from the pack (device dumps only hold
        // streamed-in files) but a family skin in the _ext bundle matches the live look.
        static const std::pair<const char*, const char*> kAlias[] = {
            {"poring", "c3_poring"},  // texture/body/poring.unity3d absent; C3 = the pink one
            {"angeling", "archangeling"},   // poring family skins shipped in poring_ext
            {"mastering", "c4_poring"},     // red-orange (S.: "кажется красный")
            {"deviling", "bombporing"},     // the dark one
            {"ghostring", "pouring"},       // pale cyan, closest to the ghost look
            {"spore", "spore_r"},  // the red-cap one (material picked blue PoisonSpore, S.)
            {"poison_spore", "poisonspore"},
            {"thief_bug_larva", "thiefbig"},  // sic; blue-grey base skin (S.)
            {"thief_bug_male", "gthiefbug"},  // green-blue = the male (S.)
            {"thief_bug_female", "thiefbig"},
            {"anacondaq", "blacksnake_r"},  // rust-red (S.: "анаконда вроде рыжая")
            {"side_winder", "blacksnake"},
            // Plant leaves in plant_ext (decoded): Plant = yellow-green, Plant_R = WHITE
            // (silvery, sparkles), Plant_R2 = red-brown. No true red/blue skins shipped.
            {"green_plant", "plant"},
            {"red_plant", "plant_r2"},      // red-brown, closest to red (S.: R was white)
            {"blue_plant", "plant"},
            {"yellow_plant", "plant_r2"},  // like red plant (S.); no true yellow skin
            {"white_plant", "plant_r"},
            {"shining_plant", "plant_r"},
            {"black_mushroom", "mushroom2"},  // white-grey fits the black one (S.)
            {"red_mushroom", "mushroom"},
            {"skel_soldier", "skeleton_soldier"},  // skins in skeleton_archer_ext
            {"skel_worker", "skeleton"},  // white bone + brown leather, closest to "рыжий"
            {"vocal", "rocker_10"},   // brown variant (no blue in the pack)
            {"worm_tail", "stemworm"},  // the white skin (S.)
            {"stem_worm", "wormtail_r"},  // brown
            {"metaller", "metaller"},
            {"deleter", "deleter_petit"},    // petit_ext family skins
            {"nightmare_terror", "n_nightmare_001a_d"},  // the dark remake skin
            {"elder_wilow", "wilow_elder"},
            {"whisper_boss", "whisper_10"},  // Giant Whisper: dark skin (re-dumped _ext)
            {"andre", "ant"},   // worker ant colours: Ant / Ant2 / Ant3
            {"piere", "ant2"},
            {"deniro", "ant3"},
            {"deleter_", "deleter_petit_f"},
        };
        std::string ml = model.name;
        for (char& c : ml) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
        for (const auto& [from, to] : kAlias)
            if (ml == from) ml = to;
        for (const auto& t : model.textures) {
            std::string low = t.name;
            for (char& c : low) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
            if (low == ml) best = &t;
        }
    }
    if (!best)
        for (const auto& t : model.textures) {
            if (t.name.size() >= 3 && t.name.compare(t.name.size() - 3, 3, "_ol") == 0) continue;
            if (!best || t.width * t.height > best->width * best->height) best = &t;
        }
    if (!best) return BGFX_INVALID_HANDLE;
    log::info("RomActor '{}': texture '{}'", model.name, best->name);
    bgfx::TextureFormat::Enum fmt = bgfx::TextureFormat::Unknown;
    switch (best->format) {
        case UnityTexture2D::kASTC_4x4: case UnityTexture2D::kASTC_RGBA_4x4:
            fmt = bgfx::TextureFormat::ASTC4x4; break;
        case UnityTexture2D::kASTC_5x5: case UnityTexture2D::kASTC_RGBA_5x5:
            fmt = bgfx::TextureFormat::ASTC5x5; break;
        case UnityTexture2D::kASTC_6x6: case UnityTexture2D::kASTC_RGBA_6x6:
            fmt = bgfx::TextureFormat::ASTC6x6; break;
        case UnityTexture2D::kASTC_8x8: case UnityTexture2D::kASTC_RGBA_8x8:
            fmt = bgfx::TextureFormat::ASTC8x8; break;
        case UnityTexture2D::kETC2_RGB: fmt = bgfx::TextureFormat::ETC2; break;
        case UnityTexture2D::kETC2_RGBA8: fmt = bgfx::TextureFormat::ETC2A; break;
        case UnityTexture2D::kRGBA32: fmt = bgfx::TextureFormat::RGBA8; break;
        default: break;
    }
    if (fmt == bgfx::TextureFormat::Unknown || best->data.empty()) {
        log::warn("RomActor '{}': unsupported texture format {}", model.name, best->format);
        return BGFX_INVALID_HANDLE;
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps->formats[fmt] & BGFX_CAPS_FORMAT_TEXTURE_2D)
        return bgfx::createTexture2D(
            static_cast<u16>(best->width), static_cast<u16>(best->height), false, 1, fmt,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            bgfx::copy(best->data.data(), static_cast<u32>(best->data.size())));
    // Desktop DX11/GL have NO native ASTC (it's a mobile format): the handle would be
    // created but sample garbage. Decode the blocks to RGBA8 on the CPU via bimg.
    // NB: the output really is RGBA — the earlier R/B swap here was wrong (it made the pupa
    // blue; the "blue poring" it tried to fix was the picker grabbing Archangeling).
    static bx::DefaultAllocator alloc;
    const u32 w = static_cast<u32>(best->width), h = static_cast<u32>(best->height);
    std::vector<u8> rgba(static_cast<usize>(w) * h * 4);
    bimg::imageDecodeToRgba8(&alloc, rgba.data(), best->data.data(), w, h, w * 4,
                             static_cast<bimg::TextureFormat::Enum>(fmt));
    if (tint && (tint[0] != 1.0f || tint[1] != 1.0f || tint[2] != 1.0f))
        for (usize p = 0; p + 3 < rgba.size(); p += 4) {
            rgba[p + 0] = static_cast<u8>(rgba[p + 0] * tint[0]);
            rgba[p + 1] = static_cast<u8>(rgba[p + 1] * tint[1]);
            rgba[p + 2] = static_cast<u8>(rgba[p + 2] * tint[2]);
        }
    log::info("RomActor '{}': ASTC decoded on CPU (no native support)", model.name);
    return bgfx::createTexture2D(static_cast<u16>(w), static_cast<u16>(h), false, 1,
                                 bgfx::TextureFormat::RGBA8,
                                 BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                 bgfx::copy(rgba.data(), static_cast<u32>(rgba.size())));
}

// Upload a UnityMesh as a romskin vertex/index buffer. Unskinned meshes (head/hair parts)
// get w0=1/i0=0 so the shared skinning shader drives them off u_bones[0] alone.
void uploadMesh(const UnityMesh& mesh, bgfx::VertexBufferHandle& vbh,
                bgfx::IndexBufferHandle& ibh) {
    std::vector<SkinVertex> verts(mesh.vertexCount);
    const bool hasN = mesh.normals.size() >= mesh.vertexCount * 3;
    const bool hasUv = mesh.uv0.size() >= mesh.vertexCount * 2;
    const bool hasSkin = mesh.boneWeights.size() >= mesh.vertexCount * 4 &&
                         mesh.boneIndices.size() >= mesh.vertexCount * 4;
    for (u32 i = 0; i < mesh.vertexCount; ++i) {
        SkinVertex& v = verts[i];
        v.px = mesh.positions[i * 3 + 0];
        v.py = mesh.positions[i * 3 + 1];
        v.pz = mesh.positions[i * 3 + 2];
        v.nx = hasN ? mesh.normals[i * 3 + 0] : 0.0f;
        v.ny = hasN ? mesh.normals[i * 3 + 1] : 1.0f;
        v.nz = hasN ? mesh.normals[i * 3 + 2] : 0.0f;
        v.u = hasUv ? mesh.uv0[i * 2 + 0] : 0.0f;
        v.v = hasUv ? mesh.uv0[i * 2 + 1] : 0.0f;
        if (hasSkin) {
            v.w0 = mesh.boneWeights[i * 4 + 0];
            v.w1 = mesh.boneWeights[i * 4 + 1];
            v.w2 = mesh.boneWeights[i * 4 + 2];
            v.w3 = mesh.boneWeights[i * 4 + 3];
            v.i0 = static_cast<f32>(std::min<u32>(mesh.boneIndices[i * 4 + 0], kMaxBones - 1));
            v.i1 = static_cast<f32>(std::min<u32>(mesh.boneIndices[i * 4 + 1], kMaxBones - 1));
            v.i2 = static_cast<f32>(std::min<u32>(mesh.boneIndices[i * 4 + 2], kMaxBones - 1));
            v.i3 = static_cast<f32>(std::min<u32>(mesh.boneIndices[i * 4 + 3], kMaxBones - 1));
        } else {
            v.w0 = 1.0f;
            v.w1 = v.w2 = v.w3 = 0.0f;
            v.i0 = v.i1 = v.i2 = v.i3 = 0.0f;
        }
    }
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Float)
        .end();
    vbh = bgfx::createVertexBuffer(
        bgfx::copy(verts.data(), static_cast<u32>(verts.size() * sizeof(SkinVertex))), layout);
    ibh = bgfx::createIndexBuffer(
        bgfx::copy(mesh.indices.data(), static_cast<u32>(mesh.indices.size() * sizeof(u32))),
        BGFX_BUFFER_INDEX32);
}

}  // namespace

bool RomActor::initShared(Application& app) {
    if (bgfx::isValid(s_prog)) return true;
    s_prog = load_program(app.assetDir(), "vs_romskin", "fs_romskin");
    if (!bgfx::isValid(s_prog)) {
        log::warn("RomActor: romskin shader unavailable -> RoM mobs off");
        return false;
    }
    s_tex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    s_bones = bgfx::createUniform("u_bones", bgfx::UniformType::Mat4, kMaxBones);
    s_lightDir = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    s_ambient = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
    s_lightColor = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
    return true;
}

void RomActor::shutdownShared() {
    for (auto* h : {&s_prog}) {
        if (bgfx::isValid(*h)) bgfx::destroy(*h);
        *h = BGFX_INVALID_HANDLE;
    }
    for (auto* u : {&s_tex, &s_bones, &s_lightDir, &s_ambient, &s_lightColor}) {
        if (bgfx::isValid(*u)) bgfx::destroy(*u);
        *u = BGFX_INVALID_HANDLE;
    }
}

bool RomActor::load(Application& app, RomModel model) {
    destroy();
    if (!initShared(app)) return false;
    model_ = std::move(model);
    const UnityMesh& mesh = model_.mesh;
    if (mesh.vertexCount == 0 || mesh.indices.empty()) return false;
    uploadMesh(mesh, vbh_, ibh_);
    tex_ = uploadModelTexture(model_);
    // Accessory meshes (dokebi's club) share the body's texture atlas; each keeps its own
    // bind poses/bone mapping and skins against the same node worlds (#126).
    for (usize i = 0; i < model_.extras.size(); ++i) {
        Extra ex;
        ex.src = i;
        uploadMesh(model_.extras[i].mesh, ex.vbh, ex.ibh);
        extras_.push_back(ex);
    }
    // Lift + per-model scale from the SKINNED 'wait' frame-0 pose (raw mesh bounds lie for
    // posed rigs: the poring sank and sizes varied wildly between mobs). Target height is
    // sprite-comparable; scale_ replaces the old fixed kRomScale.
    {
        std::vector<Mat4> pal;
        buildPalette(clipFor(Anim::Idle), 0, pal);
        float minY = 1e9f, maxY = -1e9f;
        for (u32 v = 0; v < mesh.vertexCount; ++v) {
            const float px = mesh.positions[v * 3], py = mesh.positions[v * 3 + 1],
                        pz = mesh.positions[v * 3 + 2];
            float oy = 0.0f, wsum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                const float w = mesh.boneWeights.size() > v * 4 + k ? mesh.boneWeights[v * 4 + k] : 0.0f;
                const u32 bi = mesh.boneIndices.size() > v * 4 + k ? mesh.boneIndices[v * 4 + k] : 0;
                if (w <= 0.0f || bi >= pal.size()) continue;
                const float* M = pal[bi].m;
                oy += w * (M[1] * px + M[5] * py + M[9] * pz + M[13]);
                wsum += w;
            }
            if (wsum <= 0.0f) oy = py;
            minY = std::min(minY, oy);
            maxY = std::max(maxY, oy);
        }
        const float height = std::max(0.05f, maxY - minY);
        // +3% height nudge: several mobs sink slightly (the lift comes from wait FRAME 0,
        // other frames dip lower — fabre; S.: "на 3-5% от высоты меша приподнять").
        minY -= height * 0.03f;
        // RoM models are authored in metres and RO units ≈ metres (a player is ~1.7 both
        // ways), so render 1:1. Normalizing every mob to one height made porings person-sized
        // (S. live-QA); only clamp true outliers so a broken pose can't fill the screen.
        scale_ = height > 4.0f ? 4.0f / height : height < 0.2f ? 0.2f / height : 1.0f;
        height_ = height;
        yLift_ = -minY;
        log::info("RomActor '{}' ready ({} verts, tex {}, h {:.2f}, scale {:.2f})", model_.name,
                  mesh.vertexCount, bgfx::isValid(tex_) ? "ok" : "MISSING", height, scale_);
    }
    return true;
}

// Attach a rigid part (head/hair) at the named prefab connect point; see the header.
bool RomActor::attachPart(Application& app, const RomModel& part, const char* pointName,
                          float scale, bool anchorAtNode, const float tint[3], float yLift) {
    (void)app;
    if (part.mesh.vertexCount == 0 || part.mesh.indices.empty()) return false;
    const RomAttachPoint* pt = nullptr;
    for (const auto& ap : model_.attachPoints)
        if (ap.name == pointName) pt = &ap;
    if (!pt) {
        log::warn("RomActor '{}': no attach point '{}'", model_.name, pointName);
        return false;
    }
    // Default-pose node worlds -> nearest node = the animated anchor.
    const usize nNodes = model_.skeleton.parents.size();
    std::vector<Mat4> local(nNodes, Mat4::identity());
    for (usize i = 0; i < nNodes && i < model_.skeleton.defaultPose.size(); ++i) {
        const float* x = model_.skeleton.defaultPose[i].data();
        local[i] = trsMatrix(x, x + 3, x + 7);
    }
    std::vector<Mat4> defWorld;
    composeWorlds(model_.skeleton.parents, local, defWorld);
    i32 node = -1;
    float bestD = 1e9f;
    for (usize i = 0; i < nNodes; ++i) {
        const float dx = defWorld[i].m[12] - pt->t[0], dy = defWorld[i].m[13] - pt->t[1],
                    dz = defWorld[i].m[14] - pt->t[2];
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) { bestD = d; node = static_cast<i32>(i); }
    }
    if (node < 0) return false;
    Part p;
    p.node = node;
    // rest = inv(defWorld[node]) * T(point) * S(scale): per frame the part gets
    // world[node] * rest, i.e. it sits at the point and follows the bone's animation delta.
    const float* a = defWorld[static_cast<usize>(node)].m;
    Mat4 inv = Mat4::identity();  // rigid inverse (R|t): R^T | -R^T t
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) inv.m[c * 4 + r] = a[r * 4 + c];
    for (int r = 0; r < 3; ++r)
        inv.m[12 + r] = -(inv.m[r] * a[12] + inv.m[4 + r] * a[13] + inv.m[8 + r] * a[14]);
    const float* anchor = anchorAtNode ? &defWorld[static_cast<usize>(node)].m[12] : pt->t;
    p.rest = inv * Mat4::translation(Vec3{anchor[0], anchor[1] + yLift, anchor[2]}) *
             Mat4::scaling(Vec3{scale, scale, scale});
    uploadMesh(part.mesh, p.vbh, p.ibh);
    p.tex = uploadModelTexture(part, tint);
    parts_.push_back(p);
    log::info("RomActor '{}': part '{}' at {} (node {})", model_.name, part.name, pointName,
              node);
    return true;
}

bool RomActor::attachSkinnedPart(Application& app, const RomModel& part,
                                 const char* pointName, const float tint[3], float yLift) {
    if (part.mesh.vertexCount == 0 || part.mesh.indices.empty()) return false;
    // No bones -> rigid; anchor it at the requested point (yLift lowers boneless hair onto the
    // scalp — skinned hair below positions from its own verts, so it ignores yLift).
    if (part.mesh.boneNameHashes.empty() || part.mesh.bindPoses.empty())
        return attachPart(app, part, pointName, 1.0f, false, tint, yLift);

    // The hair's bones are its OWN skeleton (no hash overlap with the body rig — verified),
    // so we can't skin it against the body nodes. Instead draw it in its authored rest pose
    // (inverse(bindPose) per bone leaves each vertex where the artist placed it, in the same
    // character space as the body) and rigidly attach the whole thing to the body's head
    // joint so it follows head animation.
    const RomAttachPoint* pt = nullptr;
    for (const auto& ap : model_.attachPoints)
        if (ap.name == pointName) pt = &ap;
    if (!pt) return false;
    const usize nNodes = model_.skeleton.parents.size();
    std::vector<Mat4> local(nNodes, Mat4::identity());
    for (usize i = 0; i < nNodes && i < model_.skeleton.defaultPose.size(); ++i) {
        const float* x = model_.skeleton.defaultPose[i].data();
        local[i] = trsMatrix(x, x + 3, x + 7);
    }
    std::vector<Mat4> defWorld;
    composeWorlds(model_.skeleton.parents, local, defWorld);
    i32 node = -1;
    float bestD = 1e9f;
    for (usize i = 0; i < nNodes; ++i) {
        const float dx = defWorld[i].m[12] - pt->t[0], dy = defWorld[i].m[13] - pt->t[1],
                    dz = defWorld[i].m[14] - pt->t[2];
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) { bestD = d; node = static_cast<i32>(i); }
    }
    if (node < 0) return false;

    Extra e;
    e.standalone = true;
    e.anchorNode = node;
    e.yLift = yLift;  // lower the whole hair onto the scalp (world Y), applied at draw
    e.anchorRestInv = defWorld[static_cast<usize>(node)].inverse();
    // A Unity skinned mesh's vertices are stored in the SHARED bind space (bindPose =
    // inverse(boneBindWorld)), so in the rest pose skinning collapses to identity and the raw
    // vertices already sit where the artist placed them in character space. So every bone's
    // matrix is just the head-joint animation delta (filled at draw time) — no per-bone rest.
    e.restPose.assign(part.mesh.boneNameHashes.size(), {});
    for (auto& m : e.restPose) {
        Mat4 id = Mat4::identity();
        std::memcpy(m.data(), id.m, 64);
    }
    uploadMesh(part.mesh, e.vbh, e.ibh);
    e.tex = uploadModelTexture(part, tint);
    extras_.push_back(std::move(e));
    log::info("RomActor '{}': skinned hair '{}' rest-attached to head node {}", model_.name,
              part.name, node);
    return true;
}

void RomActor::destroy() {
    if (bgfx::isValid(vbh_)) bgfx::destroy(vbh_);
    if (bgfx::isValid(ibh_)) bgfx::destroy(ibh_);
    if (bgfx::isValid(tex_)) bgfx::destroy(tex_);
    vbh_ = BGFX_INVALID_HANDLE;
    ibh_ = BGFX_INVALID_HANDLE;
    tex_ = BGFX_INVALID_HANDLE;
    for (Part& p : parts_) {
        if (bgfx::isValid(p.vbh)) bgfx::destroy(p.vbh);
        if (bgfx::isValid(p.ibh)) bgfx::destroy(p.ibh);
        if (bgfx::isValid(p.tex)) bgfx::destroy(p.tex);
    }
    parts_.clear();
    for (Extra& e : extras_) {
        if (bgfx::isValid(e.vbh)) bgfx::destroy(e.vbh);
        if (bgfx::isValid(e.ibh)) bgfx::destroy(e.ibh);
    }
    extras_.clear();
}

// Bone palette (worldOf(node) x bindPose) for one clip frame; untracked nodes use the
// avatar's m_DefaultPose (identity laid the pupa/lunatic on their side and detached the
// poring's eyes — clips only animate a subset of the nodes).
void RomActor::buildPalette(const RomClip* clip, u32 frame, std::vector<Mat4>& out) {
    const usize nNodes = model_.skeleton.parents.size();
    std::vector<Mat4> local(nNodes, Mat4::identity());
    for (usize i = 0; i < nNodes && i < model_.skeleton.defaultPose.size(); ++i) {
        const float* x = model_.skeleton.defaultPose[i].data();
        local[i] = trsMatrix(x, x + 3, x + 7);
    }
    if (clip && clip->frameCount > 0) {
        for (const RomBoneTrack& tk : clip->tracks) {
            if (tk.avatarNode < 0 || static_cast<usize>(tk.avatarNode) >= nNodes) continue;
            // Per-CHANNEL fallback to the default pose: many clips carry only rotations —
            // zeroing the missing translations stacked every bone at its parent (the pupa
            // collapsed into a disc, the lunatic crumpled; S. live-QA).
            float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
            if (static_cast<usize>(tk.avatarNode) < model_.skeleton.defaultPose.size()) {
                const float* dp = model_.skeleton.defaultPose[tk.avatarNode].data();
                std::memcpy(t, dp, 12);
                std::memcpy(q, dp + 3, 16);
                std::memcpy(s, dp + 7, 12);
            }
            if (tk.t.size() >= (frame + 1) * 3) std::memcpy(t, &tk.t[frame * 3], 12);
            if (tk.q.size() >= (frame + 1) * 4) std::memcpy(q, &tk.q[frame * 4], 16);
            if (tk.s.size() >= (frame + 1) * 3) std::memcpy(s, &tk.s[frame * 3], 12);
            local[static_cast<usize>(tk.avatarNode)] = trsMatrix(t, q, s);
        }
    }
    composeWorlds(model_.skeleton.parents, local, nodeWorld_);  // kept: parts render off it
    out.assign(model_.mesh.bindPoses.size(), Mat4::identity());
    for (usize b = 0; b < out.size(); ++b) {
        Mat4 bind = Mat4::identity();
        std::memcpy(bind.m, model_.mesh.bindPoses[b].data(), 64);
        const i32 node = b < model_.meshBoneToNode.size() ? model_.meshBoneToNode[b] : -1;
        out[b] = (node >= 0 ? nodeWorld_[static_cast<usize>(node)] : Mat4::identity()) * bind;
    }
}

const RomClip* RomActor::clipFor(Anim anim) const {
    const char* name = anim == Anim::Walk     ? "walk"
                       : anim == Anim::Attack ? "attack"
                       : anim == Anim::Hurt   ? "hit"
                       : anim == Anim::Dead   ? "die"
                       : anim == Anim::Sit    ? "sit_down"
                                              : "wait";
    auto it = model_.clips.find(name);
    if (it == model_.clips.end()) it = model_.clips.find("wait");
    return it != model_.clips.end() ? &it->second : nullptr;
}

static float s_lightDirV[4] = {0.3f, 0.8f, 0.3f, 0};
static float s_ambientV[4] = {0.6f, 0.6f, 0.6f, 0};
static float s_lightColorV[4] = {0.6f, 0.6f, 0.6f, 0};

void RomActor::setLight(const float dir[4], const float ambient[4], const float color[4]) {
    std::memcpy(s_lightDirV, dir, 16);
    std::memcpy(s_ambientV, ambient, 16);
    std::memcpy(s_lightColorV, color, 16);
}

void RomActor::render(bgfx::ViewId view, const Vec3& pos, u8 dir, Anim anim, double animTime) {
    if (!ready() || !bgfx::isValid(s_prog)) return;
    const RomClip* clip = clipFor(anim);
    u32 frame = 0;
    if (clip && clip->frameCount > 0) {
        frame = static_cast<u32>(animTime * clip->sampleRate);
        const bool loops = anim == Anim::Walk || anim == Anim::Idle || anim == Anim::Effect;
        frame = loops ? frame % clip->frameCount : std::min(frame, clip->frameCount - 1);
    }
    std::vector<Mat4> pal;
    buildPalette(clip, frame, pal);
    palette_.assign(static_cast<usize>(kMaxBones) * 16, 0.0f);
    for (usize b = 0; b < pal.size() && b < kMaxBones; ++b)
        std::memcpy(&palette_[b * 16], pal[b].m, 64);

    // Place in the world: RO dir 0 = south (+z), clockwise 45-degree steps; scaled to sprite
    // size and lifted so the model's lowest point stands on the cell.
    const Vec3 at{pos.x, pos.y + yLift_ * scale_, pos.z};
    // Models are authored facing +Z = RO south = dir 0, so plain rotY(-dir*45deg). (An
    // earlier +pi "fix" was tuned against the stale packet dir and made everyone walk
    // butt-first once the real movement direction was fed in; S. live-QA.)
    const Mat4 mtx = Mat4::translation(at) *
                     Mat4::rotationY(-static_cast<f32>(dir & 7) * (3.14159265f / 4.0f)) *
                     Mat4::scaling(Vec3{scale_, scale_, scale_});
    bgfx::setTransform(mtx.m);
    bgfx::setUniform(s_bones, palette_.data(), kMaxBones);
    bgfx::setUniform(s_lightDir, s_lightDirV);
    bgfx::setUniform(s_ambient, s_ambientV);
    bgfx::setUniform(s_lightColor, s_lightColorV);
    bgfx::setVertexBuffer(0, vbh_);
    bgfx::setIndexBuffer(ibh_);
    if (bgfx::isValid(tex_)) bgfx::setTexture(0, s_tex, tex_);
    // No backface culling for the first cut: Unity->our winding isn't verified yet, and a
    // wrong cull would erase the whole model. Two-sided is safe; revisit for perf later.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::submit(view, s_prog);

    // Accessory meshes: skin against the same node worlds with their OWN bind poses. Two
    // sources — from the body bundle (model_.extras, shares tex_) or standalone attachments
    // (female hair) carrying their own bind poses / bone map / texture.
    for (const Extra& e : extras_) {
        if (!bgfx::isValid(e.vbh)) continue;
        palette_.assign(static_cast<usize>(kMaxBones) * 16, 0.0f);
        if (e.standalone) {
            // Hair is a self-contained skinned mesh whose bones DON'T overlap the body rig.
            // Its verts live in bind-local space around the origin (like the body verts — both
            // are skinned, not pre-posed); its own bones would pose it about the origin (= the
            // head attach point in hair space). So place that origin at the body head joint's
            // animated world: palette[b] = nodeWorld[head] for every bone (rest = identity).
            Mat4 headWorld =
                (e.anchorNode >= 0 && static_cast<usize>(e.anchorNode) < nodeWorld_.size()
                     ? nodeWorld_[static_cast<usize>(e.anchorNode)]
                     : Mat4::identity());
            headWorld.m[13] += e.yLift;  // drop the hair onto the scalp (CP_4 crown was too high)
            for (usize b = 0; b < e.restPose.size() && b < kMaxBones; ++b)
                std::memcpy(&palette_[b * 16], headWorld.m, 64);
        } else {
            if (e.src >= model_.extras.size()) continue;
            const auto& binds = model_.extras[e.src].mesh.bindPoses;
            const auto& b2n = model_.extras[e.src].boneToNode;
            for (usize b = 0; b < binds.size() && b < kMaxBones; ++b) {
                Mat4 bind = Mat4::identity();
                std::memcpy(bind.m, binds[b].data(), 64);
                const i32 node = b < b2n.size() ? b2n[b] : -1;
                const Mat4 pm =
                    (node >= 0 && static_cast<usize>(node) < nodeWorld_.size()
                         ? nodeWorld_[static_cast<usize>(node)]
                         : Mat4::identity()) *
                    bind;
                std::memcpy(&palette_[b * 16], pm.m, 64);
            }
        }
        bgfx::setTransform(mtx.m);
        bgfx::setUniform(s_bones, palette_.data(), kMaxBones);
        bgfx::setUniform(s_lightDir, s_lightDirV);
        bgfx::setUniform(s_ambient, s_ambientV);
        bgfx::setUniform(s_lightColor, s_lightColorV);
        bgfx::setVertexBuffer(0, e.vbh);
        bgfx::setIndexBuffer(e.ibh);
        const bgfx::TextureHandle et = e.standalone ? e.tex : tex_;
        if (bgfx::isValid(et)) bgfx::setTexture(0, s_tex, et);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(view, s_prog);
    }

    // Rigid parts (head/hair): same program, u_bones[0] = the anchor node's animated world
    // times the precomputed rest offset (part verts carry w0=1/i0=0).
    for (const Part& p : parts_) {
        if (!bgfx::isValid(p.vbh) || p.node < 0 ||
            static_cast<usize>(p.node) >= nodeWorld_.size())
            continue;
        const Mat4 pm = nodeWorld_[static_cast<usize>(p.node)] * p.rest;
        palette_.assign(static_cast<usize>(kMaxBones) * 16, 0.0f);
        std::memcpy(palette_.data(), pm.m, 64);
        bgfx::setTransform(mtx.m);
        bgfx::setUniform(s_bones, palette_.data(), kMaxBones);
        bgfx::setUniform(s_lightDir, s_lightDirV);
        bgfx::setUniform(s_ambient, s_ambientV);
        bgfx::setUniform(s_lightColor, s_lightColorV);
        bgfx::setVertexBuffer(0, p.vbh);
        bgfx::setIndexBuffer(p.ibh);
        if (bgfx::isValid(p.tex)) bgfx::setTexture(0, s_tex, p.tex);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(view, s_prog);
    }
}

}  // namespace uaro
