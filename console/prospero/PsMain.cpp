// PsMain.cpp -- PlayStation (PS5/PS4) entry point. Mirrors src/main.cpp: build an AppConfig and run
// the SAME uaro::Application. The only PS-specific steps are (1) install PS console services + mount
// the save-data before the loop, and (2) no launch flags (assets are baked into the title).
//
// SCAFFOLD. NOT in any CMake target; the SDK bits are `// TODO(SDK):`. PlayStation uses a standard
// C main(), so unlike NX (nnMain) this looks almost identical to the desktop entry.

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "platform/ConsoleServices.hpp"
#include "PsConsoleServices.hpp"

int main(int /*argc*/, char** /*argv*/) {
    uaro::log::info("ps: build {} {}", __DATE__, __TIME__);

    // 1. Install PS console services + mount the user's save-data BEFORE the engine starts, so
    //    Application::loadConfig()/loadHotbar() (which call consoleServices().saveRead) see it.
    static uaro::PsConsoleServices ps;
    if (!ps.mount()) {
        // TODO(SDK): no user / save-data unavailable -> show the system message and continue with
        // defaults (or exit). The user selector is handled inside mount().
        uaro::log::warn("ps: save-data not mounted; running with defaults");
    }
    uaro::setConsoleServices(&ps);

    // 2. Same AppConfig the desktop builds, minus the CLI. Patcher OFF (assets baked into the title).
    uaro::AppConfig cfg;
    cfg.title = "UaRO Alpha12";
    cfg.width = 1920;   // advisory; PsWindow::pixelSize reports the real video-out buffer size
    cfg.height = 1080;
    cfg.noPatch = true;

    // 3. Run the identical engine loop. Application owns the PS Platform (PsPlatform.cpp is the
    //    swapped platform TU) and drives update/render on the AGC (PS5) / GNM (PS4) bgfx backend.
    uaro::Application app;
    const int rc = app.run(cfg);

    ps.unmount();  // commit + close the save-data on a clean exit
    return rc;
}
