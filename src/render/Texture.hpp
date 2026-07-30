#pragma once
#include <bgfx/bgfx.h>

#include "core/Log.hpp"
#include "core/Types.hpp"

namespace uaro {

// Minimal 2D texture wrapper. v0 only needs to upload RGBA pixels (solid colours
// and a generated checker for the test scene). Real image decoding (BMP/TGA/SPR
// and large modern formats) lands with the asset layer in v2.
class Texture {
public:
    Texture() = default;

    // `smooth` selects bilinear filtering (bgfx default) + edge clamp instead of
    // the crisp point sampling — used for sprites so upscaled pixels don't look
    // blocky. The caller must edge-bleed transparent texels first, else bilinear
    // pulls their RGB into opaque edges (a dark/magenta fringe).
    void create(u16 w, u16 h, const void* rgba8, bool smooth = false) {
        destroy();
        // Defensive: never hand bgfx a zero-sized/null texture or one past the GPU's max size --
        // bgfx asserts/aborts on bad args, which on a weak GPU or a corrupt asset reads to the
        // user as a hard crash / "disconnect" (S.: "не падать, если текстура не создалась"). On
        // any such case leave an INVALID handle; SpriteBatch already swaps in its white fallback,
        // so the worst outcome is a missing image, not a dead client.
        if (w == 0 || h == 0 || rgba8 == nullptr) {
            log::warn("Texture::create: skipped bad input {}x{} data={}", w, h, rgba8 ? "ok" : "null");
            return;  // handle_ stays BGFX_INVALID_HANDLE
        }
        const u32 maxSz = bgfx::getCaps()->limits.maxTextureSize;
        if (w > maxSz || h > maxSz) {
            log::warn("Texture::create: skipped {}x{}, exceeds GPU max texture size {}", w, h, maxSz);
            return;
        }
        const bgfx::Memory* mem = bgfx::copy(rgba8, static_cast<u32>(w) * h * 4);
        u64 flags = BGFX_TEXTURE_NONE;
        if (smooth) flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;  // bilinear + clamp
        else flags |= BGFX_SAMPLER_POINT;
        handle_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags, mem);
        if (!bgfx::isValid(handle_))  // GPU refused it (out of memory / unsupported) -> degrade, don't die
            log::warn("Texture::create: bgfx rejected a {}x{} texture (GPU out of memory?)", w, h);
    }

    // Create an EMPTY, updatable RGBA8 texture (no initial data). This matters on D3D11: a texture
    // created WITH initial pixels becomes D3D11_USAGE_IMMUTABLE, and update() (UpdateSubresource) is
    // then a no-op -- so a dynamically-filled atlas (the font glyph cache) must be created this way,
    // then filled via update(). Content is undefined until updated; the caller clears it first.
    void createDynamic(u16 w, u16 h, bool smooth = false) {
        destroy();
        if (w == 0 || h == 0) {
            log::warn("Texture::createDynamic: skipped bad size {}x{}", w, h);
            return;
        }
        const u32 maxSz = bgfx::getCaps()->limits.maxTextureSize;
        if (w > maxSz || h > maxSz) {
            log::warn("Texture::createDynamic: skipped {}x{}, exceeds GPU max {}", w, h, maxSz);
            return;
        }
        u64 flags = smooth ? (BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP) : BGFX_SAMPLER_POINT;
        handle_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags, nullptr);
        if (!bgfx::isValid(handle_))
            log::warn("Texture::createDynamic: bgfx rejected a {}x{} texture", w, h);
    }

    // Upload an RGBA8 sub-rect into an existing texture WITHOUT recreating the handle -- safe to call
    // mid-frame (bgfx defers the copy), so a SpriteBatch that already recorded this handle stays valid.
    // Used by the font's dynamic glyph atlas to add glyphs on demand. No-op on an invalid handle.
    void update(int x, int y, int w, int h, const void* rgba8) {
        if (!bgfx::isValid(handle_) || w <= 0 || h <= 0 || rgba8 == nullptr) return;
        const bgfx::Memory* mem = bgfx::copy(rgba8, static_cast<u32>(w) * static_cast<u32>(h) * 4);
        bgfx::updateTexture2D(handle_, 0, 0, static_cast<u16>(x), static_cast<u16>(y),
                              static_cast<u16>(w), static_cast<u16>(h), mem);
    }

    // 1x1 solid colour (RGBA, 0xAABBGGRR byte order in memory: r,g,b,a).
    void createSolid(u8 r, u8 g, u8 b, u8 a = 255) {
        const u8 px[4] = {r, g, b, a};
        create(1, 1, px);
    }

    void destroy() {
        if (bgfx::isValid(handle_)) {
            bgfx::destroy(handle_);
            handle_ = BGFX_INVALID_HANDLE;
        }
    }

    bgfx::TextureHandle handle() const { return handle_; }
    bool valid() const { return bgfx::isValid(handle_); }

private:
    bgfx::TextureHandle handle_ = BGFX_INVALID_HANDLE;
};

} // namespace uaro
