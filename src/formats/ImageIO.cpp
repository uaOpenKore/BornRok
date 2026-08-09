#include "formats/ImageIO.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "formats/Bmp.hpp"
#include "formats/Png.hpp"
#include "formats/Tga.hpp"
#include "formats/Webp.hpp"

namespace uaro {

bool hasTransparentPixels(const std::vector<u8>& rgba) {
    // A real alpha channel means at least one fully-transparent texel. Proper .webp effect/sprite/icon
    // assets have large alpha==0 regions; legacy opaque .bmp (magenta-keyed) and flattened re-exports
    // decode fully opaque (no alpha==0). This one bit decides whether the legacy keyers must run.
    for (usize i = 3; i < rgba.size(); i += 4)
        if (rgba[i] == 0) return true;
    return false;
}

bool hasTransparentPixels(const Image& img) { return hasTransparentPixels(img.rgba); }

usize keyAndDespillMagenta(Image& img) {
    const int w = static_cast<int>(img.width), h = static_cast<int>(img.height);
    if (w <= 0 || h <= 0 || img.rgba.size() < static_cast<usize>(w) * h * 4) return 0;
    // Content moving to .webp-with-alpha: if the texture already has a real alpha channel, it is a
    // proper cutout -- do NOT magenta-key/despill it (that would eat real magenta/pink art). Only the
    // legacy opaque .bmp path (magenta as the transparent key) needs this. (S. 2026-08-09.)
    if (hasTransparentPixels(img)) return 0;
    const int n = w * h;
    // Pass 1: hard key. A magenta-dominant texel (high R+B, low G) is the transparent colour key; a
    // slightly-off dither/compression variant counts too. Zero RGBA so no bilinear/mip bleed either.
    std::vector<u8> key(static_cast<usize>(n), 0);
    usize keyed = 0;
    for (int i = 0; i < n; ++i) {
        u8* p = &img.rgba[static_cast<usize>(i) * 4];
        // #ff00ff key OR a strongly magenta-tinted texel whose G crept above 64 (HD-PNG resample /
        // JPEG-DXT compression pushes the key colour to e.g. (200,80,200), which the strict G<64 test
        // misses -> it stayed opaque pink on foliage/leaf edges, S.: "магента пробивается на листве").
        // spill = how far R and B both exceed G; >100 with both channels high is unmistakably the key,
        // never real purple art (a purple robe's spill is ~30-60), so this doesn't over-eat.
        const int spill0 = std::min<int>(p[0], p[2]) - static_cast<int>(p[1]);
        // Three ways to recognise the magenta transparent key:
        //  (1) crisp #ff00ff (G<64);
        //  (2) a strongly magenta-tinted texel (spill>100, both channels high) -- HD/DXT variants;
        //  (3) a COMPRESSED key texel: high spill + LOW green. The leftover pink S. still saw was
        //      SKEWED toward red, e.g. (177,34,126)/(183,14,114) — R≫B, so the old |R-B|<45 gate WRONGLY
        //      excluded it (logged worst=… residPink). The real signature of the magenta key is spill
        //      (both R,B above G) + a LOW G (the key is #ff00ff, G=0; compression keeps G small). Gate on
        //      G, NOT on R≈B. This spares pure blue (spill<0), pure red (spill≈0) and purple BUILDING
        //      stone (its G is high, e.g. (149,120,189) G=120) while catching the red-skewed leaf pink.
        if ((p[0] > 160 && p[1] < 64 && p[2] > 160) || (spill0 > 100 && p[0] > 120 && p[2] > 120) ||
            (spill0 > 55 && p[1] < 70)) {
            key[static_cast<usize>(i)] = 1;
            p[0] = p[1] = p[2] = p[3] = 0;
            ++keyed;
        }
    }
    if (keyed == 0) return 0;  // no key present -> leave real purples/pinks completely alone
    // Pass 2: despill the anti-aliased rim, PROPAGATED outward a few pixels. A hi-res (2x/4x) texture
    // has a rim several pixels wide, so a single 1px-border pass leaves pink fringing (S.: "магента
    // пробивается на листве деревьев"). Iterate: each round despills magenta-tinted pixels bordering an
    // already-keyed/despilled pixel and marks them, so the cleanup spreads only ALONG the magenta rim
    // (a solid purple region far from any key is still never touched). "Spill" = how far R and B sit
    // above G; subtract it from R/B to kill the pink and fade alpha so the edge dissolves to transparent.
    // The anti-aliased magenta rim scales with resolution: a 4x/8x HD (webp) foliage/leaf texture has a
    // rim several times wider than a 64px .bmp, so a fixed 4-ring despill left magenta on HD (S.: "опять
    // магента на листве появилась" after the webp HD switch). Scale the ring count with the texture's
    // larger dimension (still stops at the first non-magenta pixel, so it never over-eats a solid purple).
    const int kDespillRadius = std::clamp(std::max(w, h) / 24, 4, 20);
    for (int pass = 0; pass < kDespillRadius; ++pass) {
        const std::vector<u8> prev = key;  // snapshot: propagate exactly ONE ring per pass (no cascade
                                           // in scan order, so the radius bound holds)
        bool any = false;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const usize i = static_cast<usize>(y) * w + x;
                if (key[i]) continue;
                u8* p = &img.rgba[i * 4];
                const int spill = std::min<int>(p[0], p[2]) - static_cast<int>(p[1]);
                if (spill <= 0) continue;  // not magenta-tinted
                bool nearKey = false;
                for (int dy = -1; dy <= 1 && !nearKey; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        if (prev[static_cast<usize>(ny) * w + nx]) { nearKey = true; break; }
                    }
                if (!nearKey) continue;
                p[0] = static_cast<u8>(static_cast<int>(p[0]) - spill);
                p[2] = static_cast<u8>(static_cast<int>(p[2]) - spill);
                const int a = static_cast<int>(p[3]) - spill;
                p[3] = static_cast<u8>(a < 0 ? 0 : a);
                key[i] = 1;  // cleaned -> the NEXT pass propagates one ring further out
                any = true;
            }
        if (!any) break;  // rim fully cleaned
    }
    // Pass 3: a final GLOBAL sweep of any leftover magenta-tinted OPAQUE texel the rim propagation
    // never reached — ISOLATED interior specks an HD upscale / webp compression scatters INSIDE the
    // leaf (not bordering the pure key, so pass 2's chain walk stops short of them). S. still saw "пара
    // розовых точек в середине" after pass 2. Only reached when keyed>0 (this texture uses magenta as
    // its colour key), so a non-keyed texture — real purple art — already returned above untouched. On
    // a magenta-keyed cutout (RO reserves magenta for transparency, never for real colour) any strongly
    // magenta-tinted texel is a key artifact; neutralise R/B down to G. Only a few dozen pixels qualify
    // on a 1 MP foliage texture, so this cannot eat the green (verified offline on newtree_02.webp).
    for (int i = 0; i < n; ++i) {
        if (key[i]) continue;
        u8* p = &img.rgba[static_cast<usize>(i) * 4];
        // A leftover magenta-KEY speck: R and B BOTH above G (spill>0) with a LOW green. Desaturate it by
        // pulling R,B down to G. Gate on LOW G (not on R≈B): the real leftover pink was red-skewed, e.g.
        // (177,34,126) R≫B, which an |R-B| gate wrongly spared (S. still saw it). Blue (spill<0), red
        // (spill≈0) and purple building stone (high G, e.g. (149,120,189) G=120) are spared; only the
        // low-G magenta-key blend is neutralised. (The channel-clamp that once killed UI blue is gone —
        // this is a spill-based subtract, which can never touch blue since blue's spill is negative.)
        const int spill = std::min<int>(p[0], p[2]) - static_cast<int>(p[1]);
        if (spill > 25 && p[1] < 90) {
            p[0] = static_cast<u8>(static_cast<int>(p[0]) - spill);
            p[2] = static_cast<u8>(static_cast<int>(p[2]) - spill);
        }
    }
    return keyed;
}

std::optional<Image> decodeImage(const std::vector<u8>& bytes) {
    if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' &&
        bytes[3] == 'G')
        return Png::decode(bytes);  // PNG
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
        return Png::decode(bytes);  // JPEG (stb handles it too)
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M')
        return Bmp::decode(bytes);  // BMP (RO 8-bit palette + magenta key)
    if (bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' &&
        bytes[3] == 'F' && bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P')
        return Webp::decode(bytes);  // WebP (libwebp; nullopt if the build lacks it)
    // TGA has no reliable magic; treat anything else as TGA (RO's other native texture format).
    return Tga::decode(bytes);
}

Image normalFromLuminance(const Image& diffuse, float strength) {
    // Classic "bump from diffuse" (S.): pixel luminance = height field; the Sobel gradient
    // of that field becomes the tangent-space normal (RGB = n*0.5+0.5, Z up), so the #107
    // shaders consume it unchanged. A hand-authored _n.png wins over this at the call sites.
    Image out;
    if (!diffuse.valid()) return out;
    const u32 w = diffuse.width, h = diffuse.height;
    std::vector<float> lum(static_cast<usize>(w) * h);
    for (usize i = 0; i < lum.size(); ++i) {
        const u8* px = &diffuse.rgba[i * 4];
        lum[i] = (0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2]) / 255.0f;
    }
    out.width = w;
    out.height = h;
    out.rgba.resize(static_cast<usize>(w) * h * 4);
    auto L = [&](i32 x, i32 y) {  // clamp-at-edge sampling
        x = x < 0 ? 0 : (x >= static_cast<i32>(w) ? static_cast<i32>(w) - 1 : x);
        y = y < 0 ? 0 : (y >= static_cast<i32>(h) ? static_cast<i32>(h) - 1 : y);
        return lum[static_cast<usize>(y) * w + x];
    };
    for (i32 y = 0; y < static_cast<i32>(h); ++y)
        for (i32 x = 0; x < static_cast<i32>(w); ++x) {
            const float gx = (L(x + 1, y - 1) + 2.0f * L(x + 1, y) + L(x + 1, y + 1)) -
                             (L(x - 1, y - 1) + 2.0f * L(x - 1, y) + L(x - 1, y + 1));
            const float gy = (L(x - 1, y + 1) + 2.0f * L(x, y + 1) + L(x + 1, y + 1)) -
                             (L(x - 1, y - 1) + 2.0f * L(x, y - 1) + L(x + 1, y - 1));
            const float nx = -gx * strength, ny = -gy * strength, nz = 1.0f;
            const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            u8* q = &out.rgba[(static_cast<usize>(y) * w + x) * 4];
            q[0] = static_cast<u8>((nx * inv * 0.5f + 0.5f) * 255.0f + 0.5f);
            q[1] = static_cast<u8>((ny * inv * 0.5f + 0.5f) * 255.0f + 0.5f);
            q[2] = static_cast<u8>((nz * inv * 0.5f + 0.5f) * 255.0f + 0.5f);
            q[3] = 255;
        }
    return out;
}

std::string normalMapPath(const std::string& diffusePath) {
    // Replace the extension after the last '.' (only if that '.' is in the final path segment) with
    // "_n.png"; if there is no extension, just append "_n.png".
    const usize slash = diffusePath.find_last_of("/\\");
    const usize dot = diffusePath.find_last_of('.');
    const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    const std::string stem = hasExt ? diffusePath.substr(0, dot) : diffusePath;
    return stem + "_n.png";
}

}  // namespace uaro
