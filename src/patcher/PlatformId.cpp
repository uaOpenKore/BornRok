#include "patcher/PlatformId.hpp"

#include <sstream>

namespace uaro {

const char* cpuArch() {
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
    return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm";
#else
    return "";
#endif
}

std::string linuxDistroFromOsRelease(const std::string& text) {
    // Look for an ID= / ID_LIKE= line; values may be quoted. We map the IDs we ship
    // patches for; everything else returns "" (unknown -> root-only).
    auto valueOf = [&](const std::string& key) -> std::string {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind(key + "=", 0) != 0) continue;
            std::string v = line.substr(key.size() + 1);
            if (!v.empty() && v.back() == '\r') v.pop_back();
            if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
                v = v.substr(1, v.size() - 2);
            return v;
        }
        return "";
    };
    auto matches = [](const std::string& v) -> std::string {
        if (v == "ubuntu" || v == "debian" || v == "fedora" || v == "steamos") return v;
        return "";
    };
    const std::string id = valueOf("ID");
    if (std::string m = matches(id); !m.empty()) return m;
    // ID_LIKE can list parents (e.g. SteamOS derivatives, ubuntu derivatives).
    std::istringstream like(valueOf("ID_LIKE"));
    std::string tok;
    while (like >> tok)
        if (std::string m = matches(tok); !m.empty()) return m;
    return "";
}

std::string currentPlatform() {
    const std::string arch = cpuArch();
    if (arch.empty()) return "";

#if defined(__ANDROID__)
    return "android-" + arch;
#elif defined(_WIN32)
    return "win-" + arch;
#elif defined(__linux__)
    // ONE folder per CPU type, not per distro -- ubuntu/fedora/debian/steamos all map to linux-<arch>
    // (S. 2026-07-08: "убрать разделение линуксов, смотреть в одну папку linux-x64/linux-arm"). The
    // downloads.list now uses platforms/linux-x64 / platforms/linux-arm. (linuxDistroFromOsRelease is
    // kept for tests / potential future per-distro needs, but no longer drives the token.)
    return "linux-" + arch;
#else
    return "";  // macOS/consoles/other: not in the supported patch set (root-only)
#endif
}

}  // namespace uaro
