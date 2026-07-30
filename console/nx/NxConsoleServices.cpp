// NxConsoleServices.cpp -- ConsoleServices on the Nintendo Switch SDK (save-data + account).
//
// Implements save-data persistence through nn::fs save-data containers and user
// identity through nn::account. The engine already routes settings/game.cfg and
// hotbar through consoleServices().saveWrite/saveRead under CLIENT_CONSOLE, so
// these three methods are ALL that is needed for config+hotbar persistence on NX.
//
// IMPORTANT: After every saveWrite() you MUST nn::fs::Commit() the mount,
// otherwise data is lost on power-off.

#include "NxConsoleServices.hpp"
#include "core/Log.hpp"

#include <nn/account.h>
#include <nn/fs.h>
#include <nn/fs/fs_File.h>
#include <nn/fs/fs_FileSystem.h>
#include <nn/fs/fs_CacheStorage.h>

namespace uaro {

namespace {
    constexpr char kMount[]  = "save";   // mount name -> paths are "save:/<key>"
    constexpr char kCache[]  = "evcache";

    std::string savePath(const std::string& key) {
        return std::string(kMount) + ":/" + key;
    }

    // Ensure parent directories exist for the given path. nn::fs paths use '/'
    // as separator; the save container supports directories.
    bool ensureParentDirs(const std::string& path) {
        // Find the last '/' to isolate the directory portion.
        auto pos = path.rfind('/');
        if (pos == std::string::npos || pos == 0) return true;
        std::string dir = path.substr(0, pos);
        // CreateDirectory returns ResultPathAlreadyExists if the dir exists,
        // which is fine -- just ignore that error.
        nn::fs::CreateDirectory(dir.c_str());
        return true;
    }

    // Map nn::Result to our simple bool return, logging failures.
    bool checkResult(nn::Result r, const char* op, const std::string& key) {
        if (r.IsSuccess()) return true;
        log::error("nx: {} '{}' failed (0x{:08x})", op, key, r.GetValue());
        return false;
    }
} // namespace

bool NxConsoleServices::mount() {
    // --- Account: get the active user ---
    nn::account::Initialize();

    nn::account::Uid uid{};
    nn::account::UserHandle userHandle{};

    bool haveUser = nn::account::TryOpenPreselectedUser(&userHandle);
    if (haveUser) {
        nn::Result r = nn::account::GetUserId(&uid, userHandle);
        if (r.IsFailure()) {
            nn::account::CloseUser(userHandle);
            haveUser = false;
        }
    }

    if (!haveUser) {
        // Show the user selector applet.
        nn::account::UserSelectionSettings settings{};
        settings.isSkipEnabled = false;
        nn::Result r = nn::account::ShowUserSelector(&uid, settings);
        if (r.IsFailure()) {
            log::error("nx: no user selected -- save-data unavailable");
            return false;
        }
    }

    // Get the user's nickname for display.
    nn::account::Nickname nickname{};
    nn::Result r = nn::account::GetNickname(&nickname, uid);
    if (r.IsSuccess()) {
        userName_ = nickname.name;
    }

    // --- Save-data: ensure and mount the container ---
    r = nn::fs::EnsureSaveData(uid);
    if (!checkResult(r, "EnsureSaveData", "")) return false;

    r = nn::fs::MountSaveData(kMount, uid);
    if (!checkResult(r, "MountSaveData", "")) return false;

    mounted_ = true;
    log::info("nx: save-data mounted for user '{}'", userName_);
    return true;
}

void NxConsoleServices::unmount() {
    if (!mounted_) return;
    nn::fs::Commit(kMount);
    nn::fs::Unmount(kMount);
    mounted_ = false;
    log::info("nx: save-data unmounted");
}

bool NxConsoleServices::saveWrite(const std::string& key, const std::vector<u8>& data) {
    if (!mounted_) return false;
    const std::string path = savePath(key);

    // Ensure parent directory exists.
    ensureParentDirs(path);

    // Delete existing file (ignore if absent), then create.
    nn::fs::DeleteFile(path.c_str());  // may fail silently
    nn::Result r = nn::fs::CreateFile(path.c_str(), s64(data.size()));
    if (!checkResult(r, "CreateFile", key)) return false;

    nn::fs::FileHandle handle{};
    r = nn::fs::OpenFile(&handle, path.c_str(), nn::fs::OpenMode_Write);
    if (!checkResult(r, "OpenFile(write)", key)) return false;

    r = nn::fs::WriteFile(handle, 0, data.data(), data.size(),
                          nn::fs::WriteOption::MakeValue(
                              nn::fs::WriteOptionFlag_Flush));
    nn::fs::CloseFile(handle);
    if (!checkResult(r, "WriteFile", key)) return false;

    // MANDATORY: commit or the write is lost on power-off.
    nn::fs::Commit(kMount);
    return true;
}

bool NxConsoleServices::saveRead(const std::string& key, std::vector<u8>& out) {
    if (!mounted_) return false;
    const std::string path = savePath(key);

    nn::fs::FileHandle handle{};
    nn::Result r = nn::fs::OpenFile(&handle, path.c_str(), nn::fs::OpenMode_Read);
    if (r.IsFailure()) return false;  // file not found is not an error

    s64 fileSize = 0;
    r = nn::fs::GetFileSize(&fileSize, handle);
    if (r.IsFailure()) { nn::fs::CloseFile(handle); return false; }

    out.resize(size_t(fileSize));
    r = nn::fs::ReadFile(handle, 0, out.data(), out.size());
    nn::fs::CloseFile(handle);
    return r.IsSuccess();
}

bool NxConsoleServices::saveExists(const std::string& key) {
    if (!mounted_) return false;
    const std::string path = savePath(key);

    nn::fs::DirectoryEntryType type{};
    nn::Result r = nn::fs::GetEntryType(&type, path.c_str());
    return r.IsSuccess() && type == nn::fs::DirectoryEntryType_File;
}

std::string NxConsoleServices::contentCacheDir() {
    // Mount a cache storage area for downloadable event-content packs.
    // Cache storage is separate from save data; the OS may evict it under
    // memory pressure.
    nn::Result r = nn::fs::MountCacheStorage(kCache);
    if (r.IsFailure()) {
        log::warn("nx: MountCacheStorage failed (0x{:08x})", r.GetValue());
        return {};  // event content disabled
    }
    return std::string(kCache) + ":/";
}

} // namespace uaro
