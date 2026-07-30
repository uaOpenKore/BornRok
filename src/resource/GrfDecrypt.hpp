#pragma once
// GRF entry decryption (the RO "DES"-like cipher). Entries in a 0x200 GRF may be flagged
// ENCRYPT_MIXED (0x02) or ENCRYPT_HEADER (0x04) by GRF editors; the bytes must be run through
// this before zlib inflate. Ported from the well-known scheme (verified against midgarts/roBrowser).
#include "core/Types.hpp"

namespace uaro {

// ENCRYPT_MIXED (flag 0x02): first 20 blocks DES-decrypted, then a cycle of DES + de-shuffle.
// aligned = the on-disk (8-byte aligned) length; compressed = the entry's compressed size (drives
// the cycle length). Operates in place on `src` (at least `aligned` bytes).
void grf_decrypt_full(u8* src, u32 aligned, u32 compressed);

// ENCRYPT_HEADER (flag 0x04): only the first 20 blocks are DES-decrypted.
void grf_decrypt_header(u8* src, u32 aligned);

// ---- Legacy GRF v0x1xx (#106) — ported from eAthena grfio.c ---------------------------------
// v0x1xx filenames: nibble-swapped + one DES round per 8-byte block. Decode in place (len must
// be a multiple of 8). Encode is the exact inverse (used to craft synthetic test archives).
void grf_v1_filename_decode(u8* buf, usize len);
void grf_v1_filename_encode(u8* buf, usize len);

// v0x1xx data: the first 20 blocks are DES'd; past those, cycleDigits == 0 (a .gnd/.gat/.act/
// .str) leaves the rest plain, while cycleDigits > 0 (= decimal digit count of the compressed
// size) DES's every cycle-th block and byte-shuffles every 8th of the remaining blocks. The
// cycle mapping (<3 -> 3, <5 -> +1, <7 -> +9, else +15) matches eAthena decode_des_etc — it
// deliberately differs from the GRF-editor 0x200 flag-0x02 scheme above. Encode = inverse.
void grf_v1_data_decode(u8* src, u32 aligned, int cycleDigits);
void grf_v1_data_encode(u8* src, u32 aligned, int cycleDigits);

}  // namespace uaro
