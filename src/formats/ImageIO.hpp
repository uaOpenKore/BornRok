#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "formats/Image.hpp"

namespace uaro {

// Decode a texture image by sniffing its magic bytes: PNG/JPEG via stb (Png), BMP via the RO BMP
// decoder (keeps 8-bit palette + magenta-key handling), else TGA. Lets any texture slot accept the
// high-colour formats a GRF editor / roBrowser ship (PNG) alongside the classic RO BMP/TGA.
std::optional<Image> decodeImage(const std::vector<u8>& bytes);

// Punch out the RO magenta colour key (#ff00ff, incl. slightly-off dither/compression variants) to
// alpha 0, then DESPILL the anti-aliased rim: a leaf/flag edge painted over magenta keeps a pink
// tinge (R and B raised over G) that stays just outside the hard key (G >= 64, so real purples aren't
// killed) and shows as a pink halo. For every such pixel ADJACENT to a keyed one, subtract the magenta
// excess from R/B and fade alpha by it, so the rim melts to transparent like a proper alpha edge. The
// despill is edge-local: a solid purple/pink region with no neighbouring pure magenta is untouched.
// Returns the number of hard-keyed pixels (callers use it to pick POINT vs mip sampling for cutouts).
usize keyAndDespillMagenta(Image& img);

// True iff the image already carries a real alpha channel (has at least one fully-transparent texel).
// The content pipeline is moving to .webp with a proper alpha channel (S. 2026-08-09: "ассеты будут с
// альфаканалом"); a decoded texture that already has transparency must NOT be run through the legacy
// colour-key / background-key / despill helpers (they were only for opaque .bmp / flattened re-exports
// and would eat real pixels). All keying paths gate on this: proper-alpha webp -> keying is a no-op;
// legacy opaque .bmp (magenta key) / flattened opaque webp -> keyed as before.
bool hasTransparentPixels(const Image& img);
bool hasTransparentPixels(const std::vector<u8>& rgba);  // raw RGBA8 buffer overload

// The companion normal-map path for a diffuse texture: the extension is replaced with "_n.png"
// (S.'s convention "texture_n.png", for all textures and sprites). e.g.
//   "data/texture/foo.bmp"        -> "data/texture/foo_n.png"
//   "data/sprite/bar.png"         -> "data/sprite/bar_n.png"
//   "baz"          (no extension) -> "baz_n.png"
std::string normalMapPath(const std::string& diffusePath);

// "Bump from diffuse" (S.): derive a tangent-space normal map from the texture's own pixel
// LUMINANCE (treated as a height field, Sobel gradient -> normal, Z up, RGB = n*0.5+0.5).
// Used as a fallback when no hand-authored <tex>_n.png exists; strength scales the relief
// (~1.5 = subtle, ~3 = pronounced). Returns an invalid Image for invalid input.
Image normalFromLuminance(const Image& diffuse, float strength);

}  // namespace uaro
