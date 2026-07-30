#pragma once
#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Types.hpp"
#include "resource/ContentRoute.hpp"
#include "resource/Grf.hpp"
#include "resource/Zip.hpp"

namespace uaro {

// Virtual filesystem: a lookup over loose override directories plus mounted ZIP and GRF
// archives. Priority: loose dirs (highest) > ZIPs (mount order) > GRFs (mount order, first
// mounted wins, matching data.ini top-to-bottom). ZIPs sit above GRFs so the content team
// can drop a .zip to shadow the base data.grf without repacking it (S.). All asset loading
// goes through here.
//
// Content-source routing (feat/content-sources): every mount carries a ContentSource tag
// (UaRO by default — the data.ini archives are our content; GRO.grf / RoM.zip mount with
// their own tags). read() categorizes the path (ContentRoute) and walks the per-category
// source chain (ROM -> UARO -> GRO / UARO -> GRO / GRO), so the player chooses per category
// where SFX / mobs / chars / ... come from. Loose dirs stay above everything.
class Vfs {
public:
    // First-mounted GRF has the highest GRF priority. Returns false if it fails
    // to open.
    bool mountGrf(const std::string& path, ContentSource tag = ContentSource::Uaro);

    // Mount a standard .zip archive (store/deflate). Sits above GRFs in priority so a zip
    // overrides same-named GRF assets. Returns false if it fails to open/parse.
    bool mountZip(const std::string& path, ContentSource tag = ContentSource::Uaro);

    // Loose directory whose files override every GRF.
    void mountDir(const std::string& dirPath);

    // Parse a data.ini [Data] section and mount the listed GRFs (in numeric key
    // order) resolved against baseDir. Returns how many mounted successfully.
    int mountDataIni(const std::string& iniText, const std::string& baseDir);

    // Drop all mounts (loose dirs + ZIP + GRF archives), closing their handles, so the VFS
    // can be rebuilt from scratch — e.g. after the patcher fetches a fresh GRF at runtime.
    void clear() { dirs_.clear(); zips_.clear(); grfs_.clear(); preloadCache_.clear(); }
    // Drop only the warmed file-byte cache (keep the mounts). Called on map-server disconnect (#cache).
    void clearPreloadCache() { preloadCache_.clear(); }

    // Per-category content source (Settings -> General -> Use content). Default: UaRO for
    // every category (our archives first, GRO.grf fallback) — today's behaviour when no
    // GRO.grf is mounted.
    void setContentMode(ContentCategory cat, ContentSource mode) {
        modes_[static_cast<usize>(cat)] = mode;
    }
    ContentSource contentMode(ContentCategory cat) const {
        return modes_[static_cast<usize>(cat)];
    }
    // True if any archive with this tag is mounted (e.g. to grey the ROeM option out).
    bool hasSource(ContentSource tag) const;

    bool exists(const std::string& vpath) const;
    std::optional<std::vector<u8>> read(const std::string& vpath) const;
    // read() but WITHOUT logging a miss to the missing-resource error log — for intentional
    // optional/fallback probes (a hi-res .png that may not exist, a .flac sibling, a loose PNG frame
    // override, bulk preload of maybe-absent files). Use this whenever a miss is expected, not a fault.
    std::optional<std::vector<u8>> readQuiet(const std::string& vpath) const;
    // readQuiet() but searching EVERY content source (UaRO + GRO + RoM), ignoring the per-category
    // content-mode routing. For assets that live in a specific archive regardless of the Chars/Mobs
    // content toggle -- e.g. gr2 model textures in texture_x4.zip (UaRO-tagged): if the "Other" category
    // is set to GRO, the normal chain would skip the UaRO zip and the model renders white (S.). No miss log.
    std::optional<std::vector<u8>> readQuietAnySource(const std::string& vpath) const;
    // Prefer a hi-res PNG over the requested raster: RO ships .bmp/.tga textures, but a same-named
    // .png (authored at higher resolution) anywhere in the VFS -- loose data/ or a GRF, in the normal
    // priority order -- wins; falls back to the original path if no .png exists. decodeImage() sniffs
    // the format by magic, so the caller decodes either. (S.: "сначала png в высоком разрешении, если
    // нигде нет -- bmp".) No-op swap if the path has no raster extension.
    std::optional<std::vector<u8>> readPreferPng(const std::string& vpath) const;
    // Human-readable source of where `vpath` resolves, mirroring readPreferPng()+read() order:
    // "loose:<root><path>" for an on-disk override, "zip:<archive>" / "grf:<archive>" for an
    // archive hit (with a "(png override)" note when the hi-res .png won), or "MISSING". Purely
    // diagnostic (S.: log which GRF/folder a texture comes from) — does not read the bytes.
    std::string whence(const std::string& vpath) const;
    // Warm the decompressed bytes of `vpath` into a cache so later read()s skip the GRF
    // inflate/decrypt (S.: preload job/hair sprites for WoE). No-op if the file is absent or
    // already cached. clear() drops the cache with the mounts.
    void preload(const std::string& vpath);
    // Read from ONE source family only (no category routing, no loose dirs) — e.g. a RoM
    // bundle path that exists solely inside RoM.zip.
    std::optional<std::vector<u8>> readFrom(ContentSource tag, const std::string& vpath) const {
        return readTagged(GrfArchive::normalize(vpath), tag);
    }
    // Existence check within ONE source family (no read/decompress) — the --view content
    // browser probes hundreds of candidate bundle paths to split mapped/unmapped.
    bool existsFrom(ContentSource tag, const std::string& vpath) const;
    // All entry vpaths under `prefix` in archives tagged `tag` (zips + GRFs, mount order).
    // Paths are normalized (lowercase, '/'); used by --view to enumerate RoM bundles and
    // GRF sprites.
    std::vector<std::string> listFrom(ContentSource tag, const std::string& prefix) const;

    // Every archived vpath under `prefix` across ALL mounted zips + grfs, deduped + sorted (tag-
    // agnostic, unlike listFrom). Loose override dirs are NOT walked (read() still finds them); used
    // for bulk warm/preload (e.g. cache every data/wav/ sound on map entry, S.).
    std::vector<std::string> list(const std::string& prefix) const;

    usize grfCount() const { return grfs_.size(); }
    usize zipCount() const { return zips_.size(); }
    usize dirCount() const { return dirs_.size(); }

private:
    // One lookup pass over the archives restricted to a source tag.
    std::optional<std::vector<u8>> readTagged(const std::string& norm, ContentSource tag) const;

    // Shared body of read()/readQuiet(): resolve `norm` across loose dirs + the archive chain. When
    // `recordMiss` and nothing resolves, log the path once to the missing-resource error log (S.).
    std::optional<std::vector<u8>> readImpl(const std::string& norm, bool recordMiss,
                                            bool allSources = false) const;
    void reportMiss(const std::string& norm) const;   // log a not-found path once (deduped)
    mutable std::set<std::string> missReported_;       // paths already logged, so each logs once

    struct ZipMount {
        std::unique_ptr<ZipArchive> arc;
        ContentSource tag;
    };
    struct GrfMount {
        std::unique_ptr<GrfArchive> arc;
        ContentSource tag;
    };
    std::vector<std::string> dirs_;        // override roots (with trailing '/')
    mutable std::vector<ZipMount> zips_;   // mount order = priority (above GRFs)
    mutable std::vector<GrfMount> grfs_;   // mount order = priority
    // Preloaded decompressed file bytes (keyed by normalized vpath) — read() serves these first
    // so warmed sprites skip the GRF inflate. Bounded by what preload() puts in (job/hair sprites).
    std::unordered_map<std::string, std::vector<u8>> preloadCache_;
    std::array<ContentSource, kContentCategories> modes_{
        ContentSource::Uaro, ContentSource::Uaro, ContentSource::Uaro,
        ContentSource::Uaro, ContentSource::Uaro, ContentSource::Uaro,
        ContentSource::Uaro, ContentSource::Uaro, ContentSource::Uaro};
};

} // namespace uaro
