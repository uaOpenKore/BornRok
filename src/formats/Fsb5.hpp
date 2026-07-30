#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// FMOD FSB5 sound bank — the container inside RoM AudioClip bundles (the .resource node).
// RoM banks are MODE 15 (Vorbis) with HEADERLESS packets: each sample's data is a run of
// u16-size-prefixed raw Vorbis packets, and the Vorbis SETUP header is not stored — only its
// CRC32, resolved against FMOD's known-encoder table (see audio/FsbVorbis). This parser only
// splits the container; decoding lives in the audio layer.
struct Fsb5Sample {
    std::string name;      // from the name table ("0000" if absent)
    u32 frequency = 0;     // Hz
    u32 channels = 1;
    u32 sampleCount = 0;   // PCM frames per channel
    u32 setupCrc32 = 0;    // VORBISDATA chunk: CRC32 of the missing setup header
    std::vector<u8> data;  // u16-size-prefixed Vorbis packets, back to back
};

struct Fsb5Bank {
    u32 mode = 0;  // 15 = Vorbis (the only mode RoM uses)
    std::vector<Fsb5Sample> samples;
};

// Parse an FSB5 blob. Returns nullopt on bad magic/short data.
std::optional<Fsb5Bank> parseFsb5(const std::vector<u8>& bytes);

}  // namespace uaro
