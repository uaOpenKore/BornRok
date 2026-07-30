#include "formats/Gnd.hpp"

#include <cstring>

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"

namespace uaro {

std::optional<Gnd> Gnd::parse(const std::vector<u8>& bytes) {
    if (bytes.size() < 18) {
        log::error("GND: too small");
        return std::nullopt;
    }
    try {
        ByteReader r(bytes);
        char magic[4];
        r.read_bytes(magic, 4);
        if (std::memcmp(magic, "GRGN", 4) != 0) {
            log::error("GND: bad signature");
            return std::nullopt;
        }
        const u8 major = r.u8v();
        const u8 minor = r.u8v();
        const u16 version = static_cast<u16>(major) * 0x100 + minor;

        Gnd gnd;
        gnd.width_ = r.u32le();
        gnd.height_ = r.u32le();
        gnd.zoom_ = r.f32le();

        const u64 cells = static_cast<u64>(gnd.width_) * gnd.height_;
        if (cells == 0 || cells > 4'000'000ull) {
            log::error("GND: implausible dimensions {}x{}", gnd.width_, gnd.height_);
            return std::nullopt;
        }

        // Texture list.
        const u32 texCount = r.u32le();
        const u32 texNameLen = r.u32le();
        if (texCount > 65535 || texNameLen == 0 || texNameLen > 1024) {
            log::error("GND: implausible textures ({} x {})", texCount, texNameLen);
            return std::nullopt;
        }
        gnd.textures_.reserve(texCount);
        for (u32 i = 0; i < texCount; ++i) gnd.textures_.push_back(r.read_cstring(texNameLen));

        // Lightmaps (version >= 1.7): header + the pixel data, kept now so the ground
        // mesh can bake the baked shadows + coloured light into the vertex colours.
        if (version >= 0x107) {
            gnd.lightmapCount_ = r.i32le();
            gnd.lmW_ = r.i32le();
            gnd.lmH_ = r.i32le();
            r.i32le();  // cells-per-lightmap field (unused here)
            if (gnd.lightmapCount_ < 0 || gnd.lmW_ <= 0 || gnd.lmH_ <= 0 || gnd.lmW_ > 256 ||
                gnd.lmH_ > 256) {
                log::error("GND: implausible lightmap header");
                return std::nullopt;
            }
            const u64 lmBytes =
                static_cast<u64>(gnd.lightmapCount_) * gnd.lmW_ * gnd.lmH_ * 4;
            gnd.lightmap_.resize(static_cast<usize>(lmBytes));
            r.read_bytes(gnd.lightmap_.data(), static_cast<usize>(lmBytes));
        }

        // Surfaces (textured quads).
        const u32 surfaceCount = r.u32le();
        if (surfaceCount > 20'000'000u) {
            log::error("GND: implausible surface count {}", surfaceCount);
            return std::nullopt;
        }
        gnd.surfaces_.reserve(surfaceCount);
        for (u32 i = 0; i < surfaceCount; ++i) {
            GndSurface s;
            for (int k = 0; k < 4; ++k) s.u[k] = r.f32le();
            for (int k = 0; k < 4; ++k) s.v[k] = r.f32le();
            s.textureId = r.read<i16>();
            s.lightmapId = r.read<i16>();
            r.read_bytes(s.color, 4);
            gnd.surfaces_.push_back(s);
        }

        // Cubes (per-cell ground geometry).
        gnd.cubes_.resize(cells);
        for (auto& c : gnd.cubes_) {
            for (int k = 0; k < 4; ++k) c.height[k] = r.f32le();
            c.tileUp = r.i32le();
            c.tileFront = r.i32le();
            c.tileRight = r.i32le();
        }
        return gnd;
    } catch (const std::out_of_range&) {
        log::error("GND: truncated/corrupt file");
        return std::nullopt;
    }
}

} // namespace uaro
