#include "formats/Tga.hpp"

#include <cstring>

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"

namespace uaro {

namespace {
constexpr u32 kMaxDim = 16384;
}

std::optional<Image> Tga::decode(const std::vector<u8>& bytes) {
    if (bytes.size() < 18) {  // an 18-byte header is the minimum
        log::error("TGA: too small");
        return std::nullopt;
    }
    try {
        ByteReader r(bytes);
        const u8 idLength = r.u8v();
        const u8 colorMapType = r.u8v();
        const u8 imageType = r.u8v();
        r.skip(2);  // colour-map first-entry index
        const u16 colorMapLength = r.u16le();
        const u8 colorMapDepth = r.u8v();
        r.skip(4);  // x/y origin
        const u16 width = r.u16le();
        const u16 height = r.u16le();
        const u8 bpp = r.u8v();
        const u8 descriptor = r.u8v();

        if (imageType != 2 && imageType != 10) {  // 2 = uncompressed, 10 = RLE (both true-color)
            log::warn("TGA: unsupported image type {} (need 2 or 10)", imageType);
            return std::nullopt;
        }
        if (bpp != 24 && bpp != 32) {
            log::warn("TGA: unsupported bpp {} (need 24 or 32)", bpp);
            return std::nullopt;
        }
        if (width == 0 || height == 0 || width > kMaxDim || height > kMaxDim) {
            log::error("TGA: implausible dimensions {}x{}", width, height);
            return std::nullopt;
        }
        r.skip(idLength);  // image-ID field
        if (colorMapType == 1)  // a present colour map (true-color RO icons have none); skip it
            r.skip(static_cast<usize>(colorMapLength) * ((colorMapDepth + 7u) / 8u));

        const int bpc = bpp / 8;  // source bytes per pixel (3 = BGR, 4 = BGRA)
        const usize pixelCount = static_cast<usize>(width) * height;
        Image img;
        img.width = width;
        img.height = height;
        img.rgba.assign(pixelCount * 4, 0);

        // Read one source pixel (B,G,R[,A]) and store it as RGBA. A 24-bit magenta
        // (255,0,255) becomes transparent -- the classic RO transparent-background key.
        auto putPixel = [&](usize px) {
            const u8 b = r.u8v();
            const u8 g = r.u8v();
            const u8 rr = r.u8v();
            u8 a = (bpc == 4) ? r.u8v() : 255;
            if (bpc == 3 && rr == 255 && g == 0 && b == 255) a = 0;
            u8* o = &img.rgba[px * 4];
            o[0] = rr;
            o[1] = g;
            o[2] = b;
            o[3] = a;
        };

        if (imageType == 2) {  // uncompressed: pixelCount pixels back to back
            for (usize i = 0; i < pixelCount; ++i) putPixel(i);
        } else {  // type 10: run-length encoded
            usize done = 0;
            while (done < pixelCount) {
                const u8 packet = r.u8v();
                usize run = static_cast<usize>(packet & 0x7f) + 1;
                if (done + run > pixelCount) run = pixelCount - done;  // clamp a malformed run
                if (packet & 0x80) {  // RLE packet: one pixel repeated `run` times
                    const u8 b = r.u8v();
                    const u8 g = r.u8v();
                    const u8 rr = r.u8v();
                    u8 a = (bpc == 4) ? r.u8v() : 255;
                    if (bpc == 3 && rr == 255 && g == 0 && b == 255) a = 0;
                    for (usize i = 0; i < run; ++i) {
                        u8* o = &img.rgba[(done + i) * 4];
                        o[0] = rr;
                        o[1] = g;
                        o[2] = b;
                        o[3] = a;
                    }
                } else {  // raw packet: `run` literal pixels
                    for (usize i = 0; i < run; ++i) putPixel(done + i);
                }
                done += run;
            }
        }

        // TGA pixels default to bottom-up; Image is top-down, so flip unless bit 5 is set.
        if ((descriptor & 0x20) == 0 && height > 1) {
            const usize rowSize = static_cast<usize>(width) * 4;
            std::vector<u8> tmp(rowSize);
            for (u32 y = 0; y < height / 2u; ++y) {
                u8* top = &img.rgba[static_cast<usize>(y) * rowSize];
                u8* bot = &img.rgba[static_cast<usize>(height - 1u - y) * rowSize];
                std::memcpy(tmp.data(), top, rowSize);
                std::memcpy(top, bot, rowSize);
                std::memcpy(bot, tmp.data(), rowSize);
            }
        }
        return img;
    } catch (const std::out_of_range&) {
        log::error("TGA: truncated / malformed");
        return std::nullopt;
    }
}

} // namespace uaro
