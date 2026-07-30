#pragma once
#include <optional>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Minimal LZ4 BLOCK decompressor (the raw block format, no frame header) — enough for Unity
// AssetBundle blocks (UnityFS stores each data block and the BlocksInfo as one LZ4/LZ4HC
// block; HC only changes the encoder, the block format is identical). Returns nullopt on
// malformed input or if the output does not reach exactly `outLen`.
std::optional<std::vector<u8>> lz4BlockDecompress(const u8* src, usize srcLen, usize outLen);

}  // namespace uaro
