#include "game/StrEffect.hpp"

#include <algorithm>
#include <cmath>

#include <bgfx/bgfx.h>

#include "core/Log.hpp"
#include "formats/Bmp.hpp"
#include "formats/Evff.hpp"  // .ezv (EVFF) effects converted to Str and played by the same renderer
#include "formats/ImageIO.hpp"  // decodeImage: hi-res PNG over BMP/TGA for .str effect frames (S.)
#include "game/CharacterActor.hpp"  // WorldSpritePass
#include "resource/Vfs.hpp"

namespace uaro {
namespace {

// The .str canvas is 640x640 centred on (320,320); 1 STR pixel = 1/35 world unit
// (roBrowser StrEffect.js: pixelRatio = 1/35, pos -= 320, a -0.5 vertical lift).
constexpr float kStrPixel = 1.0f / 35.0f;
constexpr float kCenter = 320.0f;

// Interpolated per-layer state for one frame (mirrors roBrowser calculateAnimation's result).
struct StrAnim {
    f32 pos[2] = {0, 0};
    f32 xy[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    f32 color[4] = {1, 1, 1, 1};
    f32 angle = 0;
    f32 aniframe = 0;
    u32 srcAlpha = 5, destAlpha = 6;
};

// Port of roBrowser StrEffect.js calculateAnimation: pick the active source (type 0) and morph
// target (type 1) keyframes around keyIndex; a static frame uses the source as-is, otherwise the
// target's per-frame slopes drive a linear morph. Returns false when the layer draws nothing.
bool calcAnim(const StrLayer& layer, float keyIndex, StrAnim& r) {
    int fromId = -1, toId = -1, lastFrame = 0, lastSource = 0;
    const auto& a = layer.keys;
    const int n = static_cast<int>(a.size());
    for (int i = 0; i < n; ++i) {
        if (static_cast<float>(a[i].frame) <= keyIndex) {
            if (a[i].type == 0) fromId = i;
            if (a[i].type == 1) toId = i;
        }
        lastFrame = std::max(lastFrame, a[i].frame);
        if (a[i].type == 0) lastSource = std::max(lastSource, a[i].frame);
    }
    if (fromId < 0 || (toId < 0 && static_cast<float>(lastFrame) < keyIndex)) return false;

    const StrKeyframe& from = a[fromId];
    const StrKeyframe* to = (toId >= 0) ? &a[toId] : nullptr;
    const float delta = keyIndex - static_cast<float>(from.frame);
    r.srcAlpha = from.srcAlpha;
    r.destAlpha = from.destAlpha;

    // Static frame: no adjacent morph target that shares the source's start frame.
    if (!to || toId != fromId + 1 || to->frame != from.frame) {
        if (to && lastSource <= from.frame) return false;
        r.angle = from.angle;
        r.aniframe = from.aniframe;
        for (int k = 0; k < 4; ++k) r.color[k] = from.color[k];
        r.pos[0] = from.pos[0];
        r.pos[1] = from.pos[1];
        for (int k = 0; k < 8; ++k) r.xy[k] = from.xy[k];
        return true;
    }

    // Morph: the target's fields are per-frame slopes, value = from + to*delta.
    for (int k = 0; k < 4; ++k) r.color[k] = from.color[k] + to->color[k] * delta;
    for (int k = 0; k < 8; ++k) r.xy[k] = from.xy[k] + to->xy[k] * delta;
    r.angle = from.angle + to->angle * delta;
    r.pos[0] = from.pos[0] + to->pos[0] * delta;
    r.pos[1] = from.pos[1] + to->pos[1] * delta;

    const float texcnt = static_cast<float>(layer.textures.size());
    switch (to->anitype) {
        default: r.aniframe = 0.0f; break;                                            // none / bug-fix
        case 1: r.aniframe = from.aniframe + to->aniframe * delta; break;             // normal
        case 2: r.aniframe = std::min(from.aniframe + to->delay * delta, texcnt - 1.0f); break;  // stop
        case 3: r.aniframe = texcnt > 0 ? std::fmod(from.aniframe + to->delay * delta, texcnt) : 0; break;  // repeat
        case 4: r.aniframe = texcnt > 0 ? std::fmod(from.aniframe - to->delay * delta, texcnt) : 0; break;  // reverse
    }
    return true;
}

// D3DBLEND factor (as stored in the .str) -> bgfx blend factor — the values RO effects use.
u64 d3dBlend(u32 f) {
    switch (f) {
        case 1: return BGFX_STATE_BLEND_ZERO;
        case 2: return BGFX_STATE_BLEND_ONE;
        case 3: return BGFX_STATE_BLEND_SRC_COLOR;
        case 4: return BGFX_STATE_BLEND_INV_SRC_COLOR;
        case 5: return BGFX_STATE_BLEND_SRC_ALPHA;
        case 6: return BGFX_STATE_BLEND_INV_SRC_ALPHA;
        case 7: return BGFX_STATE_BLEND_DST_ALPHA;
        case 8: return BGFX_STATE_BLEND_INV_DST_ALPHA;
        case 9: return BGFX_STATE_BLEND_DST_COLOR;
        case 10: return BGFX_STATE_BLEND_INV_DST_COLOR;
        default: return BGFX_STATE_BLEND_ONE;
    }
}

struct V {
    f32 x, y, z, u, v;
    u32 abgr;
};

// RO effect .bmp have no alpha channel and use magenta as the transparent key (S.: "розовый
// фон нужно сделать прозрачным"). Zero those texels (RGBA=0) so fs_sprite3d's alpha cutout drops
// them and bilinear filtering can't bleed pink into the surviving edges. Same heuristic as the
// ground/model magenta key (R>200, G<60, B>200).
void keyMagenta(std::vector<u8>& rgba, int w, int h) {
    // RO effect .bmp are 24-bit (no alpha): key out the flat background so it doesn't draw as an opaque
    // box under a normal-alpha layer. Two backgrounds occur: magenta (0xff00ff) AND near-pure-black
    // (S.: shield_charge etc. had a black box). Keying near-black first is safe for additive layers too
    // (a black texel contributes ~0 whatever the blend factor); keep the threshold tight so dark detail
    // survives. The magenta key + its anti-aliased rim then go through the SHARED iterative despill
    // (keyAndDespillMagenta) — the SAME multi-ring cleanup the ground/UI/model paths use — so an HD
    // (2x/4x) .png effect layer no longer keeps a pink caёmka (S.: "на эффектах магентная каёмка — ты
    // не убрал её, а отчитался как сделал"). The old 1px edge-bleed here only touched the border ring,
    // so a wide AA rim on a hi-res layer survived.
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<usize>(w) * h * 4) return;
    const int n = w * h;
    for (int i = 0; i < n; ++i) {
        u8* p = &rgba[static_cast<usize>(i) * 4];
        if (p[0] < 16 && p[1] < 16 && p[2] < 16) { p[0] = p[1] = p[2] = p[3] = 0; }  // near-black bg
    }
    Image img;
    img.width = static_cast<u32>(w);
    img.height = static_cast<u32>(h);
    img.rgba = std::move(rgba);
    keyAndDespillMagenta(img);  // magenta hard-key + multi-ring rim despill (shared with ground/UI/model)
    rgba = std::move(img.rgba);
}

}  // namespace

bool StrEffect::load(const Vfs& vfs, const std::string& name) {
    const std::string base = "data/texture/effect/";
    std::optional<Str> parsed;
    if (auto bytes = vfs.read(base + name + ".str")) {
        parsed = Str::parse(*bytes);  // Str::parse logs 'bad magic' / 'unsupported version 0x..' on fail
        if (!parsed)
            log::warn("StrEffect: '{}{}.str' loaded ({} bytes) but PARSE FAILED (format/version)", base,
                      name, bytes->size());
    } else if (auto ez = vfs.read(base + name + ".ezv")) {
        // EVFF (.ezv): a text keyframe billboard effect (hammerfall/cross/guardian/rune...). Parse and
        // convert to a Str so the same renderer + texture path plays it (formats/Evff).
        if (auto e = Evff::parse(*ez)) parsed = e->toStr();
        else log::warn("StrEffect: '{}{}.ezv' EVFF parse failed", base, name);
    } else {
        log::warn("StrEffect: effect NOT FOUND at '{}{}.str' or '.ezv' (check the GRF path)", base, name);
        return false;
    }
    if (!parsed) return false;
    releaseTextures();  // free any prior load's handles (Texture has no destructor)
    str_ = std::move(parsed);
    tex_.assign(str_->layers().size(), {});
    // Effect textures are named relative to the .str's OWN folder (roBrowser convention), not the effect
    // root: e.g. new_comet/new_comet_cast_bottom/new_comet_cast_bottom.str -> its .tga live in
    // data/texture/effect/new_comet/new_comet_cast_bottom/. Prepend that dir; fall back to the effect
    // root for effects whose texture names already carry a path (or sit at the root). (S.: warp .str
    // parsed but 0/14 textures because we only looked in the effect root.)
    const std::string dir = name.substr(0, name.find_last_of('/') + 1);  // "" if the effect is at the root
    loadLayerTextures(vfs, dir);
    return true;
}

bool StrEffect::loadFromStr(const Vfs& vfs, Str str, const std::string& dir) {
    releaseTextures();
    str_ = std::move(str);
    tex_.assign(str_->layers().size(), {});
    loadLayerTextures(vfs, dir);
    return ready();
}

void StrEffect::loadLayerTextures(const Vfs& vfs, const std::string& dir) {
    const std::string base = "data/texture/effect/";
    usize loaded = 0, expected = 0;
    for (usize li = 0; li < str_->layers().size(); ++li) {
        const StrLayer& L = str_->layers()[li];
        tex_[li].resize(L.textures.size());
        for (usize ti = 0; ti < L.textures.size(); ++ti) {
            ++expected;
            auto tb = vfs.readPreferPng(base + dir + L.textures[ti]);  // .str-folder-relative (hi-res .png first)
            if (!tb && !dir.empty()) tb = vfs.readPreferPng(base + L.textures[ti]);  // fallback: effect root
            if (!tb) continue;
            if (auto img = decodeImage(*tb); img && img->valid()) {
                keyMagenta(img->rgba, static_cast<int>(img->width),
                           static_cast<int>(img->height));  // key magenta bg + edge-bleed (no pink halo)
                tex_[li][ti].create(static_cast<u16>(img->width), static_cast<u16>(img->height),
                                    img->rgba.data(), /*smooth=*/true);
                ++loaded;
            }
        }
    }
    log::debug("StrEffect: {} layers, {}/{} textures, maxKey {}", str_->layers().size(), loaded,
               expected, str_->maxKey());
}

double StrEffect::duration() const {
    if (!str_ || str_->fps() == 0) return 0.0;
    return static_cast<double>(str_->maxKey()) / static_cast<double>(str_->fps());
}

void StrEffect::render(const WorldSpritePass& pass, const Vec3& pos, double elapsed, float scale,
                       bool flat, float intensity, bool upright) const {
    if (!str_ || !bgfx::isValid(pass.program) || !pass.layout) return;
    // Uniform size multiplier (S.: the warp .str is drawn at 0.5). Folds into the pixel->world factor
    // so quad + offset + lift all scale together around pos.
    const float sp = kStrPixel * scale;
    const float keyIndex = static_cast<float>(elapsed) * static_cast<float>(str_->fps());
    // flat=true lays the effect on the GROUND (world XZ plane) -- the warp portal must lie on the floor
    // (S.: "положить на землю, повернуть 90° по X"). upright=true stands the effect VERTICALLY (world +Y
    // up) but faces the camera horizontally (R = camera-right, which is already level): the default full
    // camera billboard uses pass.up, which tilts back with the camera pitch and lays cast effects flat
    // under the caster (S.: "спрайт каста ложится под ноги"). flat wins over upright.
    const Vec3 R = flat ? Vec3{1.0f, 0.0f, 0.0f} : pass.right;
    const Vec3 U = flat      ? Vec3{0.0f, 0.0f, 1.0f}
                   : upright ? Vec3{0.0f, 1.0f, 0.0f}
                             : pass.up;

    // 4 quad corners: (xy[i], xy[i+4]) with the standard uv winding (roBrowser geometry order).
    struct Corner {
        int xi, yi;
        float u, v;
    };
    static const Corner kC[4] = {
        {0, 4, 0.0f, 0.0f}, {1, 5, 1.0f, 0.0f}, {2, 6, 1.0f, 1.0f}, {3, 7, 0.0f, 1.0f}};

    for (usize li = 0; li < str_->layers().size(); ++li) {
        const StrLayer& L = str_->layers()[li];
        if (L.textures.empty()) continue;
        StrAnim an;
        if (!calcAnim(L, keyIndex, an)) continue;
        const int frame = std::clamp(static_cast<int>(an.aniframe), 0,
                                     static_cast<int>(tex_[li].size()) - 1);
        const Texture& t = tex_[li][static_cast<usize>(frame)];
        if (!t.valid()) continue;

        const float ang = an.angle * 3.14159265f / 180.0f;
        const float cs = std::cos(ang), sn = std::sin(ang);
        const float ox = (an.pos[0] - kCenter) * sp;
        const float oy = (an.pos[1] - kCenter) * sp;
        Vec3 wc[4];
        for (int c = 0; c < 4; ++c) {
            const float ax = an.xy[kC[c].xi] * sp;
            const float ay = -an.xy[kC[c].yi] * sp;
            const float rx = ax * cs + ay * sn;  // rotateZ(-angle)
            const float ry = -ax * sn + ay * cs;
            const float fx = rx + ox;
            // Vertical lift is for upright billboards standing on the cell; a ground-flat effect sits AT
            // the cell, so no lift (it would just push it in Z).
            const float fy = ry - oy - (flat ? 0.0f : 0.5f * scale);
            wc[c] = Vec3{pos.x + R.x * fx + U.x * fy, pos.y + R.y * fx + U.y * fy,
                        pos.z + R.z * fx + U.z * fy};
        }

        const auto cb = [](float f) -> u32 {
            return static_cast<u32>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        const u32 col = (cb(an.color[3] * intensity) << 24) | (cb(an.color[2] * intensity) << 16) |
                        (cb(an.color[1] * intensity) << 8) | cb(an.color[0] * intensity);

        if (bgfx::getAvailTransientVertexBuffer(4, *pass.layout) < 4 ||
            bgfx::getAvailTransientIndexBuffer(6) < 6)
            return;
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, 4, *pass.layout);
        bgfx::allocTransientIndexBuffer(&tib, 6);
        V* vd = reinterpret_cast<V*>(tvb.data);
        for (int c = 0; c < 4; ++c)
            vd[c] = {wc[c].x, wc[c].y, wc[c].z, kC[c].u, kC[c].v, col};
        u16* id = reinterpret_cast<u16*>(tib.data);
        id[0] = 0; id[1] = 1; id[2] = 2; id[3] = 0; id[4] = 2; id[5] = 3;

        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        bgfx::setTexture(0, pass.sampler, t.handle());
        if (bgfx::isValid(pass.bias)) {
            const float biasVec[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(pass.bias, biasVec);  // effects need no per-layer depth bias
        }
        if (bgfx::isValid(pass.fade)) {
            const float fadeVec[4] = {1.0f, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(pass.fade, fadeVec);  // shader alpha = fade * vertex.a -> use vertex.a
        }
        // Depth-TEST (so closer geometry occludes the effect) but no Z-write, per the .str blend.
        const u64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL |
                          BGFX_STATE_BLEND_FUNC(d3dBlend(an.srcAlpha), d3dBlend(an.destAlpha));
        bgfx::setState(state);
        bgfx::submit(pass.view, pass.program);
    }
}

}  // namespace uaro
