#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

struct RswWater {
    f32 level = 0;
    i32 type = 0;
    f32 waveHeight = 0;
    f32 waveSpeed = 0;
    f32 wavePitch = 0;
    i32 animSpeed = 0;
};

struct RswLight {
    i32 longitude = 45;
    i32 latitude = 45;
    f32 diffuse[3] = {1, 1, 1};
    f32 ambient[3] = {0, 0, 0};
    f32 opacity = 1;  // light intensity / shadow opacity (version >= 1.7)
};

// A point light placed on the map (RSW object type 2): position, RGB colour (float
// 0..1 per channel, as stored in the RSW), and falloff range. Lights the nearby
// ground/objects — torches and lamps that make dungeons atmospheric.
struct RswLightSource {
    f32 pos[3] = {0, 0, 0};
    f32 color[3] = {1.0f, 1.0f, 1.0f};
    f32 range = 0;
};

// An effect emitter placed on the map (RSW object type 4): position + a built-in
// effect id (torch flame, falling sakura petals, sparkles, ...) and its timing.
struct RswEffect {
    f32 pos[3] = {0, 0, 0};
    i32 id = 0;
    f32 delay = 0;
    f32 param[4] = {0, 0, 0, 0};
};

// A 3D model placed on the map (references a .rsm file).
struct RswModel {
    std::string name;
    i32 animType = 0;
    f32 animSpeed = 1;
    i32 blockType = 0;
    std::string filename;  // .rsm
    std::string nodeName;
    f32 pos[3] = {0, 0, 0};
    f32 rot[3] = {0, 0, 0};
    f32 scale[3] = {1, 1, 1};
};

// RSW (Resource World): the map definition — which GND/GAT to load, water/light,
// and the placed objects. v2 parses the model objects (the placements needed for
// rendering); light/sound/effect object layouts (version-specific) come later.
class Rsw {
public:
    static std::optional<Rsw> parse(const std::vector<u8>& bytes);

    u16 version() const { return version_; }
    const std::string& iniFile() const { return ini_; }
    const std::string& gndFile() const { return gnd_; }
    const std::string& gatFile() const { return gat_; }
    const std::string& scrFile() const { return scr_; }
    const RswWater& water() const { return water_; }
    const RswLight& light() const { return light_; }
    RswLight& light() { return light_; }  // world layer may tune it (dark-map boost)

    const std::vector<RswModel>& models() const { return models_; }
    const std::vector<RswLightSource>& lightSources() const { return lightSources_; }
    const std::vector<RswEffect>& effects() const { return effects_; }
    i32 objectCount() const { return objectCount_; }

    // True if every object parsed (none skipped). When false, stoppedAtObject() is the
    // index of the first object whose type we could not parse (a hard desync guard).
    bool complete() const { return stoppedAtObject_ < 0; }
    int stoppedAtObject() const { return stoppedAtObject_; }

    // Bytes left unread (non-model objects + the post-object quad tree).
    usize trailing() const { return trailing_; }

private:
    u16 version_ = 0;
    std::string ini_, gnd_, gat_, scr_;
    RswWater water_;
    RswLight light_;
    i32 groundBox_[4] = {0, 0, 0, 0};  // top, bottom, left, right
    i32 objectCount_ = 0;
    int stoppedAtObject_ = -1;
    std::vector<RswModel> models_;
    std::vector<RswLightSource> lightSources_;
    std::vector<RswEffect> effects_;
    usize trailing_ = 0;
};

} // namespace uaro
