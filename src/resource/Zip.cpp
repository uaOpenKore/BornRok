#include "resource/Zip.hpp"

#include <zlib.h>

#include <cstring>
#include <string>

#include "core/Log.hpp"
#include "resource/Cp949.hpp"  // portable UTF-8 -> cp949 (identical on every platform)
#include "resource/Grf.hpp"  // GrfArchive::normalize (shared vpath normalization)

namespace uaro {

namespace {

// A ZIP written by a modern tool (7-Zip, Windows Explorer) stores non-ASCII filenames as UTF-8 and
// sets general-purpose bit 11. RO assets are requested by their cp949 (EUC-KR) bytes (as the GND/GRF
// index stores them), so a UTF-8 entry name would never match. Convert UTF-8 -> cp949 so a content
// maker can just zip a folder of Korean-named overrides and it works; a char not representable in cp949
// (non-Korean name) keeps its raw bytes. Uses the embedded Unicode->cp949 table for IDENTICAL behaviour
// on EVERY platform (S. 2026-08-06 "проверь для всех платформ"): the old Windows-only OS-codepage path
// was a no-op on Android/Linux/consoles, so their whole Korean-folder content (유저인터페이스 UI skin,
// 몬스터 sprites, 이펙트 ...) silently missed every lookup and nothing textured rendered. See resource/Cp949.
std::string utf8ToCp949(const std::string& s) { return utf8ToCp949Portable(s); }

// Some content packs are zipped from INSIDE a data/ folder, so their entries are rooted at a data
// subdir ("texture/...", "model/...", "palette/...", "wav/...") instead of the "data/texture/..."
// the client (and GRF convention) looks up -> every asset in that zip misses and, for textures,
// the model/ground renders white (S.: "текстур нету на флагах"; texture_x4.zip is rooted at
// texture/). If a normalized entry isn't already under data/ but its top segment is a known RO
// data root, prepend "data/" so it matches lookups. bgm/ is deliberately NOT in the set (BGM is
// loaded loose from the client root, not via data/).
std::string rerootUnderData(std::string n) {
    if (n.rfind("data/", 0) == 0) return n;
    const usize slash = n.find('/');
    if (slash == std::string::npos) return n;
    const std::string top = n.substr(0, slash);
    static const char* kDataRoots[] = {"texture", "model", "sprite", "palette",
                                       "wav", "luafiles514", "effect", "book", "memo", "map"};
    for (const char* r : kDataRoots)
        if (top == r) return "data/" + n;
    return n;
}

// Little-endian readers over an in-memory buffer.
u16 rd16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
u32 rd32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
u64 rd64(const u8* p) {
    return static_cast<u64>(rd32(p)) | (static_cast<u64>(rd32(p + 4)) << 32);
}

constexpr u32 kSigLocal = 0x04034b50;      // "PK\3\4" local file header
constexpr u32 kSigCentral = 0x02014b50;    // "PK\1\2" central directory file header
constexpr u32 kSigEOCD = 0x06054b50;       // "PK\5\6" end of central directory
constexpr u32 kSigEOCD64 = 0x06064b50;     // "PK\6\6" Zip64 end of central directory
constexpr u32 kSigEOCD64Loc = 0x07064b50;  // "PK\6\7" Zip64 EOCD locator
constexpr u32 kU32Max = 0xFFFFFFFFu;       // Zip64 sentinel in a 32-bit field

// Inflate a RAW deflate stream (RFC1951, no zlib header) — this is what ZIP stores, unlike
// GRF which wraps its streams in a zlib header. Negative window bits selects raw mode.
std::optional<std::vector<u8>> rawInflate(const u8* src, usize srcLen, u32 outLen) {
    std::vector<u8> out(outLen);
    if (outLen == 0) return out;
    z_stream s;
    std::memset(&s, 0, sizeof(s));
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return std::nullopt;
    s.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
    s.avail_in = static_cast<uInt>(srcLen);
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(outLen);
    const int rc = inflate(&s, Z_FINISH);
    const uLong got = s.total_out;
    inflateEnd(&s);
    if (rc != Z_STREAM_END || got != outLen) {
        log::error("ZIP raw inflate failed (rc={}, got={}, want={})", rc,
                   static_cast<u64>(got), outLen);
        return std::nullopt;
    }
    return out;
}

}  // namespace

bool ZipArchive::open(const std::string& path) {
    close();
    stream_.open(path, std::ios::binary);
    if (!stream_) {
        log::error("ZIP: cannot open {}", path);
        return false;
    }
    path_ = path;
    if (!parseCentralDirectory()) {
        close();
        return false;
    }
    return true;
}

void ZipArchive::close() {
    if (stream_.is_open()) stream_.close();
    entries_.clear();
    path_.clear();
}

bool ZipArchive::parseCentralDirectory() {
    // Locate the End Of Central Directory record: it lives at the end of the file, possibly
    // followed by a <=65535-byte archive comment, so scan back over the last chunk for its
    // signature.
    stream_.seekg(0, std::ios::end);
    const std::streamoff fileSize = stream_.tellg();
    if (fileSize < 22) {  // EOCD is 22 bytes minimum
        log::error("ZIP: {} too small to be a zip", path_);
        return false;
    }
    const std::streamoff scanLen =
        std::min<std::streamoff>(fileSize, 22 + 65535);
    std::vector<u8> tail(static_cast<usize>(scanLen));
    stream_.seekg(fileSize - scanLen);
    stream_.read(reinterpret_cast<char*>(tail.data()), scanLen);
    if (stream_.gcount() != scanLen) {
        log::error("ZIP: short tail read in {}", path_);
        return false;
    }
    // Find the last EOCD signature (last, in case the comment happens to contain the bytes).
    std::streamoff eocd = -1;
    for (std::streamoff i = scanLen - 22; i >= 0; --i) {
        if (rd32(&tail[static_cast<usize>(i)]) == kSigEOCD) { eocd = i; break; }
    }
    if (eocd < 0) {
        log::error("ZIP: no EOCD record in {} (not a zip?)", path_);
        return false;
    }
    const u8* e = &tail[static_cast<usize>(eocd)];
    u64 totalEntries = rd16(e + 10);
    u64 cdSize = rd32(e + 12);
    u64 cdOffset = rd32(e + 16);

    // Zip64: when the archive is >4 GiB or has >65535 entries, the 32-bit EOCD fields carry
    // sentinels and the real 64-bit values live in a Zip64 EOCD record, pointed at by a
    // locator sitting 20 bytes before the classic EOCD. Needed for the multi-GB RoM pack.
    if (cdOffset == kU32Max || cdSize == kU32Max || totalEntries == 0xFFFFu) {
        if (eocd < 20 || rd32(&tail[static_cast<usize>(eocd - 20)]) != kSigEOCD64Loc) {
            log::error("ZIP: {} looks like Zip64 but has no EOCD64 locator", path_);
            return false;
        }
        const u64 eocd64Ofs = rd64(&tail[static_cast<usize>(eocd - 20) + 8]);
        u8 rec[56];
        stream_.seekg(static_cast<std::streamoff>(eocd64Ofs));
        stream_.read(reinterpret_cast<char*>(rec), 56);
        if (stream_.gcount() != 56 || rd32(rec) != kSigEOCD64) {
            log::error("ZIP: bad Zip64 EOCD record in {}", path_);
            return false;
        }
        totalEntries = rd64(rec + 32);
        cdSize = rd64(rec + 40);
        cdOffset = rd64(rec + 48);
    }

    // Read the whole central directory and walk its (variable-length) headers.
    std::vector<u8> cd(static_cast<usize>(cdSize));
    stream_.seekg(static_cast<std::streamoff>(cdOffset));
    stream_.read(reinterpret_cast<char*>(cd.data()), static_cast<std::streamsize>(cdSize));
    if (static_cast<u64>(stream_.gcount()) != cdSize) {
        log::error("ZIP: short central-directory read in {}", path_);
        return false;
    }

    usize pos = 0;
    for (u64 n = 0; n < totalEntries; ++n) {
        if (pos + 46 > cd.size() || rd32(&cd[pos]) != kSigCentral) {
            log::error("ZIP: malformed central directory in {} at entry {}", path_, n);
            return false;
        }
        const u16 flag = rd16(&cd[pos + 8]);  // general-purpose bit flag; bit 11 (0x0800) = UTF-8 name
        const u16 method = rd16(&cd[pos + 10]);
        u64 compressed = rd32(&cd[pos + 20]);
        u64 uncompressed = rd32(&cd[pos + 24]);
        const u16 nameLen = rd16(&cd[pos + 28]);
        const u16 extraLen = rd16(&cd[pos + 30]);
        const u16 commentLen = rd16(&cd[pos + 32]);
        u64 localOffset = rd32(&cd[pos + 42]);
        if (pos + 46 + nameLen + extraLen > cd.size()) {
            log::error("ZIP: truncated entry in central directory of {}", path_);
            return false;
        }
        std::string name(reinterpret_cast<const char*>(&cd[pos + 46]), nameLen);
        if (flag & 0x0800) name = utf8ToCp949(name);  // UTF-8 entry -> cp949 so it matches asset requests

        // Zip64 extended-information extra field (header id 0x0001): the true 64-bit values,
        // present in order only for those 32-bit fields that held the 0xFFFFFFFF sentinel.
        const u8* extra = &cd[pos + 46 + nameLen];
        for (u16 ep = 0; ep + 4 <= extraLen;) {
            const u16 id = rd16(extra + ep);
            const u16 sz = rd16(extra + ep + 2);
            if (ep + 4 + sz > extraLen) break;
            if (id == 0x0001) {
                usize q = ep + 4;
                if (uncompressed == kU32Max && q + 8 <= ep + 4u + sz) { uncompressed = rd64(extra + q); q += 8; }
                if (compressed == kU32Max && q + 8 <= ep + 4u + sz) { compressed = rd64(extra + q); q += 8; }
                if (localOffset == kU32Max && q + 8 <= ep + 4u + sz) { localOffset = rd64(extra + q); q += 8; }
                break;
            }
            ep += 4 + sz;
        }
        pos += 46 + nameLen + extraLen + commentLen;

        ZipEntry ent;
        // lowercase + '/'; matches GRF vpaths. Re-root data-subdir zips under data/ (see helper).
        ent.name = rerootUnderData(GrfArchive::normalize(std::move(name)));
        ent.method = method;
        ent.compressed = compressed;
        ent.uncompressed = uncompressed;
        ent.localHeaderOffset = localOffset;
        if (ent.isFile())  // skip directory entries
            entries_[ent.name] = std::move(ent);
    }
    return true;
}

bool ZipArchive::contains(const std::string& vpath) const { return find(vpath) != nullptr; }

const ZipEntry* ZipArchive::find(const std::string& vpath) const {
    auto it = entries_.find(GrfArchive::normalize(vpath));
    return it == entries_.end() ? nullptr : &it->second;
}

std::optional<std::vector<u8>> ZipArchive::read(const std::string& vpath) {
    const ZipEntry* ent = find(vpath);
    if (ent == nullptr || !stream_.is_open()) return std::nullopt;
    if (ent->method != 0 && ent->method != 8) {
        log::error("ZIP: {} unsupported compression method {}", ent->name, ent->method);
        return std::nullopt;
    }

    // The local header repeats the name/extra lengths, which may differ from the central
    // directory's, so read them here to find the true data start.
    u8 lh[30];
    stream_.seekg(static_cast<std::streamoff>(ent->localHeaderOffset));
    stream_.read(reinterpret_cast<char*>(lh), 30);
    if (stream_.gcount() != 30 || rd32(lh) != kSigLocal) {
        log::error("ZIP: bad local header for {}", ent->name);
        return std::nullopt;
    }
    const u16 nameLen = rd16(lh + 26);
    const u16 extraLen = rd16(lh + 28);
    const std::streamoff dataStart =
        static_cast<std::streamoff>(ent->localHeaderOffset) + 30 + nameLen + extraLen;

    std::vector<u8> comp(static_cast<usize>(ent->compressed));
    stream_.seekg(dataStart);
    if (ent->compressed > 0) {
        stream_.read(reinterpret_cast<char*>(comp.data()),
                     static_cast<std::streamsize>(ent->compressed));
        if (static_cast<u64>(stream_.gcount()) != ent->compressed) {
            log::error("ZIP: short data read for {}", ent->name);
            return std::nullopt;
        }
    }

    if (ent->method == 0) {  // stored
        if (ent->compressed != ent->uncompressed) return std::nullopt;
        return comp;
    }
    return rawInflate(comp.data(), comp.size(), static_cast<u32>(ent->uncompressed));  // deflate
}

}  // namespace uaro
