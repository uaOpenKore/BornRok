#include "game/ModelRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "formats/ImageIO.hpp"
#include "render/SamplerFilter.hpp"
#include "render/Shader.hpp"
#include "world/ModelMesh.hpp"

namespace uaro {

namespace {
struct GpuVertex {
    f32 x, y, z;
    f32 u, v;
    u32 abgr;
    f32 nx, ny, nz;  // model-space normal (#107 normal mapping)
    f32 tx, ty, tz;  // model-space tangent
};

// 3x3 RSM node matrix (column-major) -> 4x4.
Mat4 mat3to4(const f32* m3) {
    Mat4 m = Mat4::identity();
    m.m[0] = m3[0]; m.m[1] = m3[1]; m.m[2] = m3[2];
    m.m[4] = m3[3]; m.m[5] = m3[4]; m.m[6] = m3[5];
    m.m[8] = m3[6]; m.m[9] = m3[7]; m.m[10] = m3[8];
    return m;
}

// Unit quaternion (x,y,z,w) -> 4x4 rotation, same column-major layout as mat3to4 (glMatrix
// convention, matching roBrowser's RSM node animation). Used to spin animated nodes (propellers).
Mat4 quatToMat4(const f32* q) {
    const f32 x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 m = Mat4::identity();
    m.m[0] = 1 - 2 * (y * y + z * z); m.m[1] = 2 * (x * y + w * z);     m.m[2] = 2 * (x * z - w * y);
    m.m[4] = 2 * (x * y - w * z);     m.m[5] = 1 - 2 * (x * x + z * z); m.m[6] = 2 * (y * z + w * x);
    m.m[8] = 2 * (x * z + w * y);     m.m[9] = 2 * (y * z - w * x);     m.m[10] = 1 - 2 * (x * x + y * y);
    return m;
}

// Spherical interpolation of two quaternions (shortest arc), nlerp fallback when near-parallel.
void quatSlerp(const f32* a, const f32* b, f32 t, f32* out) {
    f32 d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    f32 bb[4] = {b[0], b[1], b[2], b[3]};
    if (d < 0.0f) { d = -d; bb[0] = -bb[0]; bb[1] = -bb[1]; bb[2] = -bb[2]; bb[3] = -bb[3]; }
    f32 s0, s1;
    if (d > 0.9995f) { s0 = 1.0f - t; s1 = t; }  // nearly parallel -> linear
    else {
        const f32 th = std::acos(d), st = std::sin(th);
        s0 = std::sin((1.0f - t) * th) / st;
        s1 = std::sin(t * th) / st;
    }
    for (int i = 0; i < 4; ++i) out[i] = s0 * a[i] + s1 * bb[i];
    const f32 l = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    if (l > 1e-8f) for (int i = 0; i < 4; ++i) out[i] /= l;
}

// Interpolated rotation of a keyframed node at wall-clock `timeSec`. roBrowser advances RSM node
// animation by frame = (now_ms % animLength); keyframe .frame values are those ms positions.
Mat4 animRotAt(const std::vector<RsmRotKey>& keys, i32 animLen, double timeSec) {
    if (keys.empty()) return Mat4::identity();
    if (keys.size() == 1 || animLen <= 0) return quatToMat4(keys[0].q);
    double tf = std::fmod(timeSec * 1000.0, static_cast<double>(animLen));
    if (tf < 0) tf += animLen;
    usize k1 = 0;
    while (k1 < keys.size() && static_cast<double>(keys[k1].frame) < tf) ++k1;
    if (k1 == 0) return quatToMat4(keys.front().q);
    if (k1 >= keys.size()) return quatToMat4(keys.back().q);
    const usize k0 = k1 - 1;
    const double f0 = keys[k0].frame, f1 = keys[k1].frame;
    const f32 frac = (f1 > f0) ? static_cast<f32>((tf - f0) / (f1 - f0)) : 0.0f;
    f32 q[4];
    quatSlerp(keys[k0].q, keys[k1].q, frac, q);
    return quatToMat4(q);
}

// Tightly-packed RGBA mip chain (2x2 box filter), so opaque building textures can use
// mipmaps + anisotropic filtering instead of shimmering at distance (S.: trilinear on the map).
std::vector<u8> buildMipChain(const std::vector<u8>& rgba0, u32 w, u32 h) {
    std::vector<u8> out(rgba0), prev(rgba0);
    u32 pw = w, ph = h;
    while (pw > 1 || ph > 1) {
        const u32 nw = std::max<u32>(1, pw >> 1), nh = std::max<u32>(1, ph >> 1);
        std::vector<u8> cur(static_cast<usize>(nw) * nh * 4);
        for (u32 y = 0; y < nh; ++y) {
            const u32 y0 = std::min(ph - 1, y * 2), y1 = std::min(ph - 1, y * 2 + 1);
            for (u32 x = 0; x < nw; ++x) {
                const u32 x0 = std::min(pw - 1, x * 2), x1 = std::min(pw - 1, x * 2 + 1);
                for (int c = 0; c < 4; ++c)
                    cur[(y * nw + x) * 4 + c] = static_cast<u8>(
                        (prev[(y0 * pw + x0) * 4 + c] + prev[(y0 * pw + x1) * 4 + c] +
                         prev[(y1 * pw + x0) * 4 + c] + prev[(y1 * pw + x1) * 4 + c]) /
                        4);
            }
        }
        out.insert(out.end(), cur.begin(), cur.end());
        prev.swap(cur);
        pw = nw;
        ph = nh;
    }
    return out;
}

bgfx::TextureHandle uploadBmp(const std::vector<u8>& bytes, const char* dbgName = nullptr) {
    auto img = decodeImage(bytes);  // BMP/PNG/TGA/JPG by magic (model textures may ship as PNG)
    if (!img || !img->valid()) return BGFX_INVALID_HANDLE;
    // RO magenta colour key + rim despill (shared with the ground path): punch out the magenta-
    // dominant transparent key AND fade the anti-aliased pink halo a content maker's PNG foliage
    // leaves behind (S.: "вокруг листьев розовый контур"). Edge-local, so real purples survive.
    const usize keyed = keyAndDespillMagenta(*img);
    // Diagnostic (S.: magenta persists on foliage): prove which model textures hit the despill and how
    // many key texels were punched out. Also scan for OPAQUE magenta-ish pixels that SURVIVED all passes
    // and report the worst one's RGB — that pins the exact colour of the leftover pink so the key/despill
    // thresholds can be matched to it instead of guessed.
    if (dbgName) {
        usize resid = 0;
        int wr = 0, wg = 0, wb = 0, worst = 0;
        const usize np = static_cast<usize>(img->width) * img->height;
        for (usize i = 0; i < np; ++i) {
            const u8* p = &img->rgba[i * 4];
            if (p[3] < 128) continue;  // discarded by the a<0.5 cutout anyway
            const int sp = std::min<int>(p[0], p[2]) - static_cast<int>(p[1]);
            if (sp > 15) {
                ++resid;
                if (sp > worst) { worst = sp; wr = p[0]; wg = p[1]; wb = p[2]; }
            }
        }
        log::info("modeltex {} {}x{} keyed={} residPink={} worst=({},{},{})", dbgName, img->width,
                  img->height, keyed, resid, wr, wg, wb);
    }
    const u16 w = static_cast<u16>(img->width), h = static_cast<u16>(img->height);
    // Alpha-cutout textures (foliage, flags) stay POINT-sampled with no mips: any blend bleeds
    // their thin geometry below the discard cutoff so it frays at distance ("only stumps"). Opaque
    // building textures take mips + anisotropic filtering so they stay crisp far away (S.).
    if (keyed > 8) {
        const u64 flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_POINT;
        return bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags,
                                     bgfx::copy(img->rgba.data(),
                                                static_cast<u32>(img->rgba.size())));
    }
    const u64 flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_ANISOTROPIC |
                      BGFX_SAMPLER_MAG_ANISOTROPIC;
    const std::vector<u8> chain = buildMipChain(img->rgba, img->width, img->height);
    return bgfx::createTexture2D(w, h, true, 1, bgfx::TextureFormat::RGBA8, flags,
                                 bgfx::copy(chain.data(), static_cast<u32>(chain.size())));
}
} // namespace

bool ModelRenderer::load(Application& app, const MapData& map) {
    program_ = load_program(app.assetDir(), "vs_model", "fs_model");
    if (!bgfx::isValid(program_)) {
        log::warn("ModelRenderer: model shader unavailable; objects not drawn");
        return false;
    }
    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    fade_ = bgfx::createUniform("u_fade", bgfx::UniformType::Vec4);
    nrmSampler_ = bgfx::createUniform("s_nrm", bgfx::UniformType::Sampler);
    nrmParams_ = bgfx::createUniform("u_nrmParams", bgfx::UniformType::Vec4);
    lightDir_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    lightColor_ = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
    ambient_ = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
    const u8 whitePx[4] = {255, 255, 255, 255};
    white_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                   bgfx::copy(whitePx, 4));
    const u8 flatPx[4] = {128, 128, 255, 255};  // tangent-space "up" = no perturbation
    flatNrm_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                     bgfx::copy(flatPx, 4));

    // RSW "sun": lat/long -> world direction TO the light (roBrowser convention), plus its
    // diffuse/ambient colours. Drives the normal-mapped lighting (#107).
    {
        const RswLight& L = map.rsw.light();
        const f32 lon = static_cast<f32>(L.longitude) * 3.14159265f / 180.0f;
        const f32 lat = static_cast<f32>(L.latitude) * 3.14159265f / 180.0f;
        f32 dx = std::cos(lat) * std::sin(lon);
        f32 dy = std::cos(lon);
        f32 dz = std::sin(lat) * std::sin(lon);
        const f32 dl = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dl > 1e-6f) { dx /= dl; dy /= dl; dz /= dl; }
        lightDirV_[0] = dx; lightDirV_[1] = dy; lightDirV_[2] = dz; lightDirV_[3] = 0.0f;
        lightColorV_[0] = L.diffuse[0]; lightColorV_[1] = L.diffuse[1]; lightColorV_[2] = L.diffuse[2];
        // Clamp very high indoor ambient (prt_in 0.8) so map models don't blow out white (S.). 0.55
        // = prontera's ambient; outdoor maps (<=0.55) unchanged. Matches MapRenderer's ground clamp.
        const f32 kAmbMax = 0.55f;
        ambientV_[0] = std::min(L.ambient[0], kAmbMax);
        ambientV_[1] = std::min(L.ambient[1], kAmbMax);
        ambientV_[2] = std::min(L.ambient[2], kAmbMax);
    }

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent, 3, bgfx::AttribType::Float)
        .end();

    std::unordered_map<std::string, bgfx::TextureHandle> texCache;
    auto getTexture = [&](const std::string& name) -> bgfx::TextureHandle {
        auto it = texCache.find(name);
        if (it != texCache.end()) return it->second;
        bgfx::TextureHandle h = BGFX_INVALID_HANDLE;
        // Prefer a hi-res .png model texture over the .bmp (S.); uploadBmp decodes either by magic.
        if (auto bytes = app.vfs().readPreferPng("data/texture/" + name)) h = uploadBmp(*bytes, name.c_str());
        if (bgfx::isValid(h)) textures_.push_back(h);
        texCache.emplace(name, h);
        return h;
    };
    // Optional companion normal map ("<name>_n.png"). Cached like the diffuse; invalid = none.
    // With no hand-authored map, derive the relief from the diffuse's own luminance (S.:
    // "bump из яркости") — dark seams become grooves; a real _n.png always wins.
    std::unordered_map<std::string, bgfx::TextureHandle> nrmCache;
    auto getNormal = [&](const std::string& name) -> bgfx::TextureHandle {
        auto it = nrmCache.find(name);
        if (it != nrmCache.end()) return it->second;
        bgfx::TextureHandle h = BGFX_INVALID_HANDLE;
        // Optional companion normal map: try _n.webp then _n.png, QUIETLY. Its absence is normal (most
        // tiles ship none, and all content is .webp), so it must NOT spam the missing-resource log for a
        // _n.png that only exists as .webp — or not at all (S.: "в лог не должно писаться, если webp").
        {
            const std::string np = normalMapPath("data/texture/" + name);  // ..._n.png
            std::optional<std::vector<u8>> nb;
            if (np.size() > 4) nb = app.vfs().readQuiet(np.substr(0, np.size() - 4) + ".webp");
            if (!nb) nb = app.vfs().readQuiet(np);
            if (nb) h = uploadBmp(*nb);
        }
        if (!bgfx::isValid(h)) {
            if (auto bytes = app.vfs().readPreferPng("data/texture/" + name))  // hi-res .png diffuse -> bump
                if (auto img = decodeImage(*bytes); img && img->valid()) {
                    const Image gen = normalFromLuminance(*img, 3.0f);  // x2 base; scaled live by g_normalsFactor
                    if (gen.valid()) {
                        const u64 flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                          BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
                        const std::vector<u8> chain = buildMipChain(gen.rgba, gen.width, gen.height);
                        h = bgfx::createTexture2D(static_cast<u16>(gen.width),
                                                  static_cast<u16>(gen.height), true, 1,
                                                  bgfx::TextureFormat::RGBA8, flags,
                                                  bgfx::copy(chain.data(),
                                                             static_cast<u32>(chain.size())));
                    }
                }
        }
        if (bgfx::isValid(h)) textures_.push_back(h);
        nrmCache.emplace(name, h);
        return h;
    };

    models_.resize(map.rsmCache.size());
    for (usize mi = 0; mi < map.rsmCache.size(); ++mi) {
        const Rsm& rsm = map.rsmCache[mi];
        ModelMesh mesh = ModelMesh::build(rsm);
        const auto& nodes = rsm.nodes();

        // Node world matrices (compose the hierarchy by parent name).
        std::unordered_map<std::string, int> byName;
        for (usize i = 0; i < nodes.size(); ++i) byName[nodes[i].name] = static_cast<int>(i);
        std::vector<Mat4> world(nodes.size());
        std::vector<char> done(nodes.size(), 0), visiting(nodes.size(), 0);
        std::function<Mat4(int)> worldOf = [&](int i) -> Mat4 {
            if (done[i]) return world[i];
            if (visiting[i]) return Mat4::identity();  // guard against malformed cycles
            visiting[i] = 1;
            const RsmNode& n = nodes[i];
            Mat4 local = Mat4::translation({n.pos[0], n.pos[1], n.pos[2]});
            if (n.rotAngle != 0.0f)
                local = local * Mat4::rotationAxis(n.rotAngle, {n.rotAxis[0], n.rotAxis[1], n.rotAxis[2]});
            local = local * Mat4::scaling({n.scale[0], n.scale[1], n.scale[2]});
            auto p = byName.find(n.parentName);
            Mat4 m = (p != byName.end() && p->second != i) ? worldOf(p->second) * local : local;
            visiting[i] = 0;
            done[i] = 1;
            world[i] = m;
            return m;
        };

        // Flatten node meshes into one model-space buffer.
        std::vector<GpuVertex> verts;
        std::vector<u32> indices;
        struct RawBatch { std::string tex; u32 start, count; };
        std::vector<RawBatch> raw;

        // Transform a node mesh by `m` and append its verts/indices/batches to (vv,ii,rr).
        auto bakeInto = [&](const ModelNodeMesh& nm, const Mat4& m, std::vector<GpuVertex>& vv,
                            std::vector<u32>& ii, std::vector<RawBatch>& rr) {
            const u32 base = static_cast<u32>(vv.size());
            for (const ModelVertex& v : nm.vertices) {
                const Vec4 r = m * Vec4{v.x, v.y, v.z, 1.0f};
                // Rotate the normal/tangent into model space (w=0 drops translation). #107.
                const Vec4 rn = m * Vec4{v.nx, v.ny, v.nz, 0.0f};
                const Vec4 rt = m * Vec4{v.tx, v.ty, v.tz, 0.0f};
                auto norm3 = [](f32 x, f32 y, f32 z, f32& ox, f32& oy, f32& oz, f32 dx, f32 dy, f32 dz) {
                    const f32 l = std::sqrt(x * x + y * y + z * z);
                    if (l > 1e-8f) { ox = x / l; oy = y / l; oz = z / l; }
                    else { ox = dx; oy = dy; oz = dz; }
                };
                GpuVertex gv{r.x, r.y, r.z, v.u, v.v, v.color, 0, 0, 0, 0, 0, 0};
                norm3(rn.x, rn.y, rn.z, gv.nx, gv.ny, gv.nz, 0.0f, 1.0f, 0.0f);
                norm3(rt.x, rt.y, rt.z, gv.tx, gv.ty, gv.tz, 1.0f, 0.0f, 0.0f);
                vv.push_back(gv);
            }
            const u32 ibase = static_cast<u32>(ii.size());
            for (u32 idx : nm.indices) ii.push_back(base + idx);
            for (const ModelBatch& b : nm.batches) {
                std::string texName;
                if (b.textureId >= 0 && static_cast<usize>(b.textureId) < rsm.textures().size())
                    texName = rsm.textures()[b.textureId];
                rr.push_back({texName, ibase + b.indexStart, b.indexCount});
            }
        };

        // A node with >=2 rotation keyframes (and the model animates) SPINS -- e.g. an airship
        // propeller. Bake it in its REST space (scale/offset/mat3, its animated rotation applied per
        // frame at draw), keeping its parent-world*T(pos) as basePart; everything else bakes statically.
        struct PendingAnim { std::vector<GpuVertex> v; std::vector<u32> i; std::vector<RawBatch> r;
                             Mat4 basePart; std::vector<RsmRotKey> keys; i32 animLen; };
        std::vector<PendingAnim> pending;

        for (const ModelNodeMesh& nm : mesh.nodes) {
            const RsmNode& n = nodes[nm.nodeIndex];
            // Only a node whose rotation keyframes sweep a MEANINGFUL angle is a real spin (airship
            // propeller). A tiny-amplitude SWAY (rope bridges, hanging signs — rotKeys oscillate ~1deg)
            // must NOT go through the animated path: that path bakes into `pending`, so a single-node
            // sway model's static `verts` stay EMPTY -> the bbox centre is 0 -> it never gets XZ-centred
            // -> it renders offset by its geometry-vs-origin (~0.5*length + width). Bake the sway
            // statically (its rotAngle/rotAxis already equals the rest orientation) so it centres +
            // places correctly; it just won't wobble. (S. gef_fild13 bridge — char walked on air.)
            bool bigSpin = false;
            if (rsm.animLength() > 0 && n.rotKeys.size() >= 2) {
                const f32* q0 = n.rotKeys[0].q;
                f32 minDot = 1.0f;
                for (const auto& k : n.rotKeys) {
                    const f32 dot = std::fabs(q0[0] * k.q[0] + q0[1] * k.q[1] + q0[2] * k.q[2] + q0[3] * k.q[3]);
                    minDot = std::min(minDot, dot);
                }
                bigSpin = (minDot < 0.924f);  // 2*acos(0.924) ~= 45deg total sweep -> genuine rotation
            }
            const bool animated = bigSpin;
            if (!animated) {
                // Per-node vertex matrix = world * T(offset) * mat3 (roBrowser order); offset applied
                // BEFORE mat3 and only for multi-node models (#105 Amatsu guild building fix).
                Mat4 vm = worldOf(static_cast<int>(nm.nodeIndex));
                if (nodes.size() > 1)
                    vm = vm * Mat4::translation({n.offset[0], n.offset[1], n.offset[2]});
                vm = vm * mat3to4(n.mat3);
                bakeInto(nm, vm, verts, indices, raw);
            } else {
                // animatedVm(t) = worldOf(parent) * T(pos) * animRot(t) * S * [T(offset)] * mat3.
                // basePart = worldOf(parent) * T(pos); tail (baked into the node buffer) = the rest.
                Mat4 parentWorld = Mat4::identity();
                auto pit = byName.find(n.parentName);
                if (pit != byName.end() && pit->second != static_cast<int>(nm.nodeIndex))
                    parentWorld = worldOf(pit->second);
                Mat4 tail = Mat4::scaling({n.scale[0], n.scale[1], n.scale[2]});
                if (nodes.size() > 1)
                    tail = tail * Mat4::translation({n.offset[0], n.offset[1], n.offset[2]});
                tail = tail * mat3to4(n.mat3);
                PendingAnim pa;
                pa.basePart = parentWorld * Mat4::translation({n.pos[0], n.pos[1], n.pos[2]});
                pa.keys = n.rotKeys;
                pa.animLen = rsm.animLength();
                bakeInto(nm, tail, pa.v, pa.i, pa.r);
                if (!pa.v.empty() && !pa.i.empty()) pending.push_back(std::move(pa));
            }
        }
        if ((verts.empty() || indices.empty()) && pending.empty()) continue;

        // Centre horizontally (X/Z) and sit the base (max RSM Y) at 0. Centre is measured from the
        // STATIC geometry; the same offset is folded into each animated node so it stays aligned to
        // the body (a small propeller's absence from the bbox shifts the airship centre negligibly).
        f32 cx = 0.0f, cz = 0.0f, by = 0.0f, hx = 0.0f, hz = 0.0f;
        if (!verts.empty()) {
            Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
            for (const auto& v : verts) {
                mn.x = std::min(mn.x, v.x); mx.x = std::max(mx.x, v.x);
                mn.y = std::min(mn.y, v.y); mx.y = std::max(mx.y, v.y);
                mn.z = std::min(mn.z, v.z); mx.z = std::max(mx.z, v.z);
            }
            cx = (mn.x + mx.x) * 0.5f; cz = (mn.z + mx.z) * 0.5f; by = mx.y;
            hx = (mx.x - mn.x) * 0.5f; hz = (mx.z - mn.z) * 0.5f;
            for (auto& v : verts) { v.x -= cx; v.y -= by; v.z -= cz; }
        }

        Model& M = models_[mi];
        // Horizontal bounding-circle radius about the (now centred) origin, for the camera-occlude fade.
        M.radiusXZ = std::sqrt(hx * hx + hz * hz);
        auto buildBatches = [&](const std::vector<RawBatch>& rr, std::vector<Batch>& out) {
            for (const RawBatch& rb : rr) {
                bgfx::TextureHandle t = rb.tex.empty() ? white_ : getTexture(rb.tex);
                // NB: BGFX_INVALID_HANDLE expands to a braced initializer { ... }, which can't be a
                // ternary operand (MSVC C2059), so assign it plainly then overwrite.
                bgfx::TextureHandle n = BGFX_INVALID_HANDLE;
                if (!rb.tex.empty()) n = getNormal(rb.tex);
                out.push_back({bgfx::isValid(t) ? t : white_, n, rb.start, rb.count});
            }
        };
        if (!verts.empty() && !indices.empty()) {
            M.vbh = bgfx::createVertexBuffer(
                bgfx::copy(verts.data(), static_cast<u32>(verts.size() * sizeof(GpuVertex))), layout);
            M.ibh = bgfx::createIndexBuffer(
                bgfx::copy(indices.data(), static_cast<u32>(indices.size() * sizeof(u32))),
                BGFX_BUFFER_INDEX32);
            buildBatches(raw, M.batches);
        }
        // Animated (spinning) sub-parts: own buffer + basePart with the model centre folded in.
        const Mat4 unCentre = Mat4::translation({-cx, -by, -cz});
        for (PendingAnim& pa : pending) {
            AnimNode an;
            an.vbh = bgfx::createVertexBuffer(
                bgfx::copy(pa.v.data(), static_cast<u32>(pa.v.size() * sizeof(GpuVertex))), layout);
            an.ibh = bgfx::createIndexBuffer(
                bgfx::copy(pa.i.data(), static_cast<u32>(pa.i.size() * sizeof(u32))), BGFX_BUFFER_INDEX32);
            an.basePart = unCentre * pa.basePart;
            an.rotKeys = std::move(pa.keys);
            an.animLength = pa.animLen;
            buildBatches(pa.r, an.batches);
            M.animNodes.push_back(std::move(an));
        }
        M.ok = true;
    }

    ready_ = true;
    log::info("ModelRenderer: {} model(s), {} texture(s) ready", models_.size(), textures_.size());
    return true;
}

void ModelRenderer::destroy() {
    for (auto& m : models_) {
        if (bgfx::isValid(m.vbh)) bgfx::destroy(m.vbh);
        if (bgfx::isValid(m.ibh)) bgfx::destroy(m.ibh);
        for (auto& an : m.animNodes) {
            if (bgfx::isValid(an.vbh)) bgfx::destroy(an.vbh);
            if (bgfx::isValid(an.ibh)) bgfx::destroy(an.ibh);
        }
    }
    models_.clear();
    for (auto t : textures_)
        if (bgfx::isValid(t)) bgfx::destroy(t);
    textures_.clear();
    if (bgfx::isValid(white_)) bgfx::destroy(white_);
    if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
    if (bgfx::isValid(fade_)) bgfx::destroy(fade_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    white_ = BGFX_INVALID_HANDLE;
    sampler_ = BGFX_INVALID_HANDLE;
    fade_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    ready_ = false;
}

void ModelRenderer::render(const MapData& map, const Vec3& camPos, const Vec3& playerPos,
                           double time, float forceFade) const {
    if (!ready_ || !bgfx::isValid(program_)) return;
    const bool xray = forceFade < 0.999f;  // Camera Lock: fade every model, not just line occluders
    const f32 hw = map.gnd.width() * 0.5f, hh = map.gnd.height() * 0.5f;
    // Opaque world geometry: write colour+alpha+depth, depth-test less.
    const u64 opaqueState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                            BGFX_STATE_DEPTH_TEST_LESS;
    // Camera-occlude fade: alpha-blended, depth-TEST only. We drop WRITE_Z so the
    // faded building's depth doesn't occlude the player drawn afterwards, and drop
    // WRITE_A so it can't poison later alpha tests.
    const u64 fadeState =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA;
    // Tunables (the user will adjust): a placement counts as an occluder when its
    // world origin is within kFadeRadius world units of the camera->player segment
    // and lies between them; occluders are drawn at kFadeAlpha opacity.
    // A closer camera (max zoom / Camera Lock indoors) lets walls intrude far more, and the short
    // cam->player segment makes a fixed perpendicular threshold miss them -> widen the capture radius
    // as the camera nears the player (no change to the normal far camera). (S.: ama_in01 walls.) (#104)
    const f32 segDx = camPos.x - playerPos.x, segDz = camPos.z - playerPos.z;
    const f32 segLen = std::sqrt(segDx * segDx + segDz * segDz);
    // Closer camera (max zoom / Camera Lock) widens the capture radius so RSM furniture/props along
    // the short cam->player sightline fade. (Structural interior walls are GND, not models -- those
    // are handled by drawing the player's sprite on top in Camera Lock.) (#104)
    const f32 kFadeRadius = 6.0f + std::clamp((14.0f - segLen) / 14.0f, 0.0f, 1.0f) * 9.0f;
    constexpr f32 kFadeAlpha = 0.30f;  // occluder opacity (1.0 = opaque)
    // Mirror the X axis (x -> width - x) so models match the left<->right-flipped
    // ground. The -1 scale also mirrors each model's own geometry/rotation, so a
    // building's left and right swap with the map. No culling, so winding is moot.
    const Mat4 mirror = Mat4::translation({static_cast<f32>(map.gnd.width()), 0.0f, 0.0f}) *
                        Mat4::scaling({-1.0f, 1.0f, 1.0f});

    // Camera->player line for the occlusion test, FLATTENED to the XZ (top-down) plane: a building's
    // origin sits at ground level, far below the elevated camera->player line, so a full-3D distance
    // disqualified tall occluders at close zoom (S.: fade failed at max zoom). Test the footprint
    // horizontally from the player's feet, ignoring height.
    // At close zoom the camera sits almost on top of the player, so the real cam->player
    // segment is too short to catch the buildings the low eye still looks through. Push the
    // CONCEPTUAL camera point to twice its distance from the player FOR THE OCCLUSION TEST
    // ONLY (S.: "для фейда при ближнем зуме условную точку камеры отдалить в 2 раза"); the
    // actual draw/view still uses the real camera.
    const f32 tcamX = playerPos.x + (camPos.x - playerPos.x) * 2.0f;
    const f32 tcamZ = playerPos.z + (camPos.z - playerPos.z) * 2.0f;
    const f32 abx = playerPos.x - tcamX, abz = playerPos.z - tcamZ;
    const f32 abXZ2 = abx * abx + abz * abz;

    for (const auto& [obj, mi] : map.placements) {
        if (mi < 0 || static_cast<usize>(mi) >= models_.size()) continue;
        const Model& M = models_[mi];
        if (!M.ok) continue;

        // RSW placement -> world. pos maps with the /10 + half-extent offset found empirically; the
        // model is scaled 1/10 with Y flipped (RSM Y points down). The X mirror keeps models aligned
        // with the left<->right-flipped ground.
        const Mat4 place =
            mirror *
            Mat4::translation({obj.pos[0] * 0.1f + hw, -obj.pos[1] * 0.1f, obj.pos[2] * 0.1f + hh}) *
            Mat4::rotationZ(-radians(obj.rot[2])) * Mat4::rotationX(-radians(obj.rot[0])) *
            Mat4::rotationY(radians(obj.rot[1])) *
            Mat4::scaling({obj.scale[0], obj.scale[1], obj.scale[2]}) *
            Mat4::scaling({0.1f, -0.1f, 0.1f});

        // Occlusion test in the XZ plane: project the placement footprint (origin x/z) onto the
        // flattened cam->player line. Occludes when t in (0.05..1.0) (between the camera and the
        // player) and the horizontal distance to the line is under kFadeRadius — height is ignored
        // so a tall wall always counts, at any zoom.
        const f32 px = place.m[12] - tcamX, pz = place.m[14] - tcamZ;
        bool occludes = false;
        if (abXZ2 > 1e-4f) {
            const f32 t = (px * abx + pz * abz) / abXZ2;
            if (t > 0.05f && t < 1.0f) {
                const f32 ex = px - abx * t, ez = pz - abz * t;
                // Capture radius = the constant slack PLUS this placement's own horizontal
                // footprint (model radius * the baked 0.1 RSM scale * the placement scale), so a
                // wide building is detected by its walls, not just its centre point. This is what
                // fixed "плохо определяются элементы которые скрывать нужно" — big meshes were
                // missed because only their origin was tested.
                const f32 sxz = std::max(std::fabs(obj.scale[0]), std::fabs(obj.scale[2]));
                const f32 capR = kFadeRadius + M.radiusXZ * 0.1f * sxz;
                occludes = (ex * ex + ez * ez) < capR * capR;
            }
        }
        // x-ray fades every model; otherwise only the line occluders fade.
        const f32 fadeVal = xray ? forceFade : (occludes ? kFadeAlpha : 1.0f);
        const f32 fade[4] = {fadeVal, 0.0f, 0.0f, 0.0f};
        const u64 state = (xray || occludes) ? fadeState : opaqueState;

        for (const Batch& b : M.batches) {
            bgfx::setTransform(place.m);
            bgfx::setVertexBuffer(0, M.vbh);
            bgfx::setIndexBuffer(M.ibh, b.indexStart, b.indexCount);
            bgfx::setTexture(0, sampler_, bgfx::isValid(b.tex) ? b.tex : white_);
            // Normal mapping (#107): bind the map + enable lighting only when the batch has one;
            // otherwise a flat normal + hasMap=0 -> the classic unlit look is preserved exactly.
            const bool hasNrm = bgfx::isValid(b.nrm) && g_normalsFactor > 0.001f;
            bgfx::setTexture(1, nrmSampler_, hasNrm ? b.nrm : flatNrm_);
            const f32 np[4] = {hasNrm ? 1.0f : 0.0f, g_normalsFactor, 0.0f, 0.0f};
            bgfx::setUniform(nrmParams_, np);
            bgfx::setUniform(lightDir_, lightDirV_);
            bgfx::setUniform(lightColor_, lightColorV_);
            bgfx::setUniform(ambient_, ambientV_);
            bgfx::setUniform(fade_, fade);
            bgfx::setState(state);
            bgfx::submit(0, program_);
        }

        // Spinning sub-parts (e.g. airship propellers): each animated node draws with its own
        // per-frame matrix = place * basePart * animRot(time), on top of the static body above.
        for (const AnimNode& an : M.animNodes) {
            const Mat4 nodeMat = place * an.basePart * animRotAt(an.rotKeys, an.animLength, time);
            for (const Batch& b : an.batches) {
                bgfx::setTransform(nodeMat.m);
                bgfx::setVertexBuffer(0, an.vbh);
                bgfx::setIndexBuffer(an.ibh, b.indexStart, b.indexCount);
                bgfx::setTexture(0, sampler_, bgfx::isValid(b.tex) ? b.tex : white_);
                const bool hasNrm = bgfx::isValid(b.nrm) && g_normalsFactor > 0.001f;
                bgfx::setTexture(1, nrmSampler_, hasNrm ? b.nrm : flatNrm_);
                const f32 np[4] = {hasNrm ? 1.0f : 0.0f, g_normalsFactor, 0.0f, 0.0f};
                bgfx::setUniform(nrmParams_, np);
                bgfx::setUniform(lightDir_, lightDirV_);
                bgfx::setUniform(lightColor_, lightColorV_);
                bgfx::setUniform(ambient_, ambientV_);
                bgfx::setUniform(fade_, fade);
                bgfx::setState(state);
                bgfx::submit(0, program_);
            }
        }
    }
}

} // namespace uaro
