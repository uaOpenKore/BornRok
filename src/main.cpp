// uaRO cross-platform client — entry point.
// SDL's main hijacking is disabled in the platform layer (SDL_MAIN_HANDLED), so
// this is the real main on desktop. Mobile entry points (Android activity / iOS
// app delegate) are added with the mobile backends in v6.
//
//   uaro_client                 -> patcher, then login (normal entry)
//   uaro_client --no-patch      -> skip the patcher, go straight to login (content-maker GRF test)
//   uaro_client --test          -> dev fallbacks (auto-load prontera / sprite-test scene)
//   uaro_client --view          -> content browser: 2D sprites vs RoM 3D models (S.)
//   uaro_client --view2d        -> sprite-effect (.str) binding tool: bind/unbind to item/action (S.)
//   uaro_client <map>           -> render a map; data resolved via data.ini (dev viewer)
//   uaro_client <map> <dataDir> -> as above, with an explicit data directory
//
// --test only enables the offline fallbacks when no server list/data is found; without it a
// normal launch always runs the patcher first (which fetches missing files), then login.
#include <string>

// Version string from the single source of truth (Client/VERSION -> CMake add_compile_definitions).
// Fallback keeps a non-CMake compile (e.g. a quick offline test) building.
#ifndef UARO_VERSION_STR
#define UARO_VERSION_STR "0.0.0.0"
#endif

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "platform/FileSystem.hpp"

#if defined(__ANDROID__)
// On Android there is no console main: the SDL Java activity loads the .so and calls SDL_main.
// Including SDL_main.h (with SDL_MAIN_HANDLED NOT defined for this TU) renames our main() to
// SDL_main and pulls in the JNI entry glue. Desktop keeps the real main (SDL_MAIN_HANDLED in the
// platform layer), so this is Android-only.
#include <SDL3/SDL_main.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS || TARGET_OS_SIMULATOR
// iOS/iPadOS has no console main either: SDL provides UIApplicationMain and calls our SDL_main.
// Same mechanism as Android -- include SDL_main.h so main() is renamed to SDL_main. macOS desktop
// (TARGET_OS_IOS == 0) skips this and keeps the real main. (iOS port prep)
#include <SDL3/SDL_main.h>
#endif
#endif

int main(int argc, char** argv) {
    // Mirror the log to output_log.txt, truncated each launch (S.). The release build has no console,
    // so this file is where the log lives. Use data_dir() (writable) not base_dir(): under a read-only
    // MSIX/Flatpak install the exe dir can't be written, so the log silently vanished exactly when we
    // needed it to debug the packaged build. data_dir()==base_dir() on a loose-exe build (unchanged).
    uaro::log::set_log_file(uaro::fs::data_dir() + "/output_log.txt");
    // Build stamp so we can tell whether a running client actually includes the latest changes (S.: a
    // stale/incremental build silently kept old .obj -> fixes "не изменились"). __DATE__/__TIME__ are the
    // COMPILE time of this TU, which a real rebuild always bumps.
    uaro::log::info("build: {} {}", __DATE__, __TIME__);
    // Show the exe dir vs the writable data dir up front: under a packaged (MSIX/Flatpak) install
    // these DIFFER (exe dir read-only), and the writable one is where the patcher downloads + where
    // the VFS must mount from. Decisive first line for "патчер скачал, но подключить не смог".
    uaro::log::info("paths: base_dir='{}' data_dir='{}'", uaro::fs::base_dir(), uaro::fs::data_dir());

    uaro::AppConfig cfg;
    if (argc > 0 && argv[0]) cfg.selfExePath = argv[0];  // patcher self-update target
    // Version comes from the ONE source of truth (Client/VERSION -> -DUARO_VERSION_STR via CMake), so the
    // title reads "UaRO 0.0.11.0" (S.: "версия из одной переменной, вид 0.0.11.0").
    const std::string appTitle = std::string("BornRok ") + UARO_VERSION_STR;
    cfg.title = appTitle;
    cfg.width = 1280;
    cfg.height = 720;

    // Collect positional args (map, dataDir) separately from flags (--test).
    std::string positional[2];
    int np = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--test") {
            cfg.testMode = true;
        } else if (a == "--no-patch") {
            cfg.noPatch = true;
        } else if (a == "--view") {
            cfg.viewerMode = true;
        } else if (a == "--view2d") {
            cfg.viewer2dMode = true;  // sprite-effect (.str) binding tool (S.)
        } else if (np < 2) {
            positional[np++] = a;
        }
    }
    if (np >= 1) {
        cfg.mapName = positional[0];
        cfg.title = appTitle + " - " + cfg.mapName;
    }
    if (np >= 2) cfg.dataDir = positional[1];  // optional override of where data.ini lives

    uaro::Application app;
    return app.run(cfg);
}
