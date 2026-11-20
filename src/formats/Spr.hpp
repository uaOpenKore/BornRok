#pragma once
#include <array>
#include <optional>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

struct SprPalColor {
    u8 r = 0, g = 0, b = 0, a = 0;
};

// A standalone RO palette (.pal) file: 256 RGBA entries (1024 bytes), the SAME layout as a sprite's
// trailing palette. Used to recolour (dye) a sprite -- hair/clothes colour -- by overriding the
// sprite's embedded palette with a data/palette/.../...<color>.pal. Returns nullopt if too short.
std::optional<std::array<SprPalColor, 256>> parsePal(const std::vector<u8>& bytes);

struct SprFrame {
    u16 width = 0;
    u16 height = 0;
    std::vector<u8> pixels;  // indexed: 1 byte/pixel; rgba: 4 bytes/pixel (RGBA8)
};

// Parsed RO sprite: palette-indexed frames, optional truecolor frames, and the
// 256-colour palette. Frame rendering happens later (v3/v5); this is the decode.
class Sprite {
public:
    static std::optional<Sprite> parse(const std::vector<u8>& bytes);

    // Build an in-memory truecolor sprite (no .spr file behind it) — used by the PNG-sprite
    // loader (#109), which decodes loose PNG frames and feeds them into the same pipeline.
    // renderScale < 1 shrinks the composed QUAD without touching the texture: an HD (e.g. 2k) pack
    // whose frames are K× the original .spr size passes 1/K so it renders at the original logical
    // size while the .act's (1×) layer offsets/anchors stay correct (S. 2026-09-10: "масштаб 2к / 2").
    // tcBase: where, in `frames`, the frames that were TRUECOLOR in the original .spr begin (a .png.d
    // pack stores them as t<i>.webp/.png, the indexed ones as <i>.*). An .act layer with sprType 1
    // addresses a truecolor frame by its OWN index, so the composer adds tcBase for synthetic sprites.
    // 0 = every frame is addressed as-is (PNG-sprite manifests, pure-truecolor packs).
    static Sprite fromRgbaFrames(std::vector<SprFrame> frames, float renderScale = 1.0f, u32 tcBase = 0) {
        Sprite s;
        s.version_ = 0x300;  // synthetic marker (real .spr versions stop at 0x2xx)
        s.rgba_ = std::move(frames);
        s.renderScale_ = renderScale;
        s.tcBase_ = tcBase;
        return s;
    }

    // Build an in-memory INDEXED sprite (tests, and tools that make a sprite without a .spr file).
    static Sprite fromIndexedFrames(std::vector<SprFrame> frames, const std::array<SprPalColor, 256>& palette) {
        Sprite s;
        s.version_ = 0x201;
        s.indexed_ = std::move(frames);
        s.setPalette(palette);
        return s;
    }

    u16 version() const { return version_; }
    bool synthetic() const { return version_ == 0x300; }
    u32 truecolorBase() const { return tcBase_; }  // see fromRgbaFrames
    float renderScale() const { return renderScale_; }
    const std::vector<SprFrame>& indexedFrames() const { return indexed_; }
    const std::vector<SprFrame>& rgbaFrames() const { return rgba_; }
    std::vector<SprFrame>& mutableRgbaFrames() { return rgba_; }  // recolouring (formats/SprDye.hpp)
    bool hasPalette() const { return hasPalette_; }
    const std::array<SprPalColor, 256>& palette() const { return palette_; }

    // Expand indexed frame `i` to RGBA8 using the palette. Palette index 0 is
    // the RO transparent colour (alpha 0).
    std::vector<u8> indexedToRgba(usize i) const;

    // Override the embedded palette (e.g. with a hair/clothes dye .pal). Must be called BEFORE the
    // frames are expanded/uploaded so the new colours take effect (#86).
    void setPalette(const std::array<SprPalColor, 256>& p) {
        palette_ = p;
        hasPalette_ = true;
    }

    // Swap the R and B channels of every truecolor (RGBA) frame. A handful of effect sprites
    // (e.g. the warp portal npc/portal.spr) store their truecolor pixels as ARGB rather than the
    // usual ABGR, so the standard decode renders them with red/blue swapped -- the blue warp ripple
    // came out red (S.; verified by rendering the frames both ways). The byte order is not flagged
    // in the file, so the caller opts a known-affected sprite in after parse.
    void swapTruecolorRedBlue() {
        for (SprFrame& f : rgba_)
            for (usize p = 0; p + 2 < f.pixels.size(); p += 4) {
                const u8 t = f.pixels[p];
                f.pixels[p] = f.pixels[p + 2];
                f.pixels[p + 2] = t;
            }
    }

    // Drop the indexed (palette) frames, keeping only the truecolor ones. The warp portal's
    // indexed frames are an opaque stone-ring base drawn under the blue ripple; the owner wants
    // only the ripple (S.: "убери спрайт из-под эффекта"). With these gone, an .act layer that
    // refers to an indexed frame finds none and is skipped, so only the truecolor ripple draws.
    void dropIndexedFrames() {
        indexed_.clear();
        hasPalette_ = false;
    }

private:
    u16 version_ = 0;
    u32 tcBase_ = 0;  // synthetic sprites only: first rgba_ frame that was truecolor (see fromRgbaFrames)
    std::vector<SprFrame> indexed_;
    std::vector<SprFrame> rgba_;
    std::array<SprPalColor, 256> palette_{};
    bool hasPalette_ = false;
    float renderScale_ = 1.0f;  // composed-quad size multiplier (HD packs render at 1/K logical size)
};

} // namespace uaro
