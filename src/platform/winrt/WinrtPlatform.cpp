// uaRO — WinRT/UWP implementation of the platform abstraction (Xbox Dev Mode + UWP desktop).
//
// SDL3 dropped the WinRT backend SDL2 had, so on the UWP app model (what Xbox Developer Mode runs)
// we cannot reuse platform/sdl. This file provides the SAME concrete Platform::* / Window::* the rest
// of the engine links against (there is no vtable — the abstraction is compile-time: CMake links THIS
// translation unit instead of SdlPlatform.cpp when CLIENT_CONSOLE_BACKEND=xbox). So app/ + game/ stay
// byte-for-byte backend-agnostic; only this file and WinrtConsoleServices.cpp know about WinRT.
//
// Responsibilities mirrored from SdlPlatform.cpp:
//   * Window::native()  -> { CoreWindow IUnknown*, nullptr } for bgfx (D3D12 SwapChainForCoreWindow).
//   * Platform::pump()   -> drain the CoreWindow dispatcher, fold key/pointer/wheel + poll the gamepad
//                           (Windows.Gaming.Input) into the same InputState fields SDL fills.
//   * Platform::rumble() -> GamepadVibration.
// The game LOOP itself is unchanged: uwp/App.cpp's IFrameworkView::Run() just calls Application::run(),
// whose `while (platform_.pump(input_))` now pumps CoreWindow instead of SDL.
//
// NOTE: this compiles only with the Windows SDK / C++-WinRT (cppwinrt) toolchain; it is never part of
// the desktop/Android/Linux builds. First real compile happens on a Windows box (see
// docs/xbox-uwp-build-km.md).

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "platform/FileSystem.hpp"
#include "platform/Platform.hpp"

#include <winrt/Windows.ApplicationModel.h>  // Package::InstalledLocation (base_dir)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>  // IVectorView consume (Gamepad::Gamepads Size/GetAt)
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Storage.h>  // ApplicationData::LocalFolder (pref_dir)
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.UserProfile.h>  // GlobalizationPreferences (system_language)
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>  // IPointerPoint::Properties/Position consume (PointerEventArgs)
#include <winrt/Windows.UI.ViewManagement.h>

using namespace winrt;
using namespace Windows::UI::Core;
using namespace Windows::System;
using namespace Windows::Foundation;
using namespace Windows::Gaming::Input;

namespace uaro {
namespace {

// One process-wide WinRT state block, exactly like SdlPlatform's file-static g_pad: there is a single
// Platform/Window per process and CoreWindow is thread-affine, so a namespace-static is safe and keeps
// the header (Window.hpp) clean of WinRT types. Persistent fields carry across frames; the per-frame
// block is filled by the event handlers during ProcessEvents() and drained (then cleared) each pump().
struct WinrtState {
    CoreWindow window{nullptr};

    // --- persistent (carry across frames) ---
    int mouseX = 0, mouseY = 0;
    bool mouseDown = false, rightDown = false;
    bool leftHeld = false, rightHeld = false, upHeld = false, downHeld = false;
    bool shift = false, ctrl = false, alt = false;
    int width = 0, height = 0;

    // --- per-frame (accumulated in handlers, consumed in pump) ---
    std::string text;
    bool closed = false, escape = false, resized = false;
    bool keyBackspace = false, keyEnter = false, keyTab = false, keyDelete = false;
    bool keyLeft = false, keyRight = false, keyUp = false, keyDown = false;
    bool keyHome = false, keyEnd = false, keyInsert = false;
    bool mousePressed = false, mouseReleased = false;
    int mousePrevX = 0, mousePrevY = 0, mouseDX = 0, mouseDY = 0;
    float wheel = 0.0f;
    // RO Alt-hotkeys + digits + hotbar, filled on KeyDown when Alt is held (see mapAltHotkey).
    bool hk[16] = {};    // index by HotIdx below
    int altDigit = -1;
    int hotkeyRow = -1, hotkeyCol = -1;

    // --- gamepad edge detection ---
    Gamepad activePad{nullptr};
    GamepadButtons prevButtons = GamepadButtons::None;

    // event revoker tokens (kept so close() can detach)
    event_token tKeyDown{}, tKeyUp{}, tChar{}, tPtrPressed{}, tPtrMoved{}, tPtrReleased{}, tWheel{},
        tSize{}, tClosed{};
    bool handlersBound = false;
};
WinrtState g;

// RO Alt-hotkey slots (mirror InputState fields). Index used into g.hk[].
enum HotIdx {
    HK_Inv, HK_Equip, HK_Status, HK_Skills, HK_Basic, HK_Cart, HK_Binds, HK_Friends, HK_ChatRoom,
    HK_Guild, HK_BuyStore, HK_Party, HK_Quest, HK_ChatToggle, HK_COUNT
};

// The physical-pixel scale: CoreWindow reports DIPs, bgfx swaps at raw pixels. On a 1080p Xbox this
// is 1.0; a 4K UI reports 2.0. Guarded — GetForCurrentView() needs a view on this thread.
double pixelScale() {
    try {
        return Windows::Graphics::Display::DisplayInformation::GetForCurrentView()
            .RawPixelsPerViewPixel();
    } catch (...) {
        return 1.0;
    }
}

// Append a UTF-32 code point as UTF-8 to `out` (CharacterReceived gives a code point). Control chars
// (< 0x20) and DEL are dropped — the edit keys are handled separately as edges.
void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x20 || cp == 0x7f) return;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Alt+<letter> -> RO window hotkey (matches the SDL layer's Alt keymap). Returns true if consumed.
bool mapAltHotkey(VirtualKey vk) {
    switch (vk) {
        case VirtualKey::E: g.hk[HK_Inv] = true; return true;       // Alt+E inventory
        case VirtualKey::Q: g.hk[HK_Equip] = true; return true;     // Alt+Q equipment
        case VirtualKey::A: g.hk[HK_Status] = true; return true;    // Alt+A status
        case VirtualKey::S: g.hk[HK_Skills] = true; return true;    // Alt+S skills
        case VirtualKey::V: g.hk[HK_Basic] = true; return true;     // Alt+V basic info
        case VirtualKey::W: g.hk[HK_Cart] = true; return true;      // Alt+W cart
        case VirtualKey::M: g.hk[HK_Binds] = true; return true;     // Alt+M binds
        case VirtualKey::H: g.hk[HK_Friends] = true; return true;   // Alt+H friends
        case VirtualKey::C: g.hk[HK_ChatRoom] = true; return true;  // Alt+C chat room
        case VirtualKey::G: g.hk[HK_Guild] = true; return true;     // Alt+G guild
        case VirtualKey::B: g.hk[HK_BuyStore] = true; return true;  // Alt+B buying store
        case VirtualKey::Z: g.hk[HK_Party] = true; return true;     // Alt+Z party
        case VirtualKey::U: g.hk[HK_Quest] = true; return true;     // Alt+U quest
        case VirtualKey::X: g.hk[HK_ChatToggle] = true; return true;// Alt+X chat toggle
        default: return false;
    }
}

void onKeyDown(VirtualKey vk) {
    switch (vk) {
        case VirtualKey::Shift:   g.shift = true; return;
        case VirtualKey::Control: g.ctrl = true; return;
        case VirtualKey::Menu:    g.alt = true; return;  // Alt
        case VirtualKey::Back:    g.keyBackspace = true; return;
        case VirtualKey::Enter:   g.keyEnter = true; return;
        case VirtualKey::Tab:     g.keyTab = true; return;
        case VirtualKey::Delete:  g.keyDelete = true; return;
        case VirtualKey::Escape:  g.escape = true; return;
        case VirtualKey::Insert:  g.keyInsert = true; return;
        case VirtualKey::Home:    g.keyHome = true; return;
        case VirtualKey::End:     g.keyEnd = true; return;
        case VirtualKey::Left:    g.keyLeft = true;  g.leftHeld = true;  return;
        case VirtualKey::Right:   g.keyRight = true; g.rightHeld = true; return;
        case VirtualKey::Up:      g.keyUp = true;    g.upHeld = true;    return;
        case VirtualKey::Down:    g.keyDown = true;  g.downHeld = true;  return;
        default: break;
    }
    const int code = static_cast<int>(vk);
    // Alt-combos: RO window hotkeys + Alt+digit chat binds.
    if (g.alt) {
        if (mapAltHotkey(vk)) return;
        if (code >= static_cast<int>(VirtualKey::Number0) &&
            code <= static_cast<int>(VirtualKey::Number9)) {
            g.altDigit = code - static_cast<int>(VirtualKey::Number0);
            return;
        }
    }
    // Hotbar rows: F1-F12 = row 0; number row 1..0 = row 4 (matches the SDL layer's default).
    if (code >= static_cast<int>(VirtualKey::F1) && code <= static_cast<int>(VirtualKey::F12)) {
        g.hotkeyRow = 0;
        g.hotkeyCol = code - static_cast<int>(VirtualKey::F1);
    } else if (!g.alt && code >= static_cast<int>(VirtualKey::Number1) &&
               code <= static_cast<int>(VirtualKey::Number9)) {
        g.hotkeyRow = 4;
        g.hotkeyCol = code - static_cast<int>(VirtualKey::Number1);
    }
}

void onKeyUp(VirtualKey vk) {
    switch (vk) {
        case VirtualKey::Shift:   g.shift = false; return;
        case VirtualKey::Control: g.ctrl = false; return;
        case VirtualKey::Menu:    g.alt = false; return;
        case VirtualKey::Left:    g.leftHeld = false; return;
        case VirtualKey::Right:   g.rightHeld = false; return;
        case VirtualKey::Up:      g.upHeld = false; return;
        case VirtualKey::Down:    g.downHeld = false; return;
        default: return;
    }
}

void bindHandlers(CoreWindow const& w) {
    if (g.handlersBound) return;
    g.handlersBound = true;
    g.tKeyDown = w.KeyDown([](CoreWindow const&, KeyEventArgs const& a) { onKeyDown(a.VirtualKey()); });
    g.tKeyUp   = w.KeyUp([](CoreWindow const&, KeyEventArgs const& a) { onKeyUp(a.VirtualKey()); });
    g.tChar    = w.CharacterReceived(
        [](CoreWindow const&, CharacterReceivedEventArgs const& a) { appendUtf8(g.text, a.KeyCode()); });
    g.tPtrPressed = w.PointerPressed([](CoreWindow const&, PointerEventArgs const& a) {
        auto p = a.CurrentPoint();
        auto pr = p.Properties();
        const double s = pixelScale();
        g.mouseX = static_cast<int>(p.Position().X * s);
        g.mouseY = static_cast<int>(p.Position().Y * s);
        if (pr.IsLeftButtonPressed()) { g.mousePressed = !g.mouseDown; g.mouseDown = true; }
        g.rightDown = pr.IsRightButtonPressed();
    });
    g.tPtrMoved = w.PointerMoved([](CoreWindow const&, PointerEventArgs const& a) {
        auto p = a.CurrentPoint();
        auto pr = p.Properties();
        const double s = pixelScale();
        g.mouseX = static_cast<int>(p.Position().X * s);
        g.mouseY = static_cast<int>(p.Position().Y * s);
        g.mouseDown = pr.IsLeftButtonPressed();
        g.rightDown = pr.IsRightButtonPressed();
    });
    g.tPtrReleased = w.PointerReleased([](CoreWindow const&, PointerEventArgs const& a) {
        auto pr = a.CurrentPoint().Properties();
        if (!pr.IsLeftButtonPressed() && g.mouseDown) { g.mouseReleased = true; g.mouseDown = false; }
        g.rightDown = pr.IsRightButtonPressed();
    });
    g.tWheel = w.PointerWheelChanged([](CoreWindow const&, PointerEventArgs const& a) {
        g.wheel += static_cast<float>(a.CurrentPoint().Properties().MouseWheelDelta()) / 120.0f;
    });
    g.tSize = w.SizeChanged([](CoreWindow const&, WindowSizeChangedEventArgs const& a) {
        const double s = pixelScale();
        g.width = static_cast<int>(a.Size().Width * s);
        g.height = static_cast<int>(a.Size().Height * s);
        g.resized = true;
    });
    g.tClosed = w.Closed([](CoreWindow const&, CoreWindowEventArgs const&) { g.closed = true; });
}

float deadzone(double v, double dz = 0.2) {
    if (v > dz) return static_cast<float>((v - dz) / (1.0 - dz));
    if (v < -dz) return static_cast<float>((v + dz) / (1.0 - dz));
    return 0.0f;
}

bool held(GamepadButtons b, GamepadButtons bit) { return (b & bit) == bit; }
bool edge(GamepadButtons now, GamepadButtons bit) {
    return held(now, bit) && !held(g.prevButtons, bit);
}

// Poll the first connected controller into InputState::pad (Windows.Gaming.Input). Buttons are
// position-based (A=south/bottom) so the engine's binds are identical to the SDL path.
void pollGamepad(InputState& s) {
    try {
        auto pads = Gamepad::Gamepads();
        if (pads.Size() == 0) { g.activePad = nullptr; g.prevButtons = GamepadButtons::None; return; }
        g.activePad = pads.GetAt(0);
        GamepadReading r = g.activePad.GetCurrentReading();
        const GamepadButtons b = r.Buttons;
        auto& pad = s.pad;
        pad.connected = true;
        pad.type = InputState::PadType::Xbox;
        pad.lx = deadzone(r.LeftThumbstickX);
        pad.ly = deadzone(r.LeftThumbstickY);   // Windows: +Y up == our convention
        pad.rx = deadzone(r.RightThumbstickX);
        pad.ry = deadzone(r.RightThumbstickY);
        // Face buttons (edge) + held.
        pad.south = edge(b, GamepadButtons::A); pad.southHeld = held(b, GamepadButtons::A);
        pad.east  = edge(b, GamepadButtons::B); pad.eastHeld  = held(b, GamepadButtons::B);
        pad.west  = edge(b, GamepadButtons::X); pad.westHeld  = held(b, GamepadButtons::X);
        pad.north = edge(b, GamepadButtons::Y); pad.northHeld = held(b, GamepadButtons::Y);
        // D-pad (edge) + held.
        pad.dpadUp    = edge(b, GamepadButtons::DPadUp);    pad.dpadUpHeld    = held(b, GamepadButtons::DPadUp);
        pad.dpadDown  = edge(b, GamepadButtons::DPadDown);  pad.dpadDownHeld  = held(b, GamepadButtons::DPadDown);
        pad.dpadLeft  = edge(b, GamepadButtons::DPadLeft);  pad.dpadLeftHeld  = held(b, GamepadButtons::DPadLeft);
        pad.dpadRight = edge(b, GamepadButtons::DPadRight); pad.dpadRightHeld = held(b, GamepadButtons::DPadRight);
        // Shoulders (held) + triggers past threshold.
        pad.l1 = held(b, GamepadButtons::LeftShoulder);
        pad.r1 = held(b, GamepadButtons::RightShoulder);
        pad.l2 = r.LeftTrigger > 0.5;
        pad.r2 = r.RightTrigger > 0.5;
        // Stick clicks + Start(Menu)/Back(View) (edge).
        pad.l3 = edge(b, GamepadButtons::LeftThumbstick);
        pad.r3 = edge(b, GamepadButtons::RightThumbstick);
        pad.start = edge(b, GamepadButtons::Menu);
        pad.back = edge(b, GamepadButtons::View);
        g.prevButtons = b;
    } catch (...) {
        g.activePad = nullptr;
        g.prevButtons = GamepadButtons::None;
        s.pad.connected = false;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Filesystem (the SDL layer's fs:: on WinRT/UWP; same signatures as platform/FileSystem.hpp)
// ---------------------------------------------------------------------------
namespace fs {

std::optional<std::vector<u8>> read_file(const std::string& path) {
    // Pure std -- files inside the package/LocalFolder are reachable with ordinary streams on UWP.
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;
    const std::streamsize size = in.tellg();
    in.seekg(0);
    std::vector<u8> data(static_cast<usize>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size)) return std::nullopt;
    return data;
}

std::string base_dir() {
    // The package install folder (read-only) holds the assets shipped beside the app.
    try {
        const auto p = Windows::ApplicationModel::Package::Current().InstalledLocation().Path();
        std::filesystem::path fsp(std::wstring(p.c_str()));
        return (fsp / "").string();  // trailing separator, like SDL_GetBasePath
    } catch (...) {
        return "./";
    }
}

std::string system_language() {
    // First preferred UI language ("en-US" -> "en"), matching SDL_GetPreferredLocales.
    try {
        auto langs = Windows::System::UserProfile::GlobalizationPreferences::Languages();
        if (langs.Size() > 0) {
            std::string tag = winrt::to_string(langs.GetAt(0));
            const auto dash = tag.find_first_of("-_");
            return dash == std::string::npos ? tag : tag.substr(0, dash);
        }
    } catch (...) {
    }
    return {};
}

std::string pref_dir(const std::string& org, const std::string& app) {
    // Writable per-user store = the app's LocalFolder; mirror SDL's <pref>/<org>/<app>/ layout.
    try {
        const auto p = Windows::Storage::ApplicationData::Current().LocalFolder().Path();
        std::filesystem::path dir = std::filesystem::path(std::wstring(p.c_str())) / org / app;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return (dir / "").string();  // trailing separator, like SDL_GetPrefPath
    } catch (...) {
        return "./";
    }
}

std::string data_dir() {
    // A UWP/Xbox package's install folder is ALWAYS read-only, so the writable data root is the
    // app's LocalFolder (same place pref_dir uses). No trailing slash, matching the SDL build.
    std::string d = pref_dir("uaro", "client");
    while (!d.empty() && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    return d;
}

}  // namespace fs

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------
Window::~Window() { close(); }

bool Window::open(const WindowConfig& /*cfg*/) {
    // The UWP framework already created the CoreWindow before IFrameworkView::Run(); we adopt the
    // one bound to this (main) thread rather than creating our own. Size/title come from the OS.
    g.window = CoreWindow::GetForCurrentThread();
    if (!g.window) return false;
    bindHandlers(g.window);
    g.window.Activate();
    const double s = pixelScale();
    g.width = static_cast<int>(g.window.Bounds().Width * s);
    g.height = static_cast<int>(g.window.Bounds().Height * s);
    handle_ = get_unknown(g.window);  // stash IUnknown* so valid()/sdlWindow() report non-null
    return true;
}

void Window::close() {
    handle_ = nullptr;
    // Handlers auto-detach with the CoreWindow lifetime on app exit; explicit revoke omitted since
    // there is a single window for the whole process.
    g.window = nullptr;
}

NativeWindow Window::native() const {
    // bgfx (D3D12) takes the CoreWindow's IUnknown* as nwh and builds a SwapChainForCoreWindow; no
    // native display type on Windows/Xbox.
    NativeWindow nw;
    nw.nwh = g.window ? get_unknown(g.window) : nullptr;
    nw.ndt = nullptr;
    return nw;
}

void Window::pixelSize(int& w, int& h) const {
    w = g.width;
    h = g.height;
}

void Window::setFullscreen(bool /*on*/) { /* UWP apps are always full-screen on Xbox — no-op. */ }

void Window::setTextInput(bool on) {
    if (on == textInputOn_) return;
    textInputOn_ = on;
    try {
        auto pane = Windows::UI::ViewManagement::InputPane::GetForCurrentView();
        if (on) pane.TryShow();
        else pane.TryHide();
    } catch (...) {
    }
}

// ---------------------------------------------------------------------------
// Platform
// ---------------------------------------------------------------------------
Platform::~Platform() { shutdown(); }

bool Platform::init() {
    inited_ = true;
    return true;
}

void Platform::shutdown() {
    if (!inited_) return;
    inited_ = false;
    window_.close();
}

bool Platform::createWindow(const WindowConfig& cfg) { return window_.open(cfg); }

bool Platform::pump(InputState& state) {
    // Clear the per-frame edges/text on the OUT state (mirrors SdlPlatform.cpp's top-of-pump reset),
    // then drain the dispatcher so the handlers refill g's per-frame block synchronously on this thread.
    state.text.clear();
    if (g.window) {
        try {
            g.window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        } catch (...) {
        }
    }

    // --- window / quit ---
    state.quit = g.closed;
    state.closeRequested = g.closed;
    state.escape = g.escape;
    state.resized = g.resized;
    state.width = g.width;
    state.height = g.height;

    // --- text + edge keys ---
    state.text = g.text;
    state.keyBackspace = g.keyBackspace;
    state.keyEnter = g.keyEnter;
    state.keyTab = g.keyTab;
    state.keyDelete = g.keyDelete;
    state.keyLeft = g.keyLeft;
    state.keyRight = g.keyRight;
    state.keyUp = g.keyUp;
    state.keyDown = g.keyDown;
    state.keyHome = g.keyHome;
    state.keyEnd = g.keyEnd;
    state.keyInsert = g.keyInsert;
    state.leftHeld = g.leftHeld;
    state.rightHeld = g.rightHeld;
    state.upHeld = g.upHeld;
    state.downHeld = g.downHeld;
    state.shift = g.shift;
    state.ctrl = g.ctrl;

    // --- RO Alt-hotkeys ---
    state.keyInventory = g.hk[HK_Inv];
    state.keyEquip = g.hk[HK_Equip];
    state.keyStatus = g.hk[HK_Status];
    state.keySkills = g.hk[HK_Skills];
    state.keyBasicInfo = g.hk[HK_Basic];
    state.keyCart = g.hk[HK_Cart];
    state.keyBinds = g.hk[HK_Binds];
    state.keyFriends = g.hk[HK_Friends];
    state.keyChatRoom = g.hk[HK_ChatRoom];
    state.keyGuild = g.hk[HK_Guild];
    state.keyBuyStore = g.hk[HK_BuyStore];
    state.keyParty = g.hk[HK_Party];
    state.keyQuest = g.hk[HK_Quest];
    state.keyChatToggle = g.hk[HK_ChatToggle];
    state.altDigit = g.altDigit;
    state.hotkeyRow = g.hotkeyRow;
    state.hotkeyCol = g.hotkeyCol;

    // --- mouse ---
    state.mouseX = state.mousePhysX = g.mouseX;
    state.mouseY = state.mousePhysY = g.mouseY;
    state.mouseDown = g.mouseDown;
    state.mousePressed = g.mousePressed;
    state.mouseReleased = g.mouseReleased;
    state.mouseDoubleClick = false;  // TODO: derive from click timing if needed
    state.rightDown = g.rightDown;
    state.mouseDX = g.mouseX - g.mousePrevX;
    state.mouseDY = g.mouseY - g.mousePrevY;
    state.wheel = g.wheel;
    g.mousePrevX = g.mouseX;
    g.mousePrevY = g.mouseY;

    // --- gamepad ---
    pollGamepad(state);

    // Reset per-frame accumulators for the next pump (persistent held-state kept).
    g.text.clear();
    g.escape = g.resized = false;
    g.keyBackspace = g.keyEnter = g.keyTab = g.keyDelete = false;
    g.keyLeft = g.keyRight = g.keyUp = g.keyDown = false;
    g.keyHome = g.keyEnd = g.keyInsert = false;
    g.mousePressed = g.mouseReleased = false;
    g.wheel = 0.0f;
    for (bool& b : g.hk) b = false;
    g.altDigit = -1;
    g.hotkeyRow = g.hotkeyCol = -1;

    return !state.quit;
}

void Platform::setCursorVisible(bool visible) {
    if (!g.window) return;
    try {
        if (visible)
            g.window.PointerCursor(CoreCursor(CoreCursorType::Arrow, 0));
        else
            g.window.PointerCursor(nullptr);
    } catch (...) {
    }
}

void Platform::rumble(unsigned short lowFreq, unsigned short highFreq, unsigned int /*durationMs*/) {
    // Windows.Gaming.Input has no timed rumble; set the motor levels and the caller re-issues 0 to stop.
    if (!g.activePad) return;
    try {
        GamepadVibration v;
        v.LeftMotor = static_cast<double>(lowFreq) / 65535.0;
        v.RightMotor = static_cast<double>(highFreq) / 65535.0;
        v.LeftTrigger = 0.0;
        v.RightTrigger = 0.0;
        g.activePad.Vibration(v);
    } catch (...) {
    }
}

}  // namespace uaro
