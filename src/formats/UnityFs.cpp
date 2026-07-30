#include "formats/UnityFs.hpp"

#include <cstring>

#include "core/Log.hpp"
#include "core/compress/Lz4.hpp"

#if defined(CLIENT_WITH_ZSTD)
#include <zstd.h>
#endif

namespace uaro {

namespace {

// Big-endian reader over a byte span (UnityFS headers are big-endian).
struct BeReader {
    const u8* p;
    usize n;
    usize at = 0;

    bool ok(usize need) const { return at + need <= n; }
    u8 u8v() { return p[at++]; }
    u16 u16be() {
        const u16 v = static_cast<u16>((p[at] << 8) | p[at + 1]);
        at += 2;
        return v;
    }
    u32 u32be() {
        const u32 v = (static_cast<u32>(p[at]) << 24) | (static_cast<u32>(p[at + 1]) << 16) |
                      (static_cast<u32>(p[at + 2]) << 8) | p[at + 3];
        at += 4;
        return v;
    }
    u64 u64be() {
        u64 v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | p[at + i];
        at += 8;
        return v;
    }
    std::string cstr() {
        std::string s;
        while (at < n && p[at] != 0) s.push_back(static_cast<char>(p[at++]));
        if (at < n) ++at;  // NUL
        return s;
    }
};

constexpr u32 kCompressionMask = 0x3f;  // low bits of the flags: 0 none, 1 LZMA, 2 LZ4, 3 LZ4HC
constexpr u32 kBlocksInfoAtEnd = 0x80;
constexpr u32 kBlockInfoNeedPad = 0x200;  // v2020.3.34+/2021.3.2+: data starts 16-aligned

std::optional<std::vector<u8>> decompress(const u8* src, usize srcLen, usize outLen, u32 method) {
    switch (method) {
        case 0:  // none
            if (srcLen != outLen) return std::nullopt;
            return std::vector<u8>(src, src + srcLen);
        case 2:
        case 3:  // LZ4 / LZ4HC (same block format)
            return lz4BlockDecompress(src, srcLen, outLen);
        case 5: {  // Zstandard — non-stock extension used by the RoM (Unity-China) bundles:
                   // BlocksInfo is LZ4HC but every DATA block is a zstd frame (28 B5 2F FD),
                   // verified against the real basilisk.unity3d.
#if defined(CLIENT_WITH_ZSTD)
            std::vector<u8> out(outLen);
            const usize got = ZSTD_decompress(out.data(), outLen, src, srcLen);
            if (ZSTD_isError(got) || got != outLen) return std::nullopt;
            return out;
#else
            log::warn("UnityFS: zstd block but built without CLIENT_WITH_ZSTD");
            return std::nullopt;
#endif
        }
        default:  // LZMA unsupported (not used by the RoM pack)
            return std::nullopt;
    }
}

}  // namespace

bool UnityFsBundle::parse(const std::vector<u8>& bytes) {
    nodes_.clear();
    data_.clear();
    BeReader r{bytes.data(), bytes.size()};
    if (!r.ok(8)) return false;
    const std::string sig = r.cstr();
    if (sig != "UnityFS") {
        log::warn("UnityFS: bad signature '{}'", sig);
        return false;
    }
    if (!r.ok(4)) return false;
    const u32 version = r.u32be();  // 6/7/8
    r.cstr();                       // unityWebBundleVersion ("5.x.x")
    unityVersion_ = r.cstr();       // e.g. "2021.3.21f1"
    if (!r.ok(20)) return false;
    r.u64be();  // total bundle size
    const u32 compBlocksInfoSize = r.u32be();
    const u32 uncompBlocksInfoSize = r.u32be();
    const u32 flags = r.u32be();
    if (version >= 7) r.at = (r.at + 15) & ~usize{15};  // header pads to 16 in v7+

    // BlocksInfo: usually right after the header; kBlocksInfoAtEnd moves it to the file tail.
    usize infoAt = r.at;
    if (flags & kBlocksInfoAtEnd) {
        if (bytes.size() < compBlocksInfoSize) return false;
        infoAt = bytes.size() - compBlocksInfoSize;
    } else {
        r.at += compBlocksInfoSize;
    }
    if (infoAt + compBlocksInfoSize > bytes.size()) return false;
    auto info = decompress(bytes.data() + infoAt, compBlocksInfoSize, uncompBlocksInfoSize,
                           flags & kCompressionMask);
    if (!info) {
        log::warn("UnityFS: BlocksInfo decompress failed (method {})", flags & kCompressionMask);
        return false;
    }

    BeReader ir{info->data(), info->size()};
    if (!ir.ok(16 + 4)) return false;
    ir.at += 16;  // uncompressed data hash
    const u32 blockCount = ir.u32be();
    struct Blk {
        u32 uncomp, comp;
        u16 flags;
    };
    std::vector<Blk> blocks(blockCount);
    for (Blk& b : blocks) {
        if (!ir.ok(10)) return false;
        b.uncomp = ir.u32be();
        b.comp = ir.u32be();
        b.flags = ir.u16be();
    }
    if (!ir.ok(4)) return false;
    const u32 nodeCount = ir.u32be();
    nodes_.reserve(nodeCount);
    for (u32 i = 0; i < nodeCount; ++i) {
        if (!ir.ok(20)) return false;
        UnityFsNode nd;
        nd.offset = ir.u64be();
        nd.size = ir.u64be();
        nd.flags = ir.u32be();
        nd.path = ir.cstr();
        nodes_.push_back(std::move(nd));
    }

    // Data blocks follow (16-aligned when the pad flag is set).
    usize dataAt = r.at;
    if (flags & kBlockInfoNeedPad) dataAt = (dataAt + 15) & ~usize{15};
    u64 total = 0;
    for (const Blk& b : blocks) total += b.uncomp;
    data_.reserve(static_cast<usize>(total));
    for (const Blk& b : blocks) {
        if (dataAt + b.comp > bytes.size()) return false;
        auto out = decompress(bytes.data() + dataAt, b.comp, b.uncomp, b.flags & kCompressionMask);
        if (!out) {
            log::warn("UnityFS: block decompress failed (method {})", b.flags & kCompressionMask);
            return false;
        }
        data_.insert(data_.end(), out->begin(), out->end());
        dataAt += b.comp;
    }
    return true;
}

std::optional<std::vector<u8>> UnityFsBundle::nodeData(usize index) const {
    if (index >= nodes_.size()) return std::nullopt;
    const UnityFsNode& nd = nodes_[index];
    if (nd.offset + nd.size > data_.size()) return std::nullopt;
    return std::vector<u8>(data_.begin() + static_cast<std::ptrdiff_t>(nd.offset),
                           data_.begin() + static_cast<std::ptrdiff_t>(nd.offset + nd.size));
}

}  // namespace uaro
