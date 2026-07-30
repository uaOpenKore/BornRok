// uaRO — Xbox (Dev Mode) / UWP entry.
//
// SDL3 dropped the WinRT/UWP backend SDL2 had, so the UWP app model that Xbox Developer Mode runs
// cannot reuse platform/sdl. This is the native WinRT entry (IFrameworkView). It is deliberately THIN:
// the real backend lives in src/platform/winrt/WinrtPlatform.cpp, which implements the same concrete
// Platform::*/Window::* the engine already links. So Run() does NOT re-implement the frame loop — it
// just constructs uaro::Application and calls run(), whose `while (platform_.pump(input_))` now pumps
// the CoreWindow dispatcher (WinrtPlatform::pump) instead of SDL. app/ + game/ stay untouched.
//
// Compiles only with the Windows SDK / C++-WinRT toolchain; never part of the desktop/Android builds.
// See docs/xbox-uwp-build-km.md for the Windows-side build/deploy steps.

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>

#include "app/Application.hpp"
#include "platform/winrt/WinrtConsoleServices.hpp"

using namespace winrt;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;
using namespace Windows::Foundation;

namespace uaro {

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    IFrameworkView CreateView() { return *this; }

    void Initialize(CoreApplicationView const& /*view*/) {
        // Route save-data + lifecycle (Suspending/Resuming) through the UWP services before the engine
        // starts, so config/hotbar/settings land in the app's LocalFolder.
        installWinrtConsoleServices();
    }

    void SetWindow(CoreWindow const& /*window*/) {
        // Nothing to do: WinrtPlatform::Window::open() adopts CoreWindow::GetForCurrentThread() and
        // binds the input handlers itself, keeping all WinRT event wiring in one translation unit.
    }

    void Load(hstring const& /*entryPoint*/) {}

    void Run() {
        AppConfig cfg;
        cfg.title = "uaRO";
        cfg.width = 1920;   // hint only; RenderDevice takes the real size from Window::pixelSize()
        cfg.height = 1080;
        cfg.vsync = true;
        // Bring-up defaults: skip the patcher (its libcurl/network path is a separate UWP task) and
        // allow the offline fallbacks so the first Xbox boot can render without a full data fetch.
        // Flip both off once the patcher + login are validated on UWP.
        cfg.noPatch = true;
        cfg.testMode = true;

        Application app;
        app.run(cfg);
    }

    void Uninitialize() {}
};

}  // namespace uaro

int __stdcall wWinMain(void*, void*, wchar_t*, int) {
    winrt::init_apartment();
    CoreApplication::Run(winrt::make<uaro::App>());
    return 0;
}
