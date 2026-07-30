#pragma once
#include <optional>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// RO .imf (data/imf/<job>_<sex>.imf): per-frame layering + attach-offset adjustments for a job's
// composed body sprite. Structure (verified byte-exact against a real file, e.g. 가드_남.imf):
//   float  version         (1.01)
//   int32  checksum
//   int32  maxIndex         -> layer count = maxIndex + 1
//   per layer (maxIndex+1):
//     int32 numActions      (104 = 8 dirs * 13 action types)
//     per action:
//       int32 numFrames
//       per frame: int32 priority, int32 cx, int32 cy
// `priority` is the draw order of this layer for that (action, frame) — a higher priority draws in
// front; `cx`/`cy` shift the layer's attach point. roBrowser ignores IMF entirely and still renders,
// so this is a refinement: without it the parts use their default order/anchor. (S. asked for support.)
struct ImfFrame {
    i32 priority = 0;
    i32 cx = 0;
    i32 cy = 0;
};

class Imf {
public:
    static std::optional<Imf> parse(const std::vector<u8>& bytes);

    float version() const { return version_; }
    int layerCount() const { return static_cast<int>(layers_.size()); }

    // The adjustment for (layer, action, frame), or nullptr if out of range (caller falls back to the
    // sprite's default layering/anchor). action = actIndex (dir*13 + type in RO ordering), frame = the
    // current animation frame within that action.
    const ImfFrame* at(int layer, int action, int frame) const {
        if (layer < 0 || layer >= static_cast<int>(layers_.size())) return nullptr;
        const auto& acts = layers_[static_cast<usize>(layer)];
        if (action < 0 || action >= static_cast<int>(acts.size())) return nullptr;
        const auto& frames = acts[static_cast<usize>(action)];
        if (frame < 0 || frame >= static_cast<int>(frames.size())) return nullptr;
        return &frames[static_cast<usize>(frame)];
    }

private:
    float version_ = 0.0f;
    // layers_[layer][action][frame]
    std::vector<std::vector<std::vector<ImfFrame>>> layers_;
};

}  // namespace uaro
