#pragma once
// Orchestrates a patch run: fetch downloads.list from a mirror, compare with the
// cached copy, and for each platform-relevant file verify its SHA-512 and download
// it (Google-Drive mirror first, manifest server as fallback) into a temp file,
// verify, then atomically replace. Self-update of the running client uses the
// rename-self trick. Full build only (uses libcurl + the filesystem).
#include <functional>
#include <string>

#include "core/Types.hpp"

namespace uaro {

struct PatchProgress {
    enum class Phase { FetchingManifest, Verifying, Downloading, Applying, Done, UpToDate, NoManifest };
    Phase phase = Phase::FetchingManifest;
    std::string file;        // current file's dest (or the manifest URL while fetching)
    std::string source;      // where the current download comes from ("Google Drive" or a host)
    int index = 0, total = 0;  // 1-based file counter
    u64 bytesDone = 0, bytesTotal = 0;  // current download
    int updated = 0, failed = 0;        // running tallies
};

struct PatchSummary {
    bool patched = false;  // at least one file was updated
    bool skipped = false;  // manifest unreachable or already up to date
    bool selfUpdated = false;  // the RUNNING exe was replaced -> the client must be relaunched
    int updated = 0, failed = 0;
};

class PatchRunner {
public:
    // clientRoot = the client install dir (contains patcher/). selfExePath = the
    // absolute path of the running executable (for the rename-self self-update).
    PatchRunner(std::string clientRoot, std::string selfExePath);

    // Chosen content quality ("1k"/"4k", or "" = platform default) for the texture and sprite packs
    // (#71 rework). Set before run() from the caller's game.cfg; the patcher then downloads the
    // matching `<q>/texture.zip` / `<q>/sprite.zip` rows. Ignored on bundled consoles (events only).
    void setContentQuality(std::string textureQuality, std::string spriteQuality) {
        textureQuality_ = std::move(textureQuality);
        spriteQuality_ = std::move(spriteQuality);
    }

    // Writable content-cache dir (Application::consoleServices().contentCacheDir()). The main
    // manifest's events/*.zip rows are written here as flat files so the overlay mount picks them up
    // (#71: events folded into downloads.list). Empty -> event packs are skipped (no writable cache).
    void setContentCacheDir(std::string dir) { contentCacheDir_ = std::move(dir); }

    // Packaged/read-only install (MSIX): the exe lives in a store-managed, read-only location and is
    // updated by the package, not the patcher — so skip the platforms/<token> binary (it can't be
    // written/replaced anyway). root/ + quality + events still download to the writable data dir. (S.)
    void setStorePackaged(bool v) { storePackaged_ = v; }

    // Runs the whole flow synchronously (call on a worker thread). `report` is
    // invoked from this thread at each step and during downloads; it must not
    // block. Returns the summary.
    PatchSummary run(const std::function<void(const PatchProgress&)>& report);

private:
    bool applyFile(const std::string& part, const std::string& dest);

    std::string root_;
    std::string patcherDir_;
    std::string selfExe_;
    std::string textureQuality_;  // "" = resolve to the platform default
    std::string spriteQuality_;
    std::string contentCacheDir_;  // writable cache for events/*.zip (#71); "" = skip event packs
    bool storePackaged_ = false;  // MSIX/store install -> skip the platforms/ binary (store-managed)
    bool selfUpdated_ = false;  // applyFile hit the running exe this run
};

}  // namespace uaro
