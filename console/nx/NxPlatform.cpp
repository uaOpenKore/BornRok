// NxPlatform.cpp -- Nintendo Switch (NX) implementation of uaro::Platform / uaro::Window / uaro::fs.
//
// Uses NintendoSDK (NN) APIs for display (vi), input (hid), operation environment (oe),
// and settings. bgfx receives the nn::vi native window handle for its NVN backend.
//
// This file is built ONLY on the licensed NX build machine -- it is NOT in the public
// CMake and is excluded from desktop/Android/iOS builds.

#include "platform/Platform.hpp"
#include "platform/Window.hpp"
#include "platform/Input.hpp"
#include "platform/FileSystem.hpp"
#include "platform/ConsoleServices.hpp"
#include "NxConsoleServices.hpp"
#include "core/Log.hpp"

#include <nn/vi.h>
#include <nn/hid.h>
#include <nn/oe.h>
#include <nn/settings.h>
#include <nn/fs.h>
#include <nn/fs/fs_File.h>

namespace uaro {

// File-static state because Window/Platform own no per-instance storage for
// NX-specific handles -- the shared headers (Window.hpp) only carry handle_
// (the native window pointer) and textInputOn_. Members below live here.
namespace {
    // VI handles
    nn::vi::Display* gDisplay = nullptr;
    nn::vi::Layer*   gLayer   = nullptr;

    // Current layer pixel size (changes on dock/undock).
    int gPixelWidth  = 1280;
    int gPixelHeight = 720;

    // Stick held-state tracking for stick-as-dpad edge detection.
    bool gLxHeld = false, gRxHeld = false;
    bool gLyHeld = false, gRyHeld = false;

    // Set true when the OS sends an exit/lifetime request; next pump() returns false.
    bool gQuitRequested = false;

    // Points to the active NxConsoleServices (set by Platform::init).
    NxConsoleServices* gNxSvc = nullptr;
} // namespace

// =================================================================== Window ==

bool Window::open(const WindowConfig& cfg) {
    nn::vi::Initialize();

    nn::vi::Display* display = nullptr;
    nn::Result r = nn::vi::OpenDefaultDisplay(&display);
    if (r.IsFailure()) {
        log::error("nx: OpenDefaultDisplay failed (0x{:08x})", r.GetValue());
        nn::vi::Finalize();
        return false;
    }
    gDisplay = display;

    nn::vi::Layer* layer = nullptr;
    r = nn::vi::CreateLayer(&layer, display);
    if (r.IsFailure()) {
        log::error("nx: CreateLayer failed (0x{:08x})", r.GetValue());
        nn::vi::CloseDisplay(display);
        gDisplay = nullptr;
        nn::vi::Finalize();
        return false;
    }
    gLayer = layer;

    nn::vi::NativeWindowHandle nativeWindow = nullptr;
    r = nn::vi::GetNativeWindow(&nativeWindow, layer);
    if (r.IsFailure()) {
        log::error("nx: GetNativeWindow failed (0x{:08x})", r.GetValue());
        return false;
    }
    handle_ = static_cast<void*>(nativeWindow);

    nn::oe::GetDefaultDisplayResolution(&gPixelWidth, &gPixelHeight);
    log::info("nx: window opened ({}x{})", gPixelWidth, gPixelHeight);
    return true;
}

void Window::close() {
    handle_ = nullptr;
    if (gLayer) {
        // DestroyLayer is unavailable in the public VI API; closing the
        // parent display destroys all layers belonging to this process.
        gLayer = nullptr;
    }
    if (gDisplay) {
        nn::vi::CloseDisplay(gDisplay);
        gDisplay = nullptr;
    }
    nn::vi::Finalize();
}

NativeWindow Window::native() const {
    NativeWindow n;
    n.nwh = handle_;   // nn::vi NativeWindowHandle -> bgfx::PlatformData.nwh (NVN)
    n.ndt = nullptr;
    return n;
}

void Window::pixelSize(int& w, int& h) const {
    w = gPixelWidth;
    h = gPixelHeight;
}

void Window::setFullscreen(bool /*on*/) {
    // N/A: the OS owns display mode (docked vs handheld).
}

void Window::setTextInput(bool on) {
    if (on == textInputOn_) return;
    textInputOn_ = on;
    // The software keyboard on Switch is a modal applet (nn::swkbd).
    // It is NOT raised here; instead the UI triggers it on demand via a
    // dedicated edit action, which feeds the returned UTF-8 into
    // InputState::text in one shot.
}

Window::~Window() { close(); }

// =============================================================== Platform ==

Platform::~Platform() { shutdown(); }

bool Platform::init() {
    // --- Npad (all controller styles) ---
    nn::hid::InitializeNpad();

    nn::hid::NpadStyleSet styleSet;
    styleSet.Set<nn::hid::NpadStyleFullKey>();   // Pro Controller / 3rd-party full-key
    styleSet.Set<nn::hid::NpadStyleHandheld>();   // Handheld
    styleSet.Set<nn::hid::NpadStyleJoyDual>();    // Dual Joy-Con grip
    nn::hid::SetSupportedNpadStyleSet(styleSet);

    const nn::hid::NpadIdType supportedIds[] = {
        nn::hid::NpadId::Handheld,
        nn::hid::NpadId::No1,
    };
    nn::hid::SetSupportedNpadIdType(
        supportedIds,
        int(sizeof(supportedIds) / sizeof(supportedIds[0])));

    // --- Touch screen (handheld) ---
    nn::hid::InitializeTouchScreen();

    // --- Operation environment (lifecycle, focus, performance mode) ---
    nn::oe::Initialize();
    // Notify-only: the OS does NOT auto-suspend us; we handle suspend/resume
    // ourselves via PopNotificationMessage in pump().
    nn::oe::SetFocusHandlingMode(nn::oe::FocusHandlingMode_Notify);

    // --- Display resolution for initial pixel size ---
    nn::oe::GetDefaultDisplayResolution(&gPixelWidth, &gPixelHeight);

    // Cache the NxConsoleServices pointer for lifecycle dispatch.
    gNxSvc = static_cast<NxConsoleServices*>(&consoleServices());

    inited_ = true;
    log::info("nx: platform init complete");
    return true;
}

void Platform::shutdown() {
    if (!inited_) return;
    nn::hid::FinalizeNpad();
    nn::oe::Finalize();
    gNxSvc = nullptr;
    gQuitRequested = false;
    inited_ = false;
}

bool Platform::createWindow(const WindowConfig& cfg) {
    return window_.open(cfg);
}

void Platform::setCursorVisible(bool /*visible*/) {
    // No OS cursor on Switch; the game draws the RO cursor sprite.
}

void Platform::rumble(unsigned short lowFreq, unsigned short highFreq, unsigned int durationMs) {
    // Full implementation requires enumerating vibration devices for the active
    // Npad. Outline for the licensed developer:
    //   nn::hid::VibrationDeviceHandle devices[2];
    //   int count = nn::hid::GetVibrationDeviceHandles(devices, 2, npadId);
    //   nn::hid::VibrationValue val;
    //   val.amplitudeLow  = lowFreq  / 65535.0f;
    //   val.amplitudeHigh = highFreq / 65535.0f;
    //   val.frequencyLow  = 160.0f;
    //   val.frequencyHigh = 320.0f;
    //   for (int i = 0; i < count; ++i)
    //       nn::hid::SendVibrationValue(devices[i], val, durationMs);
    (void)lowFreq; (void)highFreq; (void)durationMs;
}

// Reads one Npad state struct and translates it into our position-based Gamepad
// layout. Shared between FullKey and Handheld code paths.
template <typename NpadStateT>
static void translateNpadState(const NpadStateT& st, InputState::Gamepad& pad) {
    const auto& b = st.buttons;
    // POSITION-based mapping (south/east/west/north), NOT by label. Nintendo's A/B are MIRRORED vs
    // Xbox: A is physically on the RIGHT, B on the BOTTOM -- so bind by physical position for parity
    // with the SDL/Xbox backends (a bind on "south" hits the bottom button on every platform).
    pad.south      = b.IsOf<nn::hid::NpadButton::B>();       // physical bottom (Nintendo B)
    pad.east       = b.IsOf<nn::hid::NpadButton::A>();       // physical right  (Nintendo A)
    pad.west       = b.IsOf<nn::hid::NpadButton::Y>();       // physical left
    pad.north      = b.IsOf<nn::hid::NpadButton::X>();       // physical top
    pad.dpadLeft   = b.IsOf<nn::hid::NpadButton::Left>();
    pad.dpadUp     = b.IsOf<nn::hid::NpadButton::Up>();
    pad.dpadRight  = b.IsOf<nn::hid::NpadButton::Right>();
    pad.dpadDown   = b.IsOf<nn::hid::NpadButton::Down>();
    pad.l1         = b.IsOf<nn::hid::NpadButton::L>();
    pad.r1         = b.IsOf<nn::hid::NpadButton::R>();
    pad.l2         = b.IsOf<nn::hid::NpadButton::ZL>();
    pad.r2         = b.IsOf<nn::hid::NpadButton::ZR>();
    pad.l3         = b.IsOf<nn::hid::NpadButton::StickL>();
    pad.r3         = b.IsOf<nn::hid::NpadButton::StickR>();
    pad.start      = b.IsOf<nn::hid::NpadButton::Plus>();
    pad.back       = b.IsOf<nn::hid::NpadButton::Minus>();
    // Held mirrors for repeat navigation.
    pad.southHeld  = pad.south;
    pad.eastHeld   = pad.east;
    pad.westHeld   = pad.west;
    pad.northHeld  = pad.north;
    pad.dpadUpHeld    = pad.dpadUp;
    pad.dpadDownHeld  = pad.dpadDown;
    pad.dpadLeftHeld  = pad.dpadLeft;
    pad.dpadRightHeld = pad.dpadRight;
    // Analog sticks: normalise from [-32768..32767] to [-1..1],
    // flip Y so up = +y.
    auto norm = [](int32_t v) -> float {
        return float(v) / float(nn::hid::AnalogStickMax);
    };
    pad.lx = norm(st.analogStickL.x);
    pad.ly = -norm(st.analogStickL.y);
    pad.rx = norm(st.analogStickR.x);
    pad.ry = -norm(st.analogStickR.y);
    // Apply radial deadzone (same threshold as SdlPlatform).
    auto deadzone = [](float& x, float& y) {
        const float deadSq = 0.2f * 0.2f;
        float magSq = x * x + y * y;
        if (magSq < deadSq) { x = 0.0f; y = 0.0f; return; }
        float mag = std::sqrt(magSq);
        float scale = (mag - 0.2f) / (1.0f - 0.2f);
        if (scale < 0.0f) scale = 0.0f;
        x = (x / mag) * scale;
        y = (y / mag) * scale;
    };
    deadzone(pad.lx, pad.ly);
    deadzone(pad.rx, pad.ry);
}

// Per-frame pump: reads lifecycle messages (focus, operation mode), Npad input,
// and touch screen state. Edge-triggered fields are cleared; held/analog fields
// persist and are overwritten.
bool Platform::pump(InputState& state) {
    // Reset per-frame edges (mirrors the SDL backend).
    state.pad.south = state.pad.east = state.pad.west = state.pad.north = false;
    state.pad.dpadUp = state.pad.dpadDown = state.pad.dpadLeft = state.pad.dpadRight = false;
    state.pad.l3 = state.pad.r3 = state.pad.start = state.pad.back = false;
    state.resized = false;
    state.text.clear();
    state.escape = false;
    state.keyEnter = false;
    state.keyUp = state.keyDown = state.keyLeft = state.keyRight = false;

    // --- Lifecycle (focus, dock/undock) via oe notification messages ---
    nn::oe::Message msg;
    while (nn::oe::TryPopNotificationMessage(&msg)) {
        if (msg == nn::oe::MessageFocusStateChanged) {
            const auto focus = nn::oe::GetCurrentFocusState();
            switch (focus) {
            case nn::oe::FocusState_OutOfFocus:
                if (gNxSvc) gNxSvc->dispatchLifecycle(AppLifecycle::Constrained);
                break;
            case nn::oe::FocusState_Background:
                if (gNxSvc) gNxSvc->dispatchLifecycle(AppLifecycle::Suspended);
                break;
            case nn::oe::FocusState_InFocus:
                if (gNxSvc) gNxSvc->dispatchLifecycle(AppLifecycle::Resumed);
                break;
            }
        } else if (msg == nn::oe::MessageResume) {
            if (gNxSvc) gNxSvc->dispatchLifecycle(AppLifecycle::Resumed);
        } else if (msg == nn::oe::MessageOperationModeChanged) {
            int newW = 0, newH = 0;
            nn::oe::GetDefaultDisplayResolution(&newW, &newH);
            if (newW != gPixelWidth || newH != gPixelHeight) {
                gPixelWidth = newW;
                gPixelHeight = newH;
                state.resized = true;
                state.width  = newW;
                state.height = newH;
            }
        } else if (msg == nn::oe::MessageExitRequest) {
            gQuitRequested = true;
        }
    }

    // --- Npad gamepad ---
    InputState::Gamepad& pad = state.pad;
    pad.connected = false;

    const auto npadId = nn::hid::NpadId::No1;
    const nn::hid::NpadStyleSet activeStyle = nn::hid::GetNpadStyleSet(npadId);

    if (activeStyle.IsOf<nn::hid::NpadStyleFullKey>() ||
        activeStyle.IsOf<nn::hid::NpadStyleJoyDual>()) {
        nn::hid::NpadFullKeyState st{};
        nn::hid::GetNpadState(&st, npadId);
        if (st.attributes.IsOf<nn::hid::NpadAttribute::IsConnected>()) {
            pad.connected = true;
            pad.type = InputState::PadType::Nintendo;
            translateNpadState(st, pad);
        }
    }

    // Fallback to handheld if No1 wasn't a full-key/JoyDual controller.
    if (!pad.connected) {
        const auto handheldStyle = nn::hid::GetNpadStyleSet(nn::hid::NpadId::Handheld);
        if (handheldStyle.IsOf<nn::hid::NpadStyleHandheld>()) {
            nn::hid::NpadHandheldState st{};
            nn::hid::GetNpadState(&st, nn::hid::NpadId::Handheld);
            if (st.attributes.IsOf<nn::hid::NpadAttribute::IsConnected>()) {
                pad.connected = true;
                pad.type = InputState::PadType::Nintendo;
                translateNpadState(st, pad);
            }
        }
    }

    // --- Handheld touch screen ---
    if (nn::oe::GetOperationMode() == nn::oe::OperationMode_Handheld) {
        state.touch.present = true;
        nn::hid::TouchScreenState<nn::hid::TouchStateCountMax> ts{};
        nn::hid::GetTouchScreenState(&ts);
        const int count = (ts.count > 2) ? 2 : ts.count;
        state.touch.count = count;
        for (int i = 0; i < count; ++i) {
            auto& src = ts.touches[i];
            auto& dst = state.touch.f[i];
            dst.id = src.fingerId;
            dst.x = float(src.x);
            dst.y = float(src.y);
            dst.down = src.attributes.IsOf<nn::hid::TouchAttribute::Start>();
            dst.up   = src.attributes.IsOf<nn::hid::TouchAttribute::End>();
            if (dst.down) {
                dst.x0 = dst.x;
                dst.y0 = dst.y;
                dst.held = true;
            }
            if (dst.up) dst.held = false;
        }
    } else {
        state.touch.present = false;
        state.touch.count = 0;
    }

    // Map gamepad face buttons to keyboard equivalents for UI navigation. Position-based, so on
    // Nintendo south = the physical-bottom button (B) and east = physical-right (A) -- same as every
    // other backend: bottom confirms, right cancels.
    // South (bottom) = Enter, East (right) = Escape, Start = Enter, Back = Escape.
    if (pad.south || pad.start) state.keyEnter = true;
    if (pad.east)               state.escape  = true;

    // D-pad / left-stick → arrow keys (only when no touch is active).
    if (state.touch.count == 0) {
        state.leftHeld  = pad.dpadLeftHeld  || pad.lx < -0.5f;
        state.rightHeld = pad.dpadRightHeld || pad.lx >  0.5f;
        state.upHeld    = pad.dpadUpHeld    || pad.ly >  0.5f;
        state.downHeld  = pad.dpadDownHeld  || pad.ly < -0.5f;
        // Stick edge detection: fire key on frame the stick crosses threshold.
        bool lx = pad.lx < -0.5f, rx = pad.lx > 0.5f;
        bool ly = pad.ly >  0.5f, ry = pad.ly < -0.5f;
        state.keyLeft  = pad.dpadLeft  || (lx && !gLxHeld);
        state.keyRight = pad.dpadRight || (rx && !gRxHeld);
        state.keyUp    = pad.dpadUp    || (ly && !gLyHeld);
        state.keyDown  = pad.dpadDown  || (ry && !gRyHeld);
        gLxHeld = lx; gRxHeld = rx;
        gLyHeld = ly; gRyHeld = ry;
    }

    return !gQuitRequested;
}

} // namespace uaro

// ==================================================================== fs ===

namespace uaro::fs {

std::optional<std::vector<u8>> read_file(const std::string& path) {
    const std::string fullPath = "rom:/" + path;
    nn::fs::FileHandle handle;
    nn::Result r = nn::fs::OpenFile(&handle, fullPath.c_str(), nn::fs::OpenMode_Read);
    if (r.IsFailure()) return std::nullopt;

    int64_t fileSize = 0;
    r = nn::fs::GetFileSize(&fileSize, handle);
    if (r.IsFailure()) { nn::fs::CloseFile(handle); return std::nullopt; }

    std::vector<u8> data(size_t(fileSize));
    r = nn::fs::ReadFile(handle, 0, data.data(), data.size());
    nn::fs::CloseFile(handle);
    if (r.IsFailure()) return std::nullopt;
    return data;
}

std::string base_dir() {
    return "rom:/";
}

std::string pref_dir(const std::string& /*org*/, const std::string& /*app*/) {
    return "save:/";
}

std::string data_dir() {
    // Writable data root (real data is routed through consoleServices().save*); the save mount is the
    // writable location. base_dir() (rom:/) is read-only.
    return "save:/";
}

std::string system_language() {
    nn::settings::LanguageCode code = nn::settings::GetLanguageCode();
    std::string tag(code.string);
    if (tag.size() >= 2) return tag.substr(0, 2);
    return "en";
}

} // namespace uaro::fs
