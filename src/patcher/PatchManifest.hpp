#pragma once
// Parser for the patcher's downloads.list manifest. Each line is three
// whitespace-separated fields:
//
//   <server-path>  <sha512-hex>  <google-drive-mirror-url>
//
// The server-path prefix decides the file's category, platform and its destination
// relative to the client root:
//   platforms/<platform>/<rel>  -> Platform; dest = <rel> (only the matching platform)
//   root/<rel>                  -> Root; dest = <rel> (any platform, base content)
//   <q>/<file>  (q = 1k|4k)     -> Quality; dest = <file>, quality = q, kind from the name
//                                  (texture.zip -> "texture", sprite.zip -> "sprite"). The dest drops
//                                  the quality dir so 1k and 4k share ONE local file that overwrites.
//   events/<file>               -> Events; dest = events/<file> (downloaded by ALL clients incl consoles)
//
// Pure (core only), so it is unit-tested in the offline build.
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

enum class PatchCat { Root, Platform, Quality, Events };

// Local name of the 1k sprite pack kept beside a 2k/4k one: the dye guide. WebP frames have no palette
// indices, so the client takes them from the 1k .spr bodies/heads (CharacterActor). Not a .zip/.grf, so the
// content mount never mounts it (its 1k .spr would hide the 2k WebP frames).
inline const std::string kDyeGuideDest = "sprite_1k.dyeguide";

struct PatchEntry {
    std::string serverPath;  // field 1, verbatim (used to build the server fallback URL)
    std::string sha512;      // field 2, lowercase hex (128 chars)
    std::string mirror;      // field 3, google-drive mirror URL (may be empty)
    std::string platform;    // Platform entries: the platform token; else ""
    std::string dest;        // path relative to the client root where the file lives
    PatchCat category = PatchCat::Root;
    std::string quality;     // Quality entries: "1k" | "4k"; else ""
    std::string kind;        // Quality entries: "texture" | "sprite" (from the filename); else ""
};

// What a given client should fetch: its platform token, whether the base content is bundled in the
// distribution (then ONLY events download), and the chosen texture/sprite qualities ("1k"|"4k").
struct ContentSelection {
    std::string platform;       // manifest binary token (PlatformId::currentPlatform), "" = unknown
    bool bundled = false;       // console with baked base content -> events only
    bool skipPlatformBinary = false;  // the app binary is store-managed (iOS/Android app package, or a
                                // packaged MSIX install) -> skip platforms/, but STILL download root/ +
                                // quality (4k/2k/1k) + events into the writable data dir (S.)
    std::string textureQuality; // "1k" | "4k"
    std::string spriteQuality;  // "1k" | "4k"
};

class PatchManifest {
public:
    // Parse the whole manifest text. Blank lines and lines starting with '#' or
    // '//' are ignored; malformed lines (fewer than 2 fields, or an unknown path
    // prefix) are skipped. The mirror (field 3) is optional.
    static std::vector<PatchEntry> parse(const std::string& text);

    // Keep only the entries that apply to `platform`: every root/ entry plus the
    // platforms/<platform>/ entries. If `platform` is empty (unknown OS), only
    // the root/ entries are kept. (Legacy helper; ignores Quality/Events entries.)
    static std::vector<PatchEntry> forPlatform(const std::vector<PatchEntry>& all,
                                               const std::string& platform);

    // Select the entries to fetch for a content profile (#71 rework):
    //   - Events entries: ALWAYS included (every client, incl. bundled consoles).
    //   - Bundled clients: nothing else (base content ships in the distro).
    //   - Otherwise: Root entries, the matching Platform entries, and exactly the Quality entries
    //     whose (kind, quality) match the chosen texture/sprite qualities.
    //   - Sprites at 2k/4k: also the 1k sprite pack as kDyeGuideDest (kind "sprite-dyeguide"), placed before
    //     the sprite pack, unless the chosen pack IS the 1k file (same sha512).
    static std::vector<PatchEntry> selectForContent(const std::vector<PatchEntry>& all,
                                                    const ContentSelection& sel);
};

// Build the server-fallback URL for a file: the directory of the manifest URL
// (everything up to the last '/') + the entry's server path. Pure -> testable.
// e.g. ("http://host/downloads.list", "root/data.grf") -> "http://host/root/data.grf".
std::string serverFileUrl(const std::string& manifestUrl, const std::string& serverPath);

}  // namespace uaro
