#include "resource/GrfDecrypt.hpp"

#include <cstring>
#include <string>

namespace uaro {
namespace {

constexpr u8 kMask[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

const u8 kInitialPermutation[64] = {
    58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9,  1, 59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7,
};

const u8 kFinalPermutation[64] = {
    40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9,  49, 17, 57, 25,
};

const u8 kTransposition[32] = {
    16, 7,  20, 21, 29, 12, 28, 17, 1,  15, 23, 26, 5,  18, 31, 10,
    2,  8,  24, 14, 32, 27, 3,  9,  19, 13, 30, 6,  22, 11, 4,  25,
};

const u8 kSbox[4][64] = {
    {0xef, 0x03, 0x41, 0xfd, 0xd8, 0x74, 0x1e, 0x47, 0x26, 0xef, 0xfb, 0x22, 0xb3, 0xd8, 0x84, 0x1e,
     0x39, 0xac, 0xa7, 0x60, 0x62, 0xc1, 0xcd, 0xba, 0x5c, 0x96, 0x90, 0x59, 0x05, 0x3b, 0x7a, 0x85,
     0x40, 0xfd, 0x1e, 0xc8, 0xe7, 0x8a, 0x8b, 0x21, 0xda, 0x43, 0x64, 0x9f, 0x2d, 0x14, 0xb1, 0x72,
     0xf5, 0x5b, 0xc8, 0xb6, 0x9c, 0x37, 0x76, 0xec, 0x39, 0xa0, 0xa3, 0x05, 0x52, 0x6e, 0x0f, 0xd9},
    {0xa7, 0xdd, 0x0d, 0x78, 0x9e, 0x0b, 0xe3, 0x95, 0x60, 0x36, 0x36, 0x4f, 0xf9, 0x60, 0x5a, 0xa3,
     0x11, 0x24, 0xd2, 0x87, 0xc8, 0x52, 0x75, 0xec, 0xbb, 0xc1, 0x4c, 0xba, 0x24, 0xfe, 0x8f, 0x19,
     0xda, 0x13, 0x66, 0xaf, 0x49, 0xd0, 0x90, 0x06, 0x8c, 0x6a, 0xfb, 0x91, 0x37, 0x8d, 0x0d, 0x78,
     0xbf, 0x49, 0x11, 0xf4, 0x23, 0xe5, 0xce, 0x3b, 0x55, 0xbc, 0xa2, 0x57, 0xe8, 0x22, 0x74, 0xce},
    {0x2c, 0xea, 0xc1, 0xbf, 0x4a, 0x24, 0x1f, 0xc2, 0x79, 0x47, 0xa2, 0x7c, 0xb6, 0xd9, 0x68, 0x15,
     0x80, 0x56, 0x5d, 0x01, 0x33, 0xfd, 0xf4, 0xae, 0xde, 0x30, 0x07, 0x9b, 0xe5, 0x83, 0x9b, 0x68,
     0x49, 0xb4, 0x2e, 0x83, 0x1f, 0xc2, 0xb5, 0x7c, 0xa2, 0x19, 0xd8, 0xe5, 0x7c, 0x2f, 0x83, 0xda,
     0xf7, 0x6b, 0x90, 0xfe, 0xc4, 0x01, 0x5a, 0x97, 0x61, 0xa6, 0x3d, 0x40, 0x0b, 0x58, 0xe6, 0x3d},
    {0x4d, 0xd1, 0xb2, 0x0f, 0x28, 0xbd, 0xe4, 0x78, 0xf6, 0x4a, 0x0f, 0x93, 0x8b, 0x17, 0xd1, 0xa4,
     0x3a, 0xec, 0xc9, 0x35, 0x93, 0x56, 0x7e, 0xcb, 0x55, 0x20, 0xa0, 0xfe, 0x6c, 0x89, 0x17, 0x62,
     0x17, 0x62, 0x4b, 0xb1, 0xb4, 0xde, 0xd1, 0x87, 0xc9, 0x14, 0x3c, 0x4a, 0x7e, 0xa8, 0xe2, 0x7d,
     0xa0, 0x9f, 0xf6, 0x5c, 0x6a, 0x09, 0x8d, 0xf0, 0x0f, 0xe3, 0x53, 0x25, 0x95, 0x36, 0x28, 0xcb},
};

void permute(u8* b, const u8* table) {
    u8 t[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 64; ++i) {
        const int j = table[i] - 1;
        if (b[(j >> 3) & 7] & kMask[j & 7]) t[(i >> 3) & 7] |= kMask[i & 7];
    }
    std::memcpy(b, t, 8);
}

void expansion(u8* b) {
    u8 t[8];
    t[0] = static_cast<u8>(((b[7] << 5) | (b[4] >> 3)) & 0x3f);
    t[1] = static_cast<u8>(((b[4] << 1) | (b[5] >> 7)) & 0x3f);
    t[2] = static_cast<u8>(((b[4] << 5) | (b[5] >> 3)) & 0x3f);
    t[3] = static_cast<u8>(((b[5] << 1) | (b[6] >> 7)) & 0x3f);
    t[4] = static_cast<u8>(((b[5] << 5) | (b[6] >> 3)) & 0x3f);
    t[5] = static_cast<u8>(((b[6] << 1) | (b[7] >> 7)) & 0x3f);
    t[6] = static_cast<u8>(((b[6] << 5) | (b[7] >> 3)) & 0x3f);
    t[7] = static_cast<u8>(((b[7] << 1) | (b[4] >> 7)) & 0x3f);
    std::memcpy(b, t, 8);
}

void substitution(u8* b) {
    u8 t[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 4; ++i)
        t[i] = static_cast<u8>((kSbox[i][b[i * 2 + 0]] & 0xf0) | (kSbox[i][b[i * 2 + 1]] & 0x0f));
    std::memcpy(b, t, 8);
}

void transpose(u8* b) {
    u8 t[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 32; ++i) {
        const int j = kTransposition[i] - 1;
        if (b[j >> 3] & kMask[j & 7]) t[(i >> 3) + 4] |= kMask[i & 7];
    }
    std::memcpy(b, t, 8);
}

void round_function(u8* b) {
    u8 r[8];
    std::memcpy(r, b, 8);
    expansion(r);
    substitution(r);
    transpose(r);
    b[0] ^= r[4];
    b[1] ^= r[5];
    b[2] ^= r[6];
    b[3] ^= r[7];
}

void decrypt_block(u8* b) {
    permute(b, kInitialPermutation);
    round_function(b);
    permute(b, kFinalPermutation);
}

const u8* shuffle_dec_table() {
    static u8 tbl[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; ++i) tbl[i] = static_cast<u8>(i);
        static const u8 list[] = {0x00, 0x2b, 0x6c, 0x80, 0x01, 0x68, 0x48,
                                  0x77, 0x60, 0xff, 0xb9, 0xc0, 0xfe, 0xeb};
        for (usize i = 0; i + 1 < sizeof(list); i += 2) {
            tbl[list[i + 0]] = list[i + 1];
            tbl[list[i + 1]] = list[i + 0];
        }
        init = true;
    }
    return tbl;
}

void shuffle_dec(u8* b) {
    const u8* tbl = shuffle_dec_table();
    u8 t[8];
    t[0] = b[3];
    t[1] = b[4];
    t[2] = b[6];
    t[3] = b[0];
    t[4] = b[1];
    t[5] = b[2];
    t[6] = b[5];
    t[7] = tbl[b[7]];
    std::memcpy(b, t, 8);
}

}  // namespace

void grf_decrypt_full(u8* src, u32 aligned, u32 compressed) {
    const u32 count = aligned >> 3;
    const int digits = static_cast<int>(std::to_string(compressed).size());
    int cycle;
    if (digits < 3) cycle = 1;
    else if (digits < 5) cycle = digits + 1;
    else if (digits < 7) cycle = digits + 9;
    else cycle = digits + 15;

    for (u32 i = 0; i < 20 && i < count; ++i) decrypt_block(src + i * 8);

    int j = -1;
    for (u32 i = 20; i < count; ++i) {
        if (i % static_cast<u32>(cycle) == 0) {
            decrypt_block(src + i * 8);
            continue;
        }
        ++j;
        if (j != 0 && j % 7 == 0) shuffle_dec(src + i * 8);
    }
}

void grf_decrypt_header(u8* src, u32 aligned) {
    const u32 count = aligned >> 3;
    for (u32 i = 0; i < 20 && i < count; ++i) decrypt_block(src + i * 8);
}

// ---- Legacy GRF v0x1xx (#106) — eAthena grfio.c decode_filename / decode_des_etc -------------

namespace {

void nibble_swap(u8* b, usize len) {
    for (usize i = 0; i < len; ++i) b[i] = static_cast<u8>((b[i] >> 4) | (b[i] << 4));
}

// The single-round DES above is an involution (IP and FP are inverse permutations and the round
// function XORs the left half with a value derived only from the untouched right half), so
// encrypting a filename block is the same decrypt_block. Only the byte-shuffle needs an inverse.
void shuffle_enc(u8* b) {
    // Inverse of shuffle_dec: dec output takes t[0..7] from src[3,4,6,0,1,2,5,map(7)].
    u8 t[8];
    t[3] = b[0];
    t[4] = b[1];
    t[6] = b[2];
    t[0] = b[3];
    t[1] = b[4];
    t[2] = b[5];
    t[5] = b[6];
    t[7] = shuffle_dec_table()[b[7]];  // the value map is an involution
    std::memcpy(b, t, 8);
}

// eAthena decode_des_etc's cycle mapping (deliberately different from grf_decrypt_full's).
u32 v1_cycle(int cycleDigits) {
    int c = cycleDigits;
    if (c < 3) c = 3;
    else if (c < 5) ++c;
    else if (c < 7) c += 9;
    else c += 15;
    return static_cast<u32>(c);
}

}  // namespace

void grf_v1_filename_decode(u8* buf, usize len) {
    for (usize i = 0; i + 8 <= len; i += 8) {
        nibble_swap(buf + i, 8);
        decrypt_block(buf + i);
    }
}

void grf_v1_filename_encode(u8* buf, usize len) {
    for (usize i = 0; i + 8 <= len; i += 8) {
        decrypt_block(buf + i);  // involution: same op encrypts
        nibble_swap(buf + i, 8);
    }
}

void grf_v1_data_decode(u8* src, u32 aligned, int cycleDigits) {
    const u32 count = aligned >> 3;
    const bool mixed = cycleDigits > 0;  // 0 = header-only (.gnd/.gat/.act/.str)
    const u32 cycle = v1_cycle(cycleDigits);
    u32 cnt = 0;
    for (u32 i = 0; i < count; ++i) {
        u8* b = src + static_cast<usize>(i) * 8;
        if (i < 20 || (mixed && i % cycle == 0)) {
            decrypt_block(b);
        } else if (mixed) {
            if (cnt == 7) {
                cnt = 0;
                shuffle_dec(b);
            }
            ++cnt;
        }
    }
}

void grf_v1_data_encode(u8* src, u32 aligned, int cycleDigits) {
    const u32 count = aligned >> 3;
    const bool mixed = cycleDigits > 0;
    const u32 cycle = v1_cycle(cycleDigits);
    u32 cnt = 0;
    for (u32 i = 0; i < count; ++i) {
        u8* b = src + static_cast<usize>(i) * 8;
        if (i < 20 || (mixed && i % cycle == 0)) {
            decrypt_block(b);  // involution
        } else if (mixed) {
            if (cnt == 7) {
                cnt = 0;
                shuffle_enc(b);
            }
            ++cnt;
        }
    }
}

}  // namespace uaro
