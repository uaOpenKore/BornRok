#pragma once
// Content quality resolution for the patcher (#71 rework, S. 2026-07-16).
//
// The player picks a Texture and a Sprite quality (1k or 4k) in the ESC menu; the patcher then fetches
// the matching archive from downloads.list — Texture -> "<q>/texture.zip", Sprite -> "<q>/sprite.zip"
// (q = "1k" | "4k"; the 2k tier was dropped — every former 2k rule now maps to 1k). When the config has
// no explicit value the quality falls back to a per-platform default, then to 1k.
//
// A distinct concept from PlatformId::currentPlatform() (which yields the manifest BINARY token like
// "win-x64"): this token drives DEFAULTS + the bundled decision, in the vocabulary S. specified.
//
// "Bundled" platforms ship their base content inside the store package and fetch ONLY the event packs
// (events/*.zip) at runtime — the closed consoles Xbox / PS4 / PS5 / Switch. iOS + Android are NOT
// bundled: they still download root/ + the quality packs (4k/2k/1k). Everyone downloads events.
//
// Pure functions (core only) so the resolution table is unit-tested offline.
#include <string>

namespace uaro {

// Canonical content-platform token (S.'s vocabulary), e.g. "windows-x64", "linux-arm", "steamdeck",
// "iphone", "switch", "xbox". Empty string => unknown (treated as the 1k default, not bundled).
// Compile-time OS/arch + a runtime SteamDeck probe (SteamOS signature). Not pure (reads the OS).
std::string contentPlatformId();

// The default quality ("1k" or "4k") for a content-platform token, per S.'s table:
//   4k: windows-x64, linux-x64, macos-x64, macos-arm, and the 4k-baked consoles ps4/ps5/xbox
//   1k: windows-arm, linux-arm, android-x64 (were 2k), android-arm, iphone, ipad, steamdeck, switch
//   1k: anything else / unknown
std::string defaultQuality(const std::string& platform);

// Base content ships inside the distribution -> fetch ONLY events at runtime. xbox / ps4 / ps5 / switch.
bool isBundledPlatform(const std::string& platform);

// A distribution whose content is fully baked in and must NOT run the patcher, classified purely on the
// content-platform TOKEN: the closed consoles (xbox/ps4/ps5/switch) + iOS (iphone/ipad) + Steam Deck.
// MSIX/Flatpak share the desktop token, so their baked-ness is detected separately via
// fs::is_packaged_install() — combine the two at the call site. (S. 2026-07-29: no patcher for
// consoles/iOS/Steam Deck/MSIX/Flatpak; only a loose desktop exe and Android still patch.)
bool isBakedTokenPlatform(const std::string& platform);

// iOS + Android: the app binary ships inside the store package, so the patcher must NOT download
// platforms/<token> — but it STILL downloads root/ + the quality packs + events (unlike bundled
// consoles). True for iphone/ipad and any android-<arch> token.
bool isMobileStorePlatform(const std::string& platform);

// Final quality: an explicit config value ("1k"/"4k") wins; otherwise the platform default; else "1k".
// An unrecognised configValue (anything but "1k"/"4k", incl. the old "2k") is ignored -> default.
// isSprite: sprites default to 1k for every platform (2k/4k are an explicit opt-in); textures keep the
// per-platform default. An explicit config value ("1k"/"2k"/"4k") always wins regardless.
std::string resolveQuality(const std::string& configValue, const std::string& platform, bool isSprite = false);

}  // namespace uaro
