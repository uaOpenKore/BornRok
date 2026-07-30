#pragma once
#include <optional>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Autodesk FLIC animation (.flc / .fli). RO ships a few legacy effects in this format (e.g.
// data/sprite/효과/kasa.flc). Decode = a palette-indexed canvas updated by per-frame delta chunks;
// we expand every frame to RGBA8 up front so the runtime just blits frame N. Supported chunk types:
// COLOR256/COLOR64 (palette), BRUN (full-frame RLE), LC (FLI byte delta), SS2/DELTA_FLC (FLC word
// delta), BLACK, COPY (raw). Postage-stamp (PSTAMP) chunks are skipped.
class Flc {
public:
    static std::optional<Flc> parse(const std::vector<u8>& bytes);

    int width() const { return width_; }
    int height() const { return height_; }
    int frameCount() const { return static_cast<int>(frames_.size()); }
    int frameDelayMs() const { return delayMs_; }  // per-frame delay

    // RGBA8 (w*h*4) for frame i, top-down. Index 0 is treated as transparent (RO effects key on it).
    const std::vector<u8>& frame(int i) const { return frames_[static_cast<usize>(i)]; }

private:
    int width_ = 0, height_ = 0, delayMs_ = 100;
    std::vector<std::vector<u8>> frames_;  // each = RGBA8, w*h*4
};

}  // namespace uaro
