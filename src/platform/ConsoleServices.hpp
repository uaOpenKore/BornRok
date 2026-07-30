#pragma once
// ConsoleServices -- platform capabilities that consoles REQUIRE beyond the desktop
// Platform (lifecycle suspend/resume, save-data containers, user/account). Game code
// calls this uniformly on every platform; desktop uses the file-backed no-op
// DesktopConsoleServices, and each console backend (console/<vendor>/) supplies a real
// implementation on its (licensed) SDK. NOTHING here touches a proprietary SDK.
//
// Access the active instance via consoleServices(); a console backend installs its own
// with setConsoleServices() at startup. Default = DesktopConsoleServices. See
// docs/console-port-plan-km.md.
#include <functional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Lifecycle transitions the OS can force on a console title. The engine pauses the
// render loop + audio on Constrained/Suspended and reacquires devices on Resumed.
enum class AppLifecycle {
    Running,
    Constrained,   // backgrounded / overlay up: keep state, throttle
    Suspended,     // console sleeping: flush saves, stop the frame
    Resumed,       // woken: reacquire swapchain/devices, continue
    QuitRequested, // user/system asked to exit
};

// Abstract console services. Desktop provides a trivial file-backed implementation so
// app/game code can call these on every platform without #ifdefs.
class ConsoleServices {
public:
    virtual ~ConsoleServices() = default;

    // --- Lifecycle ---
    // Callback fired when the OS changes the app lifecycle state. Desktop never fires
    // (always Running); consoles drive it from their event pump.
    virtual void onLifecycle(std::function<void(AppLifecycle)> cb) = 0;

    // --- Save-data storage ---
    // Consoles store user data in SDK "containers", NOT loose files. These map our
    // settings/hotbar/config blobs onto whatever the platform mandates. `key` is a stable
    // logical name ("settings/game.cfg", "hotbar", ...). Return false on failure.
    virtual bool saveWrite(const std::string& key, const std::vector<u8>& data) = 0;
    virtual bool saveRead(const std::string& key, std::vector<u8>& out) = 0;
    virtual bool saveExists(const std::string& key) = 0;

    // --- User / account ---
    virtual std::string activeUserName() = 0;  // "" if none / n/a
    virtual bool onlineAllowed() = 0;          // multiplayer privilege; desktop = true

    // --- Downloadable event content (S.: consoles need to add/remove content for live events) ---
    // A WRITABLE directory where the event-content downloader drops data-only packs (.zip/.grf/loose)
    // fetched from our CDN; the VFS mounts everything here ON TOP of the baked title content so an event
    // pack overrides/extends it, and removing the pack reverts. On desktop this is a real folder under
    // the pref dir. On console it is the platform's writable cache/additional-data mount (NOT the
    // read-only title image). Return "" when there is no writable content cache (default) -> the event
    // downloader is simply skipped. Never returns a path to executable code; data packs only.
    virtual std::string contentCacheDir() { return {}; }
};

// Desktop / dev fallback: lifecycle is always Running, saves are plain files under the
// pref dir where the `key` IS the pref-relative path (pref_dir("uaro","client")/<key>),
// so routing existing configs through save*() is byte-identical to today. Single implicit
// user, online always allowed. This is what every non-console build uses.
class DesktopConsoleServices final : public ConsoleServices {
public:
    void onLifecycle(std::function<void(AppLifecycle)>) override {}  // never transitions
    bool saveWrite(const std::string& key, const std::vector<u8>& data) override;
    bool saveRead(const std::string& key, std::vector<u8>& out) override;
    bool saveExists(const std::string& key) override;
    std::string activeUserName() override { return {}; }
    bool onlineAllowed() override { return true; }
    std::string contentCacheDir() override;  // pref_dir/"content" (created on demand)
};

// The active services. Defaults to a shared DesktopConsoleServices; a console backend
// installs its own before the engine starts. Never returns null.
ConsoleServices& consoleServices();
void setConsoleServices(ConsoleServices* svc);  // nullptr restores the desktop default

} // namespace uaro
