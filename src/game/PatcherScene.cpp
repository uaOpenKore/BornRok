#include "game/PatcherScene.hpp"

#include <string>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "game/LoginScene.hpp"
#include "platform/ConsoleServices.hpp"
#include "platform/FileSystem.hpp"
#include "render/SpriteBatch.hpp"
#include "ui/Font.hpp"

namespace uaro {

PatcherScene::~PatcherScene() {
    if (worker_.joinable()) worker_.join();
}

void PatcherScene::onEnter(Application& app) {
    started_ = true;
    bool storePackaged = false;  // read-only (MSIX) install -> skip the store-managed exe (platforms/)
#if defined(__ANDROID__)
    // Android has no base/exe dir -- SDL_GetBasePath() is null so fs::base_dir() == "./", and the
    // app's cwd is "/" (read-only). Every fopen(dest,"wb") failed there, so the patcher "skipped"
    // every file (even the 118-byte data.ini). Download into the app's writable storage, the SAME
    // dir Application mounts GRFs from (fs::pref_dir). (S.: "нужно патчер починить".)
    std::string root = fs::data_dir();
    if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
#else
    // data_dir() is the exe dir on a writable/loose-exe build (unchanged), but a per-user writable
    // dir under a read-only MSIX install — so the patcher can actually write its downloads (it was
    // writing next to the exe in read-only WindowsApps and silently failing). Application mounts GRFs
    // from the same data_dir(), so what the patcher downloads here is what the game loads. (S.)
    const std::string root = fs::data_dir();
    // If data_dir() diverged from the exe dir, the exe dir is read-only = a packaged (MSIX) install;
    // the exe is updated by the package, so tell the runner to skip the platforms/ binary. (S.:
    // "для MSIX подгрузка с platforms не нужна, всё равно не работает".)
    {
        std::string bd = fs::base_dir();
        while (!bd.empty() && (bd.back() == '/' || bd.back() == '\\')) bd.pop_back();
        storePackaged = (root != bd);
    }
#endif
    // selfExe = argv[0] (AppConfig): the runner renames the running exe aside and drops the
    // new one in; the client then CLOSES so the user relaunches the fresh build (S.).
    const std::string selfExe = app.selfExePath();
    // Content quality (#71) chosen in the ESC menu, persisted in game.cfg; the runner downloads the
    // matching <q>/texture.zip + <q>/sprite.zip. "" -> the runner resolves the platform default.
    const std::string texQ = app.textureQuality();
    const std::string sprQ = app.spriteQuality();
    // Writable content cache: the events/*.zip rows from the MAIN downloads.list are written here (flat)
    // so Application mounts them on top of the base content (reloadExternalData). ONE point of truth
    // (S.: chose folding events into the main manifest) — the old separate EventContent path is gone.
    // Empty on a baked platform with no writable cache -> the runner just skips the event packs.
    const std::string cacheDir = consoleServices().contentCacheDir();
    worker_ = std::thread([this, root, selfExe, texQ, sprQ, cacheDir, storePackaged]() {
        PatchRunner runner(root, selfExe);
        runner.setContentQuality(texQ, sprQ);
        runner.setContentCacheDir(cacheDir);
        runner.setStorePackaged(storePackaged);
        PatchSummary s = runner.run([this](const PatchProgress& p) {
            std::lock_guard<std::mutex> lk(mu_);
            prog_ = p;
        });
        {
            std::lock_guard<std::mutex> lk(mu_);
            summary_ = s;
        }
        done_.store(true);
    });
    (void)app;
}

void PatcherScene::update(Application& app, double dt) {
    time_ += dt;
    if (done_.load() && doneAt_ < 0.0) doneAt_ = time_;
    // Hold the final status briefly, then continue to the login screen.
    if (done_.load() && !transitioned_ && time_ - doneAt_ > 0.6) {
        bool selfUpdated;
        {
            std::lock_guard<std::mutex> lk(mu_);
            selfUpdated = summary_.selfUpdated;
        }
        // The RUNNING exe was replaced: don't try to restart ourselves (that was flaky on
        // Windows) — show the notice for a few seconds, then close; the user relaunches and
        // the new exe runs (S.: "просто закрытие клиента при обновлении самого себя").
        if (selfUpdated) {
            transitioned_ = true;  // render() shows the relaunch notice via summary_.selfUpdated
            if (worker_.joinable()) worker_.join();
            quitAt_ = time_ + 4.0;
            return;
        }
        transitioned_ = true;
        if (worker_.joinable()) worker_.join();
        // The patch may have fetched fresh external files (a new GRF, data.ini, sclientinfo.xml).
        // Remount the VFS + reload the startup-cached tables so LoginScene and the in-game UI see
        // the new content without a restart (S.: GRF wasn't re-read after patching).
        app.reloadExternalData();
        app.scenes().push(app, std::make_unique<LoginScene>());
    }
    if (quitAt_ > 0.0 && time_ >= quitAt_) app.requestQuit();
}

void PatcherScene::render(Application& app) {
    SpriteBatch& sb = app.sprites();
    Font& font = app.font();
    const float W = static_cast<float>(app.render().width());
    const float H = static_cast<float>(app.render().height());

    PatchProgress p;
    {
        std::lock_guard<std::mutex> lk(mu_);
        p = prog_;
    }

    // Build the status line.
    std::string line;
    auto pct = [&]() {
        if (p.bytesTotal == 0) return std::string();
        const int v = static_cast<int>(p.bytesDone * 100 / p.bytesTotal);
        return "  " + std::to_string(v) + "%";
    };
    switch (p.phase) {
        case PatchProgress::Phase::FetchingManifest: line = "Checking for updates..."; break;
        case PatchProgress::Phase::Verifying:
            line = "Verifying " + p.file + "  (" + std::to_string(p.index) + "/" +
                   std::to_string(p.total) + ")";
            break;
        case PatchProgress::Phase::Downloading:
            line = "Downloading " + p.file + (p.source.empty() ? "" : " from " + p.source) + pct();
            break;
        case PatchProgress::Phase::Applying: line = "Installing " + p.file; break;
        case PatchProgress::Phase::UpToDate: line = "Up to date"; break;
        case PatchProgress::Phase::NoManifest: line = "Update server unavailable - starting game"; break;
        case PatchProgress::Phase::Done:
            line = "Done" + (p.updated > 0 ? " (" + std::to_string(p.updated) + " updated)" : "");
            break;
    }
    // Self-update: the running exe was replaced — tell the player to relaunch (the client
    // closes by itself a few seconds later; no flaky self-restart).
    if (quitAt_ > 0.0) line = "Client updated - please restart. Closing...";

    sb.begin(static_cast<int>(W), static_cast<int>(H));
    sb.draw(0.0f, 0.0f, W, H, 0xfff0c896u);  // light-blue background (ABGR: R=150,G=200,B=240)

    const float scale = 2.75f;  // ~22px (8px base * 2.75)
    auto outlined = [&](float x, float y, const std::string& s) {
        const u32 black = 0xff000000u;
        font.draw(sb, x - 1.0f, y, scale, black, s);
        font.draw(sb, x + 1.0f, y, scale, black, s);
        font.draw(sb, x, y - 1.0f, scale, black, s);
        font.draw(sb, x, y + 1.0f, scale, black, s);
        font.draw(sb, x, y, scale, 0xffffffffu, s);  // white
    };
    const float lh = font.lineHeight(scale);
    const float tw = font.width(line, scale);
    outlined((W - tw) * 0.5f, H * 0.5f - lh, line);
    // A coarse progress bar for the active download.
    if (p.phase == PatchProgress::Phase::Downloading && p.bytesTotal > 0) {
        const float bw = W * 0.5f, bx = (W - bw) * 0.5f, by = H * 0.5f + lh, bh = 14.0f;
        sb.draw(bx, by, bw, bh, 0xff404040u);
        const float f = static_cast<float>(p.bytesDone) / static_cast<float>(p.bytesTotal);
        sb.draw(bx, by, bw * f, bh, 0xff30c030u);
    }
    sb.end();
}

}  // namespace uaro
