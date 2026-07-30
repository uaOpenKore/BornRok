#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/math/Math.hpp"
#include "formats/Str.hpp"
#include "render/Texture.hpp"

namespace uaro {

class Vfs;
struct WorldSpritePass;

// Plays a parsed RO .str effect (keyframed, layered, blended billboard quads) at a world
// position — RO's format for level-up (angel/joblvup), warps, skills, sakura petals. Each
// layer interpolates a type-0 source keyframe toward the next type-1 morph target (whose
// fields are per-frame SLOPES), then draws a textured quad — rotated, tinted, blended per the
// keyframe — billboarded at the effect's world position. Reference: roBrowser StrEffect.js.
class StrEffect {
public:
    StrEffect() = default;
    // Texture has a manual destroy() and no destructor (it would double-free on copy), so the
    // owner must release the layer textures. This effect is held by value as a long-lived member
    // and never copied, so a destructor + deleted copy is the safe, leak-free ownership model.
    ~StrEffect() { releaseTextures(); }
    StrEffect(const StrEffect&) = delete;
    StrEffect& operator=(const StrEffect&) = delete;

    // Load data/texture/effect/<name>.str + its per-layer .bmp textures. false if missing.
    bool load(const Vfs& vfs, const std::string& name);
    // Play a synthesized Str (e.g. a coded skill effect reproduced from the exe: magnum/bash) through
    // the same renderer. `dir` is prepended to each layer texture name (""= effect root). Textures are
    // loaded via readPreferPng like load(), so HD .png overrides apply here too.
    bool loadFromStr(const Vfs& vfs, Str str, const std::string& dir = "");
    bool ready() const { return str_.has_value(); }
    double duration() const;  // seconds: maxKey / fps

    // Draw the whole effect for `elapsed` seconds since it started, billboarded at `pos`.
    // `intensity` (<1) dims the whole effect: it multiplies every layer's RGBA colour, so it works for
    // BOTH additive layers (RGB scales the light added) and alpha layers (alpha scales opacity). Used to
    // tone down over-bright coded effects like Pneuma (S.: "пневма очень яркая").
    // `upright` stands the billboard vertically (world +Y up) facing the camera horizontally, instead of
    // the default full camera billboard that tilts back with the camera pitch and lays a cast effect flat
    // under the caster's feet (S.: "спрайт каста ложится под ноги — повернуть на 90° по X"). `flat` (the
    // ground plane, for the warp portal) takes precedence over `upright`.
    void render(const WorldSpritePass& pass, const Vec3& pos, double elapsed, float scale = 1.0f,
                bool flat = false, float intensity = 1.0f, bool upright = false) const;

private:
    void releaseTextures() {
        for (auto& layer : tex_)
            for (auto& t : layer) t.destroy();
        tex_.clear();
    }
    void loadLayerTextures(const Vfs& vfs, const std::string& dir);  // shared by load()/loadFromStr()

    std::optional<Str> str_;
    std::vector<std::vector<Texture>> tex_;  // [layer][texture-frame]
};

}  // namespace uaro
