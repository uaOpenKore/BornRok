#pragma once
// Granny "Oodle0/Oodle1" section decompressor (the codec RO's .gr2 files use for their heavy
// sections: vertices, bone bindings, animation tracks). Ported VERBATIM from nwn2mdk's
// gr2_decompress.cpp, which derives from berenm/xoreos-tools -- distributed under the Boost
// Software License 1.0 (see the .cpp header). RO files are little-endian on a little-endian host,
// so Oodle0 and Oodle1 take the identical path (no byte-reversal); this one function covers both.
//
// Contract: `compressed_buffer` must be writable with FOUR spare readable bytes past
// `compressed_size` (the routine zero-pads them); `decompressed_buffer` must be exactly
// `decompressed_size` bytes. step1/step2 are the section's two Oodle stops.
#include <cstdint>

void gr2_decompress(uint32_t compressed_size, uint8_t* compressed_buffer,
                    uint32_t step1, uint32_t step2,
                    uint32_t decompressed_size, uint8_t* decompressed_buffer);
