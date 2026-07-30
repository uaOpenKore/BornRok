// SDL3 implementation of the platform abstraction (Window, Platform, fs).
// Built only in the full (non-core-only) configuration.
#if !defined(__ANDROID__)
#define SDL_MAIN_HANDLED  // desktop: our main() drives; on Android SDL's activity owns the entry
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // SDL_SetMainReady (declared here, not in SDL.h)

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/Log.hpp"
#include "platform/FileSystem.hpp"
#include "platform/Platform.hpp"

namespace uaro {

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------
namespace fs {

std::optional<std::vector<u8>> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;
    const std::streamsize size = in.tellg();
    in.seekg(0);
    std::vector<u8> data(static_cast<usize>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size))
        return std::nullopt;
    return data;
}

std::string base_dir() {
    const char* p = SDL_GetBasePath();  // owned by SDL, do not free
    return p ? std::string(p) : std::string("./");
}

std::string system_language() {
    // The user's preferred OS UI language as a 2-letter ISO code ("ru", "en", ...), or "" if
    // unknown. Cross-platform via SDL (Windows: GetUserPreferredUILanguages; Linux: $LANG/$LC_*).
    // Works before SDL_Init, like SDL_GetBasePath above. (S.: "автоопределение языка не работает".)
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    std::string code;
    if (locales && count > 0 && locales[0] && locales[0]->language)
        code = locales[0]->language;
    if (locales) SDL_free(locales);
    return code;
}

std::string pref_dir(const std::string& org, const std::string& app) {
    char* p = SDL_GetPrefPath(org.c_str(), app.c_str());
    std::string result = p ? std::string(p) : std::string("./");
    if (p) SDL_free(p);
    return result;
}

std::string data_dir() {
    // Resolved once: the exe directory if it is WRITABLE (portable / loose-exe build — the client's
    // existing behaviour, data beside the exe), otherwise the per-user writable dir (pref_dir). The
    // latter is the fix for a packaged MSIX install, whose exe directory (C:\Program Files\WindowsApps
    // \...) is read-only — the patcher couldn't write downloads/config/log there, so it silently did
    // nothing and dropped straight to the login screen (S. 2026-07-24). Windows transparently
    // redirects the pref path into the package's LocalCache, so this works under MSIX with no extra API.
    static const std::string cached = [] {
        std::string base = base_dir();
        if (!base.empty()) {
            std::error_code ec;
            const std::filesystem::path probe =
                std::filesystem::path(base) / ".bornrok_write_test";
            bool writable = false;
            { std::ofstream f(probe, std::ios::binary); writable = f.good() && (f << 'x').good(); }
            if (writable && std::filesystem::exists(probe, ec)) {
                std::filesystem::remove(probe, ec);
                std::string b = base;
                while (!b.empty() && (b.back() == '/' || b.back() == '\\')) b.pop_back();
                return b;
            }
            std::filesystem::remove(probe, ec);  // best-effort cleanup if it was partially created
        }
        std::string pref = pref_dir("uaro", "client");  // per-user, writable even under MSIX
        while (!pref.empty() && (pref.back() == '/' || pref.back() == '\\')) pref.pop_back();
        return pref.empty() ? base : pref;
    }();
    return cached;
}

} // namespace fs

// ---------------------------------------------------------------------------
// Native handle extraction (per platform, from SDL window properties)
// ---------------------------------------------------------------------------
namespace {
NativeWindow extract_native(SDL_Window* win) {
    NativeWindow nw;
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    if (!props) return nw;

#if defined(SDL_PLATFORM_WIN32)
    nw.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_MACOS)
    nw.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_ANDROID)
    // bgfx needs the ANativeWindow* or it builds a headless device and init fails (S. on-device log:
    // "window handle is not set, creating headless device" -> BGFX Init failed).
    nw.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_LINUX)
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "wayland") == 0) {
        nw.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        nw.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        // NOTE: bgfx's Wayland path may need a wl_egl_window; X11/XWayland is the
        // verified desktop path for v0. Revisit native Wayland in v6.
    } else {
        nw.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        auto x11win = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        nw.nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(x11win));
    }
#endif
    return nw;
}
} // namespace

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------
Window::~Window() { close(); }

bool Window::open(const WindowConfig& cfg) {
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (cfg.resizable) flags |= SDL_WINDOW_RESIZABLE;

    SDL_Window* win = SDL_CreateWindow(cfg.title.c_str(), cfg.width, cfg.height, flags);
    if (!win) {
        log::error("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }
    handle_ = win;
#if !defined(__ANDROID__)
    // Desktop: text input is always on (text events flow; hotkeys use keydown). On Android we leave it
    // OFF and let the focused text field raise the OS keyboard (Application drives setTextInput).
    SDL_StartTextInput(win);
    textInputOn_ = true;
#endif
    return true;
}

void Window::setTextInput(bool on) {
    if (!handle_ || on == textInputOn_) return;  // no-op if unchanged (don't re-trigger the keyboard)
    SDL_Window* win = static_cast<SDL_Window*>(handle_);
    if (on) SDL_StartTextInput(win);
    else SDL_StopTextInput(win);
    textInputOn_ = on;
}

void Window::close() {
    if (handle_) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(handle_));
        handle_ = nullptr;
    }
}

NativeWindow Window::native() const {
    return handle_ ? extract_native(static_cast<SDL_Window*>(handle_)) : NativeWindow{};
}

void Window::pixelSize(int& w, int& h) const {
    w = h = 0;
    if (handle_) SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(handle_), &w, &h);
}

void Window::setFullscreen(bool on) {
    if (handle_) SDL_SetWindowFullscreen(static_cast<SDL_Window*>(handle_), on);  // SDL3: bool arg (#104)
}

// ---------------------------------------------------------------------------
// Platform
// ---------------------------------------------------------------------------
Platform::~Platform() { shutdown(); }

bool Platform::init() {
#if !defined(__ANDROID__)
    SDL_SetMainReady();  // desktop: we ran main ourselves. On Android SDL's own main path did this.
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        log::error("SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    inited_ = true;
    log::info("SDL initialised (video driver: {})",
              SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?");
    return true;
}

void Platform::shutdown() {
    window_.close();
    if (inited_) {
        SDL_Quit();
        inited_ = false;
    }
}

bool Platform::createWindow(const WindowConfig& cfg) {
    return window_.open(cfg);
}

void Platform::setCursorVisible(bool visible) {
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

namespace {
SDL_Gamepad* g_pad = nullptr;  // the first opened gamepad (one-controller model, #102/#115)

InputState::PadType padTypeOf(SDL_Gamepad* g) {
    switch (SDL_GetGamepadType(g)) {
        case SDL_GAMEPAD_TYPE_XBOX360:
        case SDL_GAMEPAD_TYPE_XBOXONE:
            return InputState::PadType::Xbox;
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5:
            return InputState::PadType::PlayStation;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            return InputState::PadType::Nintendo;
        default:
            return InputState::PadType::Generic;
    }
}

// Deadzone + normalise a raw SDL stick axis (-32768..32767) to [-1,1]; inside the zone -> 0.
float padAxis(Sint16 raw) {
    constexpr float kDead = 0.20f;
    const float v = raw / 32767.0f;
    if (v > kDead) return (v - kDead) / (1.0f - kDead);
    if (v < -kDead) return (v + kDead) / (1.0f - kDead);
    return 0.0f;
}
}  // namespace

bool Platform::pump(InputState& state) {
    // Clear the per-frame fields; mouse position and button-held state persist.
    state.escape = state.resized = false;
    state.text.clear();
    state.keyBackspace = state.keyEnter = state.keyTab = state.keyDelete = false;
    state.keyLeft = state.keyRight = state.keyUp = state.keyDown = false;
    state.keyHome = state.keyEnd = state.keyInsert = false;
    state.hotkeyRow = state.hotkeyCol = -1;
    state.keyInventory = state.keyEquip = state.keyStatus = state.keySkills = false;
    state.keyBasicInfo = state.keyCart = state.keyBinds = state.keyFriends = false;
    state.keyChatRoom = state.keyGuild = state.keyBuyStore = state.keyParty = false;
    state.keyQuest = state.keyChatToggle = false;
    state.altDigit = -1;
    state.numDigit = -1;
    state.mousePressed = state.mouseReleased = state.mouseDoubleClick = false;
    state.closeRequested = false;
    state.wheel = 0.0f;
    state.mouseDX = state.mouseDY = 0;
    // Gamepad EDGE fields reset each frame (held/axes persist, refreshed below).
    state.pad.south = state.pad.east = state.pad.west = state.pad.north = false;
    state.pad.dpadUp = state.pad.dpadDown = state.pad.dpadLeft = state.pad.dpadRight = false;
    state.pad.l3 = state.pad.r3 = state.pad.start = state.pad.back = false;
    state.pad.touchPress = false;
    // Screen-touch EDGE fields reset each frame (held state + positions persist across frames).
    for (auto& fg : state.touch.f) { fg.down = fg.up = false; fg.dx = fg.dy = 0.0f; }
    // Touch-device presence (drives the auto-enable of touch mode). SDL may register the device
    // lazily, so poll until it appears; a finger event below also latches it on.
    if (!state.touch.present) {
        int nTouch = 0;
        if (SDL_TouchID* ids = SDL_GetTouchDevices(&nTouch)) SDL_free(ids);
        if (nTouch > 0) state.touch.present = true;
    }

    // Mouse events report window-coordinate (point) positions; convert to the
    // framebuffer pixel space the renderer/SpriteBatch use.
    float sx = 1.0f, sy = 1.0f;
    if (SDL_Window* win = static_cast<SDL_Window*>(window_.sdlWindow())) {
        int pw = 0, ph = 0, ww = 0, wh = 0;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        SDL_GetWindowSize(win, &ww, &wh);
        if (ww > 0) sx = static_cast<float>(pw) / ww;
        if (wh > 0) sy = static_cast<float>(ph) / wh;
    }

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
                state.quit = true;  // genuine termination (last window gone / SIGTERM)
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                // Alt+F4 / window X. Do NOT hard-quit — the game controls its own exit
                // (in-game ESC menu). Scenes read closeRequested and decide.
                state.closeRequested = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                switch (e.key.key) {
                    case SDLK_ESCAPE:    state.escape = true; break;
                    case SDLK_BACKSPACE: state.keyBackspace = true; break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:  state.keyEnter = true; break;
                    case SDLK_TAB:       state.keyTab = true; break;
                    case SDLK_DELETE:    state.keyDelete = true; break;
                    case SDLK_LEFT:      state.keyLeft = true; state.leftHeld = true; break;
                    case SDLK_RIGHT:     state.keyRight = true; state.rightHeld = true; break;
                    case SDLK_UP:        state.keyUp = true; state.upHeld = true; break;
                    case SDLK_DOWN:      state.keyDown = true; state.downHeld = true; break;
                    case SDLK_HOME:      state.keyHome = true; break;
                    case SDLK_END:       state.keyEnd = true; break;
                    case SDLK_INSERT:    state.keyInsert = true; break;
                    default: break;  // window hotkeys handled by scancode below
                }
                // Ctrl+V -> paste the OS clipboard into the focused input field. Scancode-keyed so it
                // works on any layout (S. is on a Russian layout). Text fields consume state.text as
                // typed input, so appending the clipboard there inserts it at the caret. (S.)
                if ((e.key.mod & SDL_KMOD_CTRL) && e.key.scancode == SDL_SCANCODE_V) {
                    if (char* clip = SDL_GetClipboardText()) {
                        state.text += clip;
                        SDL_free(clip);
                    }
                }
                // Window hotkeys keyed by PHYSICAL key position (scancode), NOT the
                // layout-dependent keycode, so Alt+E/Q/A/S/V fire on ANY keyboard layout.
                // (S. is on a Russian layout: there the V key's KEYCODE is Cyrillic 'м', not
                // 'v', so the old `case 'v'` never matched and Alt+V "не дёргается".)
                if (e.key.mod & SDL_KMOD_ALT) {
                    switch (e.key.scancode) {
                        case SDL_SCANCODE_E: state.keyInventory = true; break;
                        case SDL_SCANCODE_Q: state.keyEquip = true; break;
                        case SDL_SCANCODE_A: state.keyStatus = true; break;
                        case SDL_SCANCODE_S: state.keySkills = true; break;
                        case SDL_SCANCODE_V: state.keyBasicInfo = true; break;
                        case SDL_SCANCODE_W: state.keyCart = true; break;
                        case SDL_SCANCODE_M: state.keyBinds = true; break;  // Alt+M -> bind editor
                        case SDL_SCANCODE_H: state.keyFriends = true; break;  // Alt+H -> friend list (#78)
                        case SDL_SCANCODE_C: state.keyChatRoom = true; break;  // Alt+C -> create chat room (#78)
                        case SDL_SCANCODE_G: state.keyGuild = true; break;  // Alt+G -> guild window (#78)
                        case SDL_SCANCODE_B: state.keyBuyStore = true; break;  // Alt+B -> buying store (#69)
                        case SDL_SCANCODE_Z: state.keyParty = true; break;  // Alt+Z -> party tab (#78)
                        case SDL_SCANCODE_U: state.keyQuest = true; break;  // Alt+U -> quest journal (#136)
                        case SDL_SCANCODE_X: state.keyChatToggle = true; break;  // Alt+X -> collapse/expand chat (S.)
                        // Alt+1..9/0 -> fire that chat bind (top number row + keypad)
                        case SDL_SCANCODE_1: case SDL_SCANCODE_KP_1: state.altDigit = 1; break;
                        case SDL_SCANCODE_2: case SDL_SCANCODE_KP_2: state.altDigit = 2; break;
                        case SDL_SCANCODE_3: case SDL_SCANCODE_KP_3: state.altDigit = 3; break;
                        case SDL_SCANCODE_4: case SDL_SCANCODE_KP_4: state.altDigit = 4; break;
                        case SDL_SCANCODE_5: case SDL_SCANCODE_KP_5: state.altDigit = 5; break;
                        case SDL_SCANCODE_6: case SDL_SCANCODE_KP_6: state.altDigit = 6; break;
                        case SDL_SCANCODE_7: case SDL_SCANCODE_KP_7: state.altDigit = 7; break;
                        case SDL_SCANCODE_8: case SDL_SCANCODE_KP_8: state.altDigit = 8; break;
                        case SDL_SCANCODE_9: case SDL_SCANCODE_KP_9: state.altDigit = 9; break;
                        case SDL_SCANCODE_0: case SDL_SCANCODE_KP_0: state.altDigit = 0; break;
                        default: break;
                    }
                } else if (!e.key.repeat) {
                    // Quick-slot bar keys by PHYSICAL position (scancode -> row/col), so the bind
                    // is the same on any layout. The game gates these on chat-focus.
                    static const SDL_Scancode kRows[5][12] = {
                        {SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
                         SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8,
                         SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12},
                        {SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R,
                         SDL_SCANCODE_T, SDL_SCANCODE_Y, SDL_SCANCODE_U, SDL_SCANCODE_I,
                         SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_LEFTBRACKET,
                         SDL_SCANCODE_RIGHTBRACKET},
                        {SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
                         SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_J, SDL_SCANCODE_K,
                         SDL_SCANCODE_L, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE,
                         SDL_SCANCODE_UNKNOWN},
                        {SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V,
                         SDL_SCANCODE_B, SDL_SCANCODE_N, SDL_SCANCODE_M, SDL_SCANCODE_COMMA,
                         SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH, SDL_SCANCODE_UNKNOWN,
                         SDL_SCANCODE_UNKNOWN},
                        {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
                         SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8,
                         SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_MINUS,
                         SDL_SCANCODE_EQUALS}};
                    for (int r = 0; r < 5 && state.hotkeyRow < 0; ++r)
                        for (int c = 0; c < 12; ++c)
                            if (e.key.scancode == kRows[r][c]) {
                                state.hotkeyRow = r;
                                state.hotkeyCol = c;
                                break;
                            }
                    // Numpad 1..9/0 (no Alt) fires that bind-panel slot (S.: "слоты панели биндов
                    // привязать к цифрам нумпада"). Top-row 1..0 stay for the quick-slot bar above.
                    switch (e.key.scancode) {
                        case SDL_SCANCODE_KP_1: state.numDigit = 1; break;
                        case SDL_SCANCODE_KP_2: state.numDigit = 2; break;
                        case SDL_SCANCODE_KP_3: state.numDigit = 3; break;
                        case SDL_SCANCODE_KP_4: state.numDigit = 4; break;
                        case SDL_SCANCODE_KP_5: state.numDigit = 5; break;
                        case SDL_SCANCODE_KP_6: state.numDigit = 6; break;
                        case SDL_SCANCODE_KP_7: state.numDigit = 7; break;
                        case SDL_SCANCODE_KP_8: state.numDigit = 8; break;
                        case SDL_SCANCODE_KP_9: state.numDigit = 9; break;
                        case SDL_SCANCODE_KP_0: state.numDigit = 0; break;
                        default: break;
                    }
                }
                break;
            case SDL_EVENT_KEY_UP:
                switch (e.key.key) {  // release the held arrow state (smooth-nav)
                    case SDLK_LEFT:  state.leftHeld = false; break;
                    case SDLK_RIGHT: state.rightHeld = false; break;
                    case SDLK_UP:    state.upHeld = false; break;
                    case SDLK_DOWN:  state.downHeld = false; break;
                    default: break;
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                if (e.text.text) state.text += e.text.text;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                state.mouseX = state.mousePhysX = static_cast<int>(e.motion.x * sx);
                state.mouseY = state.mousePhysY = static_cast<int>(e.motion.y * sy);
                state.mouseDX += static_cast<int>(e.motion.xrel * sx);
                state.mouseDY += static_cast<int>(e.motion.yrel * sy);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    state.mouseX = state.mousePhysX = static_cast<int>(e.button.x * sx);
                    state.mouseY = state.mousePhysY = static_cast<int>(e.button.y * sy);
                    state.mouseDown = true;
                    state.mousePressed = true;
                    if (e.button.clicks >= 2) state.mouseDoubleClick = true;  // SDL double-click
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    state.rightDown = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    state.mouseX = state.mousePhysX = static_cast<int>(e.button.x * sx);
                    state.mouseY = state.mousePhysY = static_cast<int>(e.button.y * sy);
                    state.mouseDown = false;
                    state.mouseReleased = true;
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    state.rightDown = false;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                state.wheel += e.wheel.y;
                break;
            // Main-screen multi-touch (touchscreen game mode #102/#114). SDL reports normalized [0,1]
            // positions; convert to framebuffer pixels. Track up to two fingers (2nd drives pinch-zoom).
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_UP: {
                int pw = 0, ph = 0;
                if (SDL_Window* win = static_cast<SDL_Window*>(window_.sdlWindow()))
                    SDL_GetWindowSizeInPixels(win, &pw, &ph);
                const float px = e.tfinger.x * static_cast<float>(pw);
                const float py = e.tfinger.y * static_cast<float>(ph);
                const SDL_FingerID fid = e.tfinger.fingerID;
                state.touch.present = true;  // latch: a real finger arrived
                if (e.type == SDL_EVENT_FINGER_DOWN) {
                    for (auto& fg : state.touch.f)
                        if (fg.id < 0) {  // claim a free slot
                            fg.id = static_cast<long long>(fid);
                            fg.x = fg.x0 = px; fg.y = fg.y0 = py; fg.dx = fg.dy = 0.0f;
                            fg.downTime = SDL_GetTicks() / 1000.0;
                            fg.down = true; fg.held = true;
                            break;
                        }
                } else {
                    for (auto& fg : state.touch.f)
                        if (fg.held && fg.id == static_cast<long long>(fid)) {
                            fg.dx += px - fg.x; fg.dy += py - fg.y;
                            fg.x = px; fg.y = py;
                            if (e.type == SDL_EVENT_FINGER_UP) { fg.up = true; fg.held = false; fg.id = -1; }
                            break;
                        }
                }
                int c = 0;
                for (const auto& fg : state.touch.f) if (fg.held) ++c;
                state.touch.count = c;
                break;
            }
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                state.resized = true;
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!g_pad) {  // one-controller model: adopt the first that connects
                    g_pad = SDL_OpenGamepad(e.gdevice.which);
                    if (g_pad) {
                        state.pad.connected = true;
                        state.pad.type = padTypeOf(g_pad);
                        log::info("gamepad connected: {} (type {})",
                                  SDL_GetGamepadName(g_pad) ? SDL_GetGamepadName(g_pad) : "?",
                                  static_cast<int>(state.pad.type));
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (g_pad && e.gdevice.which == SDL_GetGamepadID(g_pad)) {
                    SDL_CloseGamepad(g_pad);
                    g_pad = nullptr;
                    state.pad = InputState::Gamepad{};  // clear all pad state on disconnect
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                const bool down = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                switch (e.gbutton.button) {
                    // Face buttons by POSITION. Edge pulses only on the down frame; *Held tracks state.
                    case SDL_GAMEPAD_BUTTON_SOUTH: state.pad.south = down; state.pad.southHeld = down; break;
                    case SDL_GAMEPAD_BUTTON_EAST:  state.pad.east = down;  state.pad.eastHeld = down;  break;
                    case SDL_GAMEPAD_BUTTON_WEST:  state.pad.west = down;  state.pad.westHeld = down;  break;
                    case SDL_GAMEPAD_BUTTON_NORTH: state.pad.north = down; state.pad.northHeld = down; break;
                    case SDL_GAMEPAD_BUTTON_DPAD_UP: state.pad.dpadUp = down; state.pad.dpadUpHeld = down; break;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: state.pad.dpadDown = down; state.pad.dpadDownHeld = down; break;
                    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: state.pad.dpadLeft = down; state.pad.dpadLeftHeld = down; break;
                    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: state.pad.dpadRight = down; state.pad.dpadRightHeld = down; break;
                    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: state.pad.l1 = down; break;   // bumpers = HELD
                    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: state.pad.r1 = down; break;
                    case SDL_GAMEPAD_BUTTON_LEFT_STICK: if (down) state.pad.l3 = true; break;   // click = edge
                    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: if (down) state.pad.r3 = true; break;
                    case SDL_GAMEPAD_BUTTON_START: if (down) state.pad.start = true; break;  // ≡ = ESC menu
                    case SDL_GAMEPAD_BUTTON_BACK: if (down) state.pad.back = true; break;    // View/Share = Camera Lock toggle
                    default: break;
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
            case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
                state.pad.touchActive = true;
                state.pad.touchX = e.gtouchpad.x;  // already 0..1
                state.pad.touchY = e.gtouchpad.y;
                if (e.type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN) state.pad.touchPress = true;
                break;
            case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
                state.pad.touchActive = false;
                break;
            default:
                break;
        }
    }

    // Poll continuous axes (sticks) + analog triggers each frame (events only cover discrete buttons).
    if (g_pad) {
        state.pad.lx = padAxis(SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX));
        state.pad.ly = -padAxis(SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY));   // up = +y
        state.pad.rx = padAxis(SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTX));
        state.pad.ry = -padAxis(SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTY));
        // Triggers L2/R2 are analog axes (0..32767); treat as HELD past ~50%.
        state.pad.l2 = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000;
        state.pad.r2 = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
    }

    const SDL_Keymod mods = SDL_GetModState();
    state.shift = (mods & SDL_KMOD_SHIFT) != 0;
    state.ctrl = (mods & SDL_KMOD_CTRL) != 0;
    // ESC no longer quits the app — scenes handle it (in-game menu / back / cancel).
    window_.pixelSize(state.width, state.height);
    return !state.quit;
}

void Platform::rumble(unsigned short lowFreq, unsigned short highFreq, unsigned int durationMs) {
    if (g_pad) SDL_RumbleGamepad(g_pad, lowFreq, highFreq, durationMs);
}

} // namespace uaro
