#include "render/SpriteBatch.hpp"

#include <cmath>
#include <cstring>

#include "core/Log.hpp"
#include "render/Camera.hpp"
#include "render/Shader.hpp"

namespace uaro {

namespace {
constexpr u64 kState2D = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;
}

SpriteBatch::~SpriteBatch() { shutdown(); }

bool SpriteBatch::init(const std::string& assetBaseDir) {
    layout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    white_.createSolid(255, 255, 255, 255);
    program_ = load_program(assetBaseDir, "vs_sprite", "fs_sprite");

    if (!bgfx::isValid(program_)) {
        log::warn("SpriteBatch: sprite program unavailable (shaders not compiled?); "
                  "frames will show the clear colour only.");
        return false;
    }
    return true;
}

void SpriteBatch::shutdown() {
    if (bgfx::isValid(program_)) { bgfx::destroy(program_); program_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sampler_)) { bgfx::destroy(sampler_); sampler_ = BGFX_INVALID_HANDLE; }
    white_.destroy();
}

void SpriteBatch::begin(int screenW, int screenH, bgfx::ViewId viewId, int logicalW,
                        int logicalH) {
    // Coordinate space the caller draws in (== screen unless UI scale shrinks it).
    screenW_ = logicalW > 0 ? logicalW : screenW;
    screenH_ = logicalH > 0 ? logicalH : screenH;
    viewId_ = viewId;
    verts_.clear();
    indices_.clear();
    curTex_ = BGFX_INVALID_HANDLE;

    // Viewport stays at the real backbuffer size; the ortho uses the (possibly smaller)
    // logical size, so a smaller logical space stretches to fill the screen = magnified UI.
    bgfx::setViewRect(viewId_, 0, 0, static_cast<u16>(screenW), static_cast<u16>(screenH));
    Mat4 proj = Camera::screenOrtho(screenW_, screenH_);
    bgfx::setViewTransform(viewId_, nullptr, proj.m);
}

void SpriteBatch::draw(f32 x, f32 y, f32 w, f32 h, u32 abgr, const Texture& tex) {
    if (!ready()) return;
    bgfx::TextureHandle t = tex.valid() ? tex.handle() : white_.handle();

    if (bgfx::isValid(curTex_) && curTex_.idx != t.idx) flush();
    curTex_ = t;

    const u16 base = static_cast<u16>(verts_.size());
    verts_.push_back({x,     y,     0.0f, 0.0f, abgr});
    verts_.push_back({x + w, y,     1.0f, 0.0f, abgr});
    verts_.push_back({x + w, y + h, 1.0f, 1.0f, abgr});
    verts_.push_back({x,     y + h, 0.0f, 1.0f, abgr});

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void SpriteBatch::draw(f32 x, f32 y, f32 w, f32 h, u32 abgr) {
    draw(x, y, w, h, abgr, white_);
}

void SpriteBatch::draw(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1,
                       u32 abgr, const Texture& tex) {
    if (!ready()) return;
    bgfx::TextureHandle t = tex.valid() ? tex.handle() : white_.handle();

    if (bgfx::isValid(curTex_) && curTex_.idx != t.idx) flush();
    curTex_ = t;

    const u16 base = static_cast<u16>(verts_.size());
    verts_.push_back({x,     y,     u0, v0, abgr});
    verts_.push_back({x + w, y,     u1, v0, abgr});
    verts_.push_back({x + w, y + h, u1, v1, abgr});
    verts_.push_back({x,     y + h, u0, v1, abgr});

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void SpriteBatch::drawRotated(f32 cx, f32 cy, f32 w, f32 h, f32 radians, u32 abgr,
                              const Texture& tex) {
    if (!ready()) return;
    bgfx::TextureHandle t = tex.valid() ? tex.handle() : white_.handle();
    if (bgfx::isValid(curTex_) && curTex_.idx != t.idx) flush();
    curTex_ = t;

    const f32 c = std::cos(radians), s = std::sin(radians);
    const f32 hw = w * 0.5f, hh = h * 0.5f;
    // The four corners, each (dx,dy) from the centre rotated by `radians` then offset.
    const u16 base = static_cast<u16>(verts_.size());
    verts_.push_back({cx + (-hw) * c - (-hh) * s, cy + (-hw) * s + (-hh) * c, 0.0f, 0.0f, abgr});
    verts_.push_back({cx + (hw) * c - (-hh) * s,  cy + (hw) * s + (-hh) * c,  1.0f, 0.0f, abgr});
    verts_.push_back({cx + (hw) * c - (hh) * s,   cy + (hw) * s + (hh) * c,   1.0f, 1.0f, abgr});
    verts_.push_back({cx + (-hw) * c - (hh) * s,  cy + (-hw) * s + (hh) * c,  0.0f, 1.0f, abgr});

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void SpriteBatch::flush() {
    if (verts_.empty() || !ready()) {
        verts_.clear();
        indices_.clear();
        return;
    }
    const u32 numVerts = static_cast<u32>(verts_.size());
    const u32 numIdx = static_cast<u32>(indices_.size());

    if (bgfx::getAvailTransientVertexBuffer(numVerts, layout_) >= numVerts &&
        bgfx::getAvailTransientIndexBuffer(numIdx) >= numIdx) {
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, numVerts, layout_);
        bgfx::allocTransientIndexBuffer(&tib, numIdx);
        std::memcpy(tvb.data, verts_.data(), numVerts * sizeof(Vertex));
        std::memcpy(tib.data, indices_.data(), numIdx * sizeof(u16));

        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        bgfx::setTexture(0, sampler_, curTex_);
        bgfx::setState(kState2D);
        bgfx::submit(viewId_, program_);
    } else {
        log::warn("SpriteBatch::flush: not enough transient buffer space ({} verts)", numVerts);
    }

    verts_.clear();
    indices_.clear();
}

void SpriteBatch::end() {
    flush();
}

} // namespace uaro
