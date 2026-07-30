#include "formats/Gr2.hpp"

#include <vector>

#include "microtest.hpp"

using namespace uaro;

namespace {
void pu8(std::vector<u8>& v, u8 x) { v.push_back(x); }
void pu32(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x & 0xff));
    v.push_back(static_cast<u8>((x >> 8) & 0xff));
    v.push_back(static_cast<u8>((x >> 16) & 0xff));
    v.push_back(static_cast<u8>((x >> 24) & 0xff));
}
const u8 kMagic[16] = {0xB8, 0x67, 0xB0, 0xCA, 0xF8, 0x6D, 0xB1, 0x0F,
                       0x84, 0x72, 0x8C, 0x7E, 0x5E, 0x19, 0x00, 0x1E};

// Build a minimal valid v6 .gr2: 88-byte header, 2 section descriptors (44B each), then their
// data. Section 0 is raw (mode 0), section 1 is Oodle0 (mode 1). Layout mirrors a real RO file.
std::vector<u8> synthGr2() {
    constexpr u32 kHdr = 88, kSec = 44;
    const u32 secArr = kHdr;                  // 88
    const u32 dataAt = secArr + 2 * kSec;     // 176
    const u32 raw0At = dataAt;                // section 0 raw bytes (4)
    const u32 ood1At = dataAt + 4;            // section 1 compressed bytes (8)
    const u32 total = ood1At + 8;             // 188

    std::vector<u8> v;
    v.insert(v.end(), kMagic, kMagic + 16);   // 0: magic
    pu32(v, 352);                             // 16: headerSize
    pu32(v, 0);                               // 20: compression flag
    pu32(v, 0); pu32(v, 0);                   // 24,28: reserved
    pu32(v, 6);                               // 32: version
    pu32(v, total);                           // 36: fileSize
    pu32(v, 0);                               // 40: crc
    pu32(v, 56);                              // 44: sectionOffset (-> 32+56 = 88)
    pu32(v, 2);                               // 48: sectionCount
    pu32(v, 0);                               // 52: pad
    pu32(v, 0); pu32(v, 0);                   // 56: rootType ref (section, offset)
    pu32(v, 0); pu32(v, 0x7fffffff);          // 64: rootObj ref
    pu32(v, 0);                               // 72: typeTag
    pu32(v, 0); pu32(v, 0); pu32(v, 0); pu32(v, 0);  // 76: extra tags (-> 92? no: to 88)
    // We've written to offset 92 (16 header u32s after magic). Trim back to exactly 88.
    v.resize(secArr);

    auto section = [&](u32 comp, u32 off, u32 csize, u32 dsize, u32 s0, u32 s1) {
        pu32(v, comp); pu32(v, off); pu32(v, csize); pu32(v, dsize);
        pu32(v, 0);                 // alignment
        pu32(v, s0); pu32(v, s1);   // stop0/stop1
        pu32(v, off); pu32(v, 0);   // reloc offset/count
        pu32(v, off); pu32(v, 0);   // marshal offset/count
    };
    section(0, raw0At, 4, 4, 0, 0);     // section 0: raw
    section(1, ood1At, 8, 16, 16, 16);  // section 1: Oodle0
    v.resize(dataAt);
    pu8(v, 0xDE); pu8(v, 0xAD); pu8(v, 0xBE); pu8(v, 0xEF);  // section 0 raw data
    for (int i = 0; i < 8; ++i) pu8(v, static_cast<u8>(i));  // section 1 compressed data
    return v;
}
}  // namespace

TEST_CASE(gr2_parse_header_and_sections) {
    auto v = synthGr2();
    auto p = gr2ParseSections(v);
    CHECK(p.has_value());
    CHECK_EQ(p->header.version, 6u);
    CHECK_EQ(p->header.fileSize, static_cast<u32>(v.size()));
    CHECK_EQ(p->header.sectionCount, 2u);
    CHECK_EQ(p->sections.size(), 2u);
    // Section 0: raw -> copied verbatim.
    CHECK_EQ(p->sections[0].compression, 0u);
    CHECK_EQ(p->sections[0].compressedSize, 4u);
    CHECK_EQ(p->sections[0].data.size(), 4u);
    CHECK_EQ(p->sections[0].data[0], 0xDEu);
    CHECK_EQ(p->sections[0].data[3], 0xEFu);
    // Section 1: Oodle0 -> descriptor parsed, data empty until the decoder is ported.
    CHECK_EQ(p->sections[1].compression, 1u);
    CHECK_EQ(p->sections[1].decompressedSize, 16u);
    CHECK_EQ(p->sections[1].data.size(), 0u);
}

TEST_CASE(gr2_reject_bad_magic) {
    auto v = synthGr2();
    v[0] = 0x00;  // corrupt the signature
    CHECK(!gr2ParseSections(v).has_value());
}

TEST_CASE(gr2_reject_truncated) {
    auto v = synthGr2();
    v.resize(v.size() - 4);  // now fileSize field != actual size
    CHECK(!gr2ParseSections(v).has_value());
    std::vector<u8> tiny(40, 0);
    CHECK(!gr2ParseSections(tiny).has_value());
}
