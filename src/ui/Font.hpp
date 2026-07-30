#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Types.hpp"
#include "render/Texture.hpp"

namespace uaro {

class SpriteBatch;

// Proportional text rendered from a real TrueType font (NotoSans, the same face
// korangar bakes), rasterized once at startup into an alpha atlas via
// stb_truetype and drawn through SpriteBatch with per-glyph sub-UV quads. The
// atlas is tinted by the vertex colour at draw time, so `abgr` colours the text.
// Covers Latin (0x20..0x7E) and Cyrillic (0x400..0x45F) — Cyrillic so Russian
// names/chat render. UTF-8 input is decoded per draw. If the font asset is
// missing the class falls back to an embedded 5x7 ASCII bitmap so text never
// vanishes. Owned by Application; init() once after bgfx is up.
class Font {
public:
    // Loads <assetDir>/fonts/NotoSans.ttf and bakes the glyph atlas. Falls back
    // to the embedded bitmap (and still returns true) if the TTF can't be read.
    bool init(const std::string& assetDir);
    void shutdown();
    bool ready() const { return atlas_.valid(); }

    // Draw `text` (UTF-8) at top-left (x,y); `abgr` tints it. Supports '\n'.
    // `scale` multiplies a nominal design size (see kRenderPx); 1.5 is the common
    // UI size, 2.0 a heading.
    void draw(SpriteBatch& sb, float x, float y, float scale, u32 abgr,
              const std::string& text) const;

    // Width (px) of the longest line in `text` and the per-line step, at `scale`.
    float width(const std::string& text, float scale) const;
    float lineHeight(float scale) const;
    // Pixel advance of the space glyph — a representative monospace step for the
    // few callers that still need one (proportional callers use width()).
    float advance(float scale) const;

private:
    // One baked glyph: atlas UV box + placement/advance, all in bake pixels.
    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // atlas UV
        float w = 0, h = 0;                     // quad size (bake px)
        float xoff = 0, yoff = 0;               // offset from pen/baseline (bake px)
        float xadv = 0;                         // pen advance (bake px)
    };
    bool initTtf(const std::string& path);  // load a .ttf from disk; false if unusable
    bool packTtf(const unsigned char* ttf, std::size_t size);  // set up the on-demand glyph atlas
    bool initEmbedded();                    // bake the font baked into the binary (no disk needed)
    void initBitmap();                      // embedded 5x7 fallback atlas
    void drawBitmap(SpriteBatch& sb, float x, float y, float scale, u32 abgr,
                    const std::string& text) const;
    // Look up a codepoint's glyph, rasterizing + packing it into the atlas on first use (the atlas is
    // dynamic so any script a loaded face covers renders). Tries the primary face then the fallback
    // faces (Noto per-script) in order. Returns nullptr only if no TTF is loaded; a codepoint no face
    // has resolves to '?'. const because it only fills a cache.
    const Glyph* glyphFor(int cp) const;
    int addFace(const unsigned char* ttf, std::size_t size);  // parse a face, append to faces_; -1 fail
    void addFallbackFont(const std::string& path);            // load an optional fallback .ttf if present
    int faceForCp(int cp) const;                              // index of the first face that has cp (-1 none)
    // HarfBuzz complex-script path (Arabic joining, Thai marks, Devanagari conjuncts, RTL). Only used
    // when a string actually contains such a script AND HarfBuzz is compiled in; otherwise the naive
    // per-codepoint path above runs unchanged. glyphForIndex rasterizes a shaped glyph by its font
    // glyph INDEX (not codepoint), since shaping outputs indices.
    static bool hasComplexScript(const std::string& text);
    const Glyph* glyphForIndex(int faceIdx, unsigned glyphId) const;
    void drawShaped(SpriteBatch& sb, float x, float y, float scale, u32 abgr,
                    const std::string& text) const;
    float widthShaped(const std::string& text, float scale) const;

    mutable Texture atlas_;
    bool ttf_ = false;

    // On-demand glyph atlas (TTF path): a chain of faces (primary + per-script Noto fallbacks) is kept
    // so any codepoint any face covers can be rasterized at draw time; a shelf allocator places new
    // glyphs and uploads them as sub-rects. Parallel vectors avoid an incomplete-type member here.
    std::vector<std::vector<unsigned char>> faceData_;  // persistent TTF bytes per face (info points in)
    std::vector<void*> faceInfo_;                        // stbtt_fontinfo* per face (owned; freed in shutdown)
    std::vector<float> faceScale_;                       // stbtt scale per face for kBakePx
    std::vector<void*> faceHb_;                          // hb_font_t* per face (owned; null if no HarfBuzz)
    mutable std::unordered_map<u32, Glyph> idxGlyphs_;   // shaped glyphs keyed by (faceIdx<<24 | glyphId)
    mutable std::vector<u8> atlasPixels_;   // CPU mirror of the atlas (RGBA), for shelf packing
    mutable int packX_ = 1, packY_ = 1, packRowH_ = 0;  // shelf allocator cursor (1px border)

    // TTF path: glyphs keyed by Unicode codepoint, metrics relative to a fixed
    // bake size; render scaling is kRenderPx*scale/kBakePx.
    mutable std::unordered_map<int, Glyph> glyphs_;
    float bakeCapPx_ = 0;       // cap height: top-of-caps to baseline (bake px). The
                                // text top is placed at the draw y by offsetting the
                                // baseline down by this — matching the old bitmap font,
                                // which anchored the glyph top (not the full ascent,
                                // which would sink the text below its box).
    float bakeLineAdvPx_ = 0;   // line-to-line step (bake px)
    float bakeSpaceAdvPx_ = 0;  // space glyph advance (bake px)

    // Bitmap fallback atlas layout (used only when ttf_ == false).
    int cols_ = 16;
    int rows_ = 6;

    static constexpr float kBakePx = 16.0f;    // px the atlas is rasterized at (== render so a
                                               // point-sampled glyph is 1:1 at scale 1 — crisp, no clip)
    static constexpr float kRenderPx = 16.0f;  // on-screen px the bake maps to at scale 1
};

} // namespace uaro
