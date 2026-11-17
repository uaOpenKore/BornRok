#include "world/SpriteComposer.hpp"

#include <cmath>

namespace uaro {

namespace {
const ActFrame* getFrame(const Action& act, int action, int frame) {
    const auto& actions = act.actions();
    if (action < 0 || static_cast<usize>(action) >= actions.size()) return nullptr;
    const auto& frames = actions[action].frames;
    if (frame < 0 || static_cast<usize>(frame) >= frames.size()) return nullptr;
    return &frames[frame];
}
} // namespace

std::array<i32, 2> frameAnchor(const Action& act, int action, int frame) {
    const ActFrame* f = getFrame(act, action, frame);
    if (f && !f->anchors.empty()) return {f->anchors[0][0], f->anchors[0][1]};
    return {0, 0};
}

void composeFrame(const Action& act, const Sprite& spr, int action, int frame, float ox, float oy,
                  int part, std::vector<ComposedQuad>& out, int frameNext, float t) {
    const ActFrame* f = getFrame(act, action, frame);
    if (!f) return;
    // The frame to blend toward, only when interpolation is requested and there is a distinct next
    // frame. When absent, `t` is forced to 0 so every lerp below collapses to the current value.
    const ActFrame* fN = (frameNext >= 0 && frameNext != frame && t > 0.0f)
                             ? getFrame(act, action, frameNext)
                             : nullptr;
    if (!fN) t = 0.0f;
    const float it = 1.0f - t;

    for (usize li = 0; li < f->layers.size(); ++li) {
        const ActLayer& L = f->layers[li];
        if (L.sprIndex < 0) continue;
        // A synthetic WebP sprite (S. converted the pack to WebP: no .spr, frames live in .png.d and
        // are loaded as truecolor) carries ONLY rgba frames — use them even when the .act layer says
        // indexed (its sprType targets the original .spr's indexed frames, which don't exist here).
        const bool useRgba = (L.sprType == 1) || spr.indexedFrames().empty();
        const auto& frames = useRgba ? spr.rgbaFrames() : spr.indexedFrames();
        // A synthetic sprite keeps the original TRUECOLOR frames after the indexed ones (t<i>.webp, see
        // loadSprOrPngd): a sprType-1 layer's index counts from that base, not from 0.
        const i32 sprIndex =
            (spr.synthetic() && L.sprType == 1) ? L.sprIndex + static_cast<i32>(spr.truecolorBase()) : L.sprIndex;
        if (static_cast<usize>(sprIndex) >= frames.size()) continue;
        const SprFrame& src = frames[sprIndex];
        if (src.width == 0 || src.height == 0) continue;

        // Blend the transform toward the matching layer of the next frame ONLY when that layer draws
        // the same bitmap (same sprIndex) -- otherwise a pose change (different bitmap) would slide.
        float lx = static_cast<float>(L.x), ly = static_cast<float>(L.y);
        float scx = L.scaleX, scy = L.scaleY;
        float la = static_cast<float>(L.a), lr = static_cast<float>(L.r), lg = static_cast<float>(L.g),
              lb = static_cast<float>(L.b);
        if (fN && li < fN->layers.size()) {
            const ActLayer& N = fN->layers[li];
            if (N.sprIndex == L.sprIndex) {
                lx = it * lx + t * static_cast<float>(N.x);
                ly = it * ly + t * static_cast<float>(N.y);
                scx = it * scx + t * N.scaleX;
                scy = it * scy + t * N.scaleY;
                la = it * la + t * static_cast<float>(N.a);
                lr = it * lr + t * static_cast<float>(N.r);
                lg = it * lg + t * static_cast<float>(N.g);
                lb = it * lb + t * static_cast<float>(N.b);
            }
        }
        // renderScale shrinks the QUAD only (an HD/2k synthetic pack has K× frame pixels; 1/K here
        // renders it at the original logical size while the HD texture stays crisp). 1.0 for real .spr.
        const float ps = spr.renderScale();
        const float w = src.width * std::fabs(scx) * ps;
        const float h = src.height * std::fabs(scy) * ps;

        ComposedQuad q;
        q.part = part;
        q.sprIndex = sprIndex;  // the frame actually drawn (frameTex/frameNrmTex look it up by this)
        q.indexed = !useRgba;
        q.w = w;
        q.h = h;
        // RO layer (x,y) is the sprite centre relative to the actor anchor.
        q.x = ox + lx - w * 0.5f;
        q.y = oy + ly - h * 0.5f;
        q.mirror = (L.mirror != 0);
        q.r = static_cast<u8>(lr + 0.5f);
        q.g = static_cast<u8>(lg + 0.5f);
        q.b = static_cast<u8>(lb + 0.5f);
        q.a = static_cast<u8>(la + 0.5f);
        out.push_back(q);
    }
}

} // namespace uaro
