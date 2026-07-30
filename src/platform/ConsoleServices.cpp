#include "platform/ConsoleServices.hpp"

#include <filesystem>
#include <fstream>

#include "platform/FileSystem.hpp"

namespace fs_std = std::filesystem;

namespace uaro {
namespace {

// The desktop backend treats a save `key` as a path RELATIVE TO the pref dir, so a key
// like "settings/game.cfg" lands on exactly the file the current config code already uses
// (pref_dir/settings/game.cfg). That makes routing existing configs through save*()
// byte-identical on desktop. Console backends instead keep the logical key and map it onto
// their container API.
std::string savePath(const std::string& key) {
    return (fs_std::path(fs::data_dir()) / key).string();
}

} // namespace

bool DesktopConsoleServices::saveWrite(const std::string& key, const std::vector<u8>& data) {
    const std::string path = savePath(key);
    std::error_code ec;
    fs_std::create_directories(fs_std::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty())
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

bool DesktopConsoleServices::saveRead(const std::string& key, std::vector<u8>& out) {
    auto blob = fs::read_file(savePath(key));
    if (!blob) return false;
    out = std::move(*blob);
    return true;
}

bool DesktopConsoleServices::saveExists(const std::string& key) {
    std::error_code ec;
    return fs_std::exists(savePath(key), ec);
}

std::string DesktopConsoleServices::contentCacheDir() {
    // Writable folder for downloaded event-content packs, under the pref dir (created on demand). The
    // VFS mounts everything here on top of the baked content. Desktop already has the PC patcher for
    // this, but exposing it uniformly lets the same event-downloader run on every platform.
    const std::string dir = (fs_std::path(fs::data_dir()) / "content").string();
    std::error_code ec;
    fs_std::create_directories(dir, ec);
    return dir;
}

// --- active-instance accessors ---
namespace {
DesktopConsoleServices g_desktop;   // default backend, always valid
ConsoleServices* g_active = &g_desktop;
} // namespace

ConsoleServices& consoleServices() { return *g_active; }

void setConsoleServices(ConsoleServices* svc) { g_active = svc ? svc : &g_desktop; }

} // namespace uaro
