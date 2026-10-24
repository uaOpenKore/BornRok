#include "render/Xbr.hpp"

#include <algorithm>
#include <cmath>

// Ported from kwebmn/ragnarok-graphics src/core/xbr.cpp (Hyllian xBR level 2). Operates on 0xAARRGGBB.
namespace uaro::xbr {
namespace {

// A pixel as its edges are judged: luma and the two colour differences weighted as Hyllian weighs them
// (48, 7, 6), alpha weighted as much as luma; a transparent pixel is one value whatever colour it holds.
struct Judged {
    float y, u, v, a;
};

Judged judge(uint32_t c) {
    if ((c >> 24) == 0) return Judged{0.0f, 0.0f, 0.0f, 0.0f};
    const float r = float((c >> 16) & 255) / 255.0f, g = float((c >> 8) & 255) / 255.0f,
                b = float(c & 255) / 255.0f;
    return Judged{48.0f * (0.299f * r + 0.587f * g + 0.114f * b),
                  7.0f * (-0.169f * r - 0.331f * g + 0.499f * b),
                  6.0f * (0.499f * r - 0.418f * g - 0.0813f * b), 48.0f * float(c >> 24) / 255.0f};
}

float distance(const Judged& p, const Judged& q) {
    return std::fabs(p.y - q.y) + std::fabs(p.u - q.u) + std::fabs(p.v - q.v) + std::fabs(p.a - q.a);
}

constexpr float kSame = 0.6f;  // colours the eye takes for one (Hyllian's 155 on his integer scale)

struct Premul {  // premultiplied: a transparent neighbour lends no colour
    float r, g, b, a;
};

Premul premul(uint32_t c) {
    const float a = float(c >> 24) / 255.0f;
    return Premul{a * float((c >> 16) & 255), a * float((c >> 8) & 255), a * float(c & 255), a};
}

void blend(Premul& p, const Premul& q, float t) {
    p.r += (q.r - p.r) * t;
    p.g += (q.g - p.g) * t;
    p.b += (q.b - p.b) * t;
    p.a += (q.a - p.a) * t;
}

uint32_t straight(const Premul& p) {
    const long a = std::lround(p.a * 255.0f);
    if (a <= 0) return 0u;
    const auto c = [&p](float v) { return uint32_t(std::clamp(std::lround(v / p.a), 0L, 255L)); };
    return uint32_t(std::min(a, 255L)) << 24 | c(p.r) << 16 | c(p.g) << 8 | c(p.b);
}

}  // namespace

std::vector<uint32_t> scale4(const uint32_t* argb, int w, int h) {
    if (!argb || w <= 0 || h <= 0) return {};
    const size_t n = size_t(w) * size_t(h);
    std::vector<uint32_t> same(n);  // compared whole: transparent is 0
    std::vector<Judged> judged(n);
    for (size_t i = 0; i < n; ++i) {
        same[i] = (argb[i] >> 24) ? argb[i] : 0u;
        judged[i] = judge(argb[i]);
    }
    std::vector<uint32_t> out(n * 16);
    const size_t W = size_t(w) * 4;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t E = size_t(y) * size_t(w) + size_t(x);
            Premul block[16];
            for (Premul& p : block) p = premul(same[E]);
            for (int corner = 0; corner < 4; ++corner) {
                // The rule of the lower-right corner, mirrored to this one: neighbours and block pixels.
                const int fx = corner & 1 ? -1 : 1, fy = corner & 2 ? -1 : 1;
                const auto at = [&](int dx, int dy) {
                    const int px = std::clamp(x + fx * dx, 0, w - 1), py = std::clamp(y + fy * dy, 0, h - 1);
                    return size_t(py) * size_t(w) + size_t(px);
                };
                const size_t F = at(1, 0), H = at(0, 1), I = at(1, 1), C = at(1, -1), G = at(-1, 1),
                             D = at(-1, 0);
                const size_t B = at(0, -1), F4 = at(2, 0), I4 = at(2, 1), H5 = at(0, 2), I5 = at(1, 2);
                if (same[E] == same[H] || same[E] == same[F]) continue;
                const auto d = [&judged](size_t p, size_t q) { return distance(judged[p], judged[q]); };
                const auto eq = [&d](size_t p, size_t q) { return d(p, q) < kSame; };
                // A right angle (a box's or a letter's corner): E going on to the left and up, the rest
                // outside -- stays one: rounding it would melt the letters this is for.
                if (eq(E, D) && eq(E, B) && eq(F, I) && eq(H, I) && eq(F, C) && eq(H, G)) continue;
                const float across = d(E, C) + d(E, G) + d(I, H5) + d(I, F4) + 4.0f * d(H, F);  // H-F edge
                const float along = d(H, D) + d(H, I5) + d(F, I4) + d(F, B) + 4.0f * d(E, I);   // E-I edge
                const Premul px = premul(same[d(E, F) <= d(E, H) ? F : H]);
                const auto sub = [&block, fx, fy](int sx, int sy) -> Premul& {
                    return block[(fy > 0 ? sy : 3 - sy) * 4 + (fx > 0 ? sx : 3 - sx)];
                };
                const bool edge = across < along && ((!eq(F, B) && !eq(H, D)) ||
                                                     (eq(E, I) && !eq(F, I4) && !eq(H, I5)) || eq(E, G) ||
                                                     eq(E, C));
                if (!edge) {
                    if (across <= along) blend(sub(3, 3), px, 0.5f);
                    continue;
                }
                // The edge's slope: 1:2 lying (left), 2:1 standing (up), both, or 1:1.
                const float ke = d(F, G), ki = d(H, C);
                const bool left = 2.0f * ke <= ki && same[E] != same[G] && same[D] != same[G];
                const bool up = ke >= 2.0f * ki && same[E] != same[C] && same[B] != same[C];
                if (left && up) {
                    for (const auto& s : {std::make_pair(3, 3), std::make_pair(2, 3), std::make_pair(3, 2)})
                        blend(sub(s.first, s.second), px, 1.0f);
                    blend(sub(1, 3), px, 0.75f);
                    blend(sub(3, 1), px, 0.75f);
                    blend(sub(0, 3), px, 0.25f);
                    blend(sub(3, 0), px, 0.25f);
                    blend(sub(2, 2), px, 0.25f);
                } else if (left) {
                    blend(sub(3, 3), px, 1.0f);
                    blend(sub(2, 3), px, 1.0f);
                    blend(sub(1, 3), px, 0.75f);
                    blend(sub(3, 2), px, 0.75f);
                    blend(sub(0, 3), px, 0.25f);
                    blend(sub(2, 2), px, 0.25f);
                } else if (up) {
                    blend(sub(3, 3), px, 1.0f);
                    blend(sub(3, 2), px, 1.0f);
                    blend(sub(3, 1), px, 0.75f);
                    blend(sub(2, 3), px, 0.75f);
                    blend(sub(3, 0), px, 0.25f);
                    blend(sub(2, 2), px, 0.25f);
                } else {
                    blend(sub(3, 3), px, 1.0f);
                    blend(sub(2, 3), px, 0.5f);
                    blend(sub(3, 2), px, 0.5f);
                }
            }
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx)
                    out[(size_t(y) * 4 + size_t(sy)) * W + size_t(x) * 4 + size_t(sx)] =
                        straight(block[sy * 4 + sx]);
        }
    return out;
}

}  // namespace uaro::xbr
