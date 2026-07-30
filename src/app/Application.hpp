#pragma once
#include <string>

#include "app/SceneStack.hpp"
#include "audio/Audio.hpp"
#include "net/ClientInfo.hpp"
#include "net/Session.hpp"
#include "platform/ConsoleServices.hpp"
#include "platform/Platform.hpp"
#include "render/RenderDevice.hpp"
#include "render/SpriteBatch.hpp"
#include "resource/GrfData.hpp"
#include "resource/ItemDb.hpp"
#include "resource/Vfs.hpp"
#include "ui/Font.hpp"
#include "ui/UiSkin.hpp"

namespace uaro {

struct AppConfig {
    std::string title = "UaRO";
    int width = 1280;
    int height = 720;
    bool vsync = true;
    // If set, load and render this map (assets resolved through the VFS built from
    // data.ini); otherwise the sprite test scene. dataDir overrides where data.ini
    // and the GRFs are looked for (default: cwd, then the executable directory).
    std::string mapName;
    std::string dataDir;
    // Dev/test mode (--test): allow the offline map viewer / sprite-test fallbacks when no
    // server list or data is found. Without it, a normal launch always runs the patcher first
    // (which fetches the missing files), then the login screen (S.).
    bool testMode = false;
    // --view: launch the content browser (2D sprites vs RoM 3D models) instead of the game.
    bool viewerMode = false;
    // --view2d: launch the sprite-effect (.str) binding tool instead of the game.
    bool viewer2dMode = false;
    // --no-patch: skip the patcher and go straight to login. For the content maker testing their
    // own GRF files without the patcher overwriting them (S.).
    bool noPatch = false;
    // argv[0]: the running executable's path, so the patcher can self-update it (rename aside +
    // drop the new one) and ask for a manual relaunch.
    std::string selfExePath;
};

// Top-level engine object: owns platform/render/services, runs the main loop,
// and drives the scene stack. Scenes reach services through the accessors.
class Application {
public:
    int run(const AppConfig& cfg);

    RenderDevice& render() { return render_; }
    SpriteBatch& sprites() { return sprites_; }
    Font& font() { return font_; }
    ui::UiSkin& uiSkin() { return uiSkin_; }
    ItemDb& itemDb() { return itemDb_; }
    GrfData& grfData() { return grfData_; }  // data-from-GRF lub tables (status text, sprite names)
    SceneStack& scenes() { return scenes_; }
    Vfs& vfs() { return vfs_; }
    Audio& audio() { return audio_; }
    const InputState& input() const { return input_; }
    InputState& inputMutable() { return input_; }  // UI input-consumption (gate clicks on covered windows)
    const std::string& assetDir() const { return assetDir_; }
    const std::string& dataDir() const { return dataDir_; }  // resolved data.ini/GRF root (loose data/ scan)
    const std::string& selfExePath() const { return selfExePath_; }
    net::Session& session() { return session_; }
    const net::ClientInfo& clientInfo() const { return clientInfo_; }
    // Re-read data/sclientinfo.xml through the VFS (after the patcher may have fetched it).
    // Returns true if at least one server connection is now present.
    bool reloadClientInfo();
    // Remount the whole VFS from disk and reload the startup-cached tables (clientinfo, UI skin,
    // item db). Call after the patcher writes new external files so they take effect live.
    void reloadExternalData();

    // Ask the main loop to exit to desktop (the in-game menu "Exit" item). Scenes
    // can't write the read-only InputState, so this is the explicit app-quit path.
    void requestQuit() { quitRequested_ = true; }

    // Global game config (settings/game.cfg): sound volumes 0..1 (#104). Read by the Sound setup
    // panel; setVolumes() applies them to the audio engine and remembers them, saveConfig() persists.
    float masterVol() const { return masterVol_; }
    float bgmVol() const { return bgmVol_; }
    float sfxVol() const { return sfxVol_; }
    void setVolumes(float master, float bgm, float sfx);
    void saveConfig() const;

    // Video config (#104). fpsLimit 0 = unlimited; worldFilter/objFilter: 0=off,1=bilinear,2=trilinear.
    bool fullscreen() const { return fullscreen_; }
    bool vsyncOn() const { return vsyncOn_; }
    int fpsLimit() const { return fpsLimit_; }
    int worldFilter() const { return worldFilter_; }
    int objFilter() const { return objFilter_; }
    void setVideo(bool fullscreen, bool vsync, int fpsLimit, int worldFilter, int objFilter);
    // Brightness / contrast video grade, 0..1 with 0.5 = neutral (#video sliders, S.). Applied as a
    // final fullscreen overlay each frame; persisted in game.cfg.
    float brightness() const { return brightness_; }
    float contrast() const { return contrast_; }
    void setVideoGrade(float brightness, float contrast);
    // HDR rendering toggle (#111): scene -> RGBA16F offscreen -> ACES tonemap. Persisted in game.cfg.
    bool hdr() const { return hdr_; }
    void setHdr(bool on);
    // Volumetric light (#117): 0 = off, 1 = Glow (soft light halos), 2 = Rays (Glow + light shafts from
    // the sun and each local light). Persisted in game.cfg (key `godray`, back-compat with the old bool).
    int godrayMode() const { return godrayMode_; }
    void setGodrayMode(int mode);
    // FSR1 upscale factor (#111): 1.0 = off; <1 renders lower-res + EASU/RCAS upscales. Persisted.
    float fsr() const { return fsr_; }
    void setFsr(float scale);
    // Per-category content source (Settings -> General -> Use content): where SFX/BGM/effects/
    // statuses/skills/mobs/NPC/chars load from — GRO.grf / UaRO archives / RoM.zip, with the
    // ROM -> UARO -> GRO fallback cascade. Applied to the VFS live; persisted in game.cfg.
    // Normals toggle (Settings -> Video): 0 = off, 1 / 1.5 / 2 = relief scale. -1 = auto
    // (x1.5 on real GPUs, Off on a software rasterizer); resolved once at startup.
    float normalsMode() const { return normalsMode_; }
    void setNormalsMode(float m);
    // UI scale (S.): 1.0 / 1.25 / 1.5 / 2.0 — enlarges all in-game UI (windows, panels,
    // buttons, fonts, minimap) by drawing the UI in a logical resolution = physical/scale.
    // Persisted in game.cfg.
    // In-game UI magnification (#134). "Auto" (default) derives it from the screen height so 4K/1440p
    // panels aren't tiny (S.): >1500px -> x2, >999px -> x1.5, else x1. A manual pick overrides Auto.
    float uiScale() const {
        if (uiScaleAuto_) {
#if defined(__ANDROID__)
            return 2.0f;  // phones: default the UI to x2 (a manual pick in Settings still overrides). (S.)
#else
            const int h = render_.height();
            return h > 1500 ? 2.0f : (h > 999 ? 1.5f : 1.0f);
#endif
        }
        return uiScale_;
    }
    bool uiScaleAuto() const { return uiScaleAuto_; }
    void setUiScale(float s) { uiScale_ = s; uiScaleAuto_ = false; saveConfig(); }  // manual pick disables Auto
    void setUiScaleAuto(bool a) { uiScaleAuto_ = a; saveConfig(); }

    // Gamepad control mode (#102/#115): true = pad-driven controls + glyphs are active. Auto-on at
    // startup when a pad is present, offered via a prompt on a mid-session connect, turned off manually
    // (General) or automatically when the last pad disconnects. Session state (not persisted).
    bool gamepadMode() const { return gamepadMode_; }
    // Entering gamepad mode defaults the follow-cam ON (S.: "камера лок должна включаться с режимом
    // геймпад"), but only on the off->on EDGE so the per-frame re-assert doesn't override a Back-button
    // toggle mid-session. cameraLockActive() no longer force-includes gamepadMode_ for the same reason.
    void setGamepadMode(bool on) { if (on && !gamepadMode_) cameraLock_ = true; gamepadMode_ = on; }

    // Gamepad stick-axis inversion (GamePad Setup, #102/#115): some pads (notably in DirectInput mode)
    // report a stick axis flipped (S.: Vader 5 Pro left-stick X reversed). Applied to the pad axes each
    // frame at the source, so movement / camera / cursor / menu-nav all read corrected values.
    // Persisted in game.cfg.
    bool padInvertLX() const { return padInvertLX_; }
    bool padInvertLY() const { return padInvertLY_; }
    bool padInvertRX() const { return padInvertRX_; }
    bool padInvertRY() const { return padInvertRY_; }
    void setPadInvert(bool lx, bool ly, bool rx, bool ry);

    // Mouse-look (RMB-drag camera orbit) options (ESC > General, S.). InvertX/Y flip the drag axes;
    // LockY freezes the pitch and only lets a drag change the azimuth while Ctrl/Shift is held.
    // Persisted in game.cfg.
    bool mouseInvertX() const { return mouseInvertX_; }
    bool mouseInvertY() const { return mouseInvertY_; }
    bool mouseLockY()   const { return mouseLockY_; }
    void setMouseLook(bool ix, bool iy, bool lockY);

    // Swap strafe/turn between the sticks (S.): default = left stick moves (fwd/back + strafe), right
    // stick X turns. When on, TURN moves to the left stick X and STRAFE to the right stick X (tank
    // controls: left stick fwd/back + turn, right stick strafe). Persisted in game.cfg.
    bool padSwapTurnStrafe() const { return padSwapTurnStrafe_; }
    void setPadSwapTurnStrafe(bool on) { padSwapTurnStrafe_ = on; saveConfig(); }

    // Gamepad turn sensitivity (S.): 1..10, default 10 = the current turn cadence; lower = the 45-deg
    // turn steps come slower, so a stick touch rotates the char by fewer degrees. Persisted in game.cfg.
    int padRotateSens() const { return padRotateSens_; }
    void setPadRotateSens(int s) { padRotateSens_ = s < 1 ? 1 : (s > 10 ? 10 : s); saveConfig(); }

    // Sprite-animation frame interpolation (S.): smoothly tween each .act layer's transform
    // (pos/scale/alpha) between the current and next frame instead of snapping. Two independent
    // toggles -- characters/mobs vs effects -- both OFF by default (identical to the old look).
    // Persisted in game.cfg. The bitmap still switches at the frame boundary (no ghosting).
    bool animInterp() const { return animInterp_; }
    bool fxInterp() const { return fxInterp_; }
    void setAnimInterp(bool on) { animInterp_ = on; saveConfig(); }
    void setFxInterp(bool on) { fxInterp_ = on; saveConfig(); }

    // Gamepad haptics (#102/#115, S.): each event has its own on/off toggle in GamePad Setup, all
    // default ON. Persisted in game.cfg (keys rumbdmg/rumbkill/rumblvl/rumbmenu/rumbdeath/rumbcrit).
    bool rumbleDamage() const { return rumbleDamage_; }   // taking damage -> short buzz
    bool rumbleKill() const { return rumbleKill_; }       // killing a mob/player -> normal buzz
    bool rumbleLevel() const { return rumbleLevel_; }     // base/job level-up -> long buzz
    bool rumbleMenu() const { return rumbleMenu_; }       // menu confirm -> light tick
    bool rumbleDeath() const { return rumbleDeath_; }     // own death -> double tick
    bool rumbleCrit() const { return rumbleCrit_; }       // own critical hit -> single tick
    void setRumbleDamage(bool on) { rumbleDamage_ = on; saveConfig(); }
    void setRumbleKill(bool on) { rumbleKill_ = on; saveConfig(); }
    void setRumbleLevel(bool on) { rumbleLevel_ = on; saveConfig(); }
    void setRumbleMenu(bool on) { rumbleMenu_ = on; saveConfig(); }
    void setRumbleDeath(bool on) { rumbleDeath_ = on; saveConfig(); }
    void setRumbleCrit(bool on) { rumbleCrit_ = on; saveConfig(); }
    // Fire the gamepad motors now (used by GameScene's rumble scheduler).
    void padRumble(unsigned short lo, unsigned short hi, unsigned int ms) { platform_.rumble(lo, hi, ms); }

    ContentSource contentMode(ContentCategory c) const {
        return contentModes_[static_cast<usize>(c)];
    }
    void setContentMode(ContentCategory c, ContentSource s);

    // Last account name typed at the login screen, remembered across runs so it doesn't have to be
    // retyped every time (S.). Only the account name is stored -- never the password. Persisted in
    // settings/game.cfg.
    const std::string& lastLogin() const { return lastLogin_; }
    void setLastLogin(const std::string& login);

    // Content quality (#71 rework): "1k" | "4k" | "" (= platform default). Read by the patcher to pick
    // <q>/texture.zip and <q>/sprite.zip; a change applies on the NEXT client start (re-download +
    // remount). Set from the ESC "Quality content" toggles.
    const std::string& textureQuality() const { return textureQuality_; }
    const std::string& spriteQuality() const { return spriteQuality_; }
    void setTextureQuality(const std::string& q) { textureQuality_ = q; saveConfig(); }
    void setSpriteQuality(const std::string& q) { spriteQuality_ = q; saveConfig(); }

    // General settings (#104). gameMode: 0 = Kbd+Mouse, 1 = TouchScreen, 2 = GamePad (UI layout).
    const std::string& language() const { return language_; }
    int gameMode() const { return gameMode_; }
    bool touchMode() const { return gameMode_ == 1; }  // touchscreen control scheme (#102/#114)
    bool cameraLock() const { return cameraLock_; }
    void setCameraLock(bool on) { cameraLock_ = on; saveConfig(); }  // Back-button toggle (gamepad) + persist
    // Camera Lock is on whenever explicitly enabled, OR the game mode is GamePad (UI layout), OR the
    // gamepad CONTROL mode is active (S.: Camera Lock must turn on together with gamepad mode). Drives
    // the close, low, behind-the-character follow-cam.
    bool cameraLockActive() const { return cameraLock_ || gameMode_ == 2; }
    void setGeneral(const std::string& language, int gameMode, bool cameraLock);

    // Show/hide the OS cursor (the game scene hides it to draw the RO cursor sprite).
    void setCursorVisible(bool v) { platform_.setCursorVisible(v); }

private:
    Platform platform_;
    RenderDevice render_;
    SpriteBatch sprites_;
    Font font_;
    ui::UiSkin uiSkin_;
    ItemDb itemDb_;
    GrfData grfData_;
    SceneStack scenes_;
    Vfs vfs_;
    Audio audio_;
    InputState input_;
    std::string assetDir_;
    std::string dataDir_;  // resolved data.ini/GRF root; kept so the patcher can remount the VFS
    std::string selfExePath_;  // argv[0] (patcher self-update)
    net::Session session_;
    net::ClientInfo clientInfo_;
    bool quitRequested_ = false;  // set by requestQuit(); breaks the main loop
    // Console lifecycle (suspend/resume). Desktop never leaves Running (DesktopConsoleServices
    // is a no-op), so the pause branch below is dead weight there -- it exists so a console
    // backend can halt the frame + audio when the OS sleeps the title. (console-port-prep)
    AppLifecycle lifecycle_ = AppLifecycle::Running;

    float masterVol_ = 1.0f, bgmVol_ = 1.0f, sfxVol_ = 1.0f;  // sound config (default 100%) (#104)
    bool fullscreen_ = false, vsyncOn_ = true;  // video config: windowed + vsync on by default (#104)
    int fpsLimit_ = 30, worldFilter_ = 2, objFilter_ = 1;  // 30 fps; world trilinear, objects bilinear
    std::string language_ = "en";  // UI language ISO code; loads texts/<code>.cfg (#104 i18n)
    int gameMode_ = 0;                   // 0 = Kbd+Mouse, 1 = TouchScreen, 2 = GamePad
    bool cameraLock_ = false;            // behind-the-character follow-cam (Settings) (#104)
    float brightness_ = 0.5f, contrast_ = 0.5f;  // video grade, 0.5 = neutral (S.)
    bool hdr_ = false;                           // HDR offscreen + ACES tonemap (#111)
    int godrayMode_ = 0;                         // volumetric light: 0 off, 1 glow, 2 rays (#117)
    float fsr_ = 1.0f;                           // FSR1 upscale factor, 1 = off (#111)
    float normalsMode_ = -1.0f;                  // Normals toggle; -1 = auto default
    float uiScale_ = 1.0f;                       // in-game UI magnification (#134)
    bool uiScaleAuto_ = true;                     // derive uiScale from screen height (S.: default Auto)
    bool gamepadMode_ = false;                   // pad-driven control mode active (#102/#115)
    bool padInvertLX_ = false, padInvertLY_ = false;  // stick-axis inversion (GamePad Setup, #102/#115)
    bool padInvertRX_ = false, padInvertRY_ = false;
    bool mouseInvertX_ = false, mouseInvertY_ = false, mouseLockY_ = false;  // RMB camera-orbit options (ESC>General)
    bool animInterp_ = false, fxInterp_ = false;      // sprite frame interpolation toggles (S.)
    bool padSwapTurnStrafe_ = true;                   // swap turn/strafe between the sticks (S.: default on)
    int padRotateSens_ = 5;                           // gamepad turn sensitivity 1..10 (S.: default 5)
    // Gamepad rumble per-event toggles (S.), all default ON:
    bool rumbleDamage_ = true, rumbleKill_ = true, rumbleLevel_ = true;
    bool rumbleMenu_ = true, rumbleDeath_ = true, rumbleCrit_ = true;
    std::array<ContentSource, kContentCategories> contentModes_{
        ContentSource::Uaro, ContentSource::Uaro, ContentSource::Uaro,
        ContentSource::Uaro, ContentSource::Uaro, ContentSource::Uaro,
        ContentSource::Uaro, ContentSource::Uaro, ContentSource::Uaro};  // Use content (per category)
    std::string lastLogin_;                      // remembered account name for the login field (S.)
    std::string textureQuality_;                 // "1k"/"4k"/"" content quality (#71); "" = platform default
    std::string spriteQuality_;
    void loadConfig();      // read settings/game.cfg at startup and apply to audio_ + video
    void mountGameData();   // mount data/ + data.ini GRFs into vfs_ (call after vfs_.clear() to remount)
    void applyVolumes();    // push the three volumes into audio_
    void applyVideo();      // push fullscreen + vsync to the window/render device
    void loadLanguage();    // load texts/<language_>.cfg into the i18n table
};

} // namespace uaro
