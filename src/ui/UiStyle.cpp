#include "ui/UiStyle.hpp"

#include <array>
#include <cmath>
#include <map>
#include <vector>

#include "render/SpriteBatch.hpp"
#include "render/Texture.hpp"

namespace uaro {
namespace ui {
namespace style {

namespace {

// Clamp helper (keep the generators self-contained).
inline float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

// Signed distance from point (px,py) to a rounded rect centred at (cx,cy) with half-extents (hx,hy)
// and corner radius r. Negative inside, 0 on the edge, positive outside.
inline float sdRoundRect(float px, float py, float cx, float cy, float hx, float hy, float r) {
    float qx = std::fabs(px - cx) - (hx - r);
    float qy = std::fabs(py - cy) - (hy - r);
    float ax = qx > 0.0f ? qx : 0.0f;
    float ay = qy > 0.0f ? qy : 0.0f;
    float outside = std::sqrt(ax * ax + ay * ay);
    float inside = std::fmin(std::fmax(qx, qy), 0.0f);
    return outside + inside - r;
}

// A generated coverage mask: white RGB with alpha = anti-aliased coverage, ready to tint at draw.
// Cached for the process lifetime and never copied (operator[] default-constructs in place, we fill
// the referenced slot). Keyed by {kind, w, h, iA, iB} so each distinct geometry is built once.
enum Kind { kMaskRound = 0, kMaskShadow = 1, kMaskRing = 2 };
using Key = std::array<int, 5>;
std::map<Key, Texture>& cache() {
    static std::map<Key, Texture> c;
    return c;
}

// Build and upload a mask texture via `fill(px,py)->coverage 0..1`. smooth=true so the 1px AA ramp
// is bilinear-filtered on screen (soft corners / soft shadow).
template <typename F>
const Texture& build(const Key& key, int w, int h, F&& fill) {
    Texture& slot = cache()[key];
    if (slot.valid() || w <= 0 || h <= 0) return slot;
    std::vector<u8> px(static_cast<usize>(w) * static_cast<usize>(h) * 4u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float cov = clamp01(fill(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f));
            u8* p = &px[(static_cast<usize>(y) * w + x) * 4u];
            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            p[3] = static_cast<u8>(cov * 255.0f + 0.5f);
        }
    }
    slot.create(static_cast<u16>(w), static_cast<u16>(h), px.data(), /*smooth=*/true);
    return slot;
}

int ri(float v) { return static_cast<int>(v * 16.0f + 0.5f); }  // quantise a float param for the key

// Rounded-rect fill mask at exactly w x h, corner radius r.
const Texture& roundMask(int w, int h, float r) {
    Key key{kMaskRound, w, h, ri(r), 0};
    return build(key, w, h, [=](float px, float py) {
        float sd = sdRoundRect(px, py, w * 0.5f, h * 0.5f, w * 0.5f, h * 0.5f, r);
        return 0.5f - sd;  // ~1px AA across the edge
    });
}

// Soft drop-shadow mask. The texture is (w+2*spread) x (h+2*spread); the panel box sits inset by
// `spread` and shifted DOWN by `drop` so the shadow is heavier below the window. Coverage is 0 under
// the box itself (the glass covers it) and falls off as t^2 over `spread` outside it.
const Texture& shadowMask(int boxW, int boxH, float r, float spread, float drop) {
    int sp = static_cast<int>(spread + 0.5f);
    int w = boxW + 2 * sp;
    int h = boxH + 2 * sp;
    Key key{kMaskShadow, w, h, ri(r), (static_cast<int>(drop) << 8) | sp};
    float cx = w * 0.5f;
    float cy = h * 0.5f + drop;  // bias the box (and thus the heavier shadow) downward
    float hx = boxW * 0.5f;
    float hy = boxH * 0.5f;
    return build(key, w, h, [=](float px, float py) {
        float sd = sdRoundRect(px, py, cx, cy, hx, hy, r);
        if (sd <= 0.0f) return 0.0f;             // under the glass -> no shadow
        float t = clamp01(1.0f - sd / spread);   // 1 at the edge, 0 `spread` away
        return t * t;
    });
}

// Thin ring mask hugging the inside of the rounded edge, `width` thick, both sides anti-aliased.
const Texture& ringMask(int w, int h, float r, float width) {
    Key key{kMaskRing, w, h, ri(r), ri(width)};
    return build(key, w, h, [=](float px, float py) {
        float sd = sdRoundRect(px, py, w * 0.5f, h * 0.5f, w * 0.5f, h * 0.5f, r);
        float inner = clamp01(0.5f + (sd + width));  // rises from the inner side
        float outer = clamp01(0.5f - sd);            // falls at the outer edge
        return inner * outer;
    });
}

}  // namespace

void roundedFill(SpriteBatch& sb, float x, float y, float w, float h, float radius, u32 color) {
    if (w <= 0.0f || h <= 0.0f) return;
    int iw = static_cast<int>(w + 0.5f), ih = static_cast<int>(h + 0.5f);
    float r = radius;
    if (r > iw * 0.5f) r = iw * 0.5f;
    if (r > ih * 0.5f) r = ih * 0.5f;
    const Texture& m = roundMask(iw, ih, r);
    if (m.valid())
        sb.draw(x, y, static_cast<float>(iw), static_cast<float>(ih), color, m);
    else
        sb.draw(x, y, w, h, color);  // degrade to a square fill if the mask couldn't be built
}

void windowFrame(SpriteBatch& sb, float x, float y, float w, float h, float radius, float scale,
                 u32 ground, float opacity) {
    if (w <= 0.0f || h <= 0.0f) return;
    int iw = static_cast<int>(w + 0.5f), ih = static_cast<int>(h + 0.5f);
    float r = radius * scale;
    if (r > iw * 0.5f) r = iw * 0.5f;
    if (r > ih * 0.5f) r = ih * 0.5f;

    // 1) soft drop shadow (windows only)
    float spread = kShadowSpread * scale;
    float drop = kShadowDrop * scale;
    const Texture& sh = shadowMask(iw, ih, r, spread, drop);
    if (sh.valid()) {
        int sp = static_cast<int>(spread + 0.5f);
        sb.draw(x - sp, y - sp, static_cast<float>(iw + 2 * sp), static_cast<float>(ih + 2 * sp),
                withOpacity(kBlack, kShadowAlpha), sh);
    }
    // 2) dark translucent rounded ground
    const Texture& m = roundMask(iw, ih, r);
    if (m.valid())
        sb.draw(x, y, static_cast<float>(iw), static_cast<float>(ih), withOpacity(ground, opacity), m);
    else
        sb.draw(x, y, w, h, withOpacity(ground, opacity));
    // 3) thin gold edge ring
    const Texture& rg = ringMask(iw, ih, r, kRingWidth * scale);
    if (rg.valid())
        sb.draw(x, y, static_cast<float>(iw), static_cast<float>(ih),
                withOpacity(kGold2, kRingAlpha), rg);
}

void hudPanel(SpriteBatch& sb, float x, float y, float w, float h, float radius, float scale,
              u32 ground, float opacity) {
    if (w <= 0.0f || h <= 0.0f) return;
    int iw = static_cast<int>(w + 0.5f), ih = static_cast<int>(h + 0.5f);
    float r = radius * scale;
    if (r > iw * 0.5f) r = iw * 0.5f;
    if (r > ih * 0.5f) r = ih * 0.5f;
    const Texture& m = roundMask(iw, ih, r);
    if (m.valid())
        sb.draw(x, y, static_cast<float>(iw), static_cast<float>(ih), withOpacity(ground, opacity), m);
    else
        sb.draw(x, y, w, h, withOpacity(ground, opacity));
    // faint white hairline edge (no shadow for HUD grounds)
    const Texture& rg = ringMask(iw, ih, r, 1.0f * scale);
    if (rg.valid())
        sb.draw(x, y, static_cast<float>(iw), static_cast<float>(ih), withOpacity(kWhite, 0.09f), rg);
}

}  // namespace style
}  // namespace ui
}  // namespace uaro
