// PsConsoleServices.cpp -- ConsoleServices on the PlayStation SDK (save-data + user/account).
//
// SCAFFOLD. NOT in any CMake target, includes NO proprietary SDK header, does NOT compile as-is.
// Fill every `// TODO(SDK):` with sce* on the licensed build machine.
//
// The engine already calls consoleServices().saveWrite/saveRead for settings/game.cfg and the
// per-character hotbar (Application::saveConfig/loadConfig, GameScene::writeHotbar/loadHotbar),
// gated by CLIENT_CONSOLE. Implementing these three methods is ALL that is needed for config +
// hotbar persistence on PS -- no game code changes.
//
// Save-data model on PlayStation: mount a save-data directory for the user with sceSaveDataMount
// (MODE_CREATE2 the first time, then MODE_RDWR), which gives a mount point path; our logical `key`
// becomes a file UNDER that path. Unmount (which commits) after writes; the OS shows the
// "saving..." icon during a commit-on-unmount.

#include "PsConsoleServices.hpp"
#include "core/Log.hpp"

// TODO(SDK): Sony SDK headers (NDA, licensed machine only):
//   #include <save_data.h>     // sceSaveDataMount / Unmount / directory
//   #include <user_service.h>  // sceUserServiceGetInitialUser / GetUserName
//   #include <system_service.h>

namespace uaro {

namespace {
// The save-data directory name for the title (one dir holds all our keys as files).
constexpr char kSaveDir[] = "UARO00";  // TODO(SDK): match the title's save-data spec / titleId

// Set by mount() to the OS-provided mount point (e.g. "/savedata0"). Keys are files under it.
std::string g_mountPoint;

std::string savePath(const std::string& key) { return g_mountPoint + "/" + key; }
} // namespace

bool PsConsoleServices::mount() {
    // TODO(SDK): user + username:
    //   sceUserServiceGetInitialUser(&userId_);
    //   SceUserServiceUserName n; sceUserServiceGetUserName(userId_, &n); userName_ = n.data;
    // TODO(SDK): mount (create on first run) the save-data directory:
    //   SceSaveDataMount mount = { userId_, kSaveDir, SCE_SAVE_DATA_MOUNT_MODE_CREATE2|RDWR, size };
    //   SceSaveDataMountResult r; sceSaveDataMount2(&mount, &r);   -> g_mountPoint = r.mountPoint.data;
    // TODO(SDK): online privilege (PS+ / parental) for onlineAllowed_ via sceNp... when networking lands.
    mounted_ = false;  // set true once the mount succeeds and g_mountPoint is filled
    log::info("ps: save-data mount (TODO(SDK): sceUserService user + sceSaveDataMount)");
    return mounted_;
}

void PsConsoleServices::unmount() {
    if (!mounted_) return;
    // TODO(SDK): sceSaveDataUmount(g_mountPoint) -- this COMMITS the save (OS shows the saving icon).
    mounted_ = false;
    g_mountPoint.clear();
}

bool PsConsoleServices::saveWrite(const std::string& key, const std::vector<u8>& data) {
    if (!mounted_) return false;
    const std::string path = savePath(key);
    // TODO(SDK): create parent dirs (sceKernelMkdir), then standard file IO under the mount:
    //   int fd = sceKernelOpen(path, O_WRONLY|O_CREAT|O_TRUNC, 0600);
    //   sceKernelWrite(fd, data.data(), data.size()); sceKernelClose(fd);
    // The mount is committed on unmount(); for durability mid-session you may sceSaveDataUmount/Mount
    // around a write, but committing once at exit is the usual pattern for config/hotbar.
    (void)data;
    return false;
}

bool PsConsoleServices::saveRead(const std::string& key, std::vector<u8>& out) {
    if (!mounted_) return false;
    const std::string path = savePath(key);
    // TODO(SDK):
    //   int fd = sceKernelOpen(path, O_RDONLY, 0); if (fd < 0) return false;
    //   SceKernelStat st; sceKernelFstat(fd, &st); out.resize(st.st_size);
    //   sceKernelRead(fd, out.data(), st.st_size); sceKernelClose(fd); return true;
    (void)path; (void)out;
    return false;
}

bool PsConsoleServices::saveExists(const std::string& key) {
    if (!mounted_) return false;
    // TODO(SDK): SceKernelStat st; return sceKernelStat(savePath(key), &st) == SCE_OK;
    (void)key;
    return false;
}

std::string PsConsoleServices::contentCacheDir() {
    // Writable location for downloaded EVENT-content packs (Application mounts everything here on top of
    // the /app0 title content). DATA only (sprites/maps/scripts), never executable code.
    // TODO(SDK): use a writable download-data / temp area and return its mount path, e.g. the app's
    //   temporary storage "/temp0/" (fast, may be cleared) or a dedicated additional-content save-data
    //   mount for content that must persist across sessions:
    //   sceSaveDataMount2(... "UAROEV" ...); return r.mountPoint.data;  // e.g. "/savedata1"
    // Return "" to disable event content.
    return {};
}

} // namespace uaro
