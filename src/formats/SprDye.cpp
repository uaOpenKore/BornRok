#include "formats/SprDye.hpp"

#include <algorithm>

namespace uaro {

int dyeTruecolorFromGuide(Sprite& webp, const Sprite& guide, const std::array<SprPalColor, 256>& dye) {
    const auto& pal = guide.palette();
    const auto& gframes = guide.indexedFrames();
    auto& frames = webp.mutableRgbaFrames();
    // The dye shift of each index; indices the dye leaves alone (skin, outline) shift by 0.
    std::array<std::array<int, 3>, 256> shift{};
    bool any = false;
    for (int i = 1; i < 256; ++i) {
        shift[i] = {dye[i].r - pal[i].r, dye[i].g - pal[i].g, dye[i].b - pal[i].b};
        any = any || shift[i][0] || shift[i][1] || shift[i][2];
    }
    if (!any) return 0;
    const usize n = std::min<usize>({frames.size(), gframes.size(),
                                      webp.truecolorBase() ? webp.truecolorBase() : frames.size()});
    int done = 0;
    for (usize f = 0; f < n; ++f) {
        SprFrame& wf = frames[f];
        const SprFrame& gf = gframes[f];
        if (gf.width == 0 || gf.height == 0 || wf.width == 0 || wf.pixels.size() < usize(wf.width) * wf.height * 4)
            continue;
        if (wf.width % gf.width || wf.height % gf.height) continue;
        const int k = wf.width / gf.width;
        if (k < 1 || wf.height / gf.height != k) continue;
        const int gw = gf.width, gh = gf.height;
        for (int y = 0; y < wf.height; ++y) {
            const int sy = y / k;
            for (int x = 0; x < wf.width; ++x) {
                u8* p = &wf.pixels[(usize(y) * wf.width + x) * 4];
                if (p[3] == 0) continue;
                const int sx = x / k;
                int best = 0, bestD = 0x7fffffff;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int yy = sy + dy;
                    if (yy < 0 || yy >= gh) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = sx + dx;
                        if (xx < 0 || xx >= gw) continue;
                        const int i = gf.pixels[usize(yy) * gw + xx];
                        if (i == 0) continue;
                        const int dr = p[0] - pal[i].r, dg = p[1] - pal[i].g, db = p[2] - pal[i].b;
                        const int d = dr * dr + dg * dg + db * db;
                        if (d < bestD || (d == bestD && i < best)) { bestD = d; best = i; }
                    }
                }
                if (best == 0) continue;
                for (int c = 0; c < 3; ++c) p[c] = static_cast<u8>(std::clamp(p[c] + shift[best][c], 0, 255));
            }
        }
        ++done;
    }
    return done;
}

}  // namespace uaro
