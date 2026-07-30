#pragma once
// PsConsoleServices -- PlayStation (PS5/PS4) implementation of uaro::ConsoleServices (save-data +
// user/account + lifecycle). SCAFFOLD: the .cpp fills the `// TODO(SDK):` points with sceSaveData /
// sceUserService / sceSystemService. Install it before Application::run() from PsMain.cpp:
//
//     static uaro::PsConsoleServices g_ps;
//     uaro::setConsoleServices(&g_ps);
//
// No proprietary SDK header here; the SDK types stay in the .cpp.
#include "platform/ConsoleServices.hpp"

namespace uaro {

class PsConsoleServices final : public ConsoleServices {
public:
    // Resolve the initial user + prepare the save-data directory. Call once at startup (before
    // app.run). Returns false if no user / save-data unavailable.
    bool mount();
    void unmount();

    // ConsoleServices ---------------------------------------------------------
    void onLifecycle(std::function<void(AppLifecycle)> cb) override { lifecycleCb_ = std::move(cb); }
    bool saveWrite(const std::string& key, const std::vector<u8>& data) override;
    bool saveRead(const std::string& key, std::vector<u8>& out) override;
    bool saveExists(const std::string& key) override;
    std::string activeUserName() override { return userName_; }
    bool onlineAllowed() override { return onlineAllowed_; }
    std::string contentCacheDir() override;  // writable temp/download area for event packs (TODO(SDK))

    // Called from PsPlatform::pump() on a sceSystemService lifecycle event.
    void dispatchLifecycle(AppLifecycle s) { if (lifecycleCb_) lifecycleCb_(s); }

private:
    std::function<void(AppLifecycle)> lifecycleCb_;
    std::string userName_;
    bool onlineAllowed_ = false;
    int userId_ = 0;      // SceUserServiceUserId of the initial user
    bool mounted_ = false;
};

} // namespace uaro
