#pragma once
// Reuse of a pack already on disk instead of downloading it again. The quality packs share one local
// name (1k/2k/4k sprite.zip -> sprite.zip), and a client on 2k/4k sprites also keeps the 1k pack as the dye
// guide (sprite_1k.dyeguide). Switching quality would otherwise re-download a 1.6 GB file that is already
// there under the other name (S.). findLocalCopy finds a file in the content dir holding the wanted sha512;
// adoptLocalCopy puts it under the destination name (a hard link: instant, no extra disk; else a copy).
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include "core/Types.hpp"

namespace uaro {

// The patcher's verify record of a local file: its size + mtime when its sha512 was last confirmed.
struct LocalFileRec {
    u64 size = 0;
    i64 mtime = 0;
    std::string sha;
};
using LocalFileMap = std::unordered_map<std::string, LocalFileRec>;  // file name in the content dir -> record

// Current size + mtime of a file; false if it does not exist.
bool localFileStat(const std::string& path, u64& size, i64& mtime);

// Path of a file in `dir`, other than `destName`, that holds `sha`: first a file whose verify record (still
// matching its size + mtime) has that sha, then -- for recorded names whose record went stale -- one whose
// hash (via `hashFile`) is that sha. "" when there is none.
std::string findLocalCopy(const std::string& dir, const std::string& destName, const std::string& sha,
                          const LocalFileMap& records,
                          const std::function<std::optional<std::string>(const std::string&)>& hashFile);

// Make `destPath` a copy of `srcPath` (hard link if the filesystem allows, else a file copy), replacing an
// existing destPath atomically; srcPath stays. False on failure (destPath then unchanged).
bool adoptLocalCopy(const std::string& srcPath, const std::string& destPath);

}  // namespace uaro
