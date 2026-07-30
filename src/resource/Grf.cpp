#include "resource/Grf.hpp"

#include <zlib.h>

#include <cctype>
#include <cstring>

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"
#include "resource/GrfDecrypt.hpp"

namespace uaro {

namespace {

constexpr int kHeaderSize = 46;
constexpr char kSignature[] = "Master of Magic";  // first 15 bytes of the header

std::optional<std::vector<u8>> zinflate(const u8* src, uLong srcLen, u32 outLen) {
    std::vector<u8> out(outLen);
    if (outLen == 0) return out;
    uLongf destLen = outLen;
    int rc = uncompress(out.data(), &destLen, src, srcLen);
    if (rc != Z_OK || destLen != outLen) {
        log::error("GRF zlib inflate failed (rc={}, got={}, want={})", rc,
                   static_cast<u64>(destLen), outLen);
        return std::nullopt;
    }
    return out;
}

} // namespace

// Inflate a zlib stream of unknown output size (e.g. a guild emblem) into <= maxOut bytes.
// uncompress() needs the destination sized; we pass a generous ceiling and trim to the
// actual length. A too-small ceiling yields Z_BUF_ERROR -> nullopt (no overflow).
std::optional<std::vector<u8>> zlibInflate(const u8* src, usize srcLen, usize maxOut) {
    if (src == nullptr || srcLen == 0 || maxOut == 0) return std::nullopt;
    std::vector<u8> out(maxOut);
    uLongf destLen = static_cast<uLongf>(maxOut);
    const int rc = uncompress(out.data(), &destLen, src, static_cast<uLong>(srcLen));
    if (rc != Z_OK) return std::nullopt;
    out.resize(destLen);
    return out;
}

std::string GrfArchive::normalize(std::string p) {
    for (char& c : p) {
        if (c == '\\')
            c = '/';
        else
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return p;
}

bool GrfArchive::open(const std::string& path) {
    close();
    stream_.open(path, std::ios::binary);
    if (!stream_) {
        log::error("GRF: cannot open {}", path);
        return false;
    }
    path_ = path;

    u8 header[kHeaderSize];
    stream_.read(reinterpret_cast<char*>(header), kHeaderSize);
    if (stream_.gcount() != kHeaderSize) {
        log::error("GRF: short header in {}", path);
        close();
        return false;
    }
    // Classic GRF signature is "Master of Magic" (15 bytes). GRF Editor writes its own banks
    // with an "Event Horizon" signature (13 bytes) instead — otherwise identical (S.'s GRO.grf
    // is a GRF-Editor v0x300 large archive). Accept both.
    if (std::memcmp(header, kSignature, 15) != 0 &&
        std::memcmp(header, "Event Horizon", 13) != 0) {
        log::error("GRF: bad signature in {}", path);
        close();
        return false;
    }

    ByteReader r(header, kHeaderSize);
    r.skip(16);  // signature
    r.skip(14);  // key
    const u32 f30 = r.u32le();  // bytes 30-33: table offset (low u32) / seed area
    const u32 f34 = r.u32le();  // bytes 34-37: seed (v200) / high u32 of int64 offset (v300)
    const u32 f38 = r.u32le();  // bytes 38-41: obfuscated count (v200) / real count (v300)
    version_ = r.u32le();

    // v0x200: u32 table offset + seed-obfuscated file count. v0x300 (GRF Editor's int64
    // large-GRF extension): the table offset is a 64-bit value spanning the old offset+seed
    // slots (bytes 30-37) and byte 38 holds the raw file count.
    const u32 major = version_ >> 8;
    u64 tableOffset;
    u32 fileCount;
    bool v300 = false;
    if (major == 2) {  // 0x200
        tableOffset = f30;
        fileCount = (f38 >= f34 + 7) ? (f38 - f34 - 7) : 0;
    } else if (major == 3) {  // 0x300
        v300 = true;
        tableOffset = static_cast<u64>(f30) | (static_cast<u64>(f34) << 32);
        fileCount = f38;
    } else if (major == 1) {  // 0x1xx legacy (#106): same count obfuscation, raw table to EOF
        tableOffset = f30;
        fileCount = (f38 >= f34 + 7) ? (f38 - f34 - 7) : 0;
        if (!parseTableV1(tableOffset, fileCount)) {
            close();
            return false;
        }
        log::info("GRF: {} ({} files, legacy v0x{:x})", path, entries_.size(), version_);
        return true;
    } else {
        log::warn("GRF: version 0x{:x} not supported (only 0x1xx/0x200/0x300) in {}", version_,
                  path);
        close();
        return false;
    }

    if (!parseTable(tableOffset, fileCount, v300)) {
        close();
        return false;
    }
    log::info("GRF: {} ({} files, v0x{:x})", path, entries_.size(), version_);
    return true;
}

void GrfArchive::close() {
    if (stream_.is_open()) stream_.close();
    stream_.clear();
    entries_.clear();
    version_ = 0;
    path_.clear();
}

bool GrfArchive::parseTable(u64 tableOffset, u32 fileCount, bool v300) {
    stream_.seekg(static_cast<std::streamoff>(kHeaderSize + tableOffset), std::ios::beg);

    // v0x300 prefixes the two size fields with 4 reserved (zero) bytes.
    if (v300) {
        u8 pad[4];
        stream_.read(reinterpret_cast<char*>(pad), 4);
        if (stream_.gcount() != 4) {
            log::error("GRF: cannot read v300 table pad");
            return false;
        }
    }

    u8 sizes[8];
    stream_.read(reinterpret_cast<char*>(sizes), 8);
    if (stream_.gcount() != 8) {
        log::error("GRF: cannot read table sizes");
        return false;
    }
    ByteReader sr(sizes, 8);
    const u32 compressedLen = sr.u32le();
    const u32 uncompressedLen = sr.u32le();

    std::vector<u8> comp(compressedLen);
    stream_.read(reinterpret_cast<char*>(comp.data()), compressedLen);
    if (static_cast<u32>(stream_.gcount()) != compressedLen) {
        log::error("GRF: short file table");
        return false;
    }

    auto table = zinflate(comp.data(), compressedLen, uncompressedLen);
    if (!table) return false;

    ByteReader r(*table);
    u32 parsed = 0;
    try {
        while (!r.eof() && (fileCount == 0 || parsed < fileCount)) {
            std::string name = r.read_cstr();
            if (name.empty()) break;

            GrfEntry e;
            e.compressed = r.u32le();
            e.compressedAligned = r.u32le();
            e.uncompressed = r.u32le();
            e.flags = r.u8v();
            if (v300) {  // int64 data offset (low u32 then high u32)
                const u64 lo = r.u32le();
                const u64 hi = r.u32le();
                e.offset = lo | (hi << 32);
            } else {
                e.offset = r.u32le();
            }
            e.name = normalize(name);

            ++parsed;
            if (e.isFile()) entries_[e.name] = std::move(e);
        }
    } catch (const std::out_of_range&) {
        log::warn("GRF: truncated file table after {} entries", parsed);
    }
    return true;
}

// Legacy v0x1xx table (#106), ported from eAthena grfio.c grfio_entryread: raw (uncompressed)
// bytes from the table offset to EOF. Per entry: u32 nameBlockLen, 2 pad bytes, the DES/nibble-
// obfuscated filename (nameBlockLen-6 bytes incl. NUL), then 17 bytes of masked fields:
//   [0]  u32 = compressed + uncompressed + 715
//   [4]  u32 = alignedLen + 37579
//   [8]  u32 = uncompressed
//   [12] u8  = type (0 = directory)
//   [13] u32 = data offset (relative to the 46-byte header)
// Data encryption cycle: 0 (header-only DES) for .gnd/.gat/.act/.str, else the decimal digit
// count of the compressed size (mixed DES + shuffle).
bool GrfArchive::parseTableV1(u64 tableOffset, u32 fileCount) {
    stream_.seekg(0, std::ios::end);
    const u64 fileEnd = static_cast<u64>(stream_.tellg());
    const u64 tableAt = kHeaderSize + tableOffset;
    if (tableAt >= fileEnd) {
        log::error("GRF: v1 table offset beyond EOF");
        return false;
    }
    std::vector<u8> table(static_cast<usize>(fileEnd - tableAt));
    stream_.seekg(static_cast<std::streamoff>(tableAt), std::ios::beg);
    stream_.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size()));
    if (static_cast<u64>(stream_.gcount()) != table.size()) {
        log::error("GRF: short v1 file table");
        return false;
    }

    ByteReader r(table);
    u32 parsed = 0;
    try {
        for (u32 entry = 0; entry < fileCount && !r.eof(); ++entry) {
            const u32 nameBlockLen = r.u32le();
            if (nameBlockLen < 6 || nameBlockLen > 4096) break;  // corrupt/end
            r.skip(2);
            std::vector<u8> nameBuf(nameBlockLen - 6);
            for (u8& b : nameBuf) b = r.u8v();
            grf_v1_filename_decode(nameBuf.data(), nameBuf.size());
            std::string name(reinterpret_cast<char*>(nameBuf.data()),
                             strnlen(reinterpret_cast<char*>(nameBuf.data()), nameBuf.size()));
            r.skip(4);  // eAthena: the field block starts at ofs + nameBlockLen + 4
            const u32 packedLen = r.u32le();   // compressed + uncompressed + 715
            const u32 alignedRaw = r.u32le();  // aligned + 37579
            const u32 declen = r.u32le();
            const u8 type = r.u8v();
            const u32 srcpos = r.u32le();
            ++parsed;
            if (type == 0 || name.empty()) continue;  // directory index

            GrfEntry e;
            e.name = normalize(name);
            e.uncompressed = declen;
            e.compressed = packedLen - declen - 715;
            e.compressedAligned = alignedRaw - 37579;
            e.offset = srcpos;
            e.flags = 0x01;  // file
            // .gnd/.gat/.act/.str are header-only DES (cycle 0); everything else mixed, with the
            // cycle driven by the digit count of the compressed size, minimum 1 (eAthena grfio.c).
            int digits = 0;
            const auto dot = e.name.rfind('.');
            const std::string ext = dot == std::string::npos ? "" : e.name.substr(dot);
            if (ext != ".gnd" && ext != ".gat" && ext != ".act" && ext != ".str") {
                digits = 1;
                for (u32 v = e.compressed; v >= 10; v /= 10) ++digits;
            }
            // Types 1/3/5 = DES'd + deflated (the normal case); anything else nonzero is a raw
            // stored blob (eAthena memcpy path) — marked -2 so read() skips decrypt + inflate.
            e.v1CycleDigits = (type == 1 || type == 3 || type == 5) ? digits : -2;
            entries_[e.name] = std::move(e);
        }
    } catch (const std::out_of_range&) {
        log::warn("GRF: truncated v1 file table after {} entries", parsed);
    }
    return !entries_.empty();
}

const GrfEntry* GrfArchive::find(const std::string& vpath) const {
    auto it = entries_.find(normalize(vpath));
    return it == entries_.end() ? nullptr : &it->second;
}

bool GrfArchive::contains(const std::string& vpath) const {
    return find(vpath) != nullptr;
}

std::optional<std::vector<u8>> GrfArchive::read(const std::string& vpath) {
    const GrfEntry* e = find(vpath);
    if (!e || !e->isFile()) return std::nullopt;
    if (e->uncompressed == 0) return std::vector<u8>{};

    // Guard against corrupt/implausible size fields before allocating.
    constexpr u32 kMaxAsset = 512u * 1024 * 1024;  // 512 MiB
    if (e->uncompressed > kMaxAsset || e->compressedAligned > kMaxAsset ||
        e->compressed > kMaxAsset) {
        log::warn("GRF: implausible entry size, skipping {} (comp={}, aligned={}, uncomp={})",
                  e->name, e->compressed, e->compressedAligned, e->uncompressed);
        return std::nullopt;
    }

    // Legacy v0x1xx raw stored blob (#106): no encryption, no deflate — plain bytes.
    if (e->v1CycleDigits == -2) {
        std::vector<u8> raw(e->uncompressed);
        stream_.seekg(static_cast<std::streamoff>(kHeaderSize + e->offset), std::ios::beg);
        stream_.read(reinterpret_cast<char*>(raw.data()), e->uncompressed);
        if (static_cast<u32>(stream_.gcount()) != e->uncompressed) {
            log::error("GRF: short read for {}", e->name);
            return std::nullopt;
        }
        return raw;
    }

    std::vector<u8> comp(e->compressedAligned);
    stream_.seekg(static_cast<std::streamoff>(kHeaderSize + e->offset), std::ios::beg);
    stream_.read(reinterpret_cast<char*>(comp.data()), e->compressedAligned);
    if (static_cast<u32>(stream_.gcount()) != e->compressedAligned) {
        log::error("GRF: short read for {}", e->name);
        return std::nullopt;
    }
    if (e->v1CycleDigits >= 0) {
        // Legacy v0x1xx whole-archive encryption (#106): header-only DES (cycle 0) or the
        // mixed DES + shuffle scheme keyed by the compressed size's digit count.
        grf_v1_data_decode(comp.data(), e->compressedAligned, e->v1CycleDigits);
    } else if (e->flags & 0x02) {
        // GRF-editor-encrypted 0x200 entries (flag 0x02 = mixed cycle, 0x04 = header-only).
        grf_decrypt_full(comp.data(), e->compressedAligned, e->compressed);
    } else if (e->flags & 0x04) {
        grf_decrypt_header(comp.data(), e->compressedAligned);
    }
    return zinflate(comp.data(), e->compressed, e->uncompressed);
}

} // namespace uaro
