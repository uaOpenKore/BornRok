#pragma once
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// One entry in a ZIP central directory.
struct ZipEntry {
    std::string name;         // normalized: lowercase, '/' separators
    u16 method = 0;           // 0 = stored, 8 = deflate (only these two are supported)
    u64 compressed = 0;       // stored (compressed) size
    u64 uncompressed = 0;     // size after inflate
    u64 localHeaderOffset = 0;  // offset of the local file header in the archive (64-bit: Zip64)
    bool isFile() const { return !name.empty() && name.back() != '/'; }
};

// Reader for standard (PKZIP) .zip archives, exposing the same read(vpath) interface as
// GrfArchive so the VFS can mount a zip exactly like a GRF. The content team can then ship
// loose assets as a .zip instead of repacking a GRF (S.). Parses the central directory
// eagerly; reads + inflates file data lazily on demand.
//
// Supports store (0) and deflate (8), the only methods RO/asset zips use, and Zip64
// (archives >4 GiB or >65535 entries — needed for the multi-GB RoM asset pack). Entry
// names are normalized (lowercase, '/') to match GRF vpaths.
class ZipArchive {
public:
    ZipArchive() = default;
    ~ZipArchive() = default;
    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    bool open(const std::string& path);
    void close();
    bool isOpen() const { return stream_.is_open(); }

    const std::map<std::string, ZipEntry>& entries() const { return entries_; }
    bool contains(const std::string& vpath) const;
    const ZipEntry* find(const std::string& vpath) const;
    const std::string& path() const { return path_; }  // archive file path (for source diagnostics)

    // Decompressed file bytes; nullopt if missing, a directory, an unsupported method,
    // or on a read/inflate error.
    std::optional<std::vector<u8>> read(const std::string& vpath);

private:
    bool parseCentralDirectory();

    std::ifstream stream_;
    std::string path_;
    std::map<std::string, ZipEntry> entries_;
};

}  // namespace uaro
