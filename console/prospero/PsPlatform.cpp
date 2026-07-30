// PsPlatform.cpp -- PlayStation (PS5 "Prospero" / PS4 "Orbis") implementation of uaro::Platform /
// uaro::Window / uaro::fs.
//
// SCAFFOLD. NOT in any CMake target and includes NO proprietary SDK header, so it does NOT compile
// as-is. It is the EXACT shape the licensed developer fills in: every SDK point is `// TODO(SDK):`.
// Everything else -- how the class plugs into the engine -- is final and matches SDL/WinRT/NX.
//
// PS5 and PS4 share the SAME Sony API families (scePad, sceUserService, sceSaveData, sceVideoOut,
// sceSystemService) with per-generation differences, so this one slot covers both; where they
// diverge it is noted. Integration is identical to the WinRT/Xbox and NX backends: the build SWAPS
// this translation unit in for src/platform/sdl/SdlPlatform.cpp. app/, game/, render/ are untouched.
//
// The engine expects three things from a platform TU:
//   1. uaro::Window   -- owns the native video-out surface bgfx binds its AGC/GNM swapchain to.
//   2. uaro::Platform -- OS init + per-frame event/input pump into InputState, rumble, cursor.
//   3. uaro::fs::*    -- read_file / base_dir / pref_dir / system_language.

#include "platform/Platform.hpp"
#include "platform/Window.hpp"
#include "platform/Input.hpp"
#include "platform/FileSystem.hpp"
#include "core/Log.hpp"

// TODO(SDK): Sony SDK headers (NDA, licensed machine only):
//   #include <pad.h>              // scePad (DualSense / DualShock 4) + touchpad + rumble/trigger
//   #include <user_service.h>     // sceUserService: initial user + username
//   #include <save_data.h>        // sceSaveData: mount/unmount save directories (in PsConsoleServices)
//   #include <system_service.h>   // sceSystemService: lifecycle events (entitlement/resume/etc.)
//   #include <video_out.h>        // sceVideoOut: the display surface (bgfx nwh)

namespace uaro {

// ------------------------------------------------------------------ Window ---
// handle_ (void* in Window.hpp) stores the native video-out handle bgfx binds the AGC/GNM swapchain to.

bool Window::open(const WindowConfig& cfg) {
    // TODO(SDK): sceVideoOutOpen(...) + register buffers; the output resolution is 1920x1080 (PS4)
    // or up to 3840x2160 (PS5) fixed by the OS -- cfg.width/height are advisory; pixelSize() reports
    // the real buffer size. bgfx gets the video-out handle via PlatformData.nwh with the AGC (PS5) /
    // GNM (PS4) renderer.
    //   handle_ = <native video-out surface pointer>;
    (void)cfg;
    log::info("ps: window open (TODO(SDK): sceVideoOut surface -> bgfx nwh)");
    return handle_ != nullptr;
}

void Window::close() {
    // TODO(SDK): sceVideoOutClose(handle).
    handle_ = nullptr;
}

NativeWindow Window::native() const {
    NativeWindow n;
    n.nwh = handle_;   // sceVideoOut surface -> bgfx PlatformData.nwh (renderer = AGC/GNM, NDA)
    n.ndt = nullptr;
    return n;
}

void Window::pixelSize(int& w, int& h) const {
    // TODO(SDK): report the video-out buffer size (1080p / 4K per title config + Pro/PS5 mode).
    w = 1920;
    h = 1080;
}

void Window::setFullscreen(bool /*on*/) { /* N/A: the OS owns the display mode on console. */ }

void Window::setTextInput(bool on) {
    if (on == textInputOn_) return;
    textInputOn_ = on;
    // TODO(SDK): the on-screen keyboard is a MODAL dialog on PlayStation (sceImeDialog), not an
    // inline IME. Same guidance as NX: don't raise it per focus here; open it from a dedicated UI
    // "edit" action and feed the returned UTF-8 string into InputState.text in one shot.
}

Window::~Window() { close(); }

// ---------------------------------------------------------------- Platform ---

Platform::~Platform() { shutdown(); }

bool Platform::init() {
    // TODO(SDK): sceUserServiceInitialize(); scePadInit(); get the initial user
    //   (sceUserServiceGetInitialUser(&userId)); scePadOpen(userId, SCE_PAD_PORT_TYPE_STANDARD, 0, ...)
    //   -> pad handle stored for pump(); sceSystemServiceParamGet for lifecycle. Deadzone/thresholds
    //   mirror SdlPlatform so the gamepad UX (#115) behaves identically.
    inited_ = true;
    log::info("ps: platform init (TODO(SDK): sceUserService + scePad + sceSystemService)");
    return true;
}

void Platform::shutdown() {
    if (!inited_) return;
    // TODO(SDK): scePadClose(handle); sceUserServiceTerminate().
    inited_ = false;
}

bool Platform::createWindow(const WindowConfig& cfg) { return window_.open(cfg); }

void Platform::setCursorVisible(bool /*visible*/) { /* No OS cursor; the game draws the RO cursor. */ }

void Platform::rumble(unsigned short lowFreq, unsigned short highFreq, unsigned int durationMs) {
    // TODO(SDK): ScePadVibrationParam{ largeMotor = highByte(lowFreq), smallMotor = highByte(highFreq) };
    //   scePadSetVibration(handle, &param). PS has no built-in duration -> the engine already passes
    //   durationMs; stop with a zero-vibration after durationMs (a timer or on the next matching event).
    //   (PS5 DualSense: you may also drive the adaptive triggers / haptics here later.)
    (void)lowFreq; (void)highFreq; (void)durationMs;
}

// Per-frame pump: clear edges, read lifecycle + pad + touchpad into `state`. The gamepad mapping is
// the SAME neutral, position-based layout SdlPlatform fills, so the whole gamepad UX (#115) works
// unchanged -- only the source API is scePad.
bool Platform::pump(InputState& state) {
    state.pad.south = state.pad.east = state.pad.west = state.pad.north = false;
    state.pad.dpadUp = state.pad.dpadDown = state.pad.dpadLeft = state.pad.dpadRight = false;
    state.pad.l3 = state.pad.r3 = state.pad.start = state.pad.back = false;
    state.pad.touchPress = false;
    state.resized = false;

    // --- Lifecycle (resume / entitlement / background) -----------------------------------------
    // TODO(SDK): pump sceSystemServiceReceiveEvent(). Map: an app-suspend/background event ->
    // AppLifecycle::Constrained or ::Suspended (the engine flushes saves + stops the frame); resume ->
    // Resumed (RenderDevice reacquires the swapchain); a system quit/return-to-OS -> QuitRequested.
    // Dispatch through the installed console services (see PsConsoleServices::dispatchLifecycle).

    // --- Gamepad (scePad: DualSense / DualShock 4) ---------------------------------------------
    InputState::Gamepad& pad = state.pad;
    pad.connected = true;                          // TODO(SDK): ScePadData.connected
    pad.type = InputState::PadType::PlayStation;   // glyphs use the PlayStation set
    // TODO(SDK): ScePadData d; scePadReadState(handle, &d); then translate. Keep binds POSITION-based
    // exactly like SdlPlatform (only the on-screen glyph differs per pad):
    //   Cross  (bottom) -> pad.south ;  Circle (right) -> pad.east
    //   Square (left)   -> pad.west   ;  Triangle (top) -> pad.north
    //   d-pad -> pad.dpadUp/Down/Left/Right (+ *Held) ; L1/R1 -> pad.l1/r1 ; L2/R2 -> pad.l2/r2
    //   L3 -> pad.l3 ; R3 -> pad.r3 ; OPTIONS -> pad.start ; TOUCHPAD-CLICK or CREATE/SHARE -> pad.back
    //   sticks: ScePadData.leftStick/rightStick are 0..255 (128 centre); map to [-1,1], apply the same
    //   radial deadzone as SdlPlatform, and FLIP Y so up = +y (InputState contract).
    //   Set the *Held mirrors on face/dpad for repeat navigation.

    // --- DualSense / DS4 touchpad -> our Gamepad touch fields (already wired, #102) --------------
    // TODO(SDK): from ScePadData.touchData, take finger 0: set pad.touchActive, pad.touchX/Y in [0,1]
    //   (x = touch.x / 1920, y = touch.y / 943 for DS4; use the reported resolution), and pad.touchPress
    //   on the touchpad button edge. The engine already uses these as a pointer (touchpad-as-mouse).

    return !quitRequested_;
}

} // namespace uaro

// ------------------------------------------------------------------ fs ---
namespace uaro::fs {

std::optional<std::vector<u8>> read_file(const std::string& path) {
    // TODO(SDK): read from the app package. Game assets ship inside the title; the app content is
    // mounted at "/app0/" on PlayStation, so `path` resolves under that. Return nullopt if absent.
    (void)path;
    return std::nullopt;
}

std::string base_dir() {
    // Read-only app content root -> the VFS/data.ini resolution (base_dir + "data/...") lands inside
    // the package.
    return "/app0/";  // TODO(SDK): confirm the app content mount point
}

std::string pref_dir(const std::string& /*org*/, const std::string& /*app*/) {
    // PlayStation writes user data through save-data mounts (handled by PsConsoleServices), NOT loose
    // files. Config/hotbar already route through consoleServices().save*(), so this should not be used
    // for writes. Return the temp/download mount for any incidental reads.
    return "/download0/";  // TODO(SDK): or the mounted save-data path from PsConsoleServices
}

std::string data_dir() {
    // Writable data root (real data routes through consoleServices().save*); base_dir() (/app0) is
    // read-only. Return the writable download/save mount.
    return "/download0/";  // TODO(SDK): the mounted save-data path from PsConsoleServices
}

std::string system_language() {
    // TODO(SDK): sceSystemServiceParamGetInt(SCE_SYSTEM_SERVICE_PARAM_ID_LANG, &lang) -> map the Sony
    // language enum to a 2-letter ISO ("ja","en","ko","ru",...). Auto-picks the UI language.
    return "en";
}

} // namespace uaro::fs
