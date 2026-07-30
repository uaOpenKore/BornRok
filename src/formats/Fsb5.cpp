#include "formats/Fsb5.hpp"

#include <cstring>

namespace uaro {

namespace {

// Sample-header frequency codes (FSB5 packs common rates into 4 bits; anything else ships a
// FREQUENCY metadata chunk).
u32 freqFromCode(u32 code) {
    switch (code) {
        case 1: return 8000;
        case 2: return 11000;
        case 3: return 11025;
        case 4: return 16000;
        case 5: return 22050;
        case 6: return 24000;
        case 7: return 32000;
        case 8: return 44100;
        case 9: return 48000;
        case 10: return 96000;
        default: return 0;
    }
}

u64 bits(u64 v, int start, int len) {
    return (v >> start) & ((u64{1} << len) - 1);
}

}  // namespace

std::optional<Fsb5Bank> parseFsb5(const std::vector<u8>& bytes) {
    if (bytes.size() < 60 || std::memcmp(bytes.data(), "FSB5", 4) != 0) return std::nullopt;
    const u8* p = bytes.data();
    u32 version, numSamples, shdrSize, nameSize, dataSize, mode;
    std::memcpy(&version, p + 4, 4);
    std::memcpy(&numSamples, p + 8, 4);
    std::memcpy(&shdrSize, p + 12, 4);
    std::memcpy(&nameSize, p + 16, 4);
    std::memcpy(&dataSize, p + 20, 4);
    std::memcpy(&mode, p + 24, 4);
    // 28: 8 unknown + 16 GUID + 8 unknown; v0 has 4 extra bytes before the sample headers.
    usize at = version == 0 ? 64 : 60;

    Fsb5Bank bank;
    bank.mode = mode;
    const usize headerSize = at;

    struct Raw {
        Fsb5Sample s;
        u64 dataOffset = 0;
    };
    std::vector<Raw> raws;
    for (u32 i = 0; i < numSamples; ++i) {
        if (at + 8 > bytes.size()) return std::nullopt;
        u64 raw;
        std::memcpy(&raw, p + at, 8);
        at += 8;
        Raw r;
        bool next = bits(raw, 0, 1) != 0;
        u32 freqCode = static_cast<u32>(bits(raw, 1, 4));
        r.s.channels = static_cast<u32>(bits(raw, 5, 1)) + 1;
        r.dataOffset = bits(raw, 6, 28) * 16;
        r.s.sampleCount = static_cast<u32>(bits(raw, 34, 30));
        r.s.frequency = freqFromCode(freqCode);
        r.s.name = "0000";
        while (next) {
            if (at + 4 > bytes.size()) return std::nullopt;
            u32 ch;
            std::memcpy(&ch, p + at, 4);
            at += 4;
            next = (ch & 1) != 0;
            const u32 chunkSize = (ch >> 1) & 0xFFFFFF;
            const u32 chunkType = (ch >> 25) & 0x7F;
            if (at + chunkSize > bytes.size()) return std::nullopt;
            if (chunkType == 11 && chunkSize >= 4) {  // VORBISDATA: crc32 + unknown
                std::memcpy(&r.s.setupCrc32, p + at, 4);
            } else if (chunkType == 2 && chunkSize >= 4) {  // FREQUENCY override (u32 Hz)
                u32 f;
                std::memcpy(&f, p + at, 4);
                r.s.frequency = f;
            }
            at += chunkSize;
        }
        raws.push_back(std::move(r));
    }

    // Optional name table (offsets, then NUL-terminated names).
    const usize nameStart = headerSize + shdrSize;
    if (nameSize && nameStart + nameSize <= bytes.size()) {
        for (u32 i = 0; i < numSamples && i < raws.size(); ++i) {
            if (nameStart + static_cast<usize>(i) * 4 + 4 > bytes.size()) break;
            u32 off;
            std::memcpy(&off, p + nameStart + static_cast<usize>(i) * 4, 4);
            usize s = nameStart + off;
            std::string nm;
            while (s < bytes.size() && s < nameStart + nameSize && p[s]) nm.push_back(static_cast<char>(p[s++]));
            if (!nm.empty()) raws[i].s.name = std::move(nm);
        }
    }

    const usize dataStart = headerSize + shdrSize + nameSize;
    for (usize i = 0; i < raws.size(); ++i) {
        const u64 begin = dataStart + raws[i].dataOffset;
        const u64 end = i + 1 < raws.size() ? dataStart + raws[i + 1].dataOffset
                                            : std::min<u64>(bytes.size(), dataStart + dataSize);
        if (begin > end || end > bytes.size()) return std::nullopt;
        raws[i].s.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                              bytes.begin() + static_cast<std::ptrdiff_t>(end));
        bank.samples.push_back(std::move(raws[i].s));
    }
    return bank;
}

}  // namespace uaro
