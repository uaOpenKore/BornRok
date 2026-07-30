#include "patcher/PatcherConfig.hpp"

#include <cctype>

#include "core/Types.hpp"

namespace uaro {
namespace {

std::string trim(const std::string& s) {
    usize a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

PatcherConfig PatcherConfig::defaults() {
    // Baked into the exe (used when no on-disk patcher/patcher.cfg overrides it). Mirrors are tried in
    // order; the GDrive open?id= link is resolved to a direct-download URL by GDriveUrl (S.).
    return PatcherConfig{{
        // Primary: the real downloads.list on Google Drive. STATIC id (S. 2026-07-08 -- won't change
        // again). Each row carries a per-file Drive mirror.
        "https://drive.google.com/open?id=16DbJROqte1a-gHnJ9y3hlBztLGbtMfWi",
        "http://patcher.bornrok.com/downloads.list",
        "https://patcher.bornrok.com/downloads.list",
    }};
}

PatcherConfig PatcherConfig::parse(const std::string& text) {
    PatcherConfig cfg;
    usize i = 0;
    while (i < text.size()) {
        usize nl = text.find('\n', i);
        if (nl == std::string::npos) nl = text.size();
        std::string line = trim(text.substr(i, nl - i));
        i = nl + 1;
        if (line.empty() || line[0] == '#') continue;
        cfg.manifestUrls.push_back(line);
    }
    return cfg;
}

std::string PatcherConfig::serialize() const {
    std::string out =
        "# uaRO patcher config. One downloads.list mirror URL per line, tried in order.\n";
    for (const std::string& u : manifestUrls) out += u + "\n";
    return out;
}

}  // namespace uaro
