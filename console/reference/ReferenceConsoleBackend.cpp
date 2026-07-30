// ReferenceConsoleBackend.cpp -- the SHAPE of a console platform backend, SDK-free.
//
// Copy this into console/<vendor>/ and fill the `// TODO(SDK):` points with the vendor's
// (licensed) API. It shows how a console plugs into the SAME engine entry points the SDL
// and WinRT backends use -- so game logic (app/, game/, render/) stays untouched.
//
// This file is a SCAFFOLD: it is NOT in any CMake target and does NOT include any
// proprietary header. It won't (and shouldn't) compile as-is -- the TODO points are
// where the real SDK types go. See docs/console-port-plan-km.md.

#include "platform/ConsoleServices.hpp"  // now a real compiled unit under src/platform/
#include "platform/Input.hpp"
// #include "app/Application.hpp"   // the engine's frame driver (as used by SDL/WinRT)

namespace uaro::console_ref {

// --- 1. Console services on the vendor SDK ---------------------------------------
// Real impl of the extra capabilities the game calls uniformly (see ConsoleServices.hpp).
class ConsoleServicesImpl final : public ConsoleServices {
public:
    void onLifecycle(std::function<void(AppLifecycle)> cb) override { lifecycleCb_ = std::move(cb); }

    bool saveWrite(const std::string& /*key*/, const std::vector<unsigned char>& /*data*/) override {
        // TODO(SDK): open/create the user's save container, write the blob, commit.
        return false;
    }
    bool saveRead(const std::string& /*key*/, std::vector<unsigned char>& /*out*/) override {
        // TODO(SDK): mount the save container, read the blob by logical key.
        return false;
    }
    bool saveExists(const std::string& /*key*/) override { return false; /* TODO(SDK) */ }

    std::string activeUserName() override { return {}; /* TODO(SDK): active user's name */ }
    bool onlineAllowed() override { return false; /* TODO(SDK): online-play privilege */ }

    // Called from the OS event pump below when the system changes app state.
    void dispatchLifecycle(AppLifecycle s) { if (lifecycleCb_) lifecycleCb_(s); }

private:
    std::function<void(AppLifecycle)> lifecycleCb_;
};

// --- 2. Platform backend: OS init + event/input pump -----------------------------
// Mirrors platform/sdl/SdlPlatform.cpp: owns the swapchain window and fills InputState.
class ConsolePlatform {
public:
    bool init() {
        // TODO(SDK): init the graphics/display, create the native swapchain surface, set
        // up the pad/user/hid subsystems. Hand the native window handle to bgfx::Init via
        // bgfx::PlatformData.nwh (renderer = the vendor's bgfx console backend: AGC/GNM/NVN
        // for PS/Switch, D3D12 for Xbox).
        return true;
    }

    // Pump OS + pad events into `state`; return false to quit. Called once per frame.
    bool pump(InputState& state) {
        // TODO(SDK): drain the system event queue -> translate lifecycle transitions into
        // services_.dispatchLifecycle(...) (Suspended/Resumed/Constrained/QuitRequested).

        // TODO(SDK): read the active gamepad and map it onto our neutral InputState.
        // The mapping is already designed (docs/gamepad-buttons.md); only the source API
        // differs per vendor. Example shape:
        InputState::Gamepad& pad = state.pad;
        pad.connected = true;                 // TODO(SDK): real connection state
        pad.type = InputState::PadType::Generic; // set Xbox/PlayStation/Nintendo per vendor
        // pad.lx/ly/rx/ry  <- sticks (deadzoned, up = +y)
        // pad.buttons      <- face/dpad/shoulder/stick-click bitset (same layout as SDL)
        // (Switch handheld) state.touch <- fill fingers, reusing the touch mode (#114)

        return !quitRequested_;
    }

    ConsoleServicesImpl& services() { return services_; }

private:
    ConsoleServicesImpl services_;
    bool quitRequested_ = false;
};

// --- 3. Entry point + main loop --------------------------------------------------
// The vendor entry point (name/signature differs per SDK) does what SDL_main / WinRT Run()
// do: create the platform, hand it to the engine, run update+render+present each frame.
int consoleMain() {
    ConsolePlatform platform;
    if (!platform.init()) return 1;

    // uaro::Application app;                      // same engine object as desktop/Android
    // app.setConsoleServices(&platform.services()); // TODO: wire ConsoleServices into app
    // app.init(...);

    InputState state{};
    for (;;) {
        if (!platform.pump(state)) break;          // OS events + input + lifecycle
        // app.update(state);                       // game logic (backend-agnostic)
        // app.render();                            // bgfx frame on the console renderer
        // bgfx::frame();                           // present
    }
    // app.shutdown();
    return 0;
}

} // namespace uaro::console_ref
