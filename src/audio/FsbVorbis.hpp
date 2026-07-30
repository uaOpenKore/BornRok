#pragma once
#include <optional>
#include <vector>

#include "core/Types.hpp"
#include "formats/Fsb5.hpp"

namespace uaro {

// Decoded FSB5-Vorbis sample (RoM SFX/BGM, #127). RoM banks strip the Vorbis SETUP header
// and store only its CRC32 — FMOD encodes with a fixed set of encoder configurations, so the
// header is restored from the known-CRC table (FsbVorbisTable.cpp, from python-fsb5's dump of
// FMOD's tables) and the u16-size-prefixed raw packets are decoded straight through libvorbis
// packet synthesis — no Ogg container round-trip.
struct FsbPcm {
    std::vector<float> f32;  // interleaved
    u32 channels = 0;
    u32 frequency = 0;
};

// nullopt: unknown setup CRC32, header rejected, or vorbis support compiled out.
std::optional<FsbPcm> decodeFsbVorbis(const Fsb5Sample& sample);

// FsbVorbisTable.cpp: the raw setup-header packet for an FMOD encoder CRC32 (nullptr = unknown).
const u8* fsbSetupHeader(u32 crc32, usize& lenOut);

}  // namespace uaro
