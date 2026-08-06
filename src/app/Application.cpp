#include "app/Application.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "core/Lang.hpp"
#include "core/Log.hpp"
#include "formats/Webp.hpp"
#include "core/time/Clock.hpp"
#include "game/BootScene.hpp"
#include "game/CharacterActor.hpp"
#include "game/LoginScene.hpp"
#include "game/MapScene.hpp"
#include "game/PatcherScene.hpp"
#include "game/ViewerScene.hpp"
#include "game/ViewerScene2D.hpp"
#include "net/Socket.hpp"
#include "patcher/ContentQuality.hpp"  // contentPlatformId / isBakedTokenPlatform (no-patcher decision)
#include "platform/FileSystem.hpp"
#include "render/SamplerFilter.hpp"
#include "ui/Widgets.hpp"  // ui::text_input_wanted() -> drive the OS keyboard on touch (#102)

namespace uaro {

namespace {
// A "baked" distribution ships ALL content inside the package -> the patcher must NOT run at all (S.
// 2026-07-29). The content-platform TOKEN classifies the closed consoles + iOS + Steam Deck; MSIX
// (Windows) and Flatpak (Linux) share the desktop token, so they're caught by fs::is_packaged_install()
// (itself gated to desktop, so Android — still a patching platform — is never mistaken for a package).
bool isBakedContentDistribution() {
    return isBakedTokenPlatform(contentPlatformId()) || fs::is_packaged_install();
}
}  // namespace

int Application::run(const AppConfig& cfg) {
    if (!platform_.init()) return 1;

    WindowConfig wc;
    wc.title = cfg.title;
    wc.width = cfg.width;
    wc.height = cfg.height;
    if (!platform_.createWindow(wc)) {
        platform_.shutdown();
        return 2;
    }

    int pw = cfg.width, ph = cfg.height;
    platform_.window().pixelSize(pw, ph);

    RenderConfig rc;
    rc.width = pw;
    rc.height = ph;
    rc.vsync = cfg.vsync;
    if (!render_.init(platform_.window().native(), rc)) {
        platform_.shutdown();
        return 3;
    }

    assetDir_ = fs::base_dir();
    selfExePath_ = cfg.selfExePath;
    sprites_.init(assetDir_);  // non-fatal if shaders are missing (clear-only)
    font_.init(assetDir_);
    render_.initPostProcess(assetDir_);  // HDR tonemap pipeline (#111); no-op if its shader is absent
    net::Socket::globalInit();

    // Resolve where data.ini + GRFs live (cfg.dataDir, else cwd, else the executable dir), then
    // build the VFS. Kept in dataDir_ so the patcher can trigger a remount at runtime.
    dataDir_ = cfg.dataDir;
#if defined(__ANDROID__)
    // Android has no cwd/exe dir: GRFs + data.ini live in the app's writable storage (the patcher
    // downloads them there). The UI (fonts/texts/shaders/clientinfo) is embedded in the binary, so
    // the client boots and connects with an empty data dir on first run.
    if (dataDir_.empty()) {
        dataDir_ = fs::data_dir();
        if (!dataDir_.empty() && (dataDir_.back() == '/' || dataDir_.back() == '\\'))
            dataDir_.pop_back();
    }
#else
    if (dataDir_.empty()) {
        if (fs::read_file("data.ini")) {
            dataDir_ = ".";
        } else {
            // Writable data root: the exe dir on a portable/loose-exe build, else a per-user dir
            // (identical to assetDir_ when the exe dir is writable). Under a read-only MSIX install
            // this diverges to the package's writable LocalCache so the patcher can download here and
            // the VFS mounts what it downloaded (S. 2026-07-24 — MSIX patcher downloaded nothing).
            dataDir_ = fs::data_dir();
            if (!dataDir_.empty() && (dataDir_.back() == '/' || dataDir_.back() == '\\'))
                dataDir_.pop_back();
        }
    }
#endif
    mountGameData();

    // Load the server list the original client uses (data\sclientinfo.xml; the
    // login screen connects to it).
    reloadClientInfo();

    // Load the original client's login UI skin (falls back to flat panels if absent).
    uiSkin_.init(vfs_);
    // Item display names + icon resnames for the merchant shop (best-effort; the shop
    // falls back to "#<id>" and no icon when the GRF tables are absent).
    itemDb_.load(vfs_);
    // Data-from-GRF lub tables (status-effect text, sprite names): run the client's compiled .lub
    // scripts in an embedded Lua VM so the client is driven off the GRF like the original instead of
    // hardcoded C++ tables. Best-effort -- callers fall back to their hardcoded paths if a lub is absent.
    grfData_.load([this](const std::string& vpath) { return vfs_.read(vpath); });
    // Let CharacterActor resolve any player job class NOT in its hardcoded table (3rd/4th jobs:
    // Rune Knight, Arch Bishop, Royal Guard, Genetic, Shadow Chaser, Hyper Novice, ...) from the GRF's
    // jobname lua instead of collapsing to novice (S. 2026-07-26 Korean body-name list).
    setJobSpriteResolver([this](u16 classId) { return grfData_.jobSpriteName(classId); });
    audio_.init();  // open the audio device (#103); silent no-op if built without audio
    loadConfig();   // apply saved sound volumes (settings/game.cfg) before any scene plays BGM (#104)
#if defined(__ANDROID__)
    // Post-processing (HDR/FSR/grade/god-rays/volumetric light) is FORCED OFF on Android: budget mobile
    // GLES drivers (Mali/Adreno) are fragile on the offscreen RGBA16F/D24S8 render targets these passes
    // create -> broken render or a null-deref crash in bgfx GL submit. The plain backbuffer path is used.
    // The settings rows are hidden on Android too. (S.: mobile-GPU render-glitch audit)
    hdr_ = false;
    fsr_ = 1.0f;
    godrayMode_ = 0;
#else
    render_.setHdr(hdr_);      // apply the saved HDR toggle (#111)
    hdr_ = render_.hdr();      // reconcile: a saved "on" is dropped if the GPU can't render RGBA16F
    render_.setFsr(fsr_);      // apply the saved FSR upscale factor (#111)
    fsr_ = render_.fsr();
    render_.setGrade(brightness_, contrast_);  // apply the saved brightness/contrast grade (#111)
    render_.setGodrayMode(godrayMode_);   // apply the saved volumetric-light mode (#117)
    godrayMode_ = render_.godrayMode();   // reconcile: dropped if the shader is missing
#endif
    // Normals toggle (S.): resolve the auto default once — x1.5 on a real GPU, Off on a
    // software rasterizer (WARP) where the extra shading would crawl. A saved value wins.
    if (normalsMode_ < 0.0f) normalsMode_ = render_.isSoftwareRenderer() ? 0.0f : 1.5f;
    g_normalsFactor = normalsMode_ * 0.5f;

    // Pick the starting scene:
    //  - an explicit map name on the CLI -> offline map viewer (dev path);
    //  - else a server list present     -> the login screen (normal entry);
    //  - else map data present          -> auto-load prontera;
    //  - else the sprite test scene.
    if (cfg.viewer2dMode) {
        // --view2d: the sprite-effect (.str) binding tool (S.). Same offline data mount as --view.
        scenes_.push(*this, std::make_unique<ViewerScene2D>());
    } else if (cfg.viewerMode) {
        // --view: the content browser (mobs/NPC 2D-vs-3D pairing tool, S.). Data is already
        // mounted (data.ini GRFs + RoM.zip); no server/session needed.
        scenes_.push(*this, std::make_unique<ViewerScene>());
    } else if (!cfg.mapName.empty()) {
        // Explicit map on the CLI: offline map viewer (a dev path, works in any mode).
        scenes_.push(*this, std::make_unique<MapScene>(cfg.mapName));
    } else if (cfg.testMode) {
        // --test: allow the offline fallbacks (auto-load prontera if data is present, else the
        // sprite-test scene) instead of the patcher.
        if (vfs_.exists("data/prontera.rsw")) {
            log::info("--test: no server list; data found -> auto-loading map 'prontera'");
            scenes_.push(*this, std::make_unique<MapScene>("prontera"));
        } else {
            log::warn("--test: no server list and no map data -> sprite-test BootScene.");
            scenes_.push(*this, std::make_unique<BootScene>());
        }
    } else if (cfg.noPatch) {
        // --no-patch: straight to login, no patcher (content maker testing their own GRFs so the
        // patcher doesn't overwrite them). clientinfo was already loaded above.
        log::info("--no-patch: skipping the patcher, going straight to login");
        scenes_.push(*this, std::make_unique<LoginScene>());
    } else if (isBakedContentDistribution()) {
        // Baked distribution (closed console / iOS / packaged MSIX / Flatpak): ALL content ships inside
        // the package, so there is no patcher at all — straight to login on the baked content. (S.
        // 2026-07-29: "уберём патчер ... для консолей, мсих, флетпака, ios".) Loose desktop, Steam Deck
        // and Android still fall through to the patcher below.
        log::info("baked content distribution -> skipping the patcher, going straight to login");
        scenes_.push(*this, std::make_unique<LoginScene>());
    } else {
        // Normal launch: always run the patcher first. It fetches any missing files (including
        // the server list), reloads clientinfo, then pushes LoginScene itself. Not falling back
        // to a test scene when files are absent (S.: "должен работать патчер, тест только по --test").
        scenes_.push(*this, std::make_unique<PatcherScene>());
    }

    // Console lifecycle: a console backend drives suspend/resume through ConsoleServices;
    // on desktop this callback never fires (DesktopConsoleServices no-op). (console-port-prep)
    consoleServices().onLifecycle([this](AppLifecycle s) { lifecycle_ = s; });

    log::info("entering main loop");
    Clock clock;
    while (platform_.pump(input_)) {
        const auto frameStart = std::chrono::steady_clock::now();

        // While the OS has the title suspended (console sleep), skip the frame + audio and idle
        // cheaply; keep pumping so we notice Resumed/Quit. Never taken on desktop (always Running).
        if (lifecycle_ == AppLifecycle::Suspended) {
            if (quitRequested_ || lifecycle_ == AppLifecycle::QuitRequested) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (lifecycle_ == AppLifecycle::QuitRequested) break;

        if (input_.resized) render_.resize(input_.width, input_.height);

        // Apply the saved per-axis stick inversion (GamePad Setup) at the source, before any scene reads
        // the pad, so movement / camera / cursor / menu-nav all see the corrected axes (#102/#115).
        if (padInvertLX_) input_.pad.lx = -input_.pad.lx;
        if (padInvertLY_) input_.pad.ly = -input_.pad.ly;
        if (padInvertRX_) input_.pad.rx = -input_.pad.rx;
        if (padInvertRY_) input_.pad.ry = -input_.pad.ry;

        // Render-scale (FSR/SSAA) affects only the off-screen 3D view; the UI and the mouse stay at
        // native window resolution, so no mouse-coordinate scaling is needed here anymore (#111).

        const double dt = clock.restart();
        audio_.update();  // keep BGM looping in every scene (login/char-select/in-game) (#103)
        // Default to a non-world frame; only the in-game 3D scene opts back into render-scale (below,
        // in GameScene::update) so FSR/SSAA never reroutes the UI-only login/char-select/patcher views
        // through the scaled off-screen (#111).
        render_.setWorldScene(false);
        scenes_.update(*this, dt);
        scenes_.applyPending(*this);  // process deferred scene pops

        render_.beginFrame();
        scenes_.render(*this);
        // UI click sound (S. sound audit): any ui::button / ui::imageButton activated this frame
        // flagged it; play the RO button chime once, non-positional. Works across all scenes.
        if (ui::takeButtonClicked())
            audio_.playSfx(vfs_, "\xB9\xF6\xC6\xB0\xBC\xD2\xB8\xAE.wav", 0.6f);
        // Brightness/contrast is now a shader grade in the post pass (RenderDevice::endFrame), so it
        // can raise contrast too -- the old fullscreen overlay (darken/lighten only) is gone.
        render_.endFrame();

        // Native OS keyboard on touch devices (#102): raise it only while a text field is focused
        // (any focused TextField::update set the flag this frame). Desktop keeps text input always on,
        // so we only drive it in touch mode -- no behaviour change for keyboard+mouse.
        const bool wantKbd = ui::text_input_wanted();
        ui::text_input_wanted() = false;  // reset for next frame
#if defined(__ANDROID__)
        // Android has no physical keyboard: raise the soft keyboard whenever a text field is focused,
        // regardless of game mode. Gating on gameMode_==1 left the login/password fields with no way to
        // summon it in Standard mode. (S.: "в поле ввода не вызывается клавиатура".)
        platform_.window().setTextInput(wantKbd);
#else
        if (gameMode_ == 1) platform_.window().setTextInput(wantKbd);
#endif

        if (scenes_.empty() || quitRequested_) break;

        // FPS-lock (#104): with vsync off (or a strict cap), sleep to hold the target frame time.
        if (fpsLimit_ > 0) {
            const auto target = std::chrono::duration<double>(1.0 / fpsLimit_);
            const auto spent = std::chrono::steady_clock::now() - frameStart;
            if (spent < target)
                std::this_thread::sleep_for(target - spent);
        }
    }
    log::info("exiting");

    // Deterministic teardown: scenes (textures) -> sprites/font -> device -> platform.
    scenes_.clear(*this);
    CharacterActor::clearSharedCache();  // free the shared sprite-frame textures while bgfx is alive
    sprites_.shutdown();
    font_.shutdown();
    uiSkin_.destroy();
    render_.shutdown();
    platform_.shutdown();
    net::Socket::globalShutdown();
    return 0;
}

// ---- Global game config (settings/game.cfg): sound volumes (#104) --------------------------
namespace {
std::filesystem::path gameCfgPath() {
#if defined(__ANDROID__)
    // base_dir() is "./" on Android (cwd "/" is read-only), so every config write failed silently and
    // NOTHING persisted (sound volumes, video, language, ...). Store config in the app's writable
    // storage -- the same pref_dir the patcher downloads GRFs into. (S.: "настройки звука не
    // сохраняются, наверное это относится ко всем кфг файлам".)
    std::string root = fs::data_dir();
    if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    return std::filesystem::path(root) / "settings" / "game.cfg";
#else
    // data_dir() == base_dir() on a writable/loose-exe build (unchanged), but a per-user writable dir
    // under a read-only MSIX install, so settings actually persist there (S. 2026-07-24).
    return std::filesystem::path(fs::data_dir()) / "settings" / "game.cfg";
#endif
}
}  // namespace

void Application::applyVolumes() {
    audio_.setMasterVolume(masterVol_);
    audio_.setBgmVolume(bgmVol_);
    audio_.setSfxVolume(sfxVol_);
}

void Application::mountGameData() {
    // Build the VFS like the original client: the loose data/ folder has top priority, then the
    // GRFs listed in data.ini (in order). Safe to call again after clear() to remount.
    vfs_.mountDir(dataDir_ + "/data");  // loose overrides win
    vfs_.mountDir(dataDir_);  // client root: loose BGM/ (mp3), System/, etc. live beside data/ (#103)
    // ALL game content lives in ONE content/ folder now (S. 2026-07-28: "весь загружаемый контент
    // патчером в папку content для всех платформ"). The patcher writes EVERY pack (root/ + quality +
    // event packs) AND data.ini there as flat files; a bundled platform bakes the same folder next to
    // the binary. Mount each content dir: data.ini gives the pack LOAD ORDER (index 0 = highest priority;
    // the VFS is first-mounted-wins), then any OTHER .zip/.grf in the folder (a drop-in pack not listed
    // in data.ini, sorted), then a loose data/ override. GRO.grf / RoM.zip keep their content-source tags
    // for the source switcher; everything else is OUR (Uaro) content. contentCacheDir() (the patcher's
    // writable target — desktop <pref>/content, console the SDK cache) is mounted first (higher priority),
    // then base_dir()/content (read-only baked content next to the binary on bundled Steam/Switch/PS/Xbox).
    namespace stdfs = std::filesystem;
    std::error_code ec;
    std::vector<std::string> contentDirs;
    if (const std::string cc = consoleServices().contentCacheDir(); !cc.empty())
        contentDirs.push_back(cc);
    if (const std::string bd = fs::base_dir(); !bd.empty()) {
        const std::string bc = bd + "/content";
        if (std::find(contentDirs.begin(), contentDirs.end(), bc) == contentDirs.end())
            contentDirs.push_back(bc);
    }
    auto contentTag = [](const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
        if (base == "GRO.grf") return ContentSource::Gro;   // official content-source (switcher tag)
        if (base == "RoM.zip") return ContentSource::Rom;   // RoM content-source (switcher tag)
        return ContentSource::Uaro;                          // our content
    };
    for (const std::string& cd : contentDirs) {
        if (!stdfs::exists(cd, ec)) continue;
        int mounted = 0;
        // data.ini = the pack LOAD ORDER (priority); its listed packs mount first, from THIS content dir.
        if (auto ini = fs::read_file(cd + "/data.ini"))
            mounted += vfs_.mountDataIni(std::string(ini->begin(), ini->end()), cd);
        // Any other .zip/.grf in the folder (a drop-in event pack not in data.ini). Packs already listed
        // in data.ini re-mount here but the VFS is first-wins, so the data.ini order above keeps priority.
        std::vector<std::string> packs;  // deterministic order (a.zip before b.zip)
        for (auto it = stdfs::directory_iterator(cd, ec); !ec && it != stdfs::directory_iterator();
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string ext = it->path().extension().string();
            if (ext == ".zip" || ext == ".grf") packs.push_back(it->path().string());
        }
        std::sort(packs.begin(), packs.end());
        for (const std::string& p : packs) {
            const bool grf = p.size() >= 4 && p.compare(p.size() - 4, 4, ".grf") == 0;
            if (grf ? vfs_.mountGrf(p, contentTag(p)) : vfs_.mountZip(p, contentTag(p))) ++mounted;
        }
        if (stdfs::exists(cd + "/data", ec)) { vfs_.mountDir(cd + "/data"); ++mounted; }  // loose override
        if (mounted) log::info("data: {} content pack(s) mounted from '{}'", mounted, cd);
    }
    for (usize c = 0; c < kContentCategories; ++c)
        vfs_.setContentMode(static_cast<ContentCategory>(c), contentModes_[c]);
    // Say plainly whether .webp overrides can be decoded at all -- a content maker's .webp only loads
    // when libwebp is compiled in; otherwise readPreferPng skips .webp and falls back to bmp/png (S.:
    // "почему webp не грузится из data?"). This one line in the runtime log answers it definitively.
    log::info("image: webp decoder {} (.webp overrides {})",
              Webp::supported() ? "COMPILED" : "ABSENT",
              Webp::supported() ? "honoured" : "IGNORED -> falls back to .png/.bmp");
}

void Application::reloadExternalData() {
    // After the patcher fetches fresh files (a new GRF, an updated data.ini, loose overrides), the
    // already-mounted VFS still indexes the old archives. Drop every mount and rebuild from disk,
    // then reload the cached tables that were read at startup so the login screen and in-game UI
    // see the new content without a restart (S.: "перезагрузка всех внешних файлов").
    vfs_.clear();
    mountGameData();
    reloadClientInfo();
    uiSkin_.init(vfs_);
    itemDb_.load(vfs_);
    grfData_.load([this](const std::string& vpath) { return vfs_.read(vpath); });
    log::info("patcher: remounted VFS ({} GRF, {} ZIP, {} dir) + reloaded clientinfo/skin/itemdb",
              vfs_.grfCount(), vfs_.zipCount(), vfs_.dirCount());
}

extern const std::string& embedded_clientinfo();  // baked-in default (EmbeddedClientInfo.cpp)

bool Application::reloadClientInfo() {
    if (auto xml = vfs_.read("data/sclientinfo.xml")) {
        clientInfo_ = net::parseClientInfo(std::string(xml->begin(), xml->end()));
    } else if (auto xml2 = vfs_.read("sclientinfo.xml")) {
        clientInfo_ = net::parseClientInfo(std::string(xml2->begin(), xml2->end()));
    } else {
        // No external file (bare exe): use the server list baked into the binary (S.).
        clientInfo_ = net::parseClientInfo(embedded_clientinfo());
    }
    if (!clientInfo_.connections.empty())
        log::info("clientinfo: {} server connection(s) loaded", clientInfo_.connections.size());
    return !clientInfo_.connections.empty();
}

void Application::loadConfig() {
    bool langInCfg = false;      // did the file pin a language? if not, auto-detect from the OS locale
    bool gamemodeInCfg = false;  // did the file pin a control mode? if not, auto-pick (touch on Android)
    bool fsrInCfg = false;       // did the file pin a render scale? if not, power-saver default on Android
    bool fpslimitInCfg = false;  // did the file pin an fps cap? if not, a lower default on Android
#if defined(CLIENT_CONSOLE)
    // Consoles keep user data in the OS save container, not loose files -- read the blob
    // through the ConsoleServices API and parse it the same way. (console-port-prep)
    std::vector<u8> cfgBlob;
    std::string cfgText;
    if (consoleServices().saveRead("settings/game.cfg", cfgBlob))
        cfgText.assign(cfgBlob.begin(), cfgBlob.end());
    std::istringstream in(cfgText);
#else
    std::ifstream in(gameCfgPath());
#endif
    if (in) {
        std::string key;
        while (in >> key) {
            // language / login carry a (single-token) string value; everything else is numeric.
            if (key == "language") {
                std::string val;
                if (in >> val && !val.empty()) {
                    // Migrate the old name-based values to ISO codes (texts/<code>.cfg).
                    if (val == "English") val = "en";
                    else if (val == "Russian") val = "ru";
                    language_ = val;
                    langInCfg = true;
                }
                continue;
            }
            if (key == "login") {
                std::string val;
                if (in >> val && !val.empty()) lastLogin_ = val;
                continue;
            }
            if (key == "texquality" || key == "sprquality") {  // "1k"/"2k"/"4k" content quality (#71)
                std::string val;
                if (in >> val) {
                    // Must include 2k -- the quality button cycles 1k->2k->4k, so dropping 2k here made a
                    // 2k choice silently revert to the 1k default on the next launch (S. 2026-08-06:
                    // "меняю качество - выхожу - захожу и всё равно 1k").
                    if (val != "1k" && val != "2k" && val != "4k") val.clear();  // legacy/garbage -> platform default
                    (key == "texquality" ? textureQuality_ : spriteQuality_) = val;
                }
                continue;
            }
            float v = 0.0f;
            if (!(in >> v)) break;
            if (key == "master") masterVol_ = std::clamp(v, 0.0f, 1.0f);
            else if (key == "bgm") bgmVol_ = std::clamp(v, 0.0f, 1.0f);
            else if (key == "sfx") sfxVol_ = std::clamp(v, 0.0f, 1.0f);
            else if (key == "fullscreen") fullscreen_ = (v != 0.0f);
            else if (key == "vsync") vsyncOn_ = (v != 0.0f);
            else if (key == "fpslimit") { fpsLimit_ = std::clamp(static_cast<int>(v), 0, 240); fpslimitInCfg = true; }
            else if (key == "worldfilter") worldFilter_ = std::clamp(static_cast<int>(v), 0, 2);
            else if (key == "objfilter") objFilter_ = std::clamp(static_cast<int>(v), 0, 2);
            else if (key == "gamemode") { gameMode_ = std::clamp(static_cast<int>(v), 0, 2); gamemodeInCfg = true; }
            else if (key == "cameralock") cameraLock_ = (v != 0.0f);
            else if (key == "brightness") brightness_ = std::clamp(v, 0.0f, 1.0f);
            else if (key == "contrast") contrast_ = std::clamp(v, 0.0f, 1.0f);
            else if (key == "hdr") hdr_ = (v != 0.0f);
            else if (key == "godray")  // volumetric light mode 0/1/2 (back-compat: old bool 1 -> glow) (#117)
                godrayMode_ = v < 0.5f ? 0 : (v > 2.5f ? 2 : static_cast<int>(v + 0.5f));
            else if (key == "fsr") { fsr_ = std::clamp(v, 0.5f, 2.0f); fsrInCfg = true; }  // <1 upscale, >1 SSAA (#111)
            else if (key == "normals")  // Normals toggle: 0 off, 1/1.5/2 relief scale (#107+)
                normalsMode_ = v < 0.5f ? 0.0f : (v < 1.25f ? 1.0f : (v < 1.75f ? 1.5f : 2.0f));
            else if (key == "uiscale") uiScale_ = std::clamp(v, 1.0f, 2.0f);
            else if (key == "uiscaleauto") uiScaleAuto_ = (v != 0.0f);
            else if (key == "padinvlx") padInvertLX_ = (v != 0.0f);  // stick-axis inversion (#102/#115)
            else if (key == "padinvly") padInvertLY_ = (v != 0.0f);
            else if (key == "padinvrx") padInvertRX_ = (v != 0.0f);
            else if (key == "padinvry") padInvertRY_ = (v != 0.0f);
            else if (key == "mouseinvx") mouseInvertX_ = (v != 0.0f);  // RMB camera-orbit options (ESC>General)
            else if (key == "mouseinvy") mouseInvertY_ = (v != 0.0f);
            else if (key == "mouselocky") mouseLockY_ = (v != 0.0f);
            else if (key == "animinterp") animInterp_ = (v != 0.0f);  // sprite frame interpolation (S.)
            else if (key == "fxinterp") fxInterp_ = (v != 0.0f);
            else if (key == "padswapts") padSwapTurnStrafe_ = (v != 0.0f);  // swap turn/strafe (S.)
            else if (key == "padrotsens") padRotateSens_ = std::clamp(static_cast<int>(v + 0.5f), 1, 10);
            else if (key == "rumbdmg") rumbleDamage_ = (v != 0.0f);   // gamepad haptics per-event (S.)
            else if (key == "rumbkill") rumbleKill_ = (v != 0.0f);
            else if (key == "rumblvl") rumbleLevel_ = (v != 0.0f);
            else if (key == "rumbmenu") rumbleMenu_ = (v != 0.0f);
            else if (key == "rumbdeath") rumbleDeath_ = (v != 0.0f);
            else if (key == "rumbcrit") rumbleCrit_ = (v != 0.0f);
            else if (key.rfind("content_", 0) == 0) {
                // Per-category content source (Settings -> Use content): content_<name> 0/1/2
                // = GRO/UaRO/ROeM. Unknown names are ignored (forward compat).
                for (usize c = 0; c < kContentCategories; ++c)
                    if (key.compare(8, std::string::npos,
                                    contentCategoryName(static_cast<ContentCategory>(c))) == 0)
                        contentModes_[c] = static_cast<ContentSource>(
                            std::clamp(static_cast<int>(v), 0, 2));
            }
        }
    }
    // First run (no language pinned in settings/game.cfg): auto-pick from the OS UI language, so a
    // Russian Windows starts in Russian without the user touching Settings (S.). The chosen code is
    // persisted on the next saveConfig, so this only runs once; the Settings menu still overrides it.
    if (!langInCfg) {
        const std::string sys = fs::system_language();  // "ru", "en", ... or ""
        if (!sys.empty()) {
            language_ = sys;
            log::info("i18n: no saved language -> auto-detected OS locale '{}'", sys);
        }
    }
#if defined(__ANDROID__)
    // First run (no gamemode saved): default to Touch on Android -- it's always a touchscreen, so the
    // on-screen controls + soft keyboard come up without visiting Settings. Deterministic (don't wait
    // for SDL's lazy touch-device registration). Persisted on next saveConfig; Settings still overrides.
    // (S.: "на андроиде должен включаться режим тачскрина автоматом, а оно не определило тачскрин".)
    if (!gamemodeInCfg) {
        gameMode_ = 1;
        log::info("input: first run on Android -> defaulting to Touch mode");
    }
    // Power-saver default on Android (S.: "давай авто дефолтом"): render the 3D scene at 70% resolution
    // (FSR upscales it) to cut GPU load/heat on phones. HDR/volumetric/godrays are already off by
    // default. The UI is drawn at native res so it stays crisp. Video settings can override. Persisted.
    if (!fsrInCfg) {
        fsr_ = 0.5f;  // "Performance" render-scale preset (S.: "дефолтный рендер ставь на перфоманс")
        log::info("video: first run on Android -> Performance render scale {}", fsr_);
    }
    if (!fpslimitInCfg) {
        fpsLimit_ = 25;  // lower fps cap on Android to cut heat/battery (S.: "по дефолту 25 кадров")
        log::info("video: first run on Android -> fps cap {}", fpsLimit_);
    }
#endif
    applyVolumes();
    applyVideo();
    loadLanguage();
}

// All UI text tables are baked into the exe (EmbeddedTexts.cpp) so every language works from a bare
// binary; an on-disk texts/<code>.cfg still overrides it (lets a server tweak strings without a rebuild).
extern const char* embedded_text(const std::string& code, std::size_t& size);

void Application::loadLanguage() {
    std::ifstream in(std::filesystem::path(fs::data_dir()) / "texts" / (language_ + ".cfg"));
    if (in) {  // on-disk override wins
        std::stringstream ss;
        ss << in.rdbuf();
        Lang::instance().load(ss.str());
        log::info("i18n: loaded {} strings from texts/{}.cfg (disk)", Lang::instance().count(), language_);
        return;
    }
    std::size_t sz = 0;
    if (const char* emb = embedded_text(language_, sz)) {  // baked-in table
        Lang::instance().load(std::string(emb, sz));
        log::info("i18n: loaded {} strings for '{}' (embedded)", Lang::instance().count(), language_);
        return;
    }
    log::warn("i18n: no table for '{}' on disk or embedded (UI falls back to keys)", language_);
}

void Application::setGeneral(const std::string& language, int gameMode, bool cameraLock) {
    gameMode_ = std::clamp(gameMode, 0, 2);
    cameraLock_ = cameraLock;
    if (!language.empty() && language != language_) {
        language_ = language;
        loadLanguage();
    }
}

void Application::setVolumes(float master, float bgm, float sfx) {
    masterVol_ = std::clamp(master, 0.0f, 1.0f);
    bgmVol_ = std::clamp(bgm, 0.0f, 1.0f);
    sfxVol_ = std::clamp(sfx, 0.0f, 1.0f);
    applyVolumes();
}

void Application::setVideo(bool fullscreen, bool vsync, int fpsLimit, int worldFilter, int objFilter) {
    fullscreen_ = fullscreen;
    vsyncOn_ = vsync;
    fpsLimit_ = std::clamp(fpsLimit, 0, 240);
    worldFilter_ = std::clamp(worldFilter, 0, 2);
    objFilter_ = std::clamp(objFilter, 0, 2);
    applyVideo();
    saveConfig();  // persist fullscreen/vsync/fps/filters (S.: "настройки фильтра не сохраняются")
}

void Application::applyVideo() {
    platform_.window().setFullscreen(fullscreen_);
    render_.setVsync(vsyncOn_);
    // Texture filtering: the ground (world) + actor-sprite (objects) draw paths read these globals
    // and override their bgfx sampler per draw (#104).
    g_worldFilterMode = worldFilter_;
    g_objectFilterMode = objFilter_;
}

void Application::saveConfig() const {
#if defined(CLIENT_CONSOLE)
    // Consoles: serialise into a blob and hand it to the OS save container via ConsoleServices
    // (the dedicated per-platform storage), instead of a loose file. (console-port-prep)
    std::ostringstream out;
#else
    std::error_code ec;
    std::filesystem::create_directories(gameCfgPath().parent_path(), ec);
    std::ofstream out(gameCfgPath(), std::ios::trunc);
    if (!out) return;
#endif
    out << "master " << masterVol_ << "\n"
        << "bgm " << bgmVol_ << "\n"
        << "sfx " << sfxVol_ << "\n"
        << "fullscreen " << (fullscreen_ ? 1 : 0) << "\n"
        << "vsync " << (vsyncOn_ ? 1 : 0) << "\n"
        << "fpslimit " << fpsLimit_ << "\n"
        << "worldfilter " << worldFilter_ << "\n"
        << "objfilter " << objFilter_ << "\n"
        << "gamemode " << gameMode_ << "\n"
        << "cameralock " << (cameraLock_ ? 1 : 0) << "\n"
        << "brightness " << brightness_ << "\n"
        << "contrast " << contrast_ << "\n"
        << "hdr " << (hdr_ ? 1 : 0) << "\n"
        << "godray " << godrayMode_ << "\n"
        << "fsr " << fsr_ << "\n"
        << "language " << language_ << "\n";
    if (normalsMode_ >= 0.0f) out << "normals " << normalsMode_ << "\n";  // -1 = auto (unsaved)
    out << "uiscale " << uiScale_ << "\n";
    out << "uiscaleauto " << (uiScaleAuto_ ? 1 : 0) << "\n";
    out << "padinvlx " << (padInvertLX_ ? 1 : 0) << "\n"
        << "padinvly " << (padInvertLY_ ? 1 : 0) << "\n"
        << "padinvrx " << (padInvertRX_ ? 1 : 0) << "\n"
        << "padinvry " << (padInvertRY_ ? 1 : 0) << "\n"
        << "mouseinvx " << (mouseInvertX_ ? 1 : 0) << "\n"
        << "mouseinvy " << (mouseInvertY_ ? 1 : 0) << "\n"
        << "mouselocky " << (mouseLockY_ ? 1 : 0) << "\n"
        << "animinterp " << (animInterp_ ? 1 : 0) << "\n"
        << "fxinterp " << (fxInterp_ ? 1 : 0) << "\n"
        << "padswapts " << (padSwapTurnStrafe_ ? 1 : 0) << "\n"
        << "padrotsens " << padRotateSens_ << "\n"
        << "rumbdmg " << (rumbleDamage_ ? 1 : 0) << "\n"
        << "rumbkill " << (rumbleKill_ ? 1 : 0) << "\n"
        << "rumblvl " << (rumbleLevel_ ? 1 : 0) << "\n"
        << "rumbmenu " << (rumbleMenu_ ? 1 : 0) << "\n"
        << "rumbdeath " << (rumbleDeath_ ? 1 : 0) << "\n"
        << "rumbcrit " << (rumbleCrit_ ? 1 : 0) << "\n";
    for (usize c = 0; c < kContentCategories - 1; ++c)  // skip Other (always UaRO chain)
        out << "content_" << contentCategoryName(static_cast<ContentCategory>(c)) << " "
            << static_cast<int>(contentModes_[c]) << "\n";
    // Only when set, so a blank value can't swallow the next key when the file is re-read (S.).
    if (!lastLogin_.empty()) out << "login " << lastLogin_ << "\n";
    if (!textureQuality_.empty()) out << "texquality " << textureQuality_ << "\n";  // #71 content quality
    if (!spriteQuality_.empty()) out << "sprquality " << spriteQuality_ << "\n";
#if defined(CLIENT_CONSOLE)
    const std::string blob = out.str();
    consoleServices().saveWrite("settings/game.cfg", std::vector<u8>(blob.begin(), blob.end()));
#endif
}

void Application::setPadInvert(bool lx, bool ly, bool rx, bool ry) {
    padInvertLX_ = lx;
    padInvertLY_ = ly;
    padInvertRX_ = rx;
    padInvertRY_ = ry;
    saveConfig();
}

void Application::setMouseLook(bool ix, bool iy, bool lockY) {
    mouseInvertX_ = ix;
    mouseInvertY_ = iy;
    mouseLockY_ = lockY;
    saveConfig();
}

void Application::setVideoGrade(float brightness, float contrast) {
    brightness_ = std::clamp(brightness, 0.0f, 1.0f);
    contrast_ = std::clamp(contrast, 0.0f, 1.0f);
    render_.setGrade(brightness_, contrast_);  // shader grade (can raise contrast, unlike the overlay)
    saveConfig();  // persist brightness/contrast (S.: all configs must save)
}

void Application::setHdr(bool on) {
    render_.setHdr(on);
    hdr_ = render_.hdr();  // may stay off if the GPU can't do RGBA16F -> keep the toggle honest
    saveConfig();
}

void Application::setFsr(float scale) {
    render_.setFsr(scale);
    fsr_ = render_.fsr();  // reconcile (shaders may be missing -> stays 1.0)
    saveConfig();
}

void Application::setGodrayMode(int mode) {
    render_.setGodrayMode(mode);
    godrayMode_ = render_.godrayMode();  // may stay off if the shader is missing
    saveConfig();
}

void Application::setNormalsMode(float m) {
    normalsMode_ = m < 0.5f ? 0.0f : (m < 1.25f ? 1.0f : (m < 1.75f ? 1.5f : 2.0f));
    g_normalsFactor = normalsMode_ * 0.5f;  // maps are baked at the x2 base -> factor = mode/2
    saveConfig();
}

void Application::setContentMode(ContentCategory c, ContentSource s) {
    contentModes_[static_cast<usize>(c)] = s;
    vfs_.setContentMode(c, s);  // applies to the NEXT read; already-cached assets need a re-enter
    saveConfig();
    log::info("content: {} -> {}", contentCategoryName(c), contentSourceLabel(s));
}

void Application::setLastLogin(const std::string& login) {
    if (login == lastLogin_) return;
    lastLogin_ = login;
    saveConfig();
}

} // namespace uaro
