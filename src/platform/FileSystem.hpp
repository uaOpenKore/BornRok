#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

// Filesystem helpers. v0 reads loose files from disk; the GRF-backed virtual
// filesystem (resource/Vfs) layers on top of this in v1.
namespace uaro::fs {

// Read an entire file into memory. Returns nullopt if it cannot be opened.
std::optional<std::vector<u8>> read_file(const std::string& path);

// Directory containing the executable (assets are resolved relative to it). READ-ONLY under a
// packaged install (MSIX WindowsApps) — use data_dir() for anything the client writes.
std::string base_dir();

// Writable data root for downloaded content, config and logs. Returns base_dir() when the exe
// directory is writable (portable / loose-exe build — unchanged behaviour), else a per-user
// writable directory (pref_dir). This is what makes the patcher work under MSIX, whose install
// directory is read-only (Windows transparently redirects the pref path into the package's
// LocalCache). No trailing slash.
std::string data_dir();

// True on a packaged, read-only DESKTOP install where the exe dir diverges from the writable data dir:
// MSIX (Windows) or Flatpak (Linux). False for a loose/portable desktop exe (data_dir()==base_dir()),
// and false on macOS / mobile / console backends (Android's writable-dir split is deliberately NOT
// counted here — the gate is baked in so callers can't mistake it for a package). Such installs ship
// all content baked in and must NOT run the patcher (S. 2026-07-29: "уберём патчер ... для мсих, флетпака").
inline bool is_packaged_install() {
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    auto strip = [](std::string s) {
        while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
        return s;
    };
    return strip(data_dir()) != strip(base_dir());
#else
    return false;  // mobile/console/macOS: classified by the content-platform token instead
#endif
}

// Writable per-user directory for config/saves.
std::string pref_dir(const std::string& org, const std::string& app);

// The user's preferred OS UI language as a 2-letter ISO code ("ru", "en", ...), or "" if unknown.
// Used to auto-pick the UI language on first run (no saved language in settings/game.cfg).
std::string system_language();

} // namespace uaro::fs
