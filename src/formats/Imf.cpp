#include "formats/Imf.hpp"

#include <cstring>

namespace uaro {

namespace {
// Little-endian readers with bounds checking; return false on a short read.
struct Reader {
    const u8* p;
    usize n;
    usize off = 0;
    bool i32(i32& out) {
        if (off + 4 > n) return false;
        std::memcpy(&out, p + off, 4);
        off += 4;
        return true;
    }
    bool f32v(float& out) {
        if (off + 4 > n) return false;
        std::memcpy(&out, p + off, 4);
        off += 4;
        return true;
    }
};
}  // namespace

std::optional<Imf> Imf::parse(const std::vector<u8>& bytes) {
    Reader r{bytes.data(), bytes.size()};
    Imf imf;
    i32 checksum = 0, maxIndex = 0;
    if (!r.f32v(imf.version_)) return std::nullopt;
    if (!r.i32(checksum)) return std::nullopt;
    if (!r.i32(maxIndex)) return std::nullopt;
    // maxIndex is "highest layer index" -> layerCount = maxIndex + 1. Guard against a corrupt huge
    // count (a real IMF has 1-2 layers, 104 actions, a few frames).
    if (maxIndex < 0 || maxIndex > 4096) return std::nullopt;
    const int layerCount = maxIndex + 1;
    imf.layers_.resize(static_cast<usize>(layerCount));
    for (int L = 0; L < layerCount; ++L) {
        i32 numActions = 0;
        if (!r.i32(numActions) || numActions < 0 || numActions > 100000) return std::nullopt;
        auto& acts = imf.layers_[static_cast<usize>(L)];
        acts.resize(static_cast<usize>(numActions));
        for (int a = 0; a < numActions; ++a) {
            i32 numFrames = 0;
            if (!r.i32(numFrames) || numFrames < 0 || numFrames > 100000) return std::nullopt;
            auto& frames = acts[static_cast<usize>(a)];
            frames.resize(static_cast<usize>(numFrames));
            for (int f = 0; f < numFrames; ++f) {
                ImfFrame fr;
                if (!r.i32(fr.priority) || !r.i32(fr.cx) || !r.i32(fr.cy)) return std::nullopt;
                frames[static_cast<usize>(f)] = fr;
            }
        }
    }
    return imf;
}

}  // namespace uaro
