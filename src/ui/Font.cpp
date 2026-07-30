#include "ui/Font.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "render/SpriteBatch.hpp"
#include "third_party/stb_truetype.h"

#ifdef CLIENT_WITH_HARFBUZZ
#if defined(__has_include)
#  if __has_include(<hb.h>)
#    include <hb.h>
#  elif __has_include(<harfbuzz/hb.h>)
#    include <harfbuzz/hb.h>
#  endif
#else
#  include <hb.h>
#endif
#endif

namespace uaro {

namespace {

// Baked glyph coverage ranges and atlas size. Bake at the on-screen size (kRenderPx) so the
// point-sampled atlas is ~1:1 at scale 1.0 — crisp with no bottom-row clipping (S.: "сглаживать
// не надо ... снизу обрезает"). A 32px bake down-sampled to 15px with POINT dropped pixel rows.
constexpr float kBakePxC = 16.0f;
// Dynamic glyph atlas: glyphs are rasterized on demand (any script the loaded face covers), so it must
// hold far more than the old ASCII+Cyrillic bake -- 1024x1024 fits every European + Greek + Cyrillic
// glyph with room to spare. (CJK's thousands of glyphs need a bigger/fallback scheme -- future.)
constexpr int kAtlasW = 1024;
constexpr int kAtlasH = 1024;
constexpr int kAsciiFirst = 0x20, kAsciiLast = 0x7E;  // space..~ (warmed at load)
constexpr int kCyrFirst = 0x400, kCyrLast = 0x45F;    // Ѐ..ѿ (covers А..я, Ё/ё) (warmed at load)

// Decode one UTF-8 codepoint starting at s[i], advancing i past it. Malformed
// bytes yield U+FFFD and advance a single byte so a bad string can't loop.
int nextUtf8(const std::string& s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { ++i; return c; }
    int need, cp;
    if ((c >> 5) == 0x6) { need = 1; cp = c & 0x1F; }
    else if ((c >> 4) == 0xE) { need = 2; cp = c & 0x0F; }
    else if ((c >> 3) == 0x1E) { need = 3; cp = c & 0x07; }
    else { ++i; return 0xFFFD; }
    if (i + static_cast<std::size_t>(need) >= s.size()) { ++i; return 0xFFFD; }  // truncated
    for (int k = 1; k <= need; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc >> 6) != 0x2) { ++i; return 0xFFFD; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += need + 1;
    return cp;
}

// Thin 5x7 sans-serif bitmap font, glyphs 0x20..0x7E. Each glyph is 8 rows; in a
// row bit 0 (0x01) is the leftmost pixel, top row first. Only the low 5 bits and
// rows 0..6 carry the glyph (row 7 = descenders for g/j/p/q/y and comma tails).
// The fallback used only when the NotoSans asset can't be read.
constexpr int kFirst = 0x20;
constexpr int kLast = 0x7E;
constexpr int kGlyphs = kLast - kFirst + 1;  // 95

constexpr std::array<std::array<u8, 8>, kGlyphs> kFont = {{
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  // ' '
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04,0x00},  // !
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00,0x00},  // "
    {0x00,0x0A,0x1F,0x0A,0x1F,0x0A,0x00,0x00},  // #
    {0x04,0x1E,0x05,0x0E,0x14,0x0F,0x04,0x00},  // $
    {0x03,0x13,0x08,0x04,0x02,0x19,0x18,0x00},  // %
    {0x06,0x09,0x05,0x02,0x15,0x09,0x16,0x00},  // &
    {0x04,0x04,0x02,0x00,0x00,0x00,0x00,0x00},  // '
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08,0x00},  // (
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02,0x00},  // )
    {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00,0x00},  // *
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00,0x00},  // +
    {0x00,0x00,0x00,0x00,0x00,0x04,0x04,0x02},  // ,
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00},  // -
    {0x00,0x00,0x00,0x00,0x00,0x04,0x04,0x00},  // .
    {0x10,0x10,0x08,0x04,0x02,0x01,0x01,0x00},  // /
    {0x0E,0x11,0x19,0x15,0x13,0x11,0x0E,0x00},  // 0
    {0x04,0x06,0x04,0x04,0x04,0x04,0x0E,0x00},  // 1
    {0x0E,0x11,0x10,0x08,0x04,0x02,0x1F,0x00},  // 2
    {0x1F,0x08,0x04,0x08,0x10,0x11,0x0E,0x00},  // 3
    {0x08,0x0C,0x0A,0x09,0x1F,0x08,0x08,0x00},  // 4
    {0x1F,0x01,0x0F,0x10,0x10,0x11,0x0E,0x00},  // 5
    {0x0E,0x01,0x01,0x0F,0x11,0x11,0x0E,0x00},  // 6
    {0x1F,0x10,0x08,0x04,0x04,0x04,0x04,0x00},  // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00},  // 8
    {0x0E,0x11,0x11,0x1E,0x10,0x10,0x0E,0x00},  // 9
    {0x00,0x04,0x04,0x00,0x00,0x04,0x04,0x00},  // :
    {0x00,0x04,0x04,0x00,0x00,0x04,0x04,0x02},  // ;
    {0x00,0x08,0x04,0x02,0x04,0x08,0x00,0x00},  // <
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00,0x00},  // =
    {0x00,0x02,0x04,0x08,0x04,0x02,0x00,0x00},  // >
    {0x0E,0x11,0x10,0x08,0x04,0x00,0x04,0x00},  // ?
    {0x0E,0x11,0x1D,0x15,0x1D,0x01,0x0E,0x00},  // @
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,0x00},  // A
    {0x0F,0x11,0x11,0x0F,0x11,0x11,0x0F,0x00},  // B
    {0x0E,0x11,0x01,0x01,0x01,0x11,0x0E,0x00},  // C
    {0x0F,0x11,0x11,0x11,0x11,0x11,0x0F,0x00},  // D
    {0x1F,0x01,0x01,0x0F,0x01,0x01,0x1F,0x00},  // E
    {0x1F,0x01,0x01,0x0F,0x01,0x01,0x01,0x00},  // F
    {0x0E,0x11,0x01,0x1D,0x11,0x11,0x0E,0x00},  // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x00},  // H
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00},  // I
    {0x10,0x10,0x10,0x10,0x11,0x11,0x0E,0x00},  // J
    {0x11,0x09,0x05,0x03,0x05,0x09,0x11,0x00},  // K
    {0x01,0x01,0x01,0x01,0x01,0x01,0x1F,0x00},  // L
    {0x11,0x1B,0x15,0x11,0x11,0x11,0x11,0x00},  // M
    {0x11,0x13,0x15,0x15,0x19,0x11,0x11,0x00},  // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0x00},  // O
    {0x0F,0x11,0x11,0x0F,0x01,0x01,0x01,0x00},  // P
    {0x0E,0x11,0x11,0x11,0x15,0x09,0x16,0x00},  // Q
    {0x0F,0x11,0x11,0x0F,0x05,0x09,0x11,0x00},  // R
    {0x0E,0x11,0x01,0x0E,0x10,0x11,0x0E,0x00},  // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x00},  // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0x00},  // U
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0x00},  // V
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11,0x00},  // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x00},  // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04,0x00},  // Y
    {0x1F,0x10,0x08,0x04,0x02,0x01,0x1F,0x00},  // Z
    {0x06,0x02,0x02,0x02,0x02,0x02,0x06,0x00},  // [
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10,0x00},  // backslash
    {0x0C,0x08,0x08,0x08,0x08,0x08,0x0C,0x00},  // ]
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00,0x00},  // ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F},  // _
    {0x04,0x08,0x00,0x00,0x00,0x00,0x00,0x00},  // `
    {0x00,0x00,0x0E,0x10,0x1E,0x11,0x1E,0x00},  // a
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F,0x00},  // b
    {0x00,0x00,0x0E,0x11,0x01,0x11,0x0E,0x00},  // c
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E,0x00},  // d
    {0x00,0x00,0x0E,0x11,0x1F,0x01,0x0E,0x00},  // e
    {0x0C,0x02,0x07,0x02,0x02,0x02,0x02,0x00},  // f
    {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10,0x0E},  // g
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x11,0x00},  // h
    {0x04,0x00,0x04,0x04,0x04,0x04,0x04,0x00},  // i
    {0x08,0x00,0x08,0x08,0x08,0x08,0x09,0x06},  // j
    {0x01,0x01,0x09,0x05,0x03,0x05,0x09,0x00},  // k
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00},  // l
    {0x00,0x00,0x1F,0x15,0x15,0x15,0x15,0x00},  // m
    {0x00,0x00,0x0F,0x11,0x11,0x11,0x11,0x00},  // n
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E,0x00},  // o
    {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x01},  // p
    {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10,0x10},  // q
    {0x00,0x00,0x0D,0x01,0x01,0x01,0x01,0x00},  // r
    {0x00,0x00,0x1E,0x01,0x0E,0x10,0x0F,0x00},  // s
    {0x02,0x02,0x07,0x02,0x02,0x12,0x0C,0x00},  // t
    {0x00,0x00,0x11,0x11,0x11,0x11,0x1E,0x00},  // u
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04,0x00},  // v
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A,0x00},  // w
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11,0x00},  // x
    {0x00,0x00,0x11,0x11,0x11,0x1E,0x10,0x0E},  // y
    {0x00,0x00,0x1F,0x08,0x04,0x02,0x1F,0x00},  // z
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08,0x00},  // {
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00},  // |
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02,0x00},  // }
    {0x00,0x00,0x02,0x15,0x08,0x00,0x00,0x00},  // ~
}};

}  // namespace

bool Font::initTtf(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize sz = f.tellg();
    if (sz <= 0) return false;
    std::vector<unsigned char> ttf(static_cast<std::size_t>(sz));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(ttf.data()), sz)) return false;
    return packTtf(ttf.data(), ttf.size());
}

// Parse a TTF into a new face and append it to the fallback chain. Returns its index (0 = primary),
// or -1 if the bytes aren't a usable font. The bytes are copied into faceData_ so stbtt_fontinfo's
// pointer into them stays valid for the Font's lifetime (moving the outer vector keeps each buffer).
int Font::addFace(const unsigned char* ttf, std::size_t size) {
    faceData_.emplace_back(ttf, ttf + size);
    auto* info = new stbtt_fontinfo;
    const unsigned char* data = faceData_.back().data();
    if (!stbtt_InitFont(info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        delete info;
        faceData_.pop_back();
        return -1;
    }
    faceInfo_.push_back(info);
    faceScale_.push_back(stbtt_ScaleForPixelHeight(info, kBakePxC));

    // Parallel HarfBuzz font for complex-script shaping (glyph indices match stbtt's). Uses the OT
    // shaper reading the font's own GSUB/GPOS; scale left at upem so positions come out in font units
    // (multiplied by faceScale at draw time). Null when HarfBuzz isn't compiled in.
#ifdef CLIENT_WITH_HARFBUZZ
    const unsigned char* fdata = faceData_.back().data();
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(fdata),
                                     static_cast<unsigned>(faceData_.back().size()),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* hface = hb_face_create(blob, 0);
    hb_blob_destroy(blob);
    hb_font_t* hfont = hb_font_create(hface);
    const unsigned upem = hb_face_get_upem(hface);
    hb_font_set_scale(hfont, static_cast<int>(upem), static_cast<int>(upem));
    hb_face_destroy(hface);
    faceHb_.push_back(hfont);
#else
    faceHb_.push_back(nullptr);
#endif
    return static_cast<int>(faceInfo_.size()) - 1;
}

void Font::addFallbackFont(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return;  // optional: silently skip if not present
    const std::streamsize sz = f.tellg();
    if (sz <= 0) return;
    std::vector<unsigned char> ttf(static_cast<std::size_t>(sz));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(ttf.data()), sz)) return;
    addFace(ttf.data(), ttf.size());
}

bool Font::packTtf(const unsigned char* ttf, std::size_t ttfSize) {
    // Reset any previously loaded faces (re-init is rare but keep it clean).
    for (auto* p : faceInfo_) delete static_cast<stbtt_fontinfo*>(p);
    faceInfo_.clear();
    faceData_.clear();
    faceScale_.clear();

    if (addFace(ttf, ttfSize) != 0) return false;  // primary must be face 0
    auto* info = static_cast<stbtt_fontinfo*>(faceInfo_[0]);
    const float scale0 = faceScale_[0];

    // Vertical metrics for baseline placement and line stepping (from the primary face).
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    bakeLineAdvPx_ = (asc - desc + gap) * scale0;

    // Dynamic atlas: glyphFor() rasterizes + uploads glyphs on first use, so the texture MUST be
    // created empty/updatable (create-with-pixels is immutable on D3D11 -> update() no-ops -> no
    // glyphs). Create empty, then clear it to fully transparent with one update. Bilinear so AA edges
    // stay smooth when the UI scales the 16px bake up (e.g. x1.5).
    atlasPixels_.assign(static_cast<std::size_t>(kAtlasW) * kAtlasH * 4, 0);
    atlas_.createDynamic(static_cast<u16>(kAtlasW), static_cast<u16>(kAtlasH), true);
    if (atlas_.valid()) atlas_.update(0, 0, kAtlasW, kAtlasH, atlasPixels_.data());
    ttf_ = atlas_.valid();
    if (!ttf_) {
        for (auto* p : faceInfo_) delete static_cast<stbtt_fontinfo*>(p);
        faceInfo_.clear();
        faceData_.clear();
        faceScale_.clear();
        return false;
    }
    packX_ = 1;
    packY_ = 1;
    packRowH_ = 0;
    glyphs_.clear();

    // Warm the common glyphs (ASCII + Cyrillic) so the first frames don't rasterize a storm, and so
    // the space/cap metrics below resolve. Everything else (accented Latin, Greek, ...) is lazy.
    for (int cp = kAsciiFirst; cp <= kAsciiLast; ++cp) glyphFor(cp);
    for (int cp = kCyrFirst; cp <= kCyrLast; ++cp) glyphFor(cp);

    auto sp = glyphs_.find(' ');
    bakeSpaceAdvPx_ = (sp != glyphs_.end() && sp->second.xadv > 0) ? sp->second.xadv : kBakePxC * 0.25f;
    // Cap height = glyph box height of a flat-topped capital (H sits on the baseline, so its box
    // height is the top-to-baseline distance). The draw path anchors the cap top at the requested y.
    auto cap = glyphs_.find('H');
    bakeCapPx_ = (cap != glyphs_.end() && cap->second.h > 0) ? cap->second.h : asc * scale0 * 0.72f;
    return true;
}

// Rasterize + pack a single codepoint on demand (see header). Fills the glyph cache so subsequent
// draws are a map lookup. A codepoint the loaded face lacks resolves to '?' so it stays visible.
const Font::Glyph* Font::glyphFor(int cp) const {
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return &it->second;
    if (faceInfo_.empty()) return nullptr;

    // Pick the first face (primary, then per-script fallbacks) that actually has this codepoint.
    int fi = -1;
    for (std::size_t k = 0; k < faceInfo_.size(); ++k) {
        if (stbtt_FindGlyphIndex(static_cast<stbtt_fontinfo*>(faceInfo_[k]), cp) != 0) {
            fi = static_cast<int>(k);
            break;
        }
    }
    if (fi < 0) {
        // No face has it (e.g. CJK with no CJK font loaded): reuse '?' so it stays visible, like the
        // old find('?') fallback. Cache it so we don't re-probe the same codepoint every frame.
        auto q = glyphs_.find('?');
        Glyph g = (q != glyphs_.end()) ? q->second : Glyph{};
        auto res = glyphs_.emplace(cp, g);
        return &res.first->second;
    }
    auto* info = static_cast<stbtt_fontinfo*>(faceInfo_[fi]);
    const float sc = faceScale_[fi];

    Glyph g;
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(info, cp, &adv, &lsb);
    g.xadv = adv * sc;

    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bmp = stbtt_GetCodepointBitmap(info, sc, sc, cp, &w, &h, &xoff, &yoff);
    if (bmp && w > 0 && h > 0) {
        // Open a new shelf when the glyph won't fit on the current row; skip if the atlas is full
        // (glyph stays invisible but its advance is preserved so text spacing survives).
        if (packX_ + w + 1 > kAtlasW) { packX_ = 1; packY_ += packRowH_ + 1; packRowH_ = 0; }
        if (packY_ + h + 1 <= kAtlasH) {
            const int ox = packX_, oy = packY_;
            std::vector<u8> sub(static_cast<std::size_t>(w) * h * 4);
            for (int yy = 0; yy < h; ++yy) {
                for (int xx = 0; xx < w; ++xx) {
                    // Same gamma-boosted coverage as the old bake (0.55) so weight matches Latin text.
                    const u8 a = static_cast<u8>(
                        255.0f * std::pow(static_cast<float>(bmp[yy * w + xx]) / 255.0f, 0.55f) + 0.5f);
                    const std::size_t si = (static_cast<std::size_t>(yy) * w + xx) * 4;
                    sub[si + 0] = 255; sub[si + 1] = 255; sub[si + 2] = 255; sub[si + 3] = a;
                    const std::size_t ai =
                        ((static_cast<std::size_t>(oy + yy) * kAtlasW) + (ox + xx)) * 4;
                    atlasPixels_[ai + 0] = 255; atlasPixels_[ai + 1] = 255;
                    atlasPixels_[ai + 2] = 255; atlasPixels_[ai + 3] = a;
                }
            }
            atlas_.update(ox, oy, w, h, sub.data());  // upload just this glyph's sub-rect
            g.u0 = static_cast<float>(ox) / kAtlasW;
            g.v0 = static_cast<float>(oy) / kAtlasH;
            g.u1 = static_cast<float>(ox + w) / kAtlasW;
            g.v1 = static_cast<float>(oy + h) / kAtlasH;
            g.w = static_cast<float>(w);
            g.h = static_cast<float>(h);
            g.xoff = static_cast<float>(xoff);
            g.yoff = static_cast<float>(yoff);
            packX_ += w + 1;
            if (h > packRowH_) packRowH_ = h;
        }
    }
    if (bmp) stbtt_FreeBitmap(bmp, nullptr);
    auto res = glyphs_.emplace(cp, g);
    return &res.first->second;
}

int Font::faceForCp(int cp) const {
    for (std::size_t k = 0; k < faceInfo_.size(); ++k)
        if (stbtt_FindGlyphIndex(static_cast<stbtt_fontinfo*>(faceInfo_[k]), cp) != 0)
            return static_cast<int>(k);
    return -1;
}

// Rasterize + pack a glyph by its font glyph INDEX (what shaping outputs), from a specific face.
// Cached in idxGlyphs_ keyed by (faceIdx<<24 | glyphId). Mirrors glyphFor's packing.
const Font::Glyph* Font::glyphForIndex(int faceIdx, unsigned glyphId) const {
    const u32 key = (static_cast<u32>(faceIdx) << 24) | (glyphId & 0xFFFFFFu);
    auto it = idxGlyphs_.find(key);
    if (it != idxGlyphs_.end()) return &it->second;
    if (faceIdx < 0 || faceIdx >= static_cast<int>(faceInfo_.size())) return nullptr;
    auto* info = static_cast<stbtt_fontinfo*>(faceInfo_[faceIdx]);
    const float sc = faceScale_[faceIdx];
    Glyph g;
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(info, static_cast<int>(glyphId), &adv, &lsb);
    g.xadv = adv * sc;
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bmp = stbtt_GetGlyphBitmap(info, sc, sc, static_cast<int>(glyphId), &w, &h, &xoff, &yoff);
    if (bmp && w > 0 && h > 0) {
        if (packX_ + w + 1 > kAtlasW) { packX_ = 1; packY_ += packRowH_ + 1; packRowH_ = 0; }
        if (packY_ + h + 1 <= kAtlasH) {
            const int ox = packX_, oy = packY_;
            std::vector<u8> sub(static_cast<std::size_t>(w) * h * 4);
            for (int yy = 0; yy < h; ++yy) {
                for (int xx = 0; xx < w; ++xx) {
                    const u8 a = static_cast<u8>(
                        255.0f * std::pow(static_cast<float>(bmp[yy * w + xx]) / 255.0f, 0.55f) + 0.5f);
                    const std::size_t si = (static_cast<std::size_t>(yy) * w + xx) * 4;
                    sub[si + 0] = 255; sub[si + 1] = 255; sub[si + 2] = 255; sub[si + 3] = a;
                    const std::size_t ai =
                        ((static_cast<std::size_t>(oy + yy) * kAtlasW) + (ox + xx)) * 4;
                    atlasPixels_[ai + 0] = 255; atlasPixels_[ai + 1] = 255;
                    atlasPixels_[ai + 2] = 255; atlasPixels_[ai + 3] = a;
                }
            }
            atlas_.update(ox, oy, w, h, sub.data());
            g.u0 = static_cast<float>(ox) / kAtlasW;
            g.v0 = static_cast<float>(oy) / kAtlasH;
            g.u1 = static_cast<float>(ox + w) / kAtlasW;
            g.v1 = static_cast<float>(oy + h) / kAtlasH;
            g.w = static_cast<float>(w);
            g.h = static_cast<float>(h);
            g.xoff = static_cast<float>(xoff);
            g.yoff = static_cast<float>(yoff);
            packX_ += w + 1;
            if (h > packRowH_) packRowH_ = h;
        }
    }
    if (bmp) stbtt_FreeBitmap(bmp, nullptr);
    auto res = idxGlyphs_.emplace(key, g);
    return &res.first->second;
}

bool Font::hasComplexScript(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const int cp = nextUtf8(text, i);
        if ((cp >= 0x0590 && cp <= 0x05FF) || (cp >= 0x0600 && cp <= 0x06FF) ||  // Hebrew / Arabic
            (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0x08A0 && cp <= 0x08FF) ||  // Arabic supplement
            (cp >= 0x0E00 && cp <= 0x0E7F) || (cp >= 0x0900 && cp <= 0x097F) ||  // Thai / Devanagari
            (cp >= 0xFB1D && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF))    // Hebrew/Arabic pres.
            return true;
    }
    return false;
}

#ifdef CLIENT_WITH_HARFBUZZ
namespace {
hb_script_t scriptForCp(int cp) {
    if (cp >= 0x0590 && cp <= 0x05FF) return HB_SCRIPT_HEBREW;
    if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
        (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
        (cp >= 0xFE70 && cp <= 0xFEFF))
        return HB_SCRIPT_ARABIC;
    if (cp >= 0x0E00 && cp <= 0x0E7F) return HB_SCRIPT_THAI;
    if (cp >= 0x0900 && cp <= 0x097F) return HB_SCRIPT_DEVANAGARI;
    return HB_SCRIPT_COMMON;
}
}  // namespace

// Shape one line with HarfBuzz (whole-line run in the script's face; RTL for Arabic/Hebrew) and emit
// its glyphs. Falls back to the per-codepoint path when no shaping face covers the run.
void Font::drawShaped(SpriteBatch& sb, float x, float y, float scale, u32 abgr,
                      const std::string& text) const {
    const float f = (kRenderPx / kBakePx) * scale;
    const float lineStep = bakeLineAdvPx_ * f;
    float baseline = y + bakeCapPx_ * f - 1.5f;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = text.find('\n', start);
        const std::string line =
            (nl == std::string::npos) ? text.substr(start) : text.substr(start, nl - start);
        if (!line.empty()) {
            int fi = -1;
            hb_script_t script = HB_SCRIPT_COMMON;
            std::size_t t = 0;
            while (t < line.size()) {
                const int cp = nextUtf8(line, t);
                const hb_script_t s = scriptForCp(cp);
                if (s != HB_SCRIPT_COMMON) { script = s; fi = faceForCp(cp); break; }
            }
            if (fi < 0 || fi >= static_cast<int>(faceHb_.size()) || !faceHb_[fi]) {
                // No shaping face: naive per-codepoint fallback for this line.
                float penX = x;
                std::size_t k = 0;
                while (k < line.size()) {
                    const int cp = nextUtf8(line, k);
                    const Glyph* gp = glyphFor(cp);
                    if (!gp) { penX += bakeSpaceAdvPx_ * f; continue; }
                    if (gp->w > 0 && gp->h > 0)
                        sb.draw(penX + gp->xoff * f, baseline + gp->yoff * f, gp->w * f, gp->h * f,
                                gp->u0, gp->v0, gp->u1, gp->v1, abgr, atlas_);
                    penX += gp->xadv * f;
                }
            } else {
                auto* hf = static_cast<hb_font_t*>(faceHb_[fi]);
                const bool rtl = (script == HB_SCRIPT_ARABIC || script == HB_SCRIPT_HEBREW);
                const float sc = faceScale_[fi];
                hb_buffer_t* buf = hb_buffer_create();
                hb_buffer_add_utf8(buf, line.c_str(), static_cast<int>(line.size()), 0, -1);
                hb_buffer_guess_segment_properties(buf);
                hb_buffer_set_direction(buf, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                hb_buffer_set_script(buf, script);
                hb_shape(hf, buf, nullptr, 0);
                unsigned n = 0;
                const hb_glyph_info_t* gi = hb_buffer_get_glyph_infos(buf, &n);
                const hb_glyph_position_t* gpos = hb_buffer_get_glyph_positions(buf, &n);
                float penX = x;
                for (unsigned k = 0; k < n; ++k) {
                    const Glyph* g = glyphForIndex(fi, gi[k].codepoint);  // shaped -> glyph index
                    if (!g) continue;
                    if (g->w > 0 && g->h > 0) {
                        const float gx = penX + (gpos[k].x_offset * sc + g->xoff) * f;
                        const float gy = baseline - (gpos[k].y_offset * sc) * f + g->yoff * f;
                        sb.draw(gx, gy, g->w * f, g->h * f, g->u0, g->v0, g->u1, g->v1, abgr, atlas_);
                    }
                    penX += gpos[k].x_advance * sc * f;
                }
                hb_buffer_destroy(buf);
            }
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
        baseline += lineStep;
    }
}

float Font::widthShaped(const std::string& text, float scale) const {
    const float f = (kRenderPx / kBakePx) * scale;
    float maxw = 0;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = text.find('\n', start);
        const std::string line =
            (nl == std::string::npos) ? text.substr(start) : text.substr(start, nl - start);
        float w = 0;
        if (!line.empty()) {
            int fi = -1;
            hb_script_t script = HB_SCRIPT_COMMON;
            std::size_t t = 0;
            while (t < line.size()) {
                const int cp = nextUtf8(line, t);
                const hb_script_t s = scriptForCp(cp);
                if (s != HB_SCRIPT_COMMON) { script = s; fi = faceForCp(cp); break; }
            }
            if (fi < 0 || fi >= static_cast<int>(faceHb_.size()) || !faceHb_[fi]) {
                std::size_t k = 0;
                while (k < line.size()) {
                    const int cp = nextUtf8(line, k);
                    const Glyph* gp = glyphFor(cp);
                    w += (gp ? gp->xadv : bakeSpaceAdvPx_) * f;
                }
            } else {
                auto* hf = static_cast<hb_font_t*>(faceHb_[fi]);
                const bool rtl = (script == HB_SCRIPT_ARABIC || script == HB_SCRIPT_HEBREW);
                const float sc = faceScale_[fi];
                hb_buffer_t* buf = hb_buffer_create();
                hb_buffer_add_utf8(buf, line.c_str(), static_cast<int>(line.size()), 0, -1);
                hb_buffer_guess_segment_properties(buf);
                hb_buffer_set_direction(buf, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                hb_buffer_set_script(buf, script);
                hb_shape(hf, buf, nullptr, 0);
                unsigned n = 0;
                const hb_glyph_position_t* gpos = hb_buffer_get_glyph_positions(buf, &n);
                for (unsigned k = 0; k < n; ++k) w += gpos[k].x_advance * sc * f;
                hb_buffer_destroy(buf);
            }
        }
        if (w > maxw) maxw = w;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return maxw;
}
#endif  // CLIENT_WITH_HARFBUZZ

void Font::initBitmap() {
    const int aw = cols_ * 8;  // 128
    const int ah = rows_ * 8;  // 48
    std::vector<u8> px(static_cast<usize>(aw) * ah * 4, 0);
    for (int g = 0; g < kGlyphs; ++g) {
        const int gx = (g % cols_) * 8;
        const int gy = (g / cols_) * 8;
        for (int ry = 0; ry < 8; ++ry) {
            const u8 bits = kFont[g][ry];
            for (int cx = 0; cx < 8; ++cx) {
                if (!((bits >> cx) & 1)) continue;
                const usize idx = (static_cast<usize>(gy + ry) * aw + (gx + cx)) * 4;
                px[idx + 0] = 255;
                px[idx + 1] = 255;
                px[idx + 2] = 255;
                px[idx + 3] = 255;
            }
        }
    }
    atlas_.create(static_cast<u16>(aw), static_cast<u16>(ah), px.data());
    ttf_ = false;
}

// Baked-in font bytes. Declared at namespace scope (not block scope, which would name
// ::kEmbedded* in the global namespace and fail to link against uaro::...). The primary DejaVu face
// is in EmbeddedFont.cpp; the per-script fallback faces are in EmbeddedFonts.cpp (both generated).
extern const unsigned char kEmbeddedFontTtf[];
extern const std::size_t kEmbeddedFontTtfSize;
extern const unsigned char kEmbeddedThaiTtf[];
extern const std::size_t kEmbeddedThaiTtfSize;
extern const unsigned char kEmbeddedArabicTtf[];
extern const std::size_t kEmbeddedArabicTtfSize;
extern const unsigned char kEmbeddedHebrewTtf[];
extern const std::size_t kEmbeddedHebrewTtfSize;
extern const unsigned char kEmbeddedDevanagariTtf[];
extern const std::size_t kEmbeddedDevanagariTtfSize;
extern const unsigned char kEmbeddedCjkTtf[];
extern const std::size_t kEmbeddedCjkTtfSize;

bool Font::initEmbedded() {
    // The DejaVuSansMono face is baked into the binary (EmbeddedFont.cpp), so a single exe
    // renders text with no external font asset (S.: "зашить в клиент шрифт").
    return packTtf(kEmbeddedFontTtf, kEmbeddedFontTtfSize);
}

bool Font::init(const std::string& assetDir) {
    // Primary face: DejaVuSansMono (full Latin/Greek/Cyrillic). Prefer an on-disk copy (lets a distro
    // override it), else the copy baked into the binary, else the 5x7 bitmap so text never vanishes.
    // The bake metrics (cap height, line/space advance) are read from whichever face loads.
    bool primary = initTtf(assetDir + "/fonts/DejaVuSansMono.ttf") ||
                   initEmbedded();  // no external font -> use the baked-in one
    if (primary && ttf_) {
        // Per-script fallback faces for codepoints the primary lacks. All baked into the exe so every
        // language renders from a bare binary (S.: "все шрифты в exe, загружай всё"): Thai/Arabic/
        // Hebrew/Devanagari full, CJK subset to the codepoints used by the zh/ja/ko UI texts.
        // glyphFor() tries these in order after the primary. Note: complex scripts (Thai/Arabic/
        // Devanagari) still need shaping + (Arabic/Hebrew) RTL to be fully correct -- this provides
        // the glyphs. A full-coverage CJK face can still be dropped next to the exe as an override.
        addFace(kEmbeddedThaiTtf, kEmbeddedThaiTtfSize);
        addFace(kEmbeddedArabicTtf, kEmbeddedArabicTtfSize);
        addFace(kEmbeddedHebrewTtf, kEmbeddedHebrewTtfSize);
        addFace(kEmbeddedDevanagariTtf, kEmbeddedDevanagariTtfSize);
        addFace(kEmbeddedCjkTtf, kEmbeddedCjkTtfSize);
        addFallbackFont(assetDir + "/fonts/NotoSansCJK.otf");  // optional: full CJK override for chat/names
        addFallbackFont(assetDir + "/fonts/NotoSansCJK.ttf");
        return true;
    }
    initBitmap();  // last resort: never leave text unrenderable
    return atlas_.valid();
}

void Font::shutdown() {
    atlas_.destroy();
    for (auto* p : faceInfo_) delete static_cast<stbtt_fontinfo*>(p);
#ifdef CLIENT_WITH_HARFBUZZ
    for (auto* p : faceHb_) if (p) hb_font_destroy(static_cast<hb_font_t*>(p));
#endif
    faceInfo_.clear();
    faceData_.clear();
    faceScale_.clear();
    faceHb_.clear();
    idxGlyphs_.clear();
}

float Font::lineHeight(float scale) const {
    if (ttf_) return bakeLineAdvPx_ * (kRenderPx / kBakePx) * scale;
    return (8.0f + 1.0f) * scale;  // bitmap: 7px glyph + descender + 1px leading
}

float Font::advance(float scale) const {
    if (ttf_) return bakeSpaceAdvPx_ * (kRenderPx / kBakePx) * scale;
    return (5.0f + 1.0f) * scale;  // bitmap: 5px glyph + 1px tracking
}

float Font::width(const std::string& text, float scale) const {
    if (!ttf_) {
        // bitmap fallback: fixed 6px advance per byte, longest line wins
        float w = 0, maxw = 0;
        for (char c : text) {
            if (c == '\n') { maxw = w > maxw ? w : maxw; w = 0; continue; }
            w += (5.0f + 1.0f) * scale;
        }
        return w > maxw ? w : maxw;
    }
#ifdef CLIENT_WITH_HARFBUZZ
    if (hasComplexScript(text)) return widthShaped(text, scale);
#endif
    const float f = (kRenderPx / kBakePx) * scale;
    float w = 0, maxw = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const int cp = nextUtf8(text, i);
        if (cp == '\n') { maxw = w > maxw ? w : maxw; w = 0; continue; }
        const Glyph* g = glyphFor(cp);
        w += (g ? g->xadv : bakeSpaceAdvPx_) * f;
    }
    return w > maxw ? w : maxw;
}

void Font::draw(SpriteBatch& sb, float x, float y, float scale, u32 abgr,
                const std::string& text) const {
    if (!atlas_.valid()) return;
    if (!ttf_) { drawBitmap(sb, x, y, scale, abgr, text); return; }
#ifdef CLIENT_WITH_HARFBUZZ
    // Complex scripts (Arabic/Thai/Devanagari/Hebrew) need shaping + RTL -- route them through
    // HarfBuzz. Everything else stays on the fast per-codepoint path below (byte-identical behaviour).
    if (hasComplexScript(text)) { drawShaped(sb, x, y, scale, abgr, text); return; }
#endif

    const float f = (kRenderPx / kBakePx) * scale;
    const float lineStep = bakeLineAdvPx_ * f;
    // Cap top at y (matching the old bitmap font), nudged up ~1.5px per in-build review
    // so labels sit a touch higher in their boxes.
    float baseline = y + bakeCapPx_ * f - 1.5f;
    float penX = x;
    std::size_t i = 0;
    while (i < text.size()) {
        const int cp = nextUtf8(text, i);
        if (cp == '\n') { penX = x; baseline += lineStep; continue; }
        const Glyph* gp = glyphFor(cp);
        if (!gp) { penX += bakeSpaceAdvPx_ * f; continue; }
        const Glyph& g = *gp;
        if (g.w > 0 && g.h > 0) {
            sb.draw(penX + g.xoff * f, baseline + g.yoff * f, g.w * f, g.h * f,
                    g.u0, g.v0, g.u1, g.v1, abgr, atlas_);
        }
        penX += g.xadv * f;
    }
}

void Font::drawBitmap(SpriteBatch& sb, float x, float y, float scale, u32 abgr,
                      const std::string& text) const {
    const float cell = 8.0f * scale;
    const float du = 8.0f / (cols_ * 8);
    const float dv = 8.0f / (rows_ * 8);
    float cx = x, cy = y;
    for (unsigned char uc : text) {
        if (uc == '\n') { cx = x; cy += (8.0f + 1.0f) * scale; continue; }
        if (uc < kFirst || uc > kLast) { cx += (5.0f + 1.0f) * scale; continue; }
        const int g = uc - kFirst;
        const float u0 = (g % cols_) * du;
        const float v0 = (g / cols_) * dv;
        sb.draw(cx, cy, cell, cell, u0, v0, u0 + du, v0 + dv, abgr, atlas_);
        cx += (5.0f + 1.0f) * scale;
    }
}

}  // namespace uaro
