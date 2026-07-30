#include "world/GroundMesh.hpp"

#include <algorithm>
#include <cmath>

#include "core/math/Math.hpp"

namespace uaro {

namespace {
constexpr f32 kTile = 1.0f;  // cell world size in x/z (tunable; renderer may scale)

// GND stores vertex colour as B,G,R,A; bgfx Color0 wants 0xAABBGGRR.
u32 packColor(const u8 c[4]) {
    return static_cast<u32>(c[2]) | (static_cast<u32>(c[1]) << 8) |
           (static_cast<u32>(c[0]) << 16) | (static_cast<u32>(c[3]) << 24);
}

struct Quad {
    i16 tex;
    GroundVertex v[4];
};

void setNormal(Quad& q) {
    Vec3 a{q.v[1].x - q.v[0].x, q.v[1].y - q.v[0].y, q.v[1].z - q.v[0].z};
    Vec3 b{q.v[2].x - q.v[0].x, q.v[2].y - q.v[0].y, q.v[2].z - q.v[0].z};
    Vec3 n = normalize(cross(a, b));
    for (auto& gv : q.v) {
        gv.nx = n.x;
        gv.ny = n.y;
        gv.nz = n.z;
    }
}

GroundVertex makeVert(f32 x, f32 y, f32 z, f32 u, f32 v, u32 color) {
    GroundVertex gv{};
    gv.x = x * kTile;
    // GND heights are RSW-scale and Y-down (like RSW object positions); convert to
    // world Y the same way models do (x -0.1) so terrain and models share one Y
    // space. Without this the raw (x1, un-flipped) heights tower over the models
    // and bury low decorations (e.g. the prontera fountain basin) under the ground.
    gv.y = -y * 0.1f;
    gv.z = z * kTile;
    gv.u = u;
    gv.v = v;
    gv.color = color;
    return gv;
}
} // namespace

GroundMesh GroundMesh::build(const Gnd& gnd) {
    const u32 W = gnd.width(), H = gnd.height();
    const auto& cubes = gnd.cubes();
    const auto& surfs = gnd.surfaces();

    auto cubeAt = [&](u32 x, u32 y) -> const GndCube& {
        return cubes[static_cast<usize>(y) * W + x];
    };
    auto surfaceOk = [&](i32 idx) { return idx >= 0 && static_cast<usize>(idx) < surfs.size(); };

    // Smooth the floor (top-tile) colours across shared grid corners, the way roBrowser
    // bilinearly smooths its per-tile colour map (Ground.js: draws the WxH tile colours
    // onto a 2x canvas "to smooth"). Each grid corner takes the average colour of the
    // up-tiles meeting there (up to 4), so neighbouring tiles share corner colours and
    // the flat per-tile squares blur into a continuous gradient instead of hard blocks.
    // Walls keep their own flat tint (they are vertical, the banding is not visible there).
    auto cornerColor = [&](i32 gx, i32 gy) -> u32 {
        // Gather the up-tile colours of the (up to 4) cells meeting at this corner.
        const u8* cols[4];
        int ncol = 0;
        for (i32 dy = -1; dy <= 0; ++dy)
            for (i32 dx = -1; dx <= 0; ++dx) {
                const i32 cx = gx + dx, cy = gy + dy;
                if (cx < 0 || cy < 0 || cx >= static_cast<i32>(W) || cy >= static_cast<i32>(H))
                    continue;
                const GndCube& cc = cubeAt(static_cast<u32>(cx), static_cast<u32>(cy));
                if (!surfaceOk(cc.tileUp)) continue;
                cols[ncol++] = surfs[cc.tileUp].color;
            }
        if (ncol == 0) return 0xffffffffu;
        // Average, but DROP near-black outlier tiles (RO bakes occluded floor cells to
        // ~black; scattered among bright floor they would bleed dark dots into their
        // neighbours' corners). Keep them only if every tile here is dark, so genuinely
        // shadowed regions stay dark. constexpr threshold ~ luminance 48.
        u32 sum[4] = {0, 0, 0, 0};
        int nn = 0;
        for (int k = 0; k < ncol; ++k) {
            const int lum = (cols[k][0] + cols[k][1] + cols[k][2]) / 3;  // BGRA -> rough luma
            if (lum >= 48) {
                for (int i = 0; i < 4; ++i) sum[i] += cols[k][i];
                ++nn;
            }
        }
        if (nn == 0) {  // all dark: keep them rather than forcing white
            for (int k = 0; k < ncol; ++k)
                for (int i = 0; i < 4; ++i) sum[i] += cols[k][i];
            nn = ncol;
        }
        const u8 c[4] = {static_cast<u8>(sum[0] / nn), static_cast<u8>(sum[1] / nn),
                         static_cast<u8>(sum[2] / nn), static_cast<u8>(sum[3] / nn)};
        return packColor(c);
    };

    std::vector<Quad> quads;
    quads.reserve(static_cast<usize>(W) * H);

    for (u32 y = 0; y < H; ++y) {
        for (u32 x = 0; x < W; ++x) {
            const GndCube& c = cubeAt(x, y);
            // Mirror the X axis (swap left<->right) so the map matches the original
            // client's handedness. Baked into the vertices so heightAt() and the
            // per-quad normals (computed below) stay in the same mirrored space.
            const f32 xm = static_cast<f32>(W - x);
            const f32 xm1 = static_cast<f32>(W - (x + 1));

            if (surfaceOk(c.tileUp)) {  // top tile
                const GndSurface& s = surfs[c.tileUp];
                const i32 ix = static_cast<i32>(x), iy = static_cast<i32>(y);
                // Corner-averaged (smoothed) colours instead of one flat tile colour.
                const u32 c00 = cornerColor(ix, iy),         c10 = cornerColor(ix + 1, iy);
                const u32 c01 = cornerColor(ix, iy + 1),     c11 = cornerColor(ix + 1, iy + 1);
                Quad q;
                q.tex = s.textureId;
                q.v[0] = makeVert(xm,  c.height[0], y,     s.u[0], s.v[0], c00);
                q.v[1] = makeVert(xm1, c.height[1], y,     s.u[1], s.v[1], c10);
                q.v[2] = makeVert(xm,  c.height[2], y + 1, s.u[2], s.v[2], c01);
                q.v[3] = makeVert(xm1, c.height[3], y + 1, s.u[3], s.v[3], c11);
                setNormal(q);
                quads.push_back(q);
            }
            if (surfaceOk(c.tileFront) && y + 1 < H) {  // front wall (+y)
                const GndCube& n = cubeAt(x, y + 1);
                const GndSurface& s = surfs[c.tileFront];
                const u32 col = packColor(s.color);
                Quad q;
                q.tex = s.textureId;
                q.v[0] = makeVert(xm,  c.height[2], y + 1, s.u[0], s.v[0], col);
                q.v[1] = makeVert(xm1, c.height[3], y + 1, s.u[1], s.v[1], col);
                q.v[2] = makeVert(xm,  n.height[0], y + 1, s.u[2], s.v[2], col);
                q.v[3] = makeVert(xm1, n.height[1], y + 1, s.u[3], s.v[3], col);
                setNormal(q);
                quads.push_back(q);
            }
            if (surfaceOk(c.tileRight) && x + 1 < W) {  // right wall (+x)
                const GndCube& n = cubeAt(x + 1, y);
                const GndSurface& s = surfs[c.tileRight];
                const u32 col = packColor(s.color);
                Quad q;
                q.tex = s.textureId;
                q.v[0] = makeVert(xm1, c.height[1], y,     s.u[0], s.v[0], col);
                q.v[1] = makeVert(xm1, c.height[3], y + 1, s.u[1], s.v[1], col);
                q.v[2] = makeVert(xm1, n.height[0], y,     s.u[2], s.v[2], col);
                q.v[3] = makeVert(xm1, n.height[2], y + 1, s.u[3], s.v[3], col);
                setNormal(q);
                quads.push_back(q);
            }
        }
    }

    // Group quads by texture so each texture is one contiguous index batch.
    std::stable_sort(quads.begin(), quads.end(),
                     [](const Quad& a, const Quad& b) { return a.tex < b.tex; });

    GroundMesh mesh;
    mesh.vertices.reserve(quads.size() * 4);
    mesh.indices.reserve(quads.size() * 6);

    if (!quads.empty()) {
        GroundBatch batch{quads.front().tex, 0, 0};
        for (const Quad& q : quads) {
            if (q.tex != batch.textureId) {
                mesh.batches.push_back(batch);
                batch = GroundBatch{q.tex, static_cast<u32>(mesh.indices.size()), 0};
            }
            const u32 base = static_cast<u32>(mesh.vertices.size());
            for (const auto& gv : q.v) mesh.vertices.push_back(gv);
            const u32 idx[6] = {base, base + 1, base + 2, base + 2, base + 1, base + 3};
            for (u32 i : idx) mesh.indices.push_back(i);
            batch.indexCount += 6;
        }
        mesh.batches.push_back(batch);
    }

    return mesh;
}

std::vector<u8> GroundMesh::buildLightmap(const Gnd& gnd, int& outW, int& outH) {
    const int W = static_cast<int>(gnd.width()), H = static_cast<int>(gnd.height());
    const int lmW = gnd.lightmapW(), lmH = gnd.lightmapH();
    const std::vector<u8>& lm = gnd.lightmap();
    if (W <= 0 || H <= 0 || lmW <= 0 || lmH <= 0 || lm.empty()) {
        outW = outH = 0;
        return {};
    }
    outW = W * lmW;
    outH = H * lmH;
    std::vector<u8> img(static_cast<usize>(outW) * outH * 4, 0);
    const usize tileStride = static_cast<usize>(lmW) * lmH * 4;
    const auto& cubes = gnd.cubes();
    const auto& surfs = gnd.surfaces();

    // Normalize runaway coloured-light bakes. The additive RGB layer is torch/lamp pools on
    // most maps (dungeons average 14-32 map-wide), but a few maps are flooded with it:
    // airport averages 65.8 — the whole map baked in yellow lamp light — and renders blinding
    // (S.: "airport пересвет, убрать яркость на 70%"). Maps above the gap (>40) get their
    // colour layer scaled down to a dungeon-strength average; everything else is untouched.
    float colorScale = 1.0f;
    {
        const int tiles = gnd.lightmapCount();
        const usize per = static_cast<usize>(lmW) * lmH;
        u64 sum = 0;
        for (int t2 = 0; t2 < tiles; ++t2) {
            const u8* rgb = lm.data() + static_cast<usize>(t2) * tileStride + per;
            for (usize i = 0; i < per * 3; ++i) sum += rgb[i];
        }
        const float avg = tiles ? static_cast<float>(sum) / (static_cast<float>(tiles) * per * 3)
                                : 0.0f;
        if (avg > 40.0f) colorScale = 20.0f / avg;
    }

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const GndCube& c = cubes[static_cast<usize>(y) * W + x];
            i16 lid = -1;
            if (c.tileUp >= 0 && static_cast<usize>(c.tileUp) < surfs.size())
                lid = surfs[c.tileUp].lightmapId;
            const bool ok =
                lid >= 0 && static_cast<usize>(lid) * tileStride + tileStride <= lm.size();
            const usize tb = ok ? static_cast<usize>(lid) * tileStride : 0;
            const usize cb = tb + static_cast<usize>(lmW) * lmH;  // RGB block start
            // RO bakes scattered "unlit" texels to brightness 0 (the lightmap A is binary:
            // 0 or >=80, nothing between), and they render as hard black dots at tile
            // corners. Fill each 0 with the cell's average LIT brightness so it blends in;
            // an all-0 cell stays full-bright like a no-surface cell.
            u8 fillA = 255;
            if (ok) {
                u32 sum = 0, cnt = 0;
                for (int k = 0; k < lmW * lmH; ++k) {
                    const u8 a = lm[tb + static_cast<usize>(k)];
                    if (a) { sum += a; ++cnt; }
                }
                if (cnt) fillA = static_cast<u8>(sum / cnt);
            }
            for (int sy = 0; sy < lmH; ++sy) {
                for (int sx = 0; sx < lmW; ++sx) {
                    const usize dst =
                        (static_cast<usize>(y * lmH + sy) * outW + (x * lmW + sx)) * 4;
                    const usize t = static_cast<usize>(sy) * lmW + sx;
                    if (ok) {
                        img[dst + 0] = static_cast<u8>(lm[cb + t * 3 + 0] * colorScale);
                        img[dst + 1] = static_cast<u8>(lm[cb + t * 3 + 1] * colorScale);
                        img[dst + 2] = static_cast<u8>(lm[cb + t * 3 + 2] * colorScale);
                        const u8 a = lm[tb + t];
                        img[dst + 3] = a ? a : fillA;  // fill the 0 "unlit" dots with the cell avg
                    } else {
                        img[dst + 0] = 0;  // neutral: no coloured light
                        img[dst + 1] = 0;
                        img[dst + 2] = 0;
                        img[dst + 3] = 255;  // full brightness (no shadow)
                    }
                }
            }
        }
    }
    // Smooth the baked shadow: RO dithers soft shadows/AO into the 8x8-per-cell lightmap, which
    // magnifies into a blotchy checkerboard at close zoom (S.: "пятна местами есть... зрительный
    // ужас", a shadow should be even). A small separable box blur averages the dither — and
    // neighbouring cells, adjacent in this position-keyed atlas — into the smooth shading the
    // original client shows. (roBrowser leans on a per-tile UV inset + bilinear, which keeps the
    // dither; an explicit blur is what actually makes it even.)
    // Light smoothing: the raw 8x8-per-cell lightmap gives crisp but JAGGED shadow edges (stair-steps,
    // esp. flag shadows — S.: "тени ломанные, выровнять, сглаживание небольшое"). Blend a small amount
    // of a 3x3 blur back into the sharp lightmap: evens the stair-steps without the old full-blur wash.
    // kBlurMix 0 = crisp/jagged, 1 = fully soft. GRF Editor's shadows are smooth, so ~0.5.
    constexpr int kBlurR = 2;         // wider kernel = the actual edge smoothing (upsampling was a no-op)
    constexpr float kBlurMix = 0.3f;  // S.: "сильно размывает, уменьши в 2 раза" — halved 0.6 -> 0.3
    {
        const std::vector<u8> sharp = img;  // keep the crisp version to blend the blur back into
        const int N = 2 * kBlurR + 1;
        std::vector<u8> tmp(img.size());
        for (int y = 0; y < outH; ++y)  // horizontal pass: img -> tmp (sliding window)
            for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int k = -kBlurR; k <= kBlurR; ++k)
                    sum += img[(static_cast<usize>(y) * outW + std::clamp(k, 0, outW - 1)) * 4 + c];
                for (int x = 0; x < outW; ++x) {
                    tmp[(static_cast<usize>(y) * outW + x) * 4 + c] = static_cast<u8>(sum / N);
                    sum += img[(static_cast<usize>(y) * outW + std::clamp(x + kBlurR + 1, 0, outW - 1)) * 4 + c] -
                           img[(static_cast<usize>(y) * outW + std::clamp(x - kBlurR, 0, outW - 1)) * 4 + c];
                }
            }
        for (int x = 0; x < outW; ++x)  // vertical pass: tmp -> img (sliding window)
            for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int k = -kBlurR; k <= kBlurR; ++k)
                    sum += tmp[(static_cast<usize>(std::clamp(k, 0, outH - 1)) * outW + x) * 4 + c];
                for (int y = 0; y < outH; ++y) {
                    img[(static_cast<usize>(y) * outW + x) * 4 + c] = static_cast<u8>(sum / N);
                    sum += tmp[(static_cast<usize>(std::clamp(y + kBlurR + 1, 0, outH - 1)) * outW + x) * 4 + c] -
                           tmp[(static_cast<usize>(std::clamp(y - kBlurR, 0, outH - 1)) * outW + x) * 4 + c];
                }
            }
        // Blend the blur back per channel: keep the SHADOW (alpha) crisp (light kBlurMix), but blur the
        // COLOURED LIGHT (rgb, torches/lamps) heavily — RO bakes it as a sparse dither, and the gaps
        // between the dithered orange texels are what show as "чёрные точки в канализации" (S.); a soft
        // glow has no gaps. Colour is meant to be a smooth glow anyway, so heavy blur only helps.
        constexpr float kBlurMixColor = 0.9f;
        for (usize i = 0; i < img.size(); ++i) {
            const float mix = ((i & 3u) == 3u) ? kBlurMix : kBlurMixColor;  // alpha vs rgb
            img[i] = static_cast<u8>(std::lround(sharp[i] * (1.0f - mix) + img[i] * mix));
        }
    }

    // Deepen the baked shadows so they read DARK and crisp like GRF Editor's map preview instead of
    // the pale wash (S.: "тени сильнее, чётче, как в GRF Editor"). This is the real lever — the shader
    // faithfully renders whatever the lightmap holds, and the blur+fill above smooth RO's dither but
    // also LIGHTEN shadows; a gamma>1 curve on the shadow alpha restores the darkness (fully-lit a=255
    // stays 255, mid/low alphas drop hard) without reintroducing the dither blotches. Tunable via
    // kShadowGamma. Alpha only — the coloured light (RGB, torches) is untouched.
    constexpr float kShadowGamma = 1.9f;
    // Floor the gamma'd shadow so RO's heavily-dithered dungeon lightmaps don't crush to PURE BLACK
    // dots (S.: "чёрные точки в канализации"). 0.12 is well below any real town shadow (those gamma to
    // ~0.26+), so towns keep their accepted darkness; it only lifts the near-0 dither/baked-a=0 texels
    // off black so they read as dark grey, not speckle.
    constexpr float kMinShadow = 0.12f;
    const u8 minA = static_cast<u8>(kMinShadow * 255.0f + 0.5f);
    for (usize i = 3; i < img.size(); i += 4) {
        const float a = static_cast<float>(img[i]) / 255.0f;
        const u8 g = static_cast<u8>(std::pow(a, kShadowGamma) * 255.0f + 0.5f);
        img[i] = g < minA ? minA : g;
    }

    return img;
}

} // namespace uaro
