#pragma once
#include <bgfx/bgfx.h>

#include <string>
#include <vector>

#include "core/Types.hpp"
#include "render/Texture.hpp"

namespace uaro {

// Immediate-mode 2D quad batcher. Accumulates textured quads between begin()/end()
// and flushes them via bgfx transient buffers. One texture per batch (flushes on
// texture change) — sufficient for the v0 test scene and basic UI; an atlas-based
// batcher arrives with the UI layer (v5).
class SpriteBatch {
public:
    SpriteBatch() = default;
    ~SpriteBatch();

    // assetBaseDir is the directory containing "shaders/<profile>/*.bin".
    bool init(const std::string& assetBaseDir);
    void shutdown();

    // viewId selects the bgfx view. UI-only scenes use the default (view 0); a
    // scene that also renders 3D on view 0 must draw its 2D overlay on another
    // view (e.g. 1), because a bgfx view has a single transform per frame.
    // logicalW/H (0 = same as screen): the ortho projection uses logical size while the
    // viewport stays at screen size, so passing a smaller logical size magnifies everything
    // drawn (used by the UI-scale setting, #134).
    void begin(int screenW, int screenH, bgfx::ViewId viewId = 0, int logicalW = 0,
               int logicalH = 0);
    void draw(f32 x, f32 y, f32 w, f32 h, u32 abgr, const Texture& tex);
    void draw(f32 x, f32 y, f32 w, f32 h, u32 abgr);  // solid colour (white tex)
    // Textured quad with an explicit UV sub-rect (for atlases / bitmap fonts).
    void draw(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 abgr,
              const Texture& tex);
    // Textured quad rotated by `radians` around its centre (cx, cy) — for the minimap
    // facing arrow and any other rotated 2D sprite.
    void drawRotated(f32 cx, f32 cy, f32 w, f32 h, f32 radians, u32 abgr, const Texture& tex);
    void end();

    bool ready() const { return bgfx::isValid(program_); }

private:
    void flush();

    struct Vertex {
        f32 x, y;
        f32 u, v;
        u32 abgr;
    };

    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    Texture white_;

    std::vector<Vertex> verts_;
    std::vector<u16> indices_;
    bgfx::TextureHandle curTex_ = BGFX_INVALID_HANDLE;

    int screenW_ = 0;
    int screenH_ = 0;
    bgfx::ViewId viewId_ = 0;
};

} // namespace uaro
