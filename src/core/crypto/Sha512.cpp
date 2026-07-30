#include "core/crypto/Sha512.hpp"

#include <cstring>
#include <fstream>
#include <vector>

namespace uaro {
namespace {

constexpr u64 kK[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

inline u64 ror(u64 x, int n) { return (x >> n) | (x << (64 - n)); }
inline u64 ch(u64 x, u64 y, u64 z) { return (x & y) ^ (~x & z); }
inline u64 maj(u64 x, u64 y, u64 z) { return (x & y) ^ (x & z) ^ (y & z); }
inline u64 bigS0(u64 x) { return ror(x, 28) ^ ror(x, 34) ^ ror(x, 39); }
inline u64 bigS1(u64 x) { return ror(x, 14) ^ ror(x, 18) ^ ror(x, 41); }
inline u64 smlS0(u64 x) { return ror(x, 1) ^ ror(x, 8) ^ (x >> 7); }
inline u64 smlS1(u64 x) { return ror(x, 19) ^ ror(x, 61) ^ (x >> 6); }

}  // namespace

void Sha512::reset() {
    state_[0] = 0x6a09e667f3bcc908ULL;
    state_[1] = 0xbb67ae8584caa73bULL;
    state_[2] = 0x3c6ef372fe94f82bULL;
    state_[3] = 0xa54ff53a5f1d36f1ULL;
    state_[4] = 0x510e527fade682d1ULL;
    state_[5] = 0x9b05688c2b3e6c1fULL;
    state_[6] = 0x1f83d9abfb41bd6bULL;
    state_[7] = 0x5be0cd19137e2179ULL;
    bitlenLow_ = bitlenHigh_ = 0;
    bufLen_ = 0;
}

void Sha512::processBlock(const u8* p) {
    u64 w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<u64>(p[i * 8]) << 56) | (static_cast<u64>(p[i * 8 + 1]) << 48) |
               (static_cast<u64>(p[i * 8 + 2]) << 40) | (static_cast<u64>(p[i * 8 + 3]) << 32) |
               (static_cast<u64>(p[i * 8 + 4]) << 24) | (static_cast<u64>(p[i * 8 + 5]) << 16) |
               (static_cast<u64>(p[i * 8 + 6]) << 8) | static_cast<u64>(p[i * 8 + 7]);
    }
    for (int i = 16; i < 80; ++i)
        w[i] = smlS1(w[i - 2]) + w[i - 7] + smlS0(w[i - 15]) + w[i - 16];

    u64 a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    u64 e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 80; ++i) {
        const u64 t1 = h + bigS1(e) + ch(e, f, g) + kK[i] + w[i];
        const u64 t2 = bigS0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha512::update(const void* data, usize len) {
    const u8* p = static_cast<const u8*>(data);
    // Track the 128-bit message length in bits.
    const u64 addBits = static_cast<u64>(len) << 3;
    if ((bitlenLow_ += addBits) < addBits) ++bitlenHigh_;
    bitlenHigh_ += static_cast<u64>(len) >> 61;

    while (len > 0) {
        const usize take = std::min<usize>(128 - bufLen_, len);
        std::memcpy(buffer_ + bufLen_, p, take);
        bufLen_ += take;
        p += take;
        len -= take;
        if (bufLen_ == 128) {
            processBlock(buffer_);
            bufLen_ = 0;
        }
    }
}

std::array<u8, 64> Sha512::finalize() {
    // Pad: 0x80, zeros, then 128-bit big-endian bit length.
    const u64 lenHi = bitlenHigh_, lenLo = bitlenLow_;
    u8 pad = 0x80;
    update(&pad, 1);
    pad = 0x00;
    while (bufLen_ != 112) update(&pad, 1);  // until 16 bytes remain in the block
    // update() above bumped the bit counter for the pad bytes; restore the real length.
    u8 lenBytes[16];
    for (int i = 0; i < 8; ++i) lenBytes[i] = static_cast<u8>(lenHi >> (56 - i * 8));
    for (int i = 0; i < 8; ++i) lenBytes[8 + i] = static_cast<u8>(lenLo >> (56 - i * 8));
    // Write the length directly (don't route through update(), which would re-count bits).
    std::memcpy(buffer_ + bufLen_, lenBytes, 16);
    processBlock(buffer_);

    std::array<u8, 64> out{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) out[i * 8 + j] = static_cast<u8>(state_[i] >> (56 - j * 8));
    return out;
}

std::string Sha512::toHex(const std::array<u8, 64>& d) {
    static const char* hx = "0123456789abcdef";
    std::string s(128, '0');
    for (int i = 0; i < 64; ++i) {
        s[i * 2] = hx[d[i] >> 4];
        s[i * 2 + 1] = hx[d[i] & 0xf];
    }
    return s;
}

std::string Sha512::hashHex(const void* data, usize len) {
    Sha512 h;
    h.update(data, len);
    return toHex(h.finalize());
}

std::optional<std::string> Sha512::hashFileHex(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    Sha512 h;
    std::vector<char> buf(1 << 20);  // 1 MiB chunks
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = f.gcount();
        if (got > 0) h.update(buf.data(), static_cast<usize>(got));
    }
    if (f.bad()) return std::nullopt;
    return toHex(h.finalize());
}

}  // namespace uaro
