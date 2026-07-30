// NxMain.cpp -- Nintendo Switch entry point.
//
// Builds an AppConfig and runs the SAME uaro::Application as the desktop
// build. NX-specific setup: install NxConsoleServices, mount RomFs for baked
// assets, disable the patcher. The entry symbol is nnMain (no argc/argv).

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "platform/ConsoleServices.hpp"
#include "NxConsoleServices.hpp"

#include <cstdint>
#include <nn/fs.h>
#include <nn/fs/fs_Rom.h>
#include <nn/oe.h>

extern "C" void nnMain() {
    // Switch has no writable loose directory; the log goes through the save
    // container or SDK debug output. Keep it minimal.
    uaro::log::info("nx: build {} {}", __DATE__, __TIME__);

    // --- Mount the read-only RomFs (baked game assets) ---
    // All asset paths resolved by the VFS will be under "rom:/".
    // MountRom requires a persistent file system cache buffer.
    size_t romCacheSize = 0;
    void* romCache = nullptr;
    nn::Result r = nn::fs::QueryMountRomCacheSize(&romCacheSize);
    if (r.IsSuccess() && romCacheSize > 0) {
        romCache = new uint8_t[romCacheSize];
        r = nn::fs::MountRom("rom", romCache, romCacheSize);
    }
    if (r.IsFailure()) {
        delete[] static_cast<uint8_t*>(romCache);
        uaro::log::error("nx: cannot mount RomFs -- no assets available");
        return;
    }

    // --- Install NX console services + mount save-data container ---
    static uaro::NxConsoleServices nx;
    if (!nx.mount()) {
        // mount() already logged the reason. Save-data is needed for config
        // persistence, but the game can still run with defaults.
        uaro::log::warn("nx: save-data not mounted; running with defaults");
    }
    uaro::setConsoleServices(&nx);

    // --- Build AppConfig (identical shape to desktop, no CLI) ---
    uaro::AppConfig cfg;
    cfg.title = "UaRO Alpha12";
    cfg.width = 1280;    // advisory; pixelSize reports real docked/handheld size
    cfg.height = 720;
    cfg.noPatch = true;  // assets are baked into the title image

    // --- Run the identical engine loop ---
    // Application owns the NX Platform (compiled from NxPlatform.cpp instead
    // of SdlPlatform.cpp) and drives update/render on the NVN bgfx backend.
    {
        uaro::Application app;
        app.run(cfg);
    }

    // --- Cleanup ---
    nx.unmount();
    nn::fs::Unmount("rom");
    delete[] static_cast<uint8_t*>(romCache);
}
