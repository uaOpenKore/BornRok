#pragma once
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Inflate a zlib stream whose decompressed size is not known up front, into at most
// `maxOut` bytes (use for small payloads like a 24x24 guild emblem). Returns the
// inflated bytes (resized to the true length), or nullopt on a zlib error / overflow.
std::optional<std::vector<u8>> zlibInflate(const u8* src, usize srcLen, usize maxOut);

// One entry in a GRF file table.
struct GrfEntry {
    std::string name;          // normalized: lowercase, '/' separators
    u32 compressed = 0;        // zlib-compressed size
    u32 compressedAligned = 0; // bytes actually stored (DES alignment)
    u32 uncompressed = 0;      // size after inflate
    u8 flags = 0;              // bit0=file, bit1=mixed-DES, bit2=header-DES
    u64 offset = 0;            // data offset relative to end of the 46-byte header (64-bit: v0x300)
    // Legacy v0x1xx (#106): >= 0 marks the old whole-archive encryption. 0 = header-only DES
    // (a .gnd/.gat/.act/.str), > 0 = mixed DES + shuffle, value = digit count of `compressed`.
    i32 v1CycleDigits = -1;

    bool isFile() const { return (flags & 0x01) != 0; }
    bool isEncrypted() const { return (flags & 0x06) != 0; }
};

// Reader for GRF version 0x1xx, 0x200 and 0x300 archives. Reads the header and file table
// eagerly (zlib-compressed in 0x2xx/0x3xx, DES-obfuscated plain bytes in 0x1xx); reads file
// data lazily on demand. v0x300 (GRF Editor's int64 extension) promotes the table offset +
// per-entry data offsets to 64-bit so archives can exceed 4 GiB.
class GrfArchive {
public:
    GrfArchive() = default;
    ~GrfArchive() = default;
    GrfArchive(const GrfArchive&) = delete;
    GrfArchive& operator=(const GrfArchive&) = delete;

    bool open(const std::string& path);
    void close();
    bool isOpen() const { return stream_.is_open(); }

    u32 version() const { return version_; }
    const std::map<std::string, GrfEntry>& entries() const { return entries_; }

    bool contains(const std::string& vpath) const;
    const GrfEntry* find(const std::string& vpath) const;
    const std::string& path() const { return path_; }  // archive file path (for source diagnostics)

    // Decompressed file bytes; nullopt if missing, a directory, encrypted
    // (unsupported for now), or on a read/inflate error.
    std::optional<std::vector<u8>> read(const std::string& vpath);

    // lowercase + backslash -> forward slash.
    static std::string normalize(std::string p);

private:
    // Parse the (zlib-compressed) file table at `tableOffset`. v300 selects the int64 entry
    // layout (8-byte data offsets, 21-byte fixed part, 4-byte pad before the size fields).
    bool parseTable(u64 tableOffset, u32 fileCount, bool v300);
    // Parse the legacy v0x1xx table: raw bytes from the offset to EOF, DES-obfuscated names,
    // size fields masked with magic constants (eAthena grfio.c layout). #106.
    bool parseTableV1(u64 tableOffset, u32 fileCount);

    std::ifstream stream_;
    std::string path_;
    u32 version_ = 0;
    std::map<std::string, GrfEntry> entries_;
};

} // namespace uaro
