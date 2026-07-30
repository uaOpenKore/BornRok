#include "patcher/PatchManifest.hpp"

#include <algorithm>
#include <cctype>

namespace uaro {
namespace {

// Split a line on runs of ASCII whitespace.
std::vector<std::string> splitWs(const std::string& s) {
    std::vector<std::string> out;
    usize i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        const usize start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

// Basename of a path (after the last '/').
std::string baseName(const std::string& p) {
    const usize s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

// Resolve a manifest path into the entry's category/platform/dest/quality/kind. False on unknown prefix.
bool resolvePath(const std::string& path, PatchEntry& e) {
    if (path.rfind("platforms/", 0) == 0) {  // platforms/<platform>/<rel>
        const usize p0 = 10;                  // after "platforms/"
        const usize slash = path.find('/', p0);
        if (slash == std::string::npos || slash + 1 >= path.size()) return false;
        e.category = PatchCat::Platform;
        e.platform = path.substr(p0, slash - p0);
        e.dest = path.substr(slash + 1);
        return !e.platform.empty() && !e.dest.empty();
    }
    if (path.rfind("root/", 0) == 0) {  // root/<rel>
        e.category = PatchCat::Root;
        e.dest = path.substr(5);
        return !e.dest.empty();
    }
    if (path.rfind("events/", 0) == 0) {  // events/<file> -> keep the events/ dir in the dest
        e.category = PatchCat::Events;
        e.dest = path;
        return path.size() > 7;
    }
    if (path.rfind("1k/", 0) == 0 || path.rfind("2k/", 0) == 0 || path.rfind("4k/", 0) == 0) {  // <q>/<file>
        e.category = PatchCat::Quality;
        e.quality = path.substr(0, 2);
        e.dest = baseName(path);  // drop the quality dir: 1k/2k/4k share ONE local file (overwrite)
        if (e.dest.empty()) return false;
        if (e.dest.find("texture") != std::string::npos) e.kind = "texture";
        else if (e.dest.find("sprite") != std::string::npos) e.kind = "sprite";
        else return false;  // an unrecognised quality file we don't know how to select
        return true;
    }
    return false;
}

}  // namespace

std::vector<PatchEntry> PatchManifest::parse(const std::string& text) {
    std::vector<PatchEntry> out;
    usize i = 0;
    while (i < text.size()) {
        usize nl = text.find('\n', i);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(i, nl - i);
        i = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Skip blanks and comments.
        usize j = 0;
        while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
        if (j >= line.size() || line[j] == '#' || (line[j] == '/' && j + 1 < line.size() && line[j + 1] == '/'))
            continue;

        const std::vector<std::string> f = splitWs(line);
        if (f.size() < 2) continue;  // need at least path + sha
        PatchEntry e;
        e.serverPath = f[0];
        e.sha512 = f[1];
        e.mirror = f.size() >= 3 ? f[2] : std::string();
        if (!resolvePath(e.serverPath, e)) continue;
        // Normalise the hash to lowercase for a case-insensitive compare later.
        for (char& c : e.sha512) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        out.push_back(std::move(e));
    }
    return out;
}

std::string serverFileUrl(const std::string& manifestUrl, const std::string& serverPath) {
    const usize slash = manifestUrl.find_last_of('/');
    const std::string base =
        slash == std::string::npos ? std::string() : manifestUrl.substr(0, slash + 1);
    return base + serverPath;
}

std::vector<PatchEntry> PatchManifest::forPlatform(const std::vector<PatchEntry>& all,
                                                   const std::string& platform) {
    std::vector<PatchEntry> out;
    for (const PatchEntry& e : all) {
        if (e.category != PatchCat::Root && e.category != PatchCat::Platform) continue;
        if (e.category == PatchCat::Root)  // root/ -> all platforms
            out.push_back(e);
        else if (!platform.empty() && e.platform == platform)
            out.push_back(e);
    }
    return out;
}

std::vector<PatchEntry> PatchManifest::selectForContent(const std::vector<PatchEntry>& all,
                                                        const ContentSelection& sel) {
    std::vector<PatchEntry> out;
    for (const PatchEntry& e : all) {
        switch (e.category) {
            case PatchCat::Events:  // everyone, including bundled consoles
                out.push_back(e);
                break;
            case PatchCat::Root:
                if (!sel.bundled) out.push_back(e);
                break;
            case PatchCat::Platform:
                // Skip the platform binary on bundled consoles AND when the binary is store-managed
                // (iOS/Android app package, or a packaged MSIX install — can't be patched). Portable
                // desktop still downloads it.
                if (!sel.bundled && !sel.skipPlatformBinary && !sel.platform.empty() &&
                    e.platform == sel.platform)
                    out.push_back(e);
                break;
            case PatchCat::Quality:
                if (sel.bundled) break;
                if (e.kind == "texture" && e.quality == sel.textureQuality) out.push_back(e);
                else if (e.kind == "sprite" && e.quality == sel.spriteQuality) out.push_back(e);
                break;
        }
    }
    // Fetch order = base content FIRST, regardless of the manifest's line order. The BASE game
    // (root/: data.ini, data.zip, maps/model/...) is essential; the quality packs (multi-GB
    // texture/sprite.zip) are enhancement. A downloads.list that lists a 1.6 GB sprite.zip before
    // root/ meant an interrupted download got the enhancement but NOT the base game -> "скачал, но
    // подключить не смог" (S. 2026-07-24). Rank Root < Events < Quality; stable so same-rank keeps
    // manifest order.
    auto rank = [](PatchCat c) {
        switch (c) {
            case PatchCat::Root:    return 0;
            case PatchCat::Events:  return 1;
            case PatchCat::Quality: return 2;
            default:                return 3;  // Platform (rarely reaches here)
        }
    };
    std::stable_sort(out.begin(), out.end(),
                     [&](const PatchEntry& a, const PatchEntry& b) { return rank(a.category) < rank(b.category); });
    return out;
}

}  // namespace uaro
