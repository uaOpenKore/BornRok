#pragma once
#include <string>

#include "core/Types.hpp"

namespace uaro {

struct WindowConfig {
    std::string title = "RoLegacy";
    int width = 1280;
    int height = 720;
    bool resizable = true;
};

// Native handles the renderer (bgfx) needs to bind a swap chain to this window.
//   nwh = native window handle  (HWND / NSWindow* / X11 Window / wl_surface*)
//   ndt = native display type   (X11 Display* / wl_display*; null on Win/macOS)
struct NativeWindow {
    void* nwh = nullptr;
    void* ndt = nullptr;
};

// Thin RAII wrapper over an SDL_Window. Owns no rendering context — bgfx draws
// straight to the native surface, so the window only provides a handle + size.
class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool open(const WindowConfig& cfg);
    void close();

    NativeWindow native() const;
    void pixelSize(int& w, int& h) const;
    void setFullscreen(bool on);  // borderless-desktop fullscreen toggle (#104)
    // Start/stop SDL text input -> on mobile this raises/hides the native OS keyboard. Driven by the
    // focused text field (#102). No-op if unchanged. Tracked so we don't re-trigger the keyboard.
    void setTextInput(bool on);
    bool valid() const { return handle_ != nullptr; }
    void* sdlWindow() const { return handle_; }

private:
    void* handle_ = nullptr;  // SDL_Window*
    bool textInputOn_ = false;
};

} // namespace uaro
