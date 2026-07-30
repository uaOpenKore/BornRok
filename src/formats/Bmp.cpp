#include "formats/Bmp.hpp"

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"

namespace uaro {

namespace {
constexpr u32 kMaxDim = 16384;
}

std::optional<Image> Bmp::decode(const std::vector<u8>& bytes) {
    if (bytes.size() < 54) {
        log::error("BMP: too small");
        return std::nullopt;
    }
    try {
        ByteReader r(bytes);
        if (r.u8v() != 'B' || r.u8v() != 'M') {
            log::error("BMP: bad signature");
            return std::nullopt;
        }
        r.skip(4);  // file size
        r.skip(4);  // reserved
        const u32 dataOffset = r.u32le();
        const u32 dibSize = r.u32le();
        if (dibSize < 40) {
            log::warn("BMP: unsupported DIB header size {}", dibSize);
            return std::nullopt;
        }
        const i32 width = r.i32le();
        const i32 height = r.i32le();
        r.skip(2);  // planes
        const u16 bpp = r.read<u16>();
        const u32 compression = r.u32le();
        r.skip(4 + 4 + 4);  // imageSize, xPPM, yPPM
        const u32 colorsUsed = r.u32le();

        if (compression != 0) {  // only BI_RGB
            log::warn("BMP: compression {} unsupported", compression);
            return std::nullopt;
        }
        const bool topDown = height < 0;
        const u32 w = static_cast<u32>(width < 0 ? -width : width);
        const u32 h = static_cast<u32>(height < 0 ? -height : height);
        if (w == 0 || h == 0 || w > kMaxDim || h > kMaxDim) {
            log::error("BMP: implausible dimensions {}x{}", w, h);
            return std::nullopt;
        }

        Image img;
        img.width = w;
        img.height = h;
        img.rgba.assign(static_cast<usize>(w) * h * 4, 0);

        auto inBounds = [&](usize off, usize n) { return off + n <= bytes.size(); };
        auto dstRow = [&](u32 y) { return &img.rgba[static_cast<usize>(y) * w * 4]; };
        auto srcRowIndex = [&](u32 y) { return topDown ? y : (h - 1 - y); };

        if (bpp == 4) {
            const u32 palOff = 14 + dibSize;
            const u32 palCount = colorsUsed ? colorsUsed : 16;
            const u32 rowSize = ((w * 4 + 31) / 32) * 4;
            if (!inBounds(palOff, static_cast<usize>(palCount) * 4) ||
                !inBounds(dataOffset, static_cast<usize>(rowSize) * h))
                return std::nullopt;
            for (u32 y = 0; y < h; ++y) {
                const u8* row = &bytes[dataOffset + static_cast<usize>(srcRowIndex(y)) * rowSize];
                u8* o = dstRow(y);
                for (u32 x = 0; x < w; ++x) {
                    u8 byte = row[x / 2];
                    u32 idx = (x & 1) ? (byte & 0x0f) : (byte >> 4);
                    if (idx >= palCount) idx = 0;
                    const u8* p = &bytes[palOff + static_cast<usize>(idx) * 4];
                    o[x * 4 + 0] = p[2];
                    o[x * 4 + 1] = p[1];
                    o[x * 4 + 2] = p[0];
                    o[x * 4 + 3] = 255;
                }
            }
        } else if (bpp == 8) {
            const u32 palOff = 14 + dibSize;
            const u32 palCount = colorsUsed ? colorsUsed : 256;
            const u32 rowSize = ((w + 3) / 4) * 4;
            if (!inBounds(palOff, static_cast<usize>(palCount) * 4) ||
                !inBounds(dataOffset, static_cast<usize>(rowSize) * h))
                return std::nullopt;
            for (u32 y = 0; y < h; ++y) {
                const u8* row = &bytes[dataOffset + static_cast<usize>(srcRowIndex(y)) * rowSize];
                u8* o = dstRow(y);
                for (u32 x = 0; x < w; ++x) {
                    u32 idx = row[x];
                    if (idx >= palCount) idx = 0;
                    const u8* p = &bytes[palOff + static_cast<usize>(idx) * 4];  // B,G,R,_
                    o[x * 4 + 0] = p[2];
                    o[x * 4 + 1] = p[1];
                    o[x * 4 + 2] = p[0];
                    o[x * 4 + 3] = 255;
                }
            }
        } else if (bpp == 24) {
            const u32 rowSize = ((w * 3 + 3) / 4) * 4;
            if (!inBounds(dataOffset, static_cast<usize>(rowSize) * h)) return std::nullopt;
            for (u32 y = 0; y < h; ++y) {
                const u8* row = &bytes[dataOffset + static_cast<usize>(srcRowIndex(y)) * rowSize];
                u8* o = dstRow(y);
                for (u32 x = 0; x < w; ++x) {
                    const u8* p = &row[x * 3];  // B,G,R
                    o[x * 4 + 0] = p[2];
                    o[x * 4 + 1] = p[1];
                    o[x * 4 + 2] = p[0];
                    o[x * 4 + 3] = 255;
                }
            }
        } else if (bpp == 32) {
            const u32 rowSize = w * 4;
            if (!inBounds(dataOffset, static_cast<usize>(rowSize) * h)) return std::nullopt;
            for (u32 y = 0; y < h; ++y) {
                const u8* row = &bytes[dataOffset + static_cast<usize>(srcRowIndex(y)) * rowSize];
                u8* o = dstRow(y);
                for (u32 x = 0; x < w; ++x) {
                    const u8* p = &row[x * 4];  // B,G,R,A
                    o[x * 4 + 0] = p[2];
                    o[x * 4 + 1] = p[1];
                    o[x * 4 + 2] = p[0];
                    o[x * 4 + 3] = p[3];
                }
            }
        } else if (bpp == 16) {  // BI_RGB 16-bit = X1R5G5B5
            const u32 rowSize = ((w * 2 + 3) / 4) * 4;
            if (!inBounds(dataOffset, static_cast<usize>(rowSize) * h)) return std::nullopt;
            for (u32 y = 0; y < h; ++y) {
                const u8* row = &bytes[dataOffset + static_cast<usize>(srcRowIndex(y)) * rowSize];
                u8* o = dstRow(y);
                for (u32 x = 0; x < w; ++x) {
                    u16 px = static_cast<u16>(row[x * 2] | (row[x * 2 + 1] << 8));
                    u8 r5 = (px >> 10) & 0x1f, g5 = (px >> 5) & 0x1f, b5 = px & 0x1f;
                    o[x * 4 + 0] = static_cast<u8>((r5 << 3) | (r5 >> 2));
                    o[x * 4 + 1] = static_cast<u8>((g5 << 3) | (g5 >> 2));
                    o[x * 4 + 2] = static_cast<u8>((b5 << 3) | (b5 >> 2));
                    o[x * 4 + 3] = 255;
                }
            }
        } else {
            log::warn("BMP: unsupported bpp {}", bpp);
            return std::nullopt;
        }
        return img;
    } catch (const std::out_of_range&) {
        log::error("BMP: truncated header");
        return std::nullopt;
    }
}

} // namespace uaro
