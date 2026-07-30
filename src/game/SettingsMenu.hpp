#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "app/Application.hpp"
#include "core/Lang.hpp"
#include "patcher/ContentQuality.hpp"  // contentPlatformId / resolveQuality / isBakedTokenPlatform (#71)
#include "platform/FileSystem.hpp"     // fs::is_packaged_install() -> hide quality UI on MSIX/Flatpak
#include "platform/Input.hpp"
#include "render/SpriteBatch.hpp"
#include "ui/Font.hpp"
#include "ui/Widgets.hpp"

namespace uaro {

// Help-page content, at file scope so the window can measure the widest line and size itself to the
// text (S.: "сделай ширину окна по ширине текста") before it is drawn.
namespace smhelp {
struct HLine { int kind; const char* text; };  // kind 1 = body, 2 = sub-header
inline const HLine kVideo[] = {
    {1, "help.v1"},
    {1, "help.v2"},
    {1, "help.v3"},
    {2, "help.v4"},
    {1, "help.v5"},
};
inline const HLine kKeyboard[] = {
    {1, "help.k1"},       {1, "help.k2"},
    {1, "help.k3"},       {1, "help.k4"},
    {1, "help.k5"},   {1, "help.k6"},
    {1, "help.k7"}, {1, "help.k8"},
    {1, "help.k9"},         {1, "help.k10"},
    {1, "help.k11"},           {1, "help.k12"},
    {1, "help.k13"},       {1, "help.k14"},
    {1, "help.k15"},
    {1, "help.k16"},
    {1, "help.k17"},
    {1, "help.k18"},
    {1, "help.k19"},
    {1, "help.k20"},
};
inline const HLine kGamepad[] = {
    {1, "help.g1"},
    {1, "help.g2"},
    {1, "help.g3"},
    {1, "help.g4"},
    {1, "help.g5"},
    {1, "help.g6"},
    {1, "help.g7"},
    {1, "help.g8"},
    {1, "help.g9"},
    {1, "help.g10"},
    {1, "help.g11"},
    {1, "help.g12"},
    {1, "help.g13"},
    {1, "help.g14"},
    {1, "help.g15"},
    {1, "help.g16"},
    {1, "help.g17"},
    {1, "help.g18"},
    {1, "help.g19"},
    {1, "help.g20"},
    {1, "help.g21"},
    {1, "help.g22"},
    {2, "help.g23"},
    {1, "help.g24"},
    {2, "help.g25"},
    {1, "help.g26"},
    {1, "help.g27"},
    {2, "help.g28"},
    {1, "help.g29"},
};
inline const HLine kTouch[] = {
    {1, "help.t1"},
    {2, "help.t2"},   // Gestures
    {1, "help.t3"},
    {1, "help.t4"},
    {1, "help.t5"},
    {1, "help.t6"},
    {2, "help.t7"},   // Modes (bottom bar)
    {1, "help.t8"},
    {1, "help.t9"},
    {1, "help.t10"},
    {2, "help.t11"},  // CamLock
    {1, "help.t12"},
    {1, "help.t13"},
    {2, "help.t14"},  // Text
    {1, "help.t15"},
};
inline const char* const kTopics[4] = {"Video", "Keyboard", "Gamepad", "Touch"};
inline int count(int topic) {
    return topic == 0 ? static_cast<int>(sizeof(kVideo) / sizeof(kVideo[0]))
           : topic == 1 ? static_cast<int>(sizeof(kKeyboard) / sizeof(kKeyboard[0]))
           : topic == 2 ? static_cast<int>(sizeof(kGamepad) / sizeof(kGamepad[0]))
                        : static_cast<int>(sizeof(kTouch) / sizeof(kTouch[0]));
}
inline const HLine* lines(int topic) {
    return topic == 0 ? kVideo : topic == 1 ? kKeyboard : topic == 2 ? kGamepad : kTouch;
}
inline constexpr int kTopicCount = 4;
}  // namespace smhelp

// The single, shared settings/ESC menu (Sound / Video / General), used identically by the login
// screen and the in-game ESC menu (S.: "нужно чтобы было одно и то же меню"). Holds its own
// open/panel/dropdown state and reads+writes the global config through Application. The caller adds
// context buttons to the root panel via topItems (above the Setup entries, e.g. Resume/Log out) and
// bottomItems (below, e.g. Exit). Call draw() every frame while shown; returns false once closed.
class SettingsMenu {
public:
    bool open = false;

    // Extra root-panel buttons the host screen supplies (login vs in-game differ only in these).
    struct Item {
        std::string label;
        std::function<void()> fn;
    };
    std::vector<Item> topItems;     // rendered above Setup Sound/Video/General
    std::vector<Item> bottomItems;  // rendered below (e.g. Exit)

    // Returns open (false once closed). W/H are the framebuffer size.
    bool draw(Application& app, SpriteBatch& sb, Font& font, InputState& uin, float W, float H) {
        if (!open) { panel_ = 0; selOpen_ = -1; selSwallow_ = false; return false; }  // always reopen at the root panel
        sb.draw(0, 0, W, H, ui::rgba(0, 0, 0, 150));  // modal backdrop
        if (!uin.mouseDown) selSwallow_ = false;  // release ends the swallow (panel-switch / dropdown-pick)

        // Help pages size themselves to the widest line (S.: "ширину окна по ширине текста"); every
        // other panel keeps the standard width.
        float ww = 300.0f;
        if (panel_ == 4 && helpTopic_ >= 0) {
            const smhelp::HLine* hl = smhelp::lines(helpTopic_);
            const int hn = smhelp::count(helpTopic_);
            float maxw = 0.0f;
            for (int i = 0; i < hn; ++i)
                maxw = std::max(maxw, font.width(tr(hl[i].text), hl[i].kind == 2 ? 1.15f : 1.0f));
            ww = std::clamp(maxw + 28.0f, 300.0f, static_cast<float>(W) - 24.0f);
        }
        const float rowH = 36.0f;
        // +Sound/Video/General/Help, and +GamePad Setup while in gamepad mode.
        const int rootCount =
            static_cast<int>(topItems.size() + bottomItems.size()) + 4 + (app.gamepadMode() ? 1 : 0);
        // Content-quality section (#71 rework) is hidden wherever the quality selector is inert: every
        // baked, no-patcher distribution — consoles + iOS + Steam Deck via the token, MSIX/Flatpak via
        // the packaged probe. (S. 2026-07-29 removed the patcher on all of these, so there's nothing to
        // fetch.) qualityHidden gates both the General panel height and its render below.
        const std::string qcpid = contentPlatformId();
        const bool qualityHidden = isBakedTokenPlatform(qcpid) || fs::is_packaged_install();
        const float generalQualityH = qualityHidden ? 0.0f : (26.0f + 2 * 30.0f);  // header + Texture/Sprite
        // Window height depends on the panel (Video adds brightness/contrast sliders + HDR, Render
        // Scale and God Rays toggles below its dropdowns).
        const float bodyH = panel_ == 0   ? static_cast<float>(rootCount) * 40.0f
                            : panel_ == 1 ? 3 * 46.0f + 44.0f
                            : panel_ == 2 ? 5 * rowH + 2 * 46.0f + 6 * 34.0f + 44.0f  // + Normals + 2 interp toggles
                            : panel_ == 4 ? (helpTopic_ < 0
                                                 ? smhelp::kTopicCount * 42.0f + 44.0f  // Help topic menu
                                                 // Per-topic page: size to the ACTUAL line count (+header
                                                 // +Back) so a too-tall page reports maxScroll>0 and the
                                                 // wheel/stick actually scrolls (S.: "окно помощи не скролится").
                                                 : static_cast<float>(smhelp::count(helpTopic_)) * 22.0f + 60.0f)

                            : panel_ == 5 ? 2 * 24.0f + 5 * 30.0f + 28.0f + 44.0f + 44.0f
                                                + 24.0f + 6 * 30.0f + 6.0f                 // + Vibration header + 6 toggles
                                          : 3.0f * rowH + 40.0f + 30.0f + generalQualityH + 24.0f + 3 * 30.0f + 44.0f;  // 3 sels + Scale UI + Control + Quality content (2 rows, hidden on bundled) + Camera header + 3 toggles
        // Scroll (S.): when the panel is taller than the screen, cap the window and wheel-scroll
        // the content. The wheel is consumed over the window so the camera doesn't zoom.
        const float whFull = 28.0f + bodyH + 14.0f;
        const float wh = std::min(whFull, H - 24.0f);
        const float maxScroll = std::max(0.0f, whFull - wh);
        const float wx = (W - ww) * 0.5f, wy = (H - wh) * 0.5f;
        if (maxScroll > 0.0f && uin.wheel != 0.0f && uin.hit(wx, wy, ww, wh)) {
            scroll_ = std::clamp(scroll_ - uin.wheel * 40.0f, 0.0f, maxScroll);
            uin.wheel = 0.0f;
        }
        // Re-arm the right-stick horizontal step once it returns near centre (see focusCtrl).
        if (std::abs(uin.pad.rx) < 0.3f) padAxisArmed_ = true;
        // Gamepad: the LEFT STICK (up/down) or the d-pad scrolls a too-tall panel (e.g. the Help
        // reference). Left stick is the discoverable one (S.: "как в меню скролить окно?").
        if (maxScroll > 0.0f && uin.pad.connected) {
            const bool down = uin.pad.dpadDown || uin.pad.dpadDownHeld || uin.pad.ly < -0.5f;
            const bool up = uin.pad.dpadUp || uin.pad.dpadUpHeld || uin.pad.ly > 0.5f;
            if (down) scroll_ = std::min(maxScroll, scroll_ + 12.0f);
            if (up) scroll_ = std::max(0.0f, scroll_ - 12.0f);
        }
        // Touch/drag scroll (S.: on touch there is no wheel/stick -- the Video/General/Help panels were
        // unscrollable and overflowed the screen). Dragging inside the body scrolls it; a drag past a few
        // px swallows the click so buttons under the finger don't fire.
        if (maxScroll > 0.0f) {
            const float bodyTop = wy + ui::kTitleBarH;
            if (uin.mousePressed && uin.hit(wx, bodyTop, ww, wh - ui::kTitleBarH)) {
                scrollDragging_ = true;
                scrollDragLastY_ = uin.mouseY;
                scrollDragMoved_ = 0.0f;
            }
            if (scrollDragging_ && uin.mouseDown) {
                const float dy = static_cast<float>(uin.mouseY - scrollDragLastY_);
                scrollDragLastY_ = uin.mouseY;
                scroll_ = std::clamp(scroll_ - dy, 0.0f, maxScroll);  // natural: drag up -> content up
                scrollDragMoved_ += std::abs(dy);
                if (scrollDragMoved_ > 6.0f) { uin.mousePressed = false; uin.mouseDown = false; selSwallow_ = true; }
            }
            if (!uin.mouseDown) scrollDragging_ = false;
        } else {
            scrollDragging_ = false;
        }
        if (maxScroll <= 0.0f) scroll_ = 0.0f;
        const std::string title = panel_ == 0   ? tr("menu.settings")
                                  : panel_ == 1 ? tr("panel.sound")
                                  : panel_ == 2 ? tr("panel.video")
                                  : panel_ == 4 ? tr("menu.help")
                                  : panel_ == 5 ? tr("menu.gamepad_setup")
                                                : tr("menu.general");
        ui::window(sb, font, wx, wy, ww, wh, title);
        // Scrollbar indicator on the right edge when the panel is taller than the window (S.: "без
        // скроллбара"). Thumb size/position track the visible fraction + scroll offset.
        if (maxScroll > 0.0f) {
            const float trackY = wy + ui::kTitleBarH + 2.0f, trackH = wh - ui::kTitleBarH - 6.0f;
            const float thumbH = std::max(20.0f, trackH * wh / whFull);
            const float thumbY = trackY + (trackH - thumbH) * (scroll_ / maxScroll);
            sb.draw(wx + ww - 5.0f, trackY, 3.0f, trackH, ui::rgba(255, 255, 255, 40));
            sb.draw(wx + ww - 5.0f, thumbY, 3.0f, thumbH, ui::rgba(255, 255, 255, 170));
        }
        const float cx = wx + 14.0f, cw = ww - 28.0f;
        const float cy = wy + ui::kTitleBarH + 6.0f - scroll_;

        if (panel_ == 0) {
            float ry = cy;
            for (const Item& it : topItems) {  // e.g. Resume / Log out (in-game)
                if (ui::button(sb, font, uin, cx, ry, cw, 32.0f, it.label.c_str(), 1.4f) && it.fn) it.fn();
                ry += 40.0f;
            }
            // Entering a sub-panel: swallow the still-held click so it can't fall through onto a
            // slider that happens to sit where the "Setup ..." button was (S.: клик проскакивает в
            // подменю). Cleared globally on mouse-up at the top of draw().
            if (ui::button(sb, font, uin, cx, ry, cw, 32.0f, tr("menu.setup_sound"), 1.4f)) { panel_ = 1; selOpen_ = -1; selSwallow_ = true; }
            ry += 40.0f;
            if (ui::button(sb, font, uin, cx, ry, cw, 32.0f, tr("menu.setup_video"), 1.4f)) { panel_ = 2; selOpen_ = -1; selSwallow_ = true; }
            ry += 40.0f;
            if (ui::button(sb, font, uin, cx, ry, cw, 32.0f, tr("menu.general"), 1.4f)) { panel_ = 3; selOpen_ = -1; selSwallow_ = true; }
            ry += 40.0f;
            if (ui::button(sb, font, uin, cx, ry, cw, 32.0f, tr("menu.help"), 1.4f)) { panel_ = 4; selOpen_ = -1; selSwallow_ = true; }
            ry += 40.0f;
            // GamePad Setup (#102/#115): only offered while in gamepad mode (stick-axis inversion).
            if (app.gamepadMode()) {
                if (ui::button(sb, font, uin, cx, ry, cw, 32.0f, tr("menu.gamepad_setup"), 1.4f)) { panel_ = 5; selOpen_ = -1; selSwallow_ = true; }
                ry += 40.0f;
            }
            if (bottomItems.empty()) {  // no host actions -> a plain Close
                if (ui::button(sb, font, uin, cx, ry, cw, 32.0f, tr("win.close"), 1.4f)) open = false;
            } else {
                for (const Item& it : bottomItems) {  // e.g. Exit
                    if (ui::button(sb, font, uin, cx, ry, cw, 32.0f, it.label.c_str(), 1.4f) && it.fn) it.fn();
                    ry += 40.0f;
                }
            }
            return open;
        }

        if (panel_ == 4) {  // Help: a topic MENU -> a per-topic page (S.: "по кнопкам, как в подменю").
            using smhelp::HLine;
            const char* const* kTopics = smhelp::kTopics;  // content lives at file scope (see smhelp)
            if (helpTopic_ < 0) {  // topic menu
                float ry = cy;
                for (int i = 0; i < smhelp::kTopicCount; ++i) {
                    if (ui::button(sb, font, uin, cx, ry, cw, 34.0f, kTopics[i], 1.4f)) {
                        helpTopic_ = i; scroll_ = 0.0f; selSwallow_ = true;
                    }
                    ry += 42.0f;
                }
                backButton(app, sb, font, uin, cx, wy + wh - 40.0f, cw);
                return open;
            }
            const HLine* lines = smhelp::lines(helpTopic_);
            const int nLines = smhelp::count(helpTopic_);
            // Fixed topic header (not scrolled), then the clipped/scrolled body below it.
            font.draw(sb, cx, wy + ui::kTitleBarH + 4.0f, 1.4f, ui::color::kWinAccent, kTopics[helpTopic_]);
            const float bandTop = wy + ui::kTitleBarH + 28.0f;
            const float bandBot = wy + wh - 46.0f;  // clear of the Back button
            float ry = cy + 28.0f;
            for (int i = 0; i < nLines; ++i) {
                const HLine& h = lines[i];
                float lead = 0.0f, adv = 20.0f, tsc = 1.0f, tx = cx + 6.0f;
                u32 col = ui::color::kWinTextDim;
                if (h.kind == 2) { lead = 4.0f; adv = 22.0f; tsc = 1.15f; tx = cx + 4.0f; col = ui::color::kWinText; }
                ry += lead;
                if (ry >= bandTop && ry <= bandBot) font.draw(sb, tx, ry, tsc, col, tr(h.text));
                ry += adv;
            }
            // Back returns to the topic menu (not the root settings).
            if (ui::button(sb, font, uin, cx, wy + wh - 40.0f, cw, 30.0f, tr("menu.back"), 1.4f)) {
                helpTopic_ = -1; scroll_ = 0.0f; selSwallow_ = true;
            }
            return open;
        }

        if (panel_ == 5) {  // GamePad Setup (#102/#115): per-axis stick inversion (S.: DirectInput pads
            // sometimes report an axis flipped -- Vader 5 Pro left-stick X reversed).
            bool lx = app.padInvertLX(), ly = app.padInvertLY(), rx = app.padInvertRX(), ry_ = app.padInvertRY();
            auto toggle = [&](float y, const char* label, bool& v) {
                font.draw(sb, cx + 16.0f, y + 4.0f, 1.1f, ui::color::kWinText, label);
                if (ui::button(sb, font, uin, cx + cw - 70.0f, y, 70.0f, 24.0f, v ? "On" : "Off", 1.1f)) {
                    v = !v;
                    app.setPadInvert(lx, ly, rx, ry_);  // persists to game.cfg immediately
                }
            };
            float y = cy;
            font.draw(sb, cx, y, 1.2f, ui::color::kWinText, "Left stick");
            y += 24.0f;
            toggle(y, "Invert X", lx); y += 30.0f;
            toggle(y, "Invert Y", ly); y += 30.0f;
            y += 8.0f;
            font.draw(sb, cx, y, 1.2f, ui::color::kWinText, "Right stick");
            y += 24.0f;
            toggle(y, "Invert X", rx); y += 30.0f;
            toggle(y, "Invert Y", ry_); y += 30.0f;
            y += 8.0f;
            // Swap strafe/turn between the sticks (S.): turn -> left stick, strafe -> right stick.
            font.draw(sb, cx, y + 4.0f, 1.1f, ui::color::kWinText, "Swap strafe/turn");
            if (ui::button(sb, font, uin, cx + cw - 70.0f, y, 70.0f, 24.0f,
                           app.padSwapTurnStrafe() ? "On" : "Off", 1.1f))
                app.setPadSwapTurnStrafe(!app.padSwapTurnStrafe());
            y += 34.0f;
            // Rotate sensitivity slider (S.): 1..10, 10 = current turn speed; lower = slower/finer turns.
            const int sens = app.padRotateSens();
            font.draw(sb, cx, y, 1.1f, ui::color::kWinText, "Rotate sens: " + std::to_string(sens));
            const float trackY = y + 18.0f, trackH = 12.0f;
            const float frac = static_cast<float>(sens - 1) / 9.0f;
            ui::panel(sb, cx, trackY, cw, trackH, ui::rgba(0, 0, 0, 70));
            ui::panel(sb, cx, trackY, cw * frac, trackH, ui::rgba(70, 120, 230, 255));
            ui::panel(sb, cx + cw * frac - 3.0f, trackY - 2.0f, 6.0f, trackH + 4.0f, ui::rgba(40, 90, 210, 255));
            if (uin.mouseDown && uin.hit(cx, trackY - 6.0f, cw, trackH + 12.0f)) {
                const float f = std::clamp((static_cast<float>(uin.mouseX) - cx) / cw, 0.0f, 1.0f);
                const int ns = static_cast<int>(std::round(f * 9.0f)) + 1;
                if (ns != sens) app.setPadRotateSens(ns);
            }
            { bool cf; int st; if (focusCtrl(sb, uin, cx, trackY - 4.0f, cw, trackH + 8.0f, cf, st) && st) app.setPadRotateSens(sens + st); }
            y += 34.0f;
            // Vibration (S.): per-event rumble toggles, all default On. ui::button auto-registers with the
            // gamepad nav focus, so no focusCtrl needed here.
            y += 6.0f;
            font.draw(sb, cx, y, 1.2f, ui::color::kWinText, "Vibration");
            y += 24.0f;
            auto rtoggle = [&](const char* label, bool cur, void (Application::*set)(bool)) {
                font.draw(sb, cx + 16.0f, y + 4.0f, 1.1f, ui::color::kWinText, label);
                if (ui::button(sb, font, uin, cx + cw - 70.0f, y, 70.0f, 24.0f, cur ? "On" : "Off", 1.1f))
                    (app.*set)(!cur);
                y += 30.0f;
            };
            rtoggle("Damage", app.rumbleDamage(), &Application::setRumbleDamage);
            rtoggle("Kill", app.rumbleKill(), &Application::setRumbleKill);
            rtoggle("Level up", app.rumbleLevel(), &Application::setRumbleLevel);
            rtoggle("Menu tick", app.rumbleMenu(), &Application::setRumbleMenu);
            rtoggle("Death", app.rumbleDeath(), &Application::setRumbleDeath);
            rtoggle("Crit hit", app.rumbleCrit(), &Application::setRumbleCrit);
            backButton(app, sb, font, uin, cx, wy + wh - 40.0f, cw);
            return open;
        }

        if (panel_ == 1) {  // Sound: Master / BGM / SFX sliders (blue, 5% steps)
            float vals[3] = {app.masterVol(), app.bgmVol(), app.sfxVol()};
            static const char* const lbl[3] = {"Master", "BGM", "SFX"};
            bool changed = false;
            for (int i = 0; i < 3; ++i) {
                const float ry = cy + static_cast<float>(i) * 46.0f;
                const float trackY = ry + 20.0f, trackH = 12.0f;
                font.draw(sb, cx, ry, 1.1f, ui::color::kWinText,
                          std::string(lbl[i]) + ": " +
                              std::to_string(static_cast<int>(std::lround(vals[i] * 100.0f))) + "%");
                ui::panel(sb, cx, trackY, cw, trackH, ui::rgba(0, 0, 0, 70));
                ui::panel(sb, cx, trackY, cw * vals[i], trackH, ui::rgba(70, 120, 230, 255));
                ui::panel(sb, cx + cw * vals[i] - 3.0f, trackY - 2.0f, 6.0f, trackH + 4.0f,
                          ui::rgba(40, 90, 210, 255));
                if (uin.mouseDown && !selSwallow_ && uin.hit(cx, trackY - 6.0f, cw, trackH + 12.0f)) {
                    const float frac = std::clamp((static_cast<float>(uin.mouseX) - cx) / cw, 0.0f, 1.0f);
                    const float snapped = std::round(frac * 20.0f) / 20.0f;
                    if (snapped != vals[i]) { vals[i] = snapped; changed = true; }
                }
                bool cf; int st;
                if (focusCtrl(sb, uin, cx, trackY - 4.0f, cw, trackH + 8.0f, cf, st) && st) {
                    vals[i] = std::clamp(vals[i] + st * 0.05f, 0.0f, 1.0f);
                    changed = true;
                }
            }
            if (changed) { app.setVolumes(vals[0], vals[1], vals[2]); dirty_ = true; }
            if (dirty_ && !uin.mouseDown) { app.saveConfig(); dirty_ = false; }
            backButton(app, sb, font, uin, cx, wy + wh - 40.0f, cw);
            return open;
        }

        // Panels 2 (Video) + 3 (General): dropdown selects.
        static const int kFps[9] = {24, 25, 30, 45, 50, 60, 75, 90, 0};
        int fpsIdx = 2;
        for (int i = 0; i < 9; ++i) if (kFps[i] == app.fpsLimit()) fpsIdx = i;
        static const char* const kMode[2] = {"Windowed", "Fullscreen"};
        static const char* const kVs[2] = {"Enabled", "Disabled"};
        static const char* const kFpsL[9] = {"24", "25", "30", "45", "50", "60", "75", "90", "No limit"};
        static const char* const kFilt[3] = {"Disabled", "Bilinear", "Trilinear"};
        // Language: ISO code == texts/<code>.cfg base name (stored in game.cfg); the dropdown shows the
        // native name. Keep kLangCode and kLangName in lock-step. (S.: full multi-language list.)
        static const char* const kLangCode[31] = {
            "en", "ru", "uk", "de", "fr", "es", "pt", "it", "pl", "nl", "cs", "sk", "sr", "ro", "el",
            "hu", "fi", "sv", "da", "nb", "tr", "id", "vi", "th", "hi", "ar", "he", "ja", "ko", "zh", "zh_TW"};
        // Native name where the current font atlas can render it (ASCII + Cyrillic only today), else an
        // ASCII endonym so the dropdown stays legible until the glyph atlas is expanded (font task).
        static const char* const kLangName[31] = {
            "English", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9",
            "\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0",
            "Deutsch", "Francais", "Espanol", "Portugues", "Italiano", "Polski", "Nederlands",
            "Cestina", "Slovencina", "\xD0\xA1\xD1\x80\xD0\xBF\xD1\x81\xD0\xBA\xD0\xB8", "Romana",
            "Greek", "Magyar", "Suomi", "Svenska", "Dansk", "Norsk", "Turkce", "Indonesia",
            "Vietnamese", "Thai", "Hindi", "Arabic", "Hebrew", "Japanese", "Korean", "Chinese",
            "Chinese (TW)"};
        constexpr int kLangCount = 31;
        (void)kLangCount;
        static const char* const kGm[3] = {"Kbd+Mouse", "TouchScreen", "GamePad"};
        static const char* const kOnOff[2] = {"Disabled", "Enabled"};
        struct Sel { const char* label; const char* const* opts; int n; int cur; };
        Sel sels[5];
        int nSel = 0;
        if (panel_ == 2) {
            sels[0] = {"Video mode", kMode, 2, app.fullscreen() ? 1 : 0};
            sels[1] = {"V-Sync", kVs, 2, app.vsyncOn() ? 0 : 1};
            sels[2] = {"FPS-lock", kFpsL, 9, fpsIdx};
            sels[3] = {"World filter", kFilt, 3, app.worldFilter()};
            sels[4] = {"Objects filter", kFilt, 3, app.objFilter()};
            nSel = 5;
        } else {
            int langIdx = 0;
            for (int li = 0; li < kLangCount; ++li)
                if (app.language() == kLangCode[li]) { langIdx = li; break; }
            sels[0] = {"Language", kLangName, kLangCount, langIdx};
            sels[1] = {"Game Mode", kGm, 3, std::clamp(app.gameMode(), 0, 2)};
            sels[2] = {"Camera Lock", kOnOff, 2, app.cameraLock() ? 1 : 0};
            nSel = 3;
        }
        int newSel[5];
        for (int i = 0; i < nSel; ++i) newSel[i] = sels[i].cur;
        bool changed = false;
        const float boxX = wx + ww * 0.5f - 6.0f, boxW = ww * 0.5f - 8.0f, boxH = 24.0f;
        for (int i = 0; i < nSel; ++i) {
            const float ry = cy + static_cast<float>(i) * rowH;
            font.draw(sb, cx, ry + 6.0f, 1.0f, ui::color::kWinText, sels[i].label);
            ui::panel(sb, boxX, ry + 2.0f, boxW, boxH, ui::color::kWinButton);
            ui::border(sb, boxX, ry + 2.0f, boxW, boxH, ui::rgba(90, 90, 100, 255));
            font.draw(sb, boxX + 6.0f, ry + 6.0f, 1.0f, ui::color::kWinText, sels[i].opts[sels[i].cur]);
            font.draw(sb, boxX + boxW - 12.0f, ry + 6.0f, 1.0f, ui::color::kWinTextDim, "v");
            if (uin.mousePressed && uin.hit(boxX, ry + 2.0f, boxW, boxH)) {
                if (selOpen_ < 0) {
                    selOpen_ = i;
                    // Scroll a long list so the current choice is roughly centred on open.
                    selListScroll_ = std::max(0.0f, static_cast<float>(sels[i].cur - 5) * 22.0f);
                } else if (selOpen_ == i) {
                    selOpen_ = -1;
                }
            }
            // Gamepad: focus the select box; A or d-pad right = next option, d-pad left = previous.
            bool cf; int st;
            if (focusCtrl(sb, uin, boxX, ry + 2.0f, boxW, boxH, cf, st)) {
                const int d = cf ? 1 : st;
                if (d) { newSel[i] = (sels[i].cur + d + sels[i].n) % sels[i].n; changed = true; }
            }
        }
        // Video: brightness/contrast sliders + HDR toggle below the dropdowns (parity with the old
        // in-game menu). selSwallow_ eats the held click after a dropdown pick so it can't drag a
        // slider under the just-closed list (S.: "клик проходит под селектом").
        if (panel_ == 2) {
            if (!uin.mouseDown) selSwallow_ = false;
            const float gy0 = cy + 5.0f * rowH + 6.0f;
            float gv[2] = {app.brightness(), app.contrast()};
            static const char* const kGl[2] = {"Brightness", "Contrast"};
            bool gch = false;
            for (int i = 0; i < 2; ++i) {
                const float rowY = gy0 + static_cast<float>(i) * 46.0f;
                const float trackY = rowY + 20.0f, trackH = 12.0f;
                font.draw(sb, cx, rowY, 1.1f, ui::color::kWinText,
                          std::string(kGl[i]) + ": " +
                              std::to_string(static_cast<int>(std::lround(gv[i] * 100.0f))) + "%");
                ui::panel(sb, cx, trackY, cw, trackH, ui::rgba(0, 0, 0, 70));
                ui::panel(sb, cx, trackY, cw * gv[i], trackH, ui::rgba(70, 120, 230, 255));
                ui::panel(sb, cx + cw * gv[i] - 3.0f, trackY - 2.0f, 6.0f, trackH + 4.0f,
                          ui::rgba(40, 90, 210, 255));
                if (uin.mouseDown && selOpen_ < 0 && !selSwallow_ &&
                    uin.hit(cx, trackY - 6.0f, cw, trackH + 12.0f)) {
                    const float frac = std::clamp((static_cast<float>(uin.mouseX) - cx) / cw, 0.0f, 1.0f);
                    const float snapped = std::round(frac * 20.0f) / 20.0f;
                    if (snapped != gv[i]) { gv[i] = snapped; gch = true; }
                }
                bool cf; int st;
                if (focusCtrl(sb, uin, cx, trackY - 4.0f, cw, trackH + 8.0f, cf, st) && st) {
                    gv[i] = std::clamp(gv[i] + st * 0.05f, 0.0f, 1.0f);
                    gch = true;
                }
            }
            if (gch) { app.setVideoGrade(gv[0], gv[1]); dirty_ = true; }
            if (dirty_ && !uin.mouseDown) { app.saveConfig(); dirty_ = false; }
            // HDR toggle (#111): render the scene in RGBA16F + ACES tonemap. setHdr persists + stays
            // off if the GPU can't do RGBA16F.
            const float hy = gy0 + 2.0f * 46.0f;
            font.draw(sb, cx, hy, 1.1f, ui::color::kWinText, "HDR");
            if (selOpen_ < 0 &&
                ui::button(sb, font, uin, cx + cw - 90.0f, hy - 4.0f, 90.0f, 26.0f,
                           app.hdr() ? "On" : "Off", 1.2f))
                app.setHdr(!app.hdr());
            // 3D render-scale (#111): one control for both directions -- FSR1 upscales below native
            // (performance), SSAA supersamples above native then downsamples (top quality). Cycles
            // Off -> Quality -> Balanced -> Performance -> SSAA 1.5x -> SSAA 2x. setFsr persists.
            const float fy = hy + 34.0f;
            const float f = app.fsr();
            const char* fl = f >= 1.9f ? "SSAA 2x" : f >= 1.4f ? "SSAA 1.5x"
                             : f >= 0.999f ? "Off" : f >= 0.72f ? "Quality"
                             : f >= 0.58f ? "Balanced" : "Performance";
            font.draw(sb, cx, fy, 1.1f, ui::color::kWinText, "Render Scale");
            if (selOpen_ < 0 &&
                ui::button(sb, font, uin, cx + cw - 130.0f, fy - 4.0f, 130.0f, 26.0f, fl, 1.2f)) {
                const float nf = f >= 1.9f ? 1.0f : f >= 1.4f ? 2.0f
                                 : f >= 0.999f ? 0.77f : f >= 0.72f ? 0.67f
                                 : f >= 0.58f ? 0.5f : 1.5f;
                app.setFsr(nf);
            }
            // Volumetric light (#117): Off / Glow (soft light halos) / Rays (halos + light shafts from
            // the sun and each local light). Shafts are post-only (best with HDR/render-scale off);
            // Glow's halos work in any mode. Cycles on click. setGodrayMode persists.
            const float gry = fy + 34.0f;
            const int gm = app.godrayMode();
            const char* gml = gm == 2 ? "Rays" : gm == 1 ? "Glow" : "Off";
            font.draw(sb, cx, gry, 1.1f, ui::color::kWinText, "Volum. Light");
            if (selOpen_ < 0 &&
                ui::button(sb, font, uin, cx + cw - 90.0f, gry - 4.0f, 90.0f, 26.0f, gml, 1.2f))
                app.setGodrayMode((gm + 1) % 3);
            // Normals (S.): relief from luminance normal maps — Off / x1 / x1.5 / x2, player's
            // choice. Applied live (shader strength), persisted; default x1.5 on real GPUs.
            const float nyy = gry + 34.0f;
            const float nmv = app.normalsMode();
            const char* nml = nmv < 0.5f ? "Off" : nmv < 1.25f ? "x1" : nmv < 1.75f ? "x1.5" : "x2";
            font.draw(sb, cx, nyy, 1.1f, ui::color::kWinText, "Normals");
            if (selOpen_ < 0 &&
                ui::button(sb, font, uin, cx + cw - 90.0f, nyy - 4.0f, 90.0f, 26.0f, nml, 1.2f))
                app.setNormalsMode(nmv < 0.5f ? 1.0f : nmv < 1.25f ? 1.5f : nmv < 1.75f ? 2.0f : 0.0f);
            // Sprite frame interpolation (S.): smoothly tween .act layer transforms between frames.
            // Two independent toggles -- characters/mobs vs effects. Both OFF = the classic snap look.
            const float ay = nyy + 34.0f;
            font.draw(sb, cx, ay, 1.1f, ui::color::kWinText, "Interp. anim");
            if (selOpen_ < 0 &&
                ui::button(sb, font, uin, cx + cw - 90.0f, ay - 4.0f, 90.0f, 26.0f,
                           app.animInterp() ? "On" : "Off", 1.2f))
                app.setAnimInterp(!app.animInterp());
            const float ey = ay + 34.0f;
            font.draw(sb, cx, ey, 1.1f, ui::color::kWinText, "Interp. fx");
            if (selOpen_ < 0 &&
                ui::button(sb, font, uin, cx + cw - 90.0f, ey - 4.0f, 90.0f, 26.0f,
                           app.fxInterp() ? "On" : "Off", 1.2f))
                app.setFxInterp(!app.fxInterp());
        }
        // General: "Use content" (feat/content-sources) — per asset category, which archive
        // family supplies the files: GRO (official GRO.grf) / UaRO (our archives, GRO fallback)
        // / ROeM (RoM.zip, UaRO then GRO fallback). Cycles on click; applied to the VFS live
        // (already-loaded assets refresh on map re-enter) and persisted in game.cfg.
        if (panel_ == 3) {
            // Scale UI selector (S.): x1 / x1.25 / x1.5 / x2 — enlarges the whole in-game UI.
            {
                const float ry = cy + static_cast<float>(nSel) * rowH + 4.0f;  // BELOW the 3 sels
                font.draw(sb, cx + 8.0f, ry + 4.0f, 1.1f, ui::color::kWinText, "Scale UI");  // (Lang/Mode/Cam)
                // Auto (default, S.): derive from screen height (>1500 -> x2, >999 -> x1.5, else x1).
                static const char* const kScaleL[5] = {"Auto", "x1", "x1.25", "x1.5", "x2"};
                static const float kScaleV[5] = {1.0f, 1.0f, 1.25f, 1.5f, 2.0f};  // [0] unused (Auto)
                int cur = 0;  // 0 = Auto
                if (!app.uiScaleAuto())
                    for (int i = 1; i < 5; ++i)
                        if (std::abs(app.uiScale() - kScaleV[i]) < 0.01f) cur = i;
                if (selOpen_ < 0 &&
                    ui::button(sb, font, uin, cx + cw - 90.0f, ry, 90.0f, 24.0f, kScaleL[cur], 1.1f)) {
                    const int nx = (cur + 1) % 5;
                    if (nx == 0) app.setUiScaleAuto(true);
                    else app.setUiScale(kScaleV[nx]);  // disables Auto
                }
            }
            // Control mode (#102/#115): manual Keyboard/Gamepad switch. Gamepad auto-enables when a pad
            // connects (or via the prompt) and auto-disables when all pads leave; this forces it either way.
            {
                const float ry = cy + static_cast<float>(nSel) * rowH + 34.0f;  // below Scale UI
                font.draw(sb, cx + 8.0f, ry + 4.0f, 1.1f, ui::color::kWinText, "Control");
                if (selOpen_ < 0 &&
                    ui::button(sb, font, uin, cx + cw - 110.0f, ry, 110.0f, 24.0f,
                               app.gamepadMode() ? "Gamepad" : "Keyboard", 1.1f))
                    app.setGamepadMode(!app.gamepadMode());
            }
            // "Quality content" (#71 rework): Texture + Sprite each toggle 1k/4k. Replaces the old
            // "Use content" source switcher (RoM shelved, S.). A change applies on the NEXT client start
            // (the patcher re-downloads + remounts). The button shows the EFFECTIVE quality (config value
            // or the platform default); clicking sets an explicit opposite value. Hidden on bundled
            // consoles (base content baked, quality can't change).
            if (!qualityHidden) {
                const float uy0 = cy + static_cast<float>(nSel) * rowH + 70.0f;  // under Scale UI + Control
                font.draw(sb, cx, uy0, 1.2f, ui::color::kWinText, "Quality content");
                const std::string cpid = contentPlatformId();
                auto qualityRow = [&](int i, const char* label, const std::string& cfgVal, bool isSprite,
                                      void (Application::*setter)(const std::string&)) {
                    const float rowY = uy0 + 26.0f + static_cast<float>(i) * 30.0f;
                    font.draw(sb, cx + 8.0f, rowY + 2.0f, 1.0f, ui::color::kWinText, label);
                    const std::string eff = resolveQuality(cfgVal, cpid, isSprite);  // "1k"/"2k"/"4k" (sprites default 1k)
                    if (selOpen_ < 0 &&
                        ui::button(sb, font, uin, cx + cw - 90.0f, rowY - 2.0f, 90.0f, 24.0f,
                                   eff.c_str(), 1.1f))
                        // cycle 1k -> 2k -> 4k -> 1k (S.: вернуть x2 тир), persisted
                        (app.*setter)(eff == "1k" ? "2k" : eff == "2k" ? "4k" : "1k");
                };
                qualityRow(0, "Texture", app.textureQuality(), /*isSprite=*/false, &Application::setTextureQuality);
                qualityRow(1, "Sprite", app.spriteQuality(), /*isSprite=*/true, &Application::setSpriteQuality);
            }
            // Camera control (S.: "настройка камеры"): RMB-drag orbit options. Invert X/Y flip the drag
            // axes; "Lock Y" freezes the pitch and only turns the camera (azimuth) while Ctrl/Shift is held.
            {
                const float baseY = cy + static_cast<float>(nSel) * rowH + (qualityHidden ? 64.0f : 160.0f);
                font.draw(sb, cx, baseY, 1.2f, ui::color::kWinText, "Camera");
                bool ix = app.mouseInvertX(), iy = app.mouseInvertY(), lky = app.mouseLockY();
                auto ctoggle = [&](float y, const char* label, bool& v) {
                    font.draw(sb, cx + 16.0f, y + 4.0f, 1.1f, ui::color::kWinText, label);
                    if (selOpen_ < 0 &&
                        ui::button(sb, font, uin, cx + cw - 70.0f, y, 70.0f, 24.0f, v ? "On" : "Off", 1.1f)) {
                        v = !v;
                        app.setMouseLook(ix, iy, lky);  // persists to game.cfg immediately
                    }
                };
                float y = baseY + 24.0f;
                ctoggle(y, "Invert X", ix); y += 30.0f;
                ctoggle(y, "Invert Y", iy); y += 30.0f;
                ctoggle(y, "Lock Y (Ctrl/Shift)", lky); y += 30.0f;
            }
        }
        backButton(app, sb, font, uin, cx, wy + wh - 40.0f, cw);
        if (selOpen_ >= 0 && selOpen_ < nSel) {
            const int i = selOpen_;
            const float ry = cy + static_cast<float>(i) * rowH;
            const float listY = ry + 2.0f + boxH;
            const int n = sels[i].n;
            const float rowHt = 22.0f;
            // Long lists (e.g. the 31-language selector) scroll inside a fixed viewport so they never
            // run off the screen; short lists render whole. Wheel over the list drives the offset.
            const int kMaxVis = 12;
            const bool scrollable = n > kMaxVis;
            const int vis = scrollable ? kMaxVis : n;
            if (scrollable) {
                const float maxOff = static_cast<float>(n - vis) * rowHt;
                if (uin.wheel != 0.0f && uin.hit(boxX, listY, boxW, static_cast<float>(vis) * rowHt)) {
                    selListScroll_ = std::clamp(selListScroll_ - uin.wheel * rowHt * 2.0f, 0.0f, maxOff);
                    uin.wheel = 0.0f;  // don't let it zoom the camera / scroll the panel
                }
                selListScroll_ = std::clamp(selListScroll_, 0.0f, maxOff);
            } else {
                selListScroll_ = 0.0f;
            }
            const int first = static_cast<int>(selListScroll_ / rowHt);
            for (int v = 0; v < vis; ++v) {
                const int j = first + v;
                if (j >= n) break;
                const float oy = listY + static_cast<float>(v) * rowHt;
                const bool ov = uin.hit(boxX, oy, boxW, rowHt);
                ui::panel(sb, boxX, oy, boxW, rowHt, ov ? ui::color::kWinButtonHi : ui::color::kWinButton);
                ui::border(sb, boxX, oy, boxW, rowHt, ui::rgba(90, 90, 100, 160));
                if (j == sels[i].cur)  // mark the current choice
                    ui::border(sb, boxX, oy, boxW, rowHt, ui::rgba(120, 200, 120, 220));
                font.draw(sb, boxX + 6.0f, oy + 4.0f, 1.0f, ui::color::kWinText, sels[i].opts[j]);
                if (uin.mousePressed && ov) {
                    newSel[i] = j;
                    selOpen_ = -1;
                    changed = true;
                    selSwallow_ = true;  // swallow the held click so it can't drag the slider under it
                }
            }
        }
        if (changed) {
            if (panel_ == 2)
                app.setVideo(newSel[0] == 1, newSel[1] == 0, kFps[std::clamp(newSel[2], 0, 8)], newSel[3],
                             newSel[4]);
            else
                app.setGeneral(kLangCode[newSel[0]], newSel[1], newSel[2] == 1);
            app.saveConfig();
        }
        return open;
    }

private:
    int panel_ = 0;     // 0 root, 1 sound, 2 video, 3 general, 4 help, 5 gamepad setup
    int helpTopic_ = -1;  // Help sub-page: -1 = topic list, 0 Video, 1 Keyboard, 2 Gamepad (S.)
    float scroll_ = 0.0f;  // wheel-scroll offset when a panel is taller than the screen (S.)
    bool scrollDragging_ = false;   // a touch/mouse drag inside a too-tall panel is scrolling it
    int scrollDragLastY_ = 0;       // last mouse Y during a scroll-drag
    float scrollDragMoved_ = 0.0f;  // accumulated drag distance (to tell a scroll from a tap)
    int selOpen_ = -1;  // open dropdown index in the video/general panel
    float selListScroll_ = 0.0f;  // wheel-scroll offset for a long open dropdown (language list)
    bool selSwallow_ = false;  // eat the held click after a dropdown pick (slider-under-list guard)
    bool dirty_ = false;

    void backButton(Application&, SpriteBatch& sb, Font& font, InputState& uin, float x, float y, float w) {
        if (ui::button(sb, font, uin, x, y, w, 30.0f, tr("menu.back"), 1.4f) && selOpen_ < 0) {
            panel_ = 0;
            selOpen_ = -1;
        }
    }

    // Gamepad focus for a non-button control (slider / dropdown): register the rect, draw the magenta
    // ring when focused, and report focus + confirm (A) + a d-pad left/right step. Makes the ESC-menu
    // sliders and selects reachable by the pad (S.: "фокус на ползунки/селекты не наводятся").
    bool padAxisArmed_ = true;  // right-stick horizontal re-armed (returned near centre) for a fresh step

    bool focusCtrl(SpriteBatch& sb, InputState& uin, float x, float y, float w, float h,
                   bool& confirm, int& step) {
        confirm = false;
        step = 0;
        if (!ui::nav().enabled) return false;
        const ui::Nav::R r{x, y, w, h};
        ui::nav().widgets.push_back(r);
        if (ui::nav().fx < 0.0f || !ui::nav().contains(r)) return false;
        ui::border(sb, x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f, ui::rgba(255, 0, 255, 255));
        confirm = ui::nav().confirm;
        // Adjust the focused control with the RIGHT STICK left/right (settings list is vertical, so the
        // stick's vertical moves focus and its horizontal tunes), the d-pad L/R, OR the left/right FACE
        // buttons X/B (S.: "продублируем на лицевые кнопки левую и правую"). West = -, East = +. The
        // stick is analog -> one step per flick via padAxisArmed_ (re-armed near centre in draw()).
        if (uin.pad.west) step = -1;
        else if (uin.pad.east) step = 1;
        else if (uin.pad.dpadLeft) step = -1;
        else if (uin.pad.dpadRight) step = 1;
        else if (padAxisArmed_ && uin.pad.rx > 0.6f) { step = 1; padAxisArmed_ = false; }
        else if (padAxisArmed_ && uin.pad.rx < -0.6f) { step = -1; padAxisArmed_ = false; }
        return true;
    }
};

}  // namespace uaro
