#pragma once
// NxConsoleServices -- Nintendo Switch implementation of the uaro::ConsoleServices interface
// (save-data containers + user/account + lifecycle). SCAFFOLD: the .cpp fills the `// TODO(SDK):`
// points with nn::fs / nn::account / nn::oe. Install it before Application::run() from NxMain.cpp:
//
//     static uaro::NxConsoleServices g_nx;
//     uaro::setConsoleServices(&g_nx);   // routes settings/hotbar saves through the NX containers
//
// Nothing here includes a proprietary SDK header; the SDK types stay in the .cpp.
#include "platform/ConsoleServices.hpp"

namespace uaro {

class NxConsoleServices final : public ConsoleServices {
public:
    // Mount the active user's save-data container. Call once at startup (before app.run) after the
    // user is selected. Returns false if no user / mount failed (title should show the account UI).
    bool mount();
    void unmount();

    // ConsoleServices ---------------------------------------------------------
    void onLifecycle(std::function<void(AppLifecycle)> cb) override { lifecycleCb_ = std::move(cb); }
    bool saveWrite(const std::string& key, const std::vector<u8>& data) override;
    bool saveRead(const std::string& key, std::vector<u8>& out) override;
    bool saveExists(const std::string& key) override;
    std::string activeUserName() override { return userName_; }
    bool onlineAllowed() override { return onlineAllowed_; }
    std::string contentCacheDir() override;  // writable cache-storage mount for event packs (TODO(SDK))

    // Called from NxPlatform::pump() when nn::oe reports a state change, so the engine can pause the
    // frame/audio on Suspended and reacquire the swapchain on Resumed.
    void dispatchLifecycle(AppLifecycle s) { if (lifecycleCb_) lifecycleCb_(s); }

private:
    std::function<void(AppLifecycle)> lifecycleCb_;
    std::string userName_;
    bool onlineAllowed_ = false;
    bool mounted_ = false;
};

} // namespace uaro
