#include "formats/Spr.hpp"

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"

namespace uaro {

namespace {

// Decode RO SPR RLE (version >= 0x201) into exactly width*height indices.
// Literal bytes copy through; a 0x00 byte introduces a run of `count`
// transparent (index 0) pixels.
std::vector<u8> decode_rle(ByteReader& r, u32 dataLen, u32 pixelCount) {
    std::vector<u8> out;
    out.reserve(pixelCount);
    const usize end = r.pos() + dataLen;
    // Decode the whole RLE block (do not stop at pixelCount) so we consume
    // exactly dataLen bytes and the next frame stays aligned.
    while (r.pos() < end) {
        u8 c = r.u8v();
        out.push_back(c);
        if (c == 0) {
            if (r.pos() >= end) break;
            int count = r.u8v();
            while (--count > 0) out.push_back(0);
        }
    }
    r.seek(end);                // guarantee exact consumption of dataLen
    out.resize(pixelCount, 0);  // pad/truncate to the frame's pixel count
    return out;
}

constexpr u16 kMaxDim = 4096;    // RO sprite frames are small; reject implausible
constexpr u16 kMaxFrames = 8192; // and absurd frame counts (misread guard)

} // namespace

std::optional<Sprite> Sprite::parse(const std::vector<u8>& bytes) {
    if (bytes.size() < 6) {
        log::error("SPR: too small");
        return std::nullopt;
    }
    try {
        ByteReader r(bytes);
        if (r.u8v() != 'S' || r.u8v() != 'P') {
            log::error("SPR: bad signature");
            return std::nullopt;
        }
        Sprite spr;
        spr.version_ = r.u16le();
        const u16 indexedCount = r.u16le();
        const u16 rgbaCount = (spr.version_ >= 0x101) ? r.u16le() : 0;
        if (indexedCount > kMaxFrames || rgbaCount > kMaxFrames) {
            log::error("SPR: implausible frame count ({}/{})", indexedCount, rgbaCount);
            return std::nullopt;
        }

        // Indexed frames.
        for (u16 i = 0; i < indexedCount; ++i) {
            SprFrame f;
            f.width = r.u16le();
            f.height = r.u16le();
            if (f.width > kMaxDim || f.height > kMaxDim) {
                log::error("SPR: implausible indexed frame {}x{}", f.width, f.height);
                return std::nullopt;
            }
            const u32 px = static_cast<u32>(f.width) * f.height;
            if (spr.version_ >= 0x201) {
                const u16 dataLen = r.u16le();
                f.pixels = decode_rle(r, dataLen, px);
            } else {
                f.pixels.resize(px);
                if (px) r.read_bytes(f.pixels.data(), px);
            }
            spr.indexed_.push_back(std::move(f));
        }

        // Truecolor frames.
        for (u16 i = 0; i < rgbaCount; ++i) {
            SprFrame f;
            f.width = r.u16le();
            f.height = r.u16le();
            if (f.width > kMaxDim || f.height > kMaxDim) {
                log::error("SPR: implausible rgba frame {}x{}", f.width, f.height);
                return std::nullopt;
            }
            const u32 n = static_cast<u32>(f.width) * f.height * 4;
            f.pixels.resize(n);
            if (n) r.read_bytes(f.pixels.data(), n);
            // RO stores truecolor frames as ABGR (alpha first); reverse each pixel to the
            // RGBA the texture upload expects, else the alpha and red/blue channels swap
            // (e.g. torch_01 rendered as opaque pink boxes instead of a yellow flame).
            for (u32 p = 0; p + 3 < n; p += 4) {
                const u8 t0 = f.pixels[p + 0], t1 = f.pixels[p + 1];
                f.pixels[p + 0] = f.pixels[p + 3];  // R
                f.pixels[p + 1] = f.pixels[p + 2];  // G
                f.pixels[p + 2] = t1;               // B
                f.pixels[p + 3] = t0;               // A
            }
            // RO stores truecolor frames bottom-up (roBrowser reverses height on load); our texture
            // upload treats row 0 as the TOP, so without this the sprite renders upside down -- the
            // brazier flame had no tip on top, the warp ripple was flipped (S.). Indexed frames are
            // already top-down (players render upright) so only truecolor needs the flip.
            if (f.height > 1 && f.width > 0) {
                const u32 stride = static_cast<u32>(f.width) * 4;
                for (u32 y = 0; y * 2 < f.height; ++y) {
                    u8* ra = f.pixels.data() + static_cast<usize>(y) * stride;
                    u8* rb = f.pixels.data() + static_cast<usize>(f.height - 1 - y) * stride;
                    for (u32 x = 0; x < stride; ++x) {
                        const u8 tmp = ra[x];
                        ra[x] = rb[x];
                        rb[x] = tmp;
                    }
                }
            }
            spr.rgba_.push_back(std::move(f));
        }

        // Palette: the trailing 1024 bytes, present when there are indexed frames.
        if (indexedCount > 0 && bytes.size() >= 1024) {
            ByteReader pr(bytes.data() + (bytes.size() - 1024), 1024);
            for (auto& c : spr.palette_) {
                c.r = pr.u8v();
                c.g = pr.u8v();
                c.b = pr.u8v();
                c.a = pr.u8v();  // stored alpha (often 0/unused)
            }
            spr.hasPalette_ = true;
        }
        return spr;
    } catch (const std::out_of_range&) {
        log::error("SPR: truncated/corrupt sprite");
        return std::nullopt;
    }
}

std::vector<u8> Sprite::indexedToRgba(usize i) const {
    if (i >= indexed_.size()) return {};
    const SprFrame& f = indexed_[i];
    std::vector<u8> out(static_cast<usize>(f.width) * f.height * 4);
    for (usize p = 0; p < f.pixels.size(); ++p) {
        const u8 idx = f.pixels[p];
        u8* o = &out[p * 4];
        if (idx == 0) {  // transparent
            o[0] = o[1] = o[2] = o[3] = 0;
        } else {
            const SprPalColor& c = palette_[idx];
            o[0] = c.r;
            o[1] = c.g;
            o[2] = c.b;
            o[3] = 255;
        }
    }
    return out;
}

std::optional<std::array<SprPalColor, 256>> parsePal(const std::vector<u8>& bytes) {
    if (bytes.size() < 1024) return std::nullopt;  // a .pal is exactly 256 RGBA entries
    std::array<SprPalColor, 256> pal{};
    for (usize i = 0; i < 256; ++i) {
        pal[i].r = bytes[i * 4 + 0];
        pal[i].g = bytes[i * 4 + 1];
        pal[i].b = bytes[i * 4 + 2];
        pal[i].a = bytes[i * 4 + 3];
    }
    return pal;
}

} // namespace uaro
