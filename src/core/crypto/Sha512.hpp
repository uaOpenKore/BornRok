#pragma once
// SHA-512 (FIPS 180-4). Streaming so multi-GB files (data.grf) hash without
// loading into memory. Used by the patcher to verify downloads against the
// SHA-512 hex in downloads.list. Self-contained, no dependencies.
#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include "core/Types.hpp"

namespace uaro {

class Sha512 {
public:
    Sha512() { reset(); }

    void reset();
    void update(const void* data, usize len);
    // Finalises and returns the 64-byte digest. The object must be reset() to reuse.
    std::array<u8, 64> finalize();

    // Convenience: hash a whole buffer to a lowercase hex string (128 chars).
    static std::string hashHex(const void* data, usize len);
    // Stream a file from disk and return its lowercase hex digest, or nullopt if
    // the file can't be opened. Reads in chunks (safe for very large files).
    static std::optional<std::string> hashFileHex(const std::string& path);

    static std::string toHex(const std::array<u8, 64>& digest);

private:
    void processBlock(const u8* block);

    u64 state_[8];
    u64 bitlenLow_, bitlenHigh_;  // 128-bit message length in bits
    u8 buffer_[128];
    usize bufLen_;
};

}  // namespace uaro
