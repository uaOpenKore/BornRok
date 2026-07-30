#include "resource/Vfs.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <utility>

#if defined(_WIN32)
#include <filesystem>
#include <string>
#include <windows.h>
#endif

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "resource/GrfAlias.hpp"

namespace uaro {

namespace {

// Open a loose file for binary reading. The path may carry raw cp949 (EUC-KR) bytes straight from the
// GND/GRF index (Korean texture/sprite folders). A narrow ifstream on Windows pushes those bytes
// through the process ANSI codepage (e.g. cp1251 on a Russian install) and CANNOT open a Korean-named
// override -- so a content maker's drop-in file silently never loads. Convert cp949 -> UTF-16 via the
// OS codepage (949, no embedded table) and open the WIDE path, so a loose Korean-named file opens on
// ANY locale. On Linux the filesystem is byte-oriented and the cp949 bytes match the on-disk name
// directly, so the plain narrow open is correct. (S.: drop replacement files with their real names.)
std::ifstream open_loose_binary(const std::string& path) {
#if defined(_WIN32)
    const int wlen = MultiByteToWideChar(949, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
    if (wlen > 0) {
        std::wstring w(static_cast<usize>(wlen), L'\0');
        MultiByteToWideChar(949, 0, path.c_str(), static_cast<int>(path.size()), w.data(), wlen);
        return std::ifstream(std::filesystem::path(w), std::ios::binary | std::ios::ate);
    }
#endif
    return std::ifstream(path, std::ios::binary | std::ios::ate);
}

std::optional<std::vector<u8>> read_loose(const std::string& root, const std::string& vpath) {
    std::ifstream in = open_loose_binary(root + vpath);
    if (!in) return std::nullopt;
    const std::streamsize size = in.tellg();
    in.seekg(0);
    std::vector<u8> data(static_cast<usize>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size)) return std::nullopt;
    return data;
}

} // namespace

bool Vfs::mountGrf(const std::string& path, ContentSource tag) {
    auto grf = std::make_unique<GrfArchive>();
    if (!grf->open(path)) return false;
    grfs_.push_back({std::move(grf), tag});
    return true;
}

bool Vfs::mountZip(const std::string& path, ContentSource tag) {
    auto zip = std::make_unique<ZipArchive>();
    if (!zip->open(path)) return false;
    zips_.push_back({std::move(zip), tag});
    return true;
}

void Vfs::mountDir(const std::string& dirPath) {
    std::string root = dirPath;
    if (!root.empty() && root.back() != '/' && root.back() != '\\') root.push_back('/');
    dirs_.push_back(std::move(root));
}

int Vfs::mountDataIni(const std::string& iniText, const std::string& baseDir) {
    Ini ini = Ini::parse(iniText);
    const auto& secs = ini.sections();
    auto it = secs.find("Data");
    if (it == secs.end()) {
        log::warn("Vfs: data.ini has no [Data] section");
        return 0;
    }

    // Keys are numeric indices ("0","1","2",...); mount in numeric order.
    std::vector<std::pair<long, std::string>> ordered;
    for (const auto& [key, value] : it->second) {
        char* end = nullptr;
        long idx = std::strtol(key.c_str(), &end, 10);
        ordered.emplace_back(end && *end == '\0' ? idx : 1 << 30, value);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::string base = baseDir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base.push_back('/');

    // A listed archive ending in .zip is mounted as a ZIP, anything else as a GRF — so the
    // content team can drop a .zip into data.ini exactly where a GRF would go (S.).
    int mounted = 0;
    for (const auto& [idx, name] : ordered) {
        (void)idx;
        const bool isZip = name.size() >= 4 &&
                           (name.compare(name.size() - 4, 4, ".zip") == 0 ||
                            name.compare(name.size() - 4, 4, ".ZIP") == 0);
        if (isZip ? mountZip(base + name) : mountGrf(base + name)) ++mounted;
    }
    return mounted;
}

bool Vfs::exists(const std::string& vpath) const {
    return read(vpath).has_value();
}

bool Vfs::hasSource(ContentSource tag) const {
    for (const auto& z : zips_)
        if (z.tag == tag) return true;
    for (const auto& g : grfs_)
        if (g.tag == tag) return true;
    return false;
}

// One archive pass restricted to a source tag: ZIPs first (they shadow GRFs), then GRFs in
// mount order.
std::optional<std::vector<u8>> Vfs::readTagged(const std::string& norm, ContentSource tag) const {
    for (const auto& zip : zips_)
        if (zip.tag == tag)
            if (auto data = zip.arc->read(norm)) return data;
    for (const auto& grf : grfs_)
        if (grf.tag == tag)
            if (auto data = grf.arc->read(norm)) return data;
    return std::nullopt;
}

bool Vfs::existsFrom(ContentSource tag, const std::string& vpath) const {
    const std::string norm = GrfArchive::normalize(vpath);
    for (const auto& zip : zips_)
        if (zip.tag == tag && zip.arc->contains(norm)) return true;
    for (const auto& grf : grfs_)
        if (grf.tag == tag && grf.arc->entries().count(norm)) return true;
    return false;
}

std::vector<std::string> Vfs::listFrom(ContentSource tag, const std::string& prefix) const {
    const std::string norm = GrfArchive::normalize(prefix);
    std::vector<std::string> out;
    // Both archive types keep a sorted map of vpaths: walk the [prefix, ...) range.
    const auto scan = [&](const auto& entries) {
        for (auto it = entries.lower_bound(norm); it != entries.end(); ++it) {
            if (it->first.compare(0, norm.size(), norm) != 0) break;
            out.push_back(it->first);
        }
    };
    for (const auto& zip : zips_)
        if (zip.tag == tag) scan(zip.arc->entries());
    for (const auto& grf : grfs_)
        if (grf.tag == tag) scan(grf.arc->entries());
    return out;
}

std::vector<std::string> Vfs::list(const std::string& prefix) const {
    const std::string norm = GrfArchive::normalize(prefix);
    std::set<std::string> uniq;  // dedup the same vpath present in multiple archives
    const auto scan = [&](const auto& entries) {
        for (auto it = entries.lower_bound(norm); it != entries.end(); ++it) {
            if (it->first.compare(0, norm.size(), norm) != 0) break;
            uniq.insert(it->first);
        }
    };
    for (const auto& zip : zips_) scan(zip.arc->entries());
    for (const auto& grf : grfs_) scan(grf.arc->entries());
    return {uniq.begin(), uniq.end()};
}

void Vfs::preload(const std::string& vpath) {
    const std::string norm = GrfArchive::normalize(vpath);
    if (preloadCache_.count(norm)) return;      // already warm
    // Quiet: the bulk warms probe thousands of maybe-absent files (every item's icon/spr, dye .pal),
    // so a miss here is expected, not a fault -> don't log it as a missing resource. (S.)
    if (auto data = readQuiet(vpath)) preloadCache_.emplace(norm, std::move(*data));
}

void Vfs::reportMiss(const std::string& norm) const {
    // Log every resource the client tried to load and couldn't find in ANY loose dir / zip / GRF,
    // once per path (S.: "в лог ошибок писать все ресурсы которые не смог загрузить"). Grep the log
    // for "missing resource" to get the full content-gap list.
    if (missReported_.insert(norm).second)
        log::debug("missing resource (not in loose/zip/grf): {}", norm);  // content gap, not an error -> debug (S.: не спамить)
}

std::optional<std::vector<u8>> Vfs::read(const std::string& vpath) const {
    return readImpl(GrfArchive::normalize(vpath), /*recordMiss=*/true);
}

std::optional<std::vector<u8>> Vfs::readQuiet(const std::string& vpath) const {
    return readImpl(GrfArchive::normalize(vpath), /*recordMiss=*/false);
}

std::optional<std::vector<u8>> Vfs::readQuietAnySource(const std::string& vpath) const {
    return readImpl(GrfArchive::normalize(vpath), /*recordMiss=*/false, /*allSources=*/true);
}

std::optional<std::vector<u8>> Vfs::readImpl(const std::string& norm, bool recordMiss,
                                             bool allSources) const {
    if (auto it = preloadCache_.find(norm); it != preloadCache_.end())
        return it->second;  // warmed bytes -> skip the GRF inflate/decrypt

    // Content-source chain for this path's category (Settings -> Use content). Loose dirs are
    // walked before every archive pass; within the archives each source family is exhausted
    // before falling back to the next (ROM -> UARO -> GRO / UARO -> GRO / GRO).
    std::array<ContentSource, 3> chain;
    // allSources: probe every archive tag regardless of the category's content-mode routing (a model
    // texture must resolve whether Chars/Mobs are set to UaRO/GRO/RoM). Otherwise route by category.
    usize chainLen;
    if (allSources) {
        chain = {ContentSource::Uaro, ContentSource::Gro, ContentSource::Rom};
        chainLen = 3;
    } else {
        chainLen = sourceChain(modes_[static_cast<usize>(categorizeVpath(norm))], chain);
    }

    // Probe one candidate path: loose dirs (override everything) then the archive source chain.
    auto probe = [&](const std::string& cand) -> std::optional<std::vector<u8>> {
        for (const auto& root : dirs_)
            if (auto data = read_loose(root, cand)) return data;
        for (usize i = 0; i < chainLen; ++i)
            if (auto data = readTagged(cand, chain[i])) return data;
        return std::nullopt;
    };

    // Candidate paths, in priority order:
    //  1. English-aliased path  — lets the content team migrate GRF names to English gradually
    //     (aliasKoreanPath() is a no-op for pure-ASCII / unaliasable paths).
    //  2. the path as requested (Korean original).
    //  3/4. the same two WITHOUT the leading "data/" — so a content maker's archive or folder can
    //     drop the mandatory data/ prefix and hold e.g. texture\...\foo.png (S.: "texture.zip без
    //     data в пути").
    //  5/6. the same two WITH "data/" added when it is absent — so a request that OMITS data/ (e.g. a
    //     .gr2 model's texture name, or a loose model path) still resolves against an archive that keeps
    //     the full data/ prefix. Net effect: "data/" is OPTIONAL in the path either way (S.).
    //  Full (data/) paths win over the data-less form.
    const std::string eng = aliasKoreanPath(norm);
    auto stripData = [](const std::string& p) {
        return p.rfind("data/", 0) == 0 ? p.substr(5) : p;
    };
    auto addData = [](const std::string& p) {
        return p.rfind("data/", 0) == 0 ? p : "data/" + p;
    };
    std::array<std::string, 6> cands{eng, norm, addData(eng), addData(norm), stripData(eng), stripData(norm)};
    for (usize k = 0; k < cands.size(); ++k) {
        bool dup = false;
        for (usize j = 0; j < k; ++j)
            if (cands[j] == cands[k]) { dup = true; break; }
        if (dup) continue;  // skip identical candidates (pure-ASCII / no data/ prefix)
        if (auto data = probe(cands[k])) return data;
    }
    if (recordMiss) reportMiss(norm);  // genuinely not found anywhere -> log it once (S.)
    return std::nullopt;
}

std::optional<std::vector<u8>> Vfs::readPreferPng(const std::string& vpath) const {
    // Swap a trailing .bmp/.tga/.jpg for a hi-res override and try that first (through the normal VFS
    // priority: loose data/ then GRFs). Preference order (S.): .webp > .png > the original raster.
    // WebP wins (smallest HD format with alpha); .png is the fallback override; the original .bmp/.tga
    // is last. decodeImage() sniffs the format by magic, so the caller decodes whichever hits.
    if (const auto dot = vpath.find_last_of('.'); dot != std::string::npos) {
        std::string ext = vpath.substr(dot + 1);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == "bmp" || ext == "tga" || ext == "jpg" || ext == "jpeg" || ext == "png") {
            const std::string stem = vpath.substr(0, dot);
#if defined(CLIENT_WITH_WEBP)
            // (edit bumps this TU's timestamp so MSBuild recompiles it with the CLIENT_WITH_WEBP flag;
            //  an incremental build otherwise keeps a stale .obj compiled before the define existed.)
            // Only prefer a .webp when this build can actually decode it. Otherwise a content maker's
            // .webp override resolves to bytes decodeImage() can't handle -> the texture renders white
            // (S.: "ни одна текстура webp не загрузилась"). Without libwebp we fall through to .png/.bmp.
            if (auto webp = readQuiet(stem + ".webp")) return webp;        // WebP top priority (optional)
#endif
            if (ext != "png")
                if (auto png = readQuiet(stem + ".png")) return png;       // then .png override (optional)
        }
    }
    return read(vpath);
}

std::string Vfs::whence(const std::string& vpath) const {
    // Report where a path resolves without reading it, mirroring read()'s order: english-alias then
    // normal, loose dirs before archives, and within a source family zips shadow grfs.
    auto locate = [&](const std::string& p) -> std::string {
        const std::string norm = GrfArchive::normalize(p);
        auto stripData = [](const std::string& s) {
            return s.rfind("data/", 0) == 0 ? s.substr(5) : s;
        };
        const std::string eng = aliasKoreanPath(norm);
        std::array<std::string, 4> all{eng, norm, stripData(eng), stripData(norm)};
        std::array<std::string, 4> cands;  // de-duplicated, preserving order (mirrors read())
        usize nCand = 0;
        for (const auto& c : all) {
            bool dup = false;
            for (usize j = 0; j < nCand; ++j) if (cands[j] == c) { dup = true; break; }
            if (!dup) cands[nCand++] = c;
        }
        for (usize ci = 0; ci < nCand; ++ci)
            for (const auto& root : dirs_) {
                std::ifstream in = open_loose_binary(root + cands[ci]);
                if (in.good()) return "loose:" + root + cands[ci];
            }
        std::array<ContentSource, 3> chain;
        const usize chainLen = sourceChain(modes_[static_cast<usize>(categorizeVpath(norm))], chain);
        for (usize ci = 0; ci < nCand; ++ci)
            for (usize i = 0; i < chainLen; ++i) {
                for (const auto& z : zips_)
                    if (z.tag == chain[i] && z.arc->contains(cands[ci])) return "zip:" + z.arc->path();
                for (const auto& g : grfs_)
                    if (g.tag == chain[i] && g.arc->contains(cands[ci])) return "grf:" + g.arc->path();
            }
        return {};
    };
    // Prefer the hi-res .png exactly like readPreferPng, and say so when it wins.
    if (const auto dot = vpath.find_last_of('.'); dot != std::string::npos) {
        std::string ext = vpath.substr(dot + 1);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == "bmp" || ext == "tga" || ext == "jpg" || ext == "jpeg")
            if (auto s = locate(vpath.substr(0, dot) + ".png"); !s.empty()) return s + "  (png override)";
    }
    if (auto s = locate(vpath); !s.empty()) return s;
    return "MISSING";
}

} // namespace uaro
