#include "game/Gr2Models.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "formats/Gr2.hpp"
#include "formats/ImageIO.hpp"
#include "render/Shader.hpp"  // load_program

namespace uaro {

namespace {
// Vertex fed to vs_model (same attribute set as ModelRenderer): pos, uv, colour, normal, tangent.
struct GpuVertex {
    f32 x, y, z;
    f32 u, v;
    u32 abgr;
    f32 nx, ny, nz;
    f32 tx, ty, tz;
};

// Key out the flat BACKGROUND matte of a gr2 flag/prop texture. The artist leaves a large uniform matte
// (the transparent area) whose RGB equals the parchment gold; S.'s webp-x4 upscale drops the alpha, so
// that matte renders as an opaque yellow rectangle behind the banner. Alpha is gone, so we key by colour
// -- but a global colour key would also punch holes in the banner's own light parchment (same colour).
// Instead key every LARGE connected blob of the matte colour: the outer background AND any region walled
// off from the edge by artwork (the yellow behind the spire tassel, S.: "хвост на шпиле"). The banner's
// varied gold never forms a big uniform blob at a tight tolerance, so it stays intact (verified on flag01
// + the tail: banner untouched, both matte regions cleared). Reference = dominant (mode) colour; guarded
// to a real matte (>=12% share) so a texture with none is left be.
// (S.: "флаг в оттенках, сделай только этот цвет прозрачным, флаг не пострадает".)
void keyFlatBackground(Image& img) {
    const int w = static_cast<int>(img.width), h = static_cast<int>(img.height);
    if (w <= 0 || h <= 0 || static_cast<usize>(w) * h * 4 != img.rgba.size()) return;
    std::vector<u32> hist(4096, 0);
    usize opaque = 0;
    for (int i = 0; i < w * h; ++i) {
        const u8* p = &img.rgba[static_cast<usize>(i) * 4];
        if (p[3] < 8) continue;
        ++opaque;
        ++hist[((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4)];
    }
    if (opaque == 0) return;
    u32 best = 0, bestN = 0;
    for (u32 b = 0; b < 4096; ++b)
        if (hist[b] > bestN) { bestN = hist[b]; best = b; }
    if (static_cast<usize>(bestN) * 100 < opaque * 12) return;  // no dominant matte -> leave it
    const int br = static_cast<int>(((best >> 8) & 15) * 16 + 8);
    const int bg = static_cast<int>(((best >> 4) & 15) * 16 + 8);
    const int bb = static_cast<int>((best & 15) * 16 + 8);
    constexpr int kTol = 14;  // tight: the matte is one uniform colour; the banner's gold varies more
    auto matte = [&](int id) {
        const u8* p = &img.rgba[static_cast<usize>(id) * 4];
        return p[3] >= 8 && std::abs(static_cast<int>(p[0]) - br) <= kTol &&
               std::abs(static_cast<int>(p[1]) - bg) <= kTol && std::abs(static_cast<int>(p[2]) - bb) <= kTol;
    };
    // Key every LARGE connected blob of matte colour -- both the edge-connected outer background AND any
    // region enclosed by artwork (e.g. the yellow behind the spire tassel, walled off from the edge by the
    // tail, S.: "хвост на шпиле"). A big uniform blob is the matte; the banner's varied gold never forms
    // one at this tight tolerance, so it stays intact. Small matte-matching specks (< blob threshold) are
    // left alone. Flood each unvisited matte pixel into a component, key it only if it's big enough.
    const usize minBlob = std::max<usize>(16, static_cast<usize>(w) * h / 200);  // >=0.5% of the texture
    std::vector<u8> seen(static_cast<usize>(w) * h, 0);
    std::vector<int> stack, comp;
    for (int s = 0; s < w * h; ++s) {
        if (seen[s]) continue;
        seen[s] = 1;
        if (!matte(s)) continue;
        comp.clear();
        stack.clear();
        stack.push_back(s);
        while (!stack.empty()) {
            const int id = stack.back();
            stack.pop_back();
            comp.push_back(id);
            const int x = id % w, y = id / w;
            const int nb[4] = {x + 1 < w ? id + 1 : -1, x > 0 ? id - 1 : -1,
                               y + 1 < h ? id + w : -1, y > 0 ? id - w : -1};
            for (int k = 0; k < 4; ++k)
                if (nb[k] >= 0 && !seen[nb[k]] && matte(nb[k])) { seen[nb[k]] = 1; stack.push_back(nb[k]); }
        }
        if (comp.size() >= minBlob)
            for (int id : comp) {
                u8* p = &img.rgba[static_cast<usize>(id) * 4];
                p[0] = p[1] = p[2] = 0;
                p[3] = 0;
            }
    }
}
}  // namespace

bool Gr2Models::init(Application& app) {
    program_ = load_program(app.assetDir(), "vs_model", "fs_model");
    if (!bgfx::isValid(program_)) {
        log::warn("gr2: vs_model/fs_model program failed to load; 3D models disabled");
        return false;
    }
    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    nrmSampler_ = bgfx::createUniform("s_nrm", bgfx::UniformType::Sampler);
    nrmParams_ = bgfx::createUniform("u_nrmParams", bgfx::UniformType::Vec4);
    lightDir_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    lightColor_ = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
    ambient_ = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
    fade_ = bgfx::createUniform("u_fade", bgfx::UniformType::Vec4);
    const u8 wpx[4] = {255, 255, 255, 255};
    white_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                   bgfx::copy(wpx, 4));
    const u8 flat[4] = {128, 128, 255, 255};
    flatNrm_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                     bgfx::copy(flat, 4));
    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent, 3, bgfx::AttribType::Float)
        .end();
    return true;
}

void Gr2Models::destroy() {
    for (auto& kv : cache_)
        for (Mesh& m : kv.second.meshes) {
            if (bgfx::isValid(m.vbh)) bgfx::destroy(m.vbh);
            if (bgfx::isValid(m.ibh)) bgfx::destroy(m.ibh);
            if (bgfx::isValid(m.dvbh)) bgfx::destroy(m.dvbh);  // per-frame skinning buffer (#71)
        }
    cache_.clear();
    clips_.clear();
    for (bgfx::TextureHandle t : ownedTex_)
        if (bgfx::isValid(t)) bgfx::destroy(t);
    ownedTex_.clear();
    auto kill = [](bgfx::UniformHandle& u) { if (bgfx::isValid(u)) { bgfx::destroy(u); u = BGFX_INVALID_HANDLE; } };
    kill(sampler_); kill(nrmSampler_); kill(nrmParams_); kill(lightDir_); kill(lightColor_);
    kill(ambient_); kill(fade_);
    if (bgfx::isValid(white_)) { bgfx::destroy(white_); white_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(flatNrm_)) { bgfx::destroy(flatNrm_); flatNrm_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(program_)) { bgfx::destroy(program_); program_ = BGFX_INVALID_HANDLE; }
}

const Gr2Models::Model& Gr2Models::get(Application& app, const std::string& vfsPath) {
    auto it = cache_.find(vfsPath);
    if (it != cache_.end()) return it->second;
    Model& mdl = cache_[vfsPath];
    mdl.tried = true;

    auto bytes = app.vfs().readQuiet(vfsPath);
    if (!bytes) { log::warn("gr2: model not found in VFS: {}", vfsPath); return mdl; }
    auto parsed = gr2Load(*bytes);
    if (!parsed) { log::warn("gr2: could not parse model {}", vfsPath); return mdl; }

    auto texFor = [&](const std::string& name) -> bgfx::TextureHandle {
        if (name.empty()) return white_;
        // gr2 texture names are the artist's basenames; the content maker's renamed HD files live under
        // data/texture/기타마을/ (etc-town, UTF-8) with the SAME " copy" suffix. Map the ones that exist
        // (scan of the content, 2026-07-15) so those models get their texture instead of white. Missing
        // from content (stay white until added): h_guardian01..04, normal_guard01..04, "Treasure chest";
        // "emblem" is the guild emblem (server-driven, separate). data/ + 기타마을/ resolve via the VFS.
        // The renamed HD files live under data/texture/기타마을/ (etc-town). The folder name is Korean, and
        // the content zip stores it UTF-8 on Linux but the mount transcodes it to cp949 on Windows (S.'s
        // build) -- so we must try BOTH byte encodings or the flag renders white on his machine.
        static const char* kEtcUtf8 = "\xea\xb8\xb0\xed\x83\x80\xeb\xa7\x88\xec\x9d\x84/";  // 기타마을/ UTF-8
        static const char* kEtcCp949 = "\xb1\xe2\xc5\xb8\xb8\xb6\xc0\xbb/";                 // 기타마을/ cp949
        std::string leaf;  // an etc-town HD rename that EXISTS in the content (scan 2026-07-15). Only map
                           // the ones actually present -- a wrong guess renders the wrong picture (S.: the
                           // golden guild-flag banner is NOT cursed_flag01). Verified present:
        if (name == "empelium") leaf = "empelium_01";
        else if (name.rfind("guardian", 0) == 0 || name.rfind("g-bang", 0) == 0)
            leaf = name;  // "guardian copy"/"guardian2 copy"/.../"g-bang copy"
        // NOTE: "flag01" (golden guild-flag cloth) and "emblem" (the guild emblem panel) have NO matching
        // file in the current content packs -- the golden banner texture the original client shows isn't
        // under data/texture/기타마을/ or any pack we mount (searched texture_x4/data/model/gro/UaRO.grf).
        // So DON'T alias them to a placeholder (cursed_flag01 was wrong). Fall through to the raw-name
        // candidates below: if the real "flag01"/"emblem" texture is in one of S.'s GRFs (base data.grf we
        // don't have a copy of), the reroot fix will resolve it; otherwise white until content adds it.
        // The "emblem" mesh is separately overridden by the server-sent guild emblem in draw() (#5).

        // Build the candidate resolved paths. Known-good alias under both folder encodings first, then the
        // raw gr2 name under a few likely RO texture roots (models reference a bare basename; the file may
        // sit directly under data/texture/ or in a subfolder). readQuietAnySource (NOT readQuiet): textures
        // live in texture_x4.zip (UaRO-tagged); this path categorizes as "Other" whose content-mode may be
        // GRO, so the normal chain would skip the UaRO zip. Any-source ignores the mode. QUIET: a missing
        // name is expected and must NOT spam "missing resource" (S.). White fallback.
        std::vector<std::string> candidates;
        if (!leaf.empty()) {
            candidates.push_back(std::string(kEtcUtf8) + leaf);
            candidates.push_back(std::string(kEtcCp949) + leaf);
        }
        candidates.push_back(name);                            // data/texture/<name>
        candidates.push_back("3dmob/" + name);                 // data/texture/3dmob/<name>
        candidates.push_back(std::string(kEtcUtf8) + name);    // etc-town, raw name (UTF-8)
        candidates.push_back(std::string(kEtcCp949) + name);   // etc-town, raw name (cp949)
        std::optional<std::vector<u8>> tb;
        std::string resolved, usedExt;
        for (const std::string& cand : candidates) {
            for (const char* ext : {".webp", ".png", ".tga", ".bmp", ""}) {
                tb = app.vfs().readQuietAnySource("data/texture/" + cand + ext);
                if (tb) { resolved = cand; usedExt = ext; break; }
            }
            if (tb) break;
        }
        if (resolved.empty()) resolved = candidates.front();
        // Diagnostic (once per unique texture, models are cached): tells us if the alias resolved (S.:
        // "текстур нету"). Shows the gr2 name, the resolved path and whether a file loaded.
        log::info("gr2 tex(any-source) '{}' -> 'data/texture/{}'{} : {}", name, resolved, usedExt,
                  tb ? "LOADED" : "NOT FOUND");
        if (!tb) return white_;
        auto img = decodeImage(*tb);
        if (!img || !img->valid()) return white_;
        // RO model textures use magenta (0xff00ff) as the transparent colour key (no alpha channel in
        // the source .bmp). The flag cloth/pole textures carry a magenta surround that must be punched
        // out or it renders as a pink border (S.: розовая рамка вокруг флага). Same shared key+despill
        // the ground/UI/RSM paths use; fs_model then discards a<0.5, so no blend/Z pollution.
        keyAndDespillMagenta(*img);
        const u64 flags = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
        bgfx::TextureHandle h = bgfx::createTexture2D(
            static_cast<u16>(img->width), static_cast<u16>(img->height), false, 1,
            bgfx::TextureFormat::RGBA8, flags,
            bgfx::copy(img->rgba.data(), static_cast<u32>(img->rgba.size())));
        if (bgfx::isValid(h)) ownedTex_.push_back(h);
        return bgfx::isValid(h) ? h : white_;
    };

    // Model base path without the .gr2 extension, for TGA sidecar lookup below.
    std::string modelBase = vfsPath;
    if (modelBase.size() >= 4) {
        std::string e = modelBase.substr(modelBase.size() - 4);
        for (char& c : e) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (e == ".gr2") modelBase.resize(modelBase.size() - 4);
    }
    // Bink-encoded embedded textures (256x256 flag cloth) can't be decoded in-client; the offline
    // gr2texbake tool decodes them via granny2.dll into a TGA next to the model: <model>.<texname>.tga.
    // Load that if present (any content source; magenta-keyed like the rest).
    auto loadSidecar = [&](const std::string& tname) -> bgfx::TextureHandle {
        if (tname.empty()) return BGFX_INVALID_HANDLE;
        for (const char* ext : {".tga", ".png", ".webp"}) {
            auto tb = app.vfs().readQuietAnySource(modelBase + "." + tname + ext);
            if (!tb) continue;
            auto img = decodeImage(*tb);
            if (!img || !img->valid()) continue;
            keyAndDespillMagenta(*img);
            keyFlatBackground(*img);  // drop the flat parchment matte the webp-x4 upscale flattened opaque
            const u64 flags = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
            bgfx::TextureHandle h = bgfx::createTexture2D(
                static_cast<u16>(img->width), static_cast<u16>(img->height), false, 1,
                bgfx::TextureFormat::RGBA8, flags,
                bgfx::copy(img->rgba.data(), static_cast<u32>(img->rgba.size())));
            if (bgfx::isValid(h)) { ownedTex_.push_back(h); return h; }
        }
        return BGFX_INVALID_HANDLE;
    };

    // Upload a texture whose pixels are EMBEDDED in the .gr2 (RO flag/guardian skins; FromFileName has
    // no GRF match, S.). Already RGBA8 from gr2Load; run the shared magenta key (harmless if the texture
    // carries real alpha) so any 0xff00ff surround drops out like the name-loaded path.
    auto uploadEmbedded = [&](const Gr2Texture& gt) -> bgfx::TextureHandle {
        Image img;
        img.width = gt.width;
        img.height = gt.height;
        img.rgba = gt.rgba;
        if (!img.valid()) return white_;
        keyAndDespillMagenta(img);
        const u64 flags = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
        bgfx::TextureHandle h = bgfx::createTexture2D(
            static_cast<u16>(img.width), static_cast<u16>(img.height), false, 1,
            bgfx::TextureFormat::RGBA8, flags,
            bgfx::copy(img.rgba.data(), static_cast<u32>(img.rgba.size())));
        if (bgfx::isValid(h)) ownedTex_.push_back(h);
        return bgfx::isValid(h) ? h : white_;
    };

    // Centre of the banner cloth (flag01 mesh) in swapped space (x = horizontal, y = height), so the
    // emblem panel can be recentred onto it (S.: "логотип нужно отцентровать" — it sits low/right).
    f32 clothCx = 0.0f, clothCy = 0.0f;
    bool haveCloth = false;
    for (const Gr2Mesh& gm : parsed->meshes) {
        if (gm.texture != "flag01" || gm.positions.size() < 3) continue;
        f64 sx = 0, sy = 0;
        const usize nv = gm.positions.size() / 3;
        for (usize i = 0; i < nv; ++i) { sx += gm.positions[i * 3 + 0]; sy += gm.positions[i * 3 + 2]; }
        clothCx = static_cast<f32>(sx / nv);
        clothCy = static_cast<f32>(sy / nv);
        haveCloth = true;
        break;
    }

    for (const Gr2Mesh& gm : parsed->meshes) {
        const usize vcount = gm.positions.size() / 3;
        if (vcount == 0 || gm.indices.empty()) continue;
        std::vector<GpuVertex> verts(vcount);
        for (usize i = 0; i < vcount; ++i) {
            GpuVertex& v = verts[i];
            // 3ds Max / Granny models are Z-up; RO world is Y-up -> swap (x,y,z)->(x,z,-y). The per-actor
            // FACING rotation (each flag in a ring faces outward) is applied by the caller's world matrix
            // (Mat4::rotationY by the actor dir), NOT baked here — so it varies per flag (S.: "смотреть в
            // разные стороны"). Scale/position also come from that matrix.
            v.x = gm.positions[i * 3 + 0];
            v.y = gm.positions[i * 3 + 2];
            v.z = -gm.positions[i * 3 + 1];
            if (gm.uvs.size() >= (i + 1) * 2) { v.u = gm.uvs[i * 2 + 0]; v.v = gm.uvs[i * 2 + 1]; }
            else { v.u = v.v = 0.0f; }
            v.abgr = 0xffffffffu;
            if (gm.normals.size() >= (i + 1) * 3) {
                v.nx = gm.normals[i * 3 + 0]; v.ny = gm.normals[i * 3 + 2]; v.nz = -gm.normals[i * 3 + 1];
            } else { v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f; }
            v.tx = v.ty = v.tz = 0.0f;
        }
        // Emblem panel: (1) halve its size (S.: "уменьшить в 2 раза"), (2) recentre it onto the banner
        // cloth centre (S.: "логотип отцентровать" — it sat low/right). Scale about the centroid, then
        // translate the centroid horizontally + vertically to the cloth centre (keep its own depth so it
        // stays in front of the cloth).
        if (gm.texture == "emblem" && !verts.empty()) {
            f32 cx = 0, cy = 0, cz = 0;
            for (const GpuVertex& v : verts) { cx += v.x; cy += v.y; cz += v.z; }
            const f32 inv = 1.0f / static_cast<f32>(verts.size());
            cx *= inv; cy *= inv; cz *= inv;
            const f32 dx = haveCloth ? clothCx - cx : 0.0f;
            const f32 dy = haveCloth ? clothCy - cy : 0.0f;
            for (GpuVertex& v : verts) {
                v.x = cx + (v.x - cx) * 0.5f + dx;
                v.y = cy + (v.y - cy) * 0.5f + dy;
                v.z = cz + (v.z - cz) * 0.5f;
            }
        }
        Mesh mesh;
        mesh.vbh = bgfx::createVertexBuffer(
            bgfx::copy(verts.data(), static_cast<u32>(verts.size() * sizeof(GpuVertex))), layout_);
        mesh.ibh = bgfx::createIndexBuffer(
            bgfx::copy(gm.indices.data(), static_cast<u32>(gm.indices.size() * sizeof(u16))));
        mesh.idxCount = static_cast<u32>(gm.indices.size());
        // Retain CPU-side skinning source so drawAnimated() can re-pose the mesh (#71). Positions/normals
        // are kept in NATIVE (Z-up) space (the bones live there); the axis swap to Y-up is re-applied per
        // frame after skinning. `baseVerts` is the exact bind-pose vertex byte template (uv/colour/tangent
        // reused as-is). A rigid mesh (1 bone binding, no weights) rides `rigidBone`.
        mesh.skinned = (gm.boneWeights.size() == vcount * 4 && gm.boneIndices.size() == vcount * 4);
        mesh.rigidBone = gm.rigidBone;
        mesh.bindings = gm.boneBindings;
        if (mesh.skinned || mesh.rigidBone >= 0) {
            mesh.baseVerts.assign(reinterpret_cast<const u8*>(verts.data()),
                                  reinterpret_cast<const u8*>(verts.data()) + verts.size() * sizeof(GpuVertex));
            mesh.posNative = gm.positions;   // native x,y,z per vertex
            mesh.nrmNative = gm.normals;
            mesh.boneIdx = gm.boneIndices;
            mesh.boneWeight = gm.boneWeights;
        }
        // Texture precedence: EMBEDDED pixels decoded in-client (emblem + raw skins) -> baked TGA sidecar
        // (Bink cloth the client can't decode, produced by gr2texbake) -> GRF name lookup (external skins).
        bgfx::TextureHandle chosen = BGFX_INVALID_HANDLE;
        if (gm.texIndex >= 0 && static_cast<usize>(gm.texIndex) < parsed->textures.size() &&
            !parsed->textures[gm.texIndex].rgba.empty())
            chosen = uploadEmbedded(parsed->textures[gm.texIndex]);
        if (!bgfx::isValid(chosen)) chosen = loadSidecar(gm.texture);
        if (!bgfx::isValid(chosen)) chosen = texFor(gm.texture);
        mesh.tex = chosen;
        mesh.emblem = (gm.texture == "emblem");  // guild-flag cloth -> overridable by the guild emblem (#5)
        mdl.meshes.push_back(mesh);
    }
    mdl.bones = parsed->bones;  // skeleton for #71 skeletal animation (empty on a static prop)
    log::info("gr2: loaded model {} ({} mesh, {} bones)", vfsPath, mdl.meshes.size(), mdl.bones.size());
    return mdl;
}

// Resolve + cache the animation clip for (model, action). The clip files sit at
// data/model/3dmob_bone/<group>_<motion>.gr2 where <group> is the model's trailing "_N" (aguardian90_8
// -> group 8) — RO groups mob skeletons this way. Some motions only ship as a shared clip (1_attack,
// 2_damage, 2_dead), so we try the group-specific file then the common fallback. A parsed-once result
// (including "no clip" as an empty optional) is cached by clip path so a missing file isn't reparsed.
const Gr2Animation* Gr2Models::clipFor(Application& app, const std::string& modelPath, Gr2Action action) {
    if (action == Gr2Action::Idle) return nullptr;  // idle = the model's own bind pose
    // Extract the skeleton group: the digits after the last '_' in the basename (before ".gr2").
    std::string base = modelPath;
    if (const auto s = base.find_last_of('/'); s != std::string::npos) base = base.substr(s + 1);
    if (base.size() >= 4 && base.compare(base.size() - 4, 4, ".gr2") == 0) base.resize(base.size() - 4);
    std::string group;
    if (const auto u = base.find_last_of('_'); u != std::string::npos) {
        group = base.substr(u + 1);
        for (char c : group) if (c < '0' || c > '9') { group.clear(); break; }
    }
    const char* motion = action == Gr2Action::Walk ? "move" : action == Gr2Action::Attack ? "attack"
                       : action == Gr2Action::Hurt ? "damage" : "dead";
    std::vector<std::string> tries;
    if (!group.empty()) tries.push_back("data/model/3dmob_bone/" + group + "_" + motion + ".gr2");
    // Shared fallbacks seen in RO content.
    if (action == Gr2Action::Attack) tries.push_back("data/model/3dmob_bone/1_attack.gr2");
    else if (action == Gr2Action::Hurt) tries.push_back("data/model/3dmob_bone/2_damage.gr2");
    else if (action == Gr2Action::Dead) tries.push_back("data/model/3dmob_bone/2_dead.gr2");
    for (const std::string& path : tries) {
        auto it = clips_.find(path);
        if (it == clips_.end()) {
            auto bytes = app.vfs().readQuiet(path);
            std::optional<Gr2Animation> anim;
            if (bytes) anim = gr2LoadAnimation(*bytes);
            if (bytes && anim) log::info("gr2: loaded clip {} ({} tracks, {:.2f}s)", path,
                                         anim->tracks.size(), anim->duration);
            it = clips_.emplace(path, std::move(anim)).first;
        }
        if (it->second) return &*it->second;
    }
    return nullptr;
}

void Gr2Models::draw(Application& app, bgfx::ViewId view, const std::string& vfsPath, const float world[16],
                     bgfx::TextureHandle emblemTex) {
    if (!bgfx::isValid(program_)) return;
    const Model& mdl = get(app, vfsPath);
    if (mdl.meshes.empty()) return;

    // Lit fullbright-ish for now (ambient 1, no directional) so the model reads clearly; the RSW sun
    // lighting can be wired in once S. confirms the scale/orientation on his rebuild.
    const f32 np[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // hasNrm=0
    const f32 ld[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const f32 lc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const f32 amb[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const f32 fade[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    const u64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                      BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
    for (const Mesh& m : mdl.meshes) {
        // The emblem panel shows the OWNING guild's server-sent emblem. With no owner (unowned flag,
        // guildId 0) there is nothing to show -- draw nothing rather than the tiny embedded default
        // stretched into a blurry blob (S.: "что за синий круг? тут должна быть эмблема, а не пятно").
        // The banner cloth already renders its own centre, so the flag still looks right.
        if (m.emblem && !bgfx::isValid(emblemTex)) continue;
        bgfx::setTransform(world);
        bgfx::setVertexBuffer(0, m.vbh);
        bgfx::setIndexBuffer(m.ibh, 0, m.idxCount);
        // Guild-flag cloth: the owning guild's server-sent emblem overrides the placeholder texture (#5).
        const bgfx::TextureHandle diff = (m.emblem && bgfx::isValid(emblemTex))
                                             ? emblemTex
                                             : (bgfx::isValid(m.tex) ? m.tex : white_);
        bgfx::setTexture(0, sampler_, diff);
        bgfx::setTexture(1, nrmSampler_, flatNrm_);
        bgfx::setUniform(nrmParams_, np);
        bgfx::setUniform(lightDir_, ld);
        bgfx::setUniform(lightColor_, lc);
        bgfx::setUniform(ambient_, amb);
        bgfx::setUniform(fade_, fade);
        bgfx::setState(state);
        bgfx::submit(view, program_);
    }
}

void Gr2Models::drawAnimated(Application& app, bgfx::ViewId view, const std::string& vfsPath,
                             Gr2Action action, float timeSec, const float world[16],
                             bgfx::TextureHandle emblemTex) {
    if (!bgfx::isValid(program_)) return;
    // Mutable ref (get() caches into cache_; drawAnimated updates each mesh's per-frame dynamic VB).
    Model& mdl = const_cast<Model&>(get(app, vfsPath));
    if (mdl.meshes.empty()) return;
    const Gr2Animation* clip = mdl.bones.empty() ? nullptr : clipFor(app, vfsPath, action);
    if (!clip) { draw(app, view, vfsPath, world, emblemTex); return; }  // no skeleton/clip -> bind pose

    // Sample the clip into skin matrices (skin[b] = world[b] * invBind[b], row-major column-vector).
    std::vector<f32> skin;
    if (!gr2SamplePose(mdl.bones, *clip, timeSec, skin)) { draw(app, view, vfsPath, world, emblemTex); return; }

    const f32 np[4] = {0, 0, 0, 0}, ld[4] = {0, 1, 0, 0}, lc[4] = {0, 0, 0, 0};
    const f32 amb[4] = {1, 1, 1, 1}, fade[4] = {1, 0, 0, 0};
    const u64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                      BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
    const usize nbones = mdl.bones.size();

    // Skin one native (Z-up) vector by the 4 weighted bone matrices of vertex `vi`; caller axis-swaps.
    auto skinVec = [&](const Mesh& m, usize vi, const f32* src, f32 out[3], bool isNormal) {
        auto mul3 = [&](const f32* mm, const f32* v, f32* o) {
            o[0] = mm[0] * v[0] + mm[1] * v[1] + mm[2] * v[2] + (isNormal ? 0.0f : mm[3]);
            o[1] = mm[4] * v[0] + mm[5] * v[1] + mm[6] * v[2] + (isNormal ? 0.0f : mm[7]);
            o[2] = mm[8] * v[0] + mm[9] * v[1] + mm[10] * v[2] + (isNormal ? 0.0f : mm[11]);
        };
        if (m.rigidBone >= 0 && static_cast<usize>(m.rigidBone) < nbones) {  // rigid: single bone
            mul3(&skin[m.rigidBone * 16], src, out);
            return;
        }
        f32 acc[3] = {0, 0, 0}, wsum = 0;
        for (int k = 0; k < 4; ++k) {
            const f32 w = m.boneWeight[vi * 4 + k];
            if (w <= 0.0f) continue;
            const int local = m.boneIdx[vi * 4 + k];
            const int sk = (local >= 0 && static_cast<usize>(local) < m.bindings.size()) ? m.bindings[local] : -1;
            if (sk < 0 || static_cast<usize>(sk) >= nbones) continue;
            f32 o[3]; mul3(&skin[sk * 16], src, o);
            for (int c = 0; c < 3; ++c) acc[c] += w * o[c];
            wsum += w;
        }
        if (wsum < 1e-4f) { out[0] = src[0]; out[1] = src[1]; out[2] = src[2]; }
        else { out[0] = acc[0]; out[1] = acc[1]; out[2] = acc[2]; }
    };

    for (Mesh& m : mdl.meshes) {
        if (m.emblem && !bgfx::isValid(emblemTex)) continue;
        // Non-skinned static mesh in an animated model (rare) -> draw its static buffer unchanged.
        const bool canSkin = (m.skinned || m.rigidBone >= 0) && !m.baseVerts.empty();
        if (!canSkin) {
            bgfx::setTransform(world);
            bgfx::setVertexBuffer(0, m.vbh);
            bgfx::setIndexBuffer(m.ibh, 0, m.idxCount);
        } else {
            constexpr usize kStride = 48;  // sizeof(GpuVertex): pos@0, normal@24
            const usize nv = m.baseVerts.size() / kStride;
            const bgfx::Memory* mem = bgfx::alloc(static_cast<u32>(m.baseVerts.size()));
            std::memcpy(mem->data, m.baseVerts.data(), m.baseVerts.size());
            for (usize i = 0; i < nv; ++i) {
                u8* v = mem->data + i * kStride;
                f32 pn[3] = {m.posNative[i * 3], m.posNative[i * 3 + 1], m.posNative[i * 3 + 2]};
                f32 posed[3]; skinVec(m, i, pn, posed, false);
                // native Z-up -> RO Y-up: (x, z, -y) (same swap the static build applies).
                const f32 sx = posed[0], sy = posed[2], sz = -posed[1];
                std::memcpy(v + 0, &sx, 4); std::memcpy(v + 4, &sy, 4); std::memcpy(v + 8, &sz, 4);
                if (m.nrmNative.size() >= (i + 1) * 3) {
                    f32 nn[3] = {m.nrmNative[i * 3], m.nrmNative[i * 3 + 1], m.nrmNative[i * 3 + 2]};
                    f32 pnrm[3]; skinVec(m, i, nn, pnrm, true);
                    const f32 nx = pnrm[0], ny = pnrm[2], nz = -pnrm[1];
                    std::memcpy(v + 24, &nx, 4); std::memcpy(v + 28, &ny, 4); std::memcpy(v + 32, &nz, 4);
                }
            }
            if (!bgfx::isValid(m.dvbh))
                m.dvbh = bgfx::createDynamicVertexBuffer(static_cast<u32>(nv), layout_);
            bgfx::update(m.dvbh, 0, mem);
            bgfx::setTransform(world);
            bgfx::setVertexBuffer(0, m.dvbh);
            bgfx::setIndexBuffer(m.ibh, 0, m.idxCount);
        }
        const bgfx::TextureHandle diff = (m.emblem && bgfx::isValid(emblemTex))
                                             ? emblemTex
                                             : (bgfx::isValid(m.tex) ? m.tex : white_);
        bgfx::setTexture(0, sampler_, diff);
        bgfx::setTexture(1, nrmSampler_, flatNrm_);
        bgfx::setUniform(nrmParams_, np);
        bgfx::setUniform(lightDir_, ld);
        bgfx::setUniform(lightColor_, lc);
        bgfx::setUniform(ambient_, amb);
        bgfx::setUniform(fade_, fade);
        bgfx::setState(state);
        bgfx::submit(view, program_);
    }
}

}  // namespace uaro
