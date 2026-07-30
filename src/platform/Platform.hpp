#pragma once
#include "platform/Input.hpp"
#include "platform/Window.hpp"

namespace uaro {

// Owns SDL initialisation, the main window, and the OS event pump. The rest of
// the engine talks to the OS only through this type.
class Platform {
public:
    Platform() = default;
    ~Platform();
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    bool init();
    void shutdown();

    bool createWindow(const WindowConfig& cfg);
    Window& window() { return window_; }

    // Pump OS events into `state`. Returns false when the app should quit.
    bool pump(InputState& state);

    // Show/hide the OS mouse cursor (the in-game scene hides it to draw the RO cursor
    // sprite, and restores it on exit).
    void setCursorVisible(bool visible);

    // Rumble the connected gamepad (#102/#115): low/high-frequency motor strength (0..65535)
    // for durationMs. No-op if no pad is present or it lacks rumble. (S.: haptics per event)
    void rumble(unsigned short lowFreq, unsigned short highFreq, unsigned int durationMs);

private:
    bool inited_ = false;
    Window window_;
};

} // namespace uaro
