#include "patcher/ContentQuality.hpp"

#include "patcher/PlatformId.hpp"  // cpuArch(), linuxDistroFromOsRelease()

#if defined(__linux__) && !defined(__ANDROID__)
#include <fstream>
#include <sstream>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace uaro {

std::string defaultQuality(const std::string& platform) {
    // S. 2026-07-28: 1k is the DEFAULT tier for EVERYONE now -- both textures and sprites ("и тоже самое
    // для текстур"). 2k/4k are an explicit opt-in in Settings. (Was: 2k on desktop x64/arm-mac + HD
    // consoles, 1k elsewhere.)
    (void)platform;
    return "1k";
}

bool isBundledPlatform(const std::string& platform) {
    // ONLY the closed consoles ship their base content inside the store package and fetch events/* only.
    // iOS + Android are NOT bundled — they still download root/ + the quality packs (4k/2k/1k) like
    // desktop (S. 2026-07-24 correction: "для iOS, Android загрузку с 4k/2k/1k/root нужно выполнять").
    return platform == "xbox" || platform == "ps4" || platform == "ps5" || platform == "switch";
}

bool isBakedTokenPlatform(const std::string& platform) {
    // Consoles + iOS + Steam Deck ship all content inside the package -> no patcher at runtime. (S.
    // 2026-07-29: "Steam Deck - патчер не нужен, в нем все пакеты зашиты".) Android is NOT here: it still
    // patches. MSIX/Flatpak share the desktop token and are caught by fs::is_packaged_install() at the
    // call site.
    return isBundledPlatform(platform) || platform == "iphone" || platform == "ipad" ||
           platform == "steamdeck";
}

bool isMobileStorePlatform(const std::string& platform) {
    return platform == "iphone" || platform == "ipad" || platform.rfind("android-", 0) == 0;
}

std::string resolveQuality(const std::string& configValue, const std::string& platform, bool isSprite) {
    if (configValue == "1k" || configValue == "2k" || configValue == "4k") return configValue;  // explicit choice wins
    // Both sprites AND textures default to 1k for EVERYONE now (S. 2026-07-28); 2k/4k are an explicit
    // opt-in in Settings. isSprite is kept so the two can diverge again later without touching callers.
    // (Bundled consoles don't download quality packs at all -- they use their baked content -- so this
    // default only affects the platforms that fetch <q>/texture.zip or <q>/sprite.zip.)
    if (isSprite) return "1k";
    return defaultQuality(platform);  // now also 1k for every platform
}

// --- runtime detection ---------------------------------------------------------------------------

namespace {
#if defined(__linux__) && !defined(__ANDROID__)
// A Steam Deck reports ID=steamos in /etc/os-release (the OS signature S. asked to key on). SteamOS
// derivatives list it in ID_LIKE; linuxDistroFromOsRelease() already handles both. The DMI product
// name ("Jupiter"=LCD, "Galileo"=OLED) is a secondary signal for a non-SteamOS install on Deck HW.
bool isSteamDeck() {
    std::ifstream os("/etc/os-release");
    if (os) {
        std::stringstream ss;
        ss << os.rdbuf();
        if (linuxDistroFromOsRelease(ss.str()) == "steamos") return true;
    }
    std::ifstream dmi("/sys/devices/virtual/dmi/id/product_name");
    if (dmi) {
        std::string name;
        std::getline(dmi, name);
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' '))
            name.pop_back();
        if (name == "Jupiter" || name == "Galileo") return true;
    }
    return false;
}
#endif
}  // namespace

std::string contentPlatformId() {
    const std::string arch = cpuArch();  // "x64" | "arm" | ""

#if defined(CLIENT_XBOX)
    return "xbox";
#elif defined(CLIENT_PS5)
    return "ps5";
#elif defined(CLIENT_PS4)
    return "ps4";
#elif defined(CLIENT_SWITCH)
    return "switch";
#elif defined(__ANDROID__)
    return arch.empty() ? std::string() : "android-" + arch;
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
    // iPad reports a "pad" idiom; without UIKit here we can't tell — default both to iphone's 1k tier
    // (identical quality), so the plain token is fine for the quality decision.
    return "iphone";
#else
    return arch.empty() ? std::string() : "macos-" + arch;
#endif
#elif defined(_WIN32)
    return arch.empty() ? std::string() : "windows-" + arch;
#elif defined(__linux__)
    if (arch.empty()) return "";
    if (arch == "x64" && isSteamDeck()) return "steamdeck";  // Deck HW is x64 but wants the 1k default
    return "linux-" + arch;
#else
    return "";
#endif
}

}  // namespace uaro
