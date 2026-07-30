#include "core/compress/Lz4.hpp"

#include <cstring>

namespace uaro {

// LZ4 block format: a sequence of tokens. Token high nibble = literal length (15 = more
// length bytes follow, 255-terminated), then the literals; token low nibble = match length
// - 4 (15 = more bytes), preceded by a 2-byte little-endian match offset back into the
// output. The last sequence is literals-only. Reference: lz4_Block_format.md (lz4/lz4).
std::optional<std::vector<u8>> lz4BlockDecompress(const u8* src, usize srcLen, usize outLen) {
    if (!src || outLen == 0) return std::nullopt;
    std::vector<u8> out(outLen);
    usize ip = 0, op = 0;

    auto readLen = [&](usize base) -> std::optional<usize> {
        usize len = base;
        if (base == 15) {
            u8 b;
            do {
                if (ip >= srcLen) return std::nullopt;
                b = src[ip++];
                len += b;
            } while (b == 255);
        }
        return len;
    };

    while (ip < srcLen) {
        const u8 token = src[ip++];
        // Literals.
        auto lit = readLen(token >> 4);
        if (!lit) return std::nullopt;
        if (*lit > 0) {
            if (ip + *lit > srcLen || op + *lit > outLen) return std::nullopt;
            std::memcpy(out.data() + op, src + ip, *lit);
            ip += *lit;
            op += *lit;
        }
        if (ip >= srcLen) break;  // last sequence has no match part
        // Match.
        if (ip + 2 > srcLen) return std::nullopt;
        const usize offset = static_cast<usize>(src[ip]) | (static_cast<usize>(src[ip + 1]) << 8);
        ip += 2;
        if (offset == 0 || offset > op) return std::nullopt;
        auto ml = readLen(token & 0x0f);
        if (!ml) return std::nullopt;
        usize matchLen = *ml + 4;
        if (op + matchLen > outLen) return std::nullopt;
        // Overlapping copy must run byte-by-byte (RLE-style matches reference just-written bytes).
        const u8* from = out.data() + (op - offset);
        u8* to = out.data() + op;
        for (usize i = 0; i < matchLen; ++i) to[i] = from[i];
        op += matchLen;
    }
    if (op != outLen) return std::nullopt;
    return out;
}

}  // namespace uaro
