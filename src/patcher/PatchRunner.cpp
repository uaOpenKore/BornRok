#include "patcher/PatchRunner.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "core/Log.hpp"
#include "core/crypto/Sha512.hpp"
#include "net/Http.hpp"
#include "patcher/GDriveUrl.hpp"
#include "patcher/ContentQuality.hpp"
#include "patcher/PatchManifest.hpp"
#include "patcher/PatcherConfig.hpp"
#include "patcher/PlatformId.hpp"

namespace fs = std::filesystem;

namespace uaro {
namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

bool samePath(const std::string& a, const std::string& b) {
    std::error_code ec;
    const fs::path pa = fs::weakly_canonical(fs::path(a), ec);
    const fs::path pb = fs::weakly_canonical(fs::path(b), ec);
    return !pa.empty() && pa == pb;
}

// --- verify cache ---------------------------------------------------------
// So we don't re-hash unchanged large files (e.g. a multi-GB GRF) on every start,
// we remember (size, mtime, sha512) for each file whose hash we last confirmed
// good. Next start, if the file's current size+mtime still match the record AND
// the record's sha matches the manifest's expected sha, we trust it and skip the
// (expensive) hash. Any write to a file bumps its mtime, so edits/corruption via
// normal I/O invalidate the record and force a re-hash. Cache lives in
// patcher/verify.cache; a missing/garbled cache just means everything re-hashes.
struct VerifyRec {
    u64 size = 0;
    i64 mtime = 0;
    std::string sha;
};

// Current on-disk size + mtime of a file. false if it does not exist.
bool statFile(const std::string& path, u64& size, i64& mtime) {
    std::error_code ec;
    const auto sz = fs::file_size(path, ec);
    if (ec) return false;
    const auto wt = fs::last_write_time(path, ec);
    if (ec) return false;
    size = static_cast<u64>(sz);
    mtime = static_cast<i64>(wt.time_since_epoch().count());
    return true;
}

// Line format: "<sha512>\t<size>\t<mtime>\t<dest>" (dest last so it may hold spaces).
std::unordered_map<std::string, VerifyRec> loadVerifyCache(const std::string& path) {
    std::unordered_map<std::string, VerifyRec> m;
    std::istringstream in(readFile(path));
    std::string line;
    while (std::getline(in, line)) {
        const auto t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        const auto t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        const auto t3 = line.find('\t', t2 + 1);
        if (t3 == std::string::npos) continue;
        VerifyRec r;
        r.sha = line.substr(0, t1);
        r.size = std::strtoull(line.substr(t1 + 1, t2 - t1 - 1).c_str(), nullptr, 10);
        r.mtime = std::strtoll(line.substr(t2 + 1, t3 - t2 - 1).c_str(), nullptr, 10);
        m[line.substr(t3 + 1)] = std::move(r);
    }
    return m;
}

std::string serializeVerifyCache(const std::unordered_map<std::string, VerifyRec>& m) {
    std::string out;
    for (const auto& [dest, r] : m) {
        out += r.sha;
        out += '\t';
        out += std::to_string(r.size);
        out += '\t';
        out += std::to_string(r.mtime);
        out += '\t';
        out += dest;
        out += '\n';
    }
    return out;
}

}  // namespace

PatchRunner::PatchRunner(std::string clientRoot, std::string selfExePath)
    : root_(std::move(clientRoot)), selfExe_(std::move(selfExePath)) {
    patcherDir_ = (fs::path(root_) / "patcher").string();
}

bool PatchRunner::applyFile(const std::string& part, const std::string& dest) {
    std::error_code ec;
    if (samePath(dest, selfExe_)) {
        // Can't overwrite the running exe: move it aside, then drop the new one in. The client
        // then CLOSES after the patch (no self-restart — S.: "просто закрытие клиента"); the
        // user relaunches manually and the new exe runs.
        const std::string old = dest + ".old";
        fs::remove(old, ec);                 // clear a previous leftover
        fs::rename(dest, old, ec);           // move the running exe aside (allowed on Windows)
        selfUpdated_ = true;
        // (ec ignored: if dest didn't exist, nothing to move.)
    } else {
        fs::remove(dest, ec);                // so rename succeeds on Windows when dest exists
    }
    fs::create_directories(fs::path(dest).parent_path(), ec);
    fs::rename(part, dest, ec);
    if (ec) {  // cross-device or locked: fall back to copy
        ec.clear();
        fs::copy_file(part, dest, fs::copy_options::overwrite_existing, ec);
        fs::remove(part, ec);
    }
    return !ec;
}

PatchSummary PatchRunner::run(const std::function<void(const PatchProgress&)>& report) {
    PatchSummary sum;
    PatchProgress pr;
    std::error_code ec;
    fs::create_directories(patcherDir_, ec);

    // Clean a leftover self-update backup from a previous run.
    if (!selfExe_.empty()) fs::remove(selfExe_ + ".old", ec);
    selfUpdated_ = false;

    // --- 1. config ---
    const std::string cfgPath = (fs::path(patcherDir_) / "patcher.cfg").string();
    PatcherConfig cfg;
    if (const std::string cfgText = readFile(cfgPath); !cfgText.empty()) {
        cfg = PatcherConfig::parse(cfgText);
    } else {
        cfg = PatcherConfig::defaults();
        writeFile(cfgPath, cfg.serialize());
    }
    if (cfg.manifestUrls.empty()) cfg = PatcherConfig::defaults();

    // --- 2. fetch manifest from the first working mirror ---
    std::string manifestText, usedUrl;
    for (const std::string& url : cfg.manifestUrls) {
        pr.phase = PatchProgress::Phase::FetchingManifest;
        pr.file = url;
        report(pr);
        std::string body;
        // A raw Google-Drive share/mirror link (open?id=...) returns the Drive HTML interstitial, NOT
        // the downloads.list -- resolve it to the direct-download endpoint first (S.: patcher "не
        // загружает файлы"; the old code fetched the HTML, saw a non-empty body, and used it as an
        // empty manifest, which also stopped it from trying the working http mirrors).
        std::string fetchUrl = url;
        if (std::string fid = gdriveFileId(url); !fid.empty()) fetchUrl = gdriveDirectUrl(fid);
        const net::HttpResult r = net::fetchToString(fetchUrl, body);
        // Accept a mirror ONLY if the body actually parses as a downloads.list (>=1 entry). This
        // rejects a Drive HTML interstitial, an http->https 301 redirect page, AND a wrong file --
        // the config's Drive mirror id currently points at patcher.cfg itself, not downloads.list, so
        // it returned the config text, parsed to 0 entries, and the patcher reported "done" without
        // downloading (S.: "делает проверку а потом сразу done"). Now we fall through to the working
        // https://patcher.bornrok.com mirror, which serves the real list.
        if (r.ok && !PatchManifest::parse(body).empty()) {
            manifestText = std::move(body);
            usedUrl = url;
            break;
        }
        log::warn("patcher: mirror is not a valid downloads.list, skipping: {} ({})", url, r.error);
    }
    if (usedUrl.empty()) {  // no mirror reachable -> proceed to the game
        pr.phase = PatchProgress::Phase::NoManifest;
        report(pr);
        sum.skipped = true;
        sum.selfUpdated = selfUpdated_;
    return sum;
    }

    // --- 3. cached-manifest path ---
    // We DELIBERATELY do NOT short-circuit on "cached downloads.list == fetched manifest". An
    // unchanged manifest does NOT mean the local game files are intact -- one may have been deleted,
    // truncated, or corrupted since the last run. The patcher must verify the actual SHA-512 of every
    // file on each start (S. 2026-07-08: "при старте должен проверять sha на файлы, а он этого не
    // делает"). So we always fall through to the per-file verify loop below; files whose hash already
    // matches are skipped there (cheap: no download), only mismatched/missing ones are fetched. The
    // cached list is still written at the end purely for diagnostics.
    const std::string listPath = (fs::path(patcherDir_) / "downloads.list").string();

    // --- 4. parse + select for our platform + content quality (#71 rework) ---
    // platform (win-x64/...) drives the platforms/<plat>/ binaries; the content-platform token
    // (windows-x64/steamdeck/xbox/...) drives the quality DEFAULT + the bundled decision. Bundled
    // consoles fetch only events/*; everyone else gets root/ + their binary + the chosen quality packs
    // (texture/sprite at 1k|4k). Empty config quality -> the platform default.
    const std::string platform = currentPlatform();
    const std::string contentPlat = contentPlatformId();
    ContentSelection sel;
    sel.platform = platform;
    sel.bundled = isBundledPlatform(contentPlat);
    // Skip the platform binary when it's store-managed and shouldn't be patched by the client:
    //  - iOS/Android app package;
    //  - a packaged read-only (MSIX) install;
    //  - macOS (.app/binary distributed via DMG/App Store — patching it breaks the code signature);
    //  - Steam (Deck HW token, OR launched via Steam anywhere — Steam updates the binary through its
    //    depots and "Verify integrity of game files" would revert any client self-patch; S. 2026-07-24).
    // root/ + quality + events still download to the writable data dir in every case.
    const bool viaSteam = contentPlat == "steamdeck" || std::getenv("SteamAppId") != nullptr;
    sel.skipPlatformBinary = isMobileStorePlatform(contentPlat) || storePackaged_ ||
                             contentPlat.rfind("macos-", 0) == 0 || viaSteam;
    sel.textureQuality = resolveQuality(textureQuality_, contentPlat);
    sel.spriteQuality = resolveQuality(spriteQuality_, contentPlat, /*isSprite=*/true);  // sprites default 1k (S.)
    const auto entries = PatchManifest::selectForContent(PatchManifest::parse(manifestText), sel);
    pr.total = static_cast<int>(entries.size());

    // Verify cache: trust (size,mtime,sha) records to skip re-hashing unchanged files.
    const std::string cachePath = (fs::path(patcherDir_) / "verify.cache").string();
    const auto oldCache = loadVerifyCache(cachePath);
    std::unordered_map<std::string, VerifyRec> newCache;

    // --- 5. per-file verify + download ---
    int idx = 0;
    for (const PatchEntry& e : entries) {
        ++idx;
        pr.index = idx;
        pr.file = e.dest;
        // S. 2026-07-28: ALL downloaded content (root/ + quality + event packs AND data.ini) goes into
        // ONE writable content/ folder for every platform (contentCacheDir_) as FLAT files, so Application
        // mounts a single place. Only the platform BINARY keeps its own path. No writable cache (a
        // fully-baked console) -> skip every data pack (that title ships them baked in the package).
        if (e.category != PatchCat::Platform && contentCacheDir_.empty()) continue;
        const std::string dest =
            (e.category == PatchCat::Platform)
                ? (fs::path(root_) / e.dest).string()
                : (fs::path(contentCacheDir_) / fs::path(e.dest).filename()).string();

        pr.phase = PatchProgress::Phase::Verifying;
        report(pr);

        // Fast path: current size+mtime match a cached record whose sha equals the
        // manifest's -> trust it without hashing (skips a full read of a huge GRF).
        u64 curSize = 0;
        i64 curMtime = 0;
        const bool haveStat = statFile(dest, curSize, curMtime);
        if (haveStat) {
            const auto it = oldCache.find(e.dest);
            if (it != oldCache.end() && it->second.size == curSize &&
                it->second.mtime == curMtime && it->second.sha == e.sha512) {
                newCache[e.dest] = it->second;  // still good; carry the record forward
                continue;
            }
            // Cache miss/stale -> hash to be sure; on match, remember for next time.
            if (auto local = Sha512::hashFileHex(dest); local && *local == e.sha512) {
                newCache[e.dest] = VerifyRec{curSize, curMtime, e.sha512};
                continue;  // already up to date
            }
        }

        const std::string part = dest + ".part";
        fs::create_directories(fs::path(dest).parent_path(), ec);

        auto progressCb = [&](u64 done, u64 total) {
            pr.bytesDone = done;
            pr.bytesTotal = total;
            pr.phase = PatchProgress::Phase::Downloading;
            report(pr);
            return true;  // never cancel mid-file here
        };

        // Human-readable host of the manifest server, for the "downloading from ..." status.
        auto hostOf = [](const std::string& url) -> std::string {
            usize a = url.find("://");
            a = (a == std::string::npos) ? 0 : a + 3;
            usize b = url.find('/', a);
            return url.substr(a, (b == std::string::npos ? url.size() : b) - a);
        };

        bool ok = false;
        std::string why;  // last failure reason, for the skip log
        // Fetch a source into .part with RESUME, then SHA-verify. `download(resume)` returns the
        // HttpResult. A surviving .part from a prior interrupted run is continued (resume=true);
        // if that yields the wrong hash (stale partial, or a server that ignored the Range and
        // re-sent the whole body appended), we wipe it and re-download once from scratch. On a
        // network drop the partial is KEPT so the NEXT patcher run resumes instead of restarting.
        auto fetchVerify = [&](auto&& download) {
            net::HttpResult r = download(true);  // resume if a .part survived a prior run
            if (r.ok) {
                if (auto h = Sha512::hashFileHex(part); h && *h == e.sha512) { ok = true; return; }
                why = "sha512 mismatch after resume; re-downloading full";
                fs::remove(part, ec);        // discard the bad partial, fetch fresh once
                r = download(false);
                if (r.ok) {
                    if (auto h2 = Sha512::hashFileHex(part); h2 && *h2 == e.sha512) { ok = true; return; }
                    why = "sha512 mismatch";
                    fs::remove(part, ec);    // complete but wrong -> not worth resuming
                } else {
                    why = "download: " + r.error;  // resume=false already removed the .part
                }
            } else {
                why = "download: " + r.error;  // partial kept -> resumes on the next run
            }
        };
        // 5a. Google-Drive mirror first.
        if (!e.mirror.empty()) {
            pr.source = "Google Drive";
            report(pr);
            fetchVerify([&](bool rs) { return net::downloadGDrive(e.mirror, part, progressCb, rs); });
        }
        // 5b. Server fallback.
        if (!ok) {
            const std::string url = serverFileUrl(usedUrl, e.serverPath);
            pr.source = hostOf(usedUrl);
            report(pr);
            fetchVerify([&](bool rs) { return net::downloadToFile(url, part, progressCb, rs); });
        }
        if (!ok) {  // couldn't get a good copy -> skip; a partial .part (if any) is kept for resume
            ++pr.failed;
            ++sum.failed;
            log::warn("patcher: skipping {} ({})", e.dest, why.empty() ? "download/verify failed" : why);
            continue;
        }

        pr.phase = PatchProgress::Phase::Applying;
        report(pr);
        if (applyFile(part, dest)) {
            ++pr.updated;
            ++sum.updated;
            // Freshly written -> record its size+mtime so next start trusts it without hashing.
            u64 nsz = 0;
            i64 nmt = 0;
            if (statFile(dest, nsz, nmt)) newCache[e.dest] = VerifyRec{nsz, nmt, e.sha512};
        } else {
            fs::remove(part, ec);
            ++pr.failed;
            ++sum.failed;
            log::warn("patcher: could not replace {}", e.dest);
        }
    }

    // --- 6. cache the new manifest + verify records ---
    writeFile(listPath, manifestText);
    writeFile(cachePath, serializeVerifyCache(newCache));

    sum.patched = sum.updated > 0;
    pr.phase = PatchProgress::Phase::Done;
    report(pr);
    sum.selfUpdated = selfUpdated_;
    return sum;
}

}  // namespace uaro
