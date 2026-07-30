#include "formats/Flc.hpp"

#include <array>
#include <cstring>

namespace uaro {

namespace {

// Little-endian byte cursor with bounds checks. Any short read aborts the parse (returns false up
// the chain), so a truncated/corrupt file can't over-read.
struct Cur {
    const u8* p;
    usize n;
    usize off = 0;
    bool ok = true;
    u8 u8v() { if (off + 1 > n) { ok = false; return 0; } return p[off++]; }
    i8 i8v() { return static_cast<i8>(u8v()); }
    u16 u16v() { u8 a = u8v(), b = u8v(); return static_cast<u16>(a | (b << 8)); }
    i16 i16v() { return static_cast<i16>(u16v()); }
    u32 u32v() { u16 a = u16v(), b = u16v(); return static_cast<u32>(a) | (static_cast<u32>(b) << 16); }
    void skip(usize k) { off += k; if (off > n) { off = n; ok = false; } }
};

constexpr u16 kFlcMagic = 0xAF12;   // .flc
constexpr u16 kFliMagic = 0xAF11;   // .fli
constexpr u16 kFrameMagic = 0xF1FA;

constexpr u16 CHUNK_COLOR256 = 4;
constexpr u16 CHUNK_SS2 = 7;        // FLC word-oriented delta
constexpr u16 CHUNK_COLOR64 = 11;
constexpr u16 CHUNK_LC = 12;        // FLI byte-oriented delta
constexpr u16 CHUNK_BLACK = 13;
constexpr u16 CHUNK_BRUN = 15;      // full-frame byte RLE
constexpr u16 CHUNK_COPY = 16;      // raw
constexpr u16 CHUNK_PSTAMP = 18;    // thumbnail, skip

struct Pal {
    std::array<u8, 256 * 3> rgb{};
};

// --- palette + pixel chunk decoders. `canvas` is w*h indexed bytes. --------------------------------

void doColor(Cur& c, Pal& pal, bool scale64) {
    const u16 packets = c.u16v();
    int idx = 0;
    for (u16 pk = 0; pk < packets && c.ok; ++pk) {
        idx += c.u8v();                         // skip N palette entries
        int count = c.u8v();
        if (count == 0) count = 256;            // 0 means 256
        for (int i = 0; i < count && idx < 256 && c.ok; ++i, ++idx) {
            u8 r = c.u8v(), g = c.u8v(), b = c.u8v();
            if (scale64) { r = static_cast<u8>(r << 2); g = static_cast<u8>(g << 2); b = static_cast<u8>(b << 2); }
            pal.rgb[static_cast<usize>(idx) * 3 + 0] = r;
            pal.rgb[static_cast<usize>(idx) * 3 + 1] = g;
            pal.rgb[static_cast<usize>(idx) * 3 + 2] = b;
        }
    }
}

void doBrun(Cur& c, std::vector<u8>& canvas, int w, int h) {
    for (int y = 0; y < h && c.ok; ++y) {
        u8* row = &canvas[static_cast<usize>(y) * w];
        int x = 0;
        c.u8v();  // legacy packet-count byte, ignored (real count is derived from the row width)
        while (x < w && c.ok) {
            i8 cnt = c.i8v();
            if (cnt >= 0) {                     // run: one value repeated `cnt` times
                u8 v = c.u8v();
                for (int i = 0; i < cnt && x < w; ++i) row[x++] = v;
            } else {                            // literal: -cnt raw bytes
                for (int i = 0; i < -cnt && x < w && c.ok; ++i) row[x++] = c.u8v();
            }
        }
    }
}

void doLc(Cur& c, std::vector<u8>& canvas, int w, int h) {
    const u16 skipLines = c.u16v();
    const u16 numLines = c.u16v();
    int y = skipLines;
    for (u16 ln = 0; ln < numLines && c.ok; ++ln, ++y) {
        if (y < 0 || y >= h) { c.ok = false; break; }
        u8* row = &canvas[static_cast<usize>(y) * w];
        int x = 0;
        const u8 packets = c.u8v();
        for (u8 pk = 0; pk < packets && c.ok; ++pk) {
            x += c.u8v();                       // column skip
            i8 cnt = c.i8v();
            if (cnt >= 0) {                     // literal: cnt bytes
                for (int i = 0; i < cnt && x < w && c.ok; ++i) row[x++] = c.u8v();
            } else {                            // run: -cnt of one byte
                u8 v = c.u8v();
                for (int i = 0; i < -cnt && x < w; ++i) row[x++] = v;
            }
        }
    }
}

void doSs2(Cur& c, std::vector<u8>& canvas, int w, int h) {
    const u16 numLines = c.u16v();
    int y = 0;
    for (u16 ln = 0; ln < numLines && c.ok;) {
        const u16 op = c.u16v();
        const u16 hi = op & 0xC000;
        if (hi == 0xC000) {                     // line skip: -(signed) lines down
            y += -static_cast<i16>(op);
            continue;                           // same line-op loop, don't advance ln
        } else if (hi == 0x8000) {              // set last pixel of this line (odd width)
            if (y >= 0 && y < h) canvas[static_cast<usize>(y) * w + (w - 1)] = static_cast<u8>(op & 0xff);
            continue;
        }
        // hi == 0x0000: `op` = packet count for this line.
        if (y < 0 || y >= h) { c.ok = false; break; }
        u8* row = &canvas[static_cast<usize>(y) * w];
        int x = 0;
        for (u16 pk = 0; pk < op && c.ok; ++pk) {
            x += c.u8v();                       // column skip (in pixels)
            i8 cnt = c.i8v();
            if (cnt >= 0) {                     // literal: cnt words (2 px each)
                for (int i = 0; i < cnt && x + 1 < w + 1 && c.ok; ++i) {
                    u8 a = c.u8v(), b = c.u8v();
                    if (x < w) row[x++] = a;
                    if (x < w) row[x++] = b;
                }
            } else {                            // run: -cnt of one word
                u8 a = c.u8v(), b = c.u8v();
                for (int i = 0; i < -cnt && x + 1 <= w; ++i) { row[x++] = a; if (x < w) row[x++] = b; }
            }
        }
        ++y;
        ++ln;
    }
}

}  // namespace

std::optional<Flc> Flc::parse(const std::vector<u8>& bytes) {
    if (bytes.size() < 128) return std::nullopt;
    Cur c{bytes.data(), bytes.size()};
    c.u32v();                                   // file size
    const u16 magic = c.u16v();
    if (magic != kFlcMagic && magic != kFliMagic) return std::nullopt;
    const u16 nFrames = c.u16v();
    const int w = c.u16v();
    const int h = c.u16v();
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return std::nullopt;
    c.u16v();                                   // depth
    c.u16v();                                   // flags
    const u32 speed = c.u32v();                 // FLC: ms/frame; FLI: 1/70s jiffies

    Flc flc;
    flc.width_ = w;
    flc.height_ = h;
    flc.delayMs_ = magic == kFliMagic ? static_cast<int>(speed * 1000 / 70) : static_cast<int>(speed);
    if (flc.delayMs_ <= 0) flc.delayMs_ = 100;

    c.off = 128;                                // frame data starts after the 128-byte header
    Pal pal;
    std::vector<u8> canvas(static_cast<usize>(w) * h, 0);

    for (u16 f = 0; f < nFrames && c.ok; ++f) {
        const usize frameStart = c.off;
        const u32 frameSize = c.u32v();
        const u16 frameType = c.u16v();
        const u16 subChunks = c.u16v();
        c.skip(8);                              // 8 reserved bytes -> sub-chunks begin here
        if (frameType != kFrameMagic) {         // prefix/other frame types: skip whole chunk
            c.off = frameStart + (frameSize ? frameSize : 16);
            continue;
        }
        for (u16 sc = 0; sc < subChunks && c.ok; ++sc) {
            const usize scStart = c.off;
            const u32 scSize = c.u32v();
            const u16 scType = c.u16v();
            switch (scType) {
                case CHUNK_COLOR256: doColor(c, pal, /*scale64=*/false); break;
                case CHUNK_COLOR64:  doColor(c, pal, /*scale64=*/true);  break;
                case CHUNK_BRUN:     doBrun(c, canvas, w, h); break;
                case CHUNK_LC:       doLc(c, canvas, w, h);   break;
                case CHUNK_SS2:      doSs2(c, canvas, w, h);  break;
                case CHUNK_BLACK:    std::memset(canvas.data(), 0, canvas.size()); break;
                case CHUNK_COPY:
                    for (usize i = 0; i < canvas.size() && c.ok; ++i) canvas[i] = c.u8v();
                    break;
                case CHUNK_PSTAMP:
                default: break;                 // skip via scSize below
            }
            // Always resync to the sub-chunk's declared end (chunks are word-padded; our per-op
            // decoders may stop a byte early on padding).
            c.off = scStart + (scSize ? scSize : 6);
            if (c.off > c.n) { c.ok = false; break; }
        }
        c.off = frameStart + (frameSize ? frameSize : c.off - frameStart);

        // Snapshot the canvas to RGBA (index 0 = transparent, RO effect convention).
        std::vector<u8> rgba(static_cast<usize>(w) * h * 4);
        for (usize i = 0; i < canvas.size(); ++i) {
            const u8 idx = canvas[i];
            rgba[i * 4 + 0] = pal.rgb[static_cast<usize>(idx) * 3 + 0];
            rgba[i * 4 + 1] = pal.rgb[static_cast<usize>(idx) * 3 + 1];
            rgba[i * 4 + 2] = pal.rgb[static_cast<usize>(idx) * 3 + 2];
            rgba[i * 4 + 3] = idx == 0 ? 0 : 255;
        }
        flc.frames_.push_back(std::move(rgba));
    }
    if (flc.frames_.empty()) return std::nullopt;
    return flc;
}

}  // namespace uaro
