#include "game/LoginScene.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/Application.hpp"
#include "core/Lang.hpp"
#include "core/Log.hpp"
#include "game/CharSelectScene.hpp"
#include "net/Protocol.hpp"
#include "game/SceneAudio.hpp"
#include "ui/Widgets.hpp"

namespace uaro {

namespace {
constexpr float kWinW = 280, kWinH = 120;  // win_login / win_service native size
constexpr float kBtnW = 42, kBtnH = 20;    // button native size
} // namespace

void LoginScene::onEnter(Application& app) {
    user_.maxLen = 23;
    pass_.maxLen = 23;
    pass_.password = true;
    user_.asciiOnly = true;  // account + password: English only, no Cyrillic/other scripts (S.)
    pass_.asciiOnly = true;
    user_.fontScale = 2.0f;  // full-size login window (S.: back to 1x) -> default field text size
    pass_.fontScale = 2.0f;
    // Pre-fill the account name remembered from last time so it doesn't have to be retyped (S.).
    // Start focus on the password when we already have an account name, else on the account field.
    if (user_.value.empty() && !app.lastLogin().empty()) {
        user_.value = app.lastLogin();
        user_.caret = user_.value.size();  // caret at the end of the pre-filled name, not the start (S.)
    }
    focus_ = user_.value.empty() ? 0 : 1;
    uiState_ = UiState::Service;
    phase_ = Phase::Input;
    status_.clear();
    if (app.clientInfo().connections.empty())
        log::warn("LoginScene: no clientinfo connections to offer");
    // Title/login/char-select share new_zone01's theme (S.). Resolve it via mp3nametable; idempotent
    // playBgm means it keeps going unbroken into char-select and back. (#103)
    app.audio().playBgm(app.vfs(), titleBgmPath(app.vfs()));
}

void LoginScene::onResume(Application& app) {
    conn_.disconnect();
    sentLogin_ = false;
    uiState_ = UiState::Service;
    phase_ = Phase::Input;
    status_.clear();
    (void)app;
}

LoginScene::Layout LoginScene::layout(Application& app) const {
    Layout L{};
    // Logical canvas = physical / UI-scale (#134), so the centred login window + text fields grow
    // with the UI-scale setting the same way the in-game HUD does (S.: "окна в логине не
    // масштабируются"). render() draws into this logical space via the logical-begin path and the
    // mouse is scaled to match.
    const float uis = app.uiScale();
    L.W = static_cast<float>(app.render().width()) / uis;
    L.H = static_cast<float>(app.render().height()) / uis;
    L.cx = L.W * 0.5f;

    // Draw the login + service windows (and their buttons) at half the 280x120 skin size (S.: "окно
    // логина/выбора сервиса большое — подложку и кнопки в 2 раза"). Every window-relative offset,
    // button size and inner text scale is multiplied by `s`, so hit-testing stays in sync with drawing.
    const float s = 1.0f;  // full size (S.: 0.5x made the login/service windows too small -> back to 1x)
    L.winW = kWinW * s; L.winH = kWinH * s;
    L.btnW = kBtnW * s; L.btnH = kBtnH * s;
    L.txt = s;  // font scale factor for labels/fields inside the shrunk window

    L.lx = L.cx - L.winW * 0.5f;
    L.ly = L.H * 0.5f - L.winH * 0.5f;
    L.idX = L.lx + 92 * s; L.idY = L.ly + 31 * s; L.idW = 158 * s; L.idH = 20 * s;
    L.pwX = L.lx + 92 * s; L.pwY = L.ly + 64 * s; L.pwW = 158 * s; L.pwH = 20 * s;
    // Buttons sit INSIDE the window's bottom panel (the empty strip below the password
    // field at y~88..118 in the 280x120 skin), not floating below the window (S. report).
    L.loginBtnX = L.cx - 47 * s; L.loginBtnY = L.ly + 98 * s;
    L.backBtnX = L.cx + 5 * s;  L.backBtnY = L.ly + 98 * s;

    L.sx = L.lx; L.sy = L.ly;
    L.listX = L.sx + 10 * s; L.listY = L.sy + 24 * s; L.listW = L.winW - 20 * s; L.rowH = 15 * s;
    L.svcBtnX = L.cx - 47 * s; L.svcBtnY = L.sy + 98 * s;
    L.svcExitX = L.cx + 5 * s; L.svcExitY = L.sy + 98 * s;

    L.statusY = L.ly + L.winH + 8;  // status line below the window
    return L;
}

void LoginScene::submit(Application& app) {
    const auto& conns = app.clientInfo().connections;
    if (conns.empty()) {
        phase_ = Phase::Failed;
        status_ = "No server configured (sclientinfo.xml)";
        return;
    }
    if (user_.value.empty() || pass_.value.empty()) {
        phase_ = Phase::Failed;
        status_ = "Enter account and password";
        return;
    }
    if (activeServer_ >= conns.size()) activeServer_ = 0;
    const auto& c = conns[activeServer_];

    auto& s = app.session();
    s.version = c.version;
    s.langtype = c.langtype;
    s.clientType = static_cast<u8>(c.langtype);
    s.login = user_.value;  // remember the account name to key client-side per-account config
    app.setLastLogin(user_.value);  // persist it so the field is pre-filled next launch (S.)

    conn_.connect(c.address, c.port);
    sentLogin_ = false;
    phase_ = Phase::Connecting;
    status_ = "Connecting to " + (c.display.empty() ? c.address : c.display) + " ...";
    log::info("login: connecting to {}:{} (version {})", c.address, c.port, c.version);
}

void LoginScene::onAccept(Application& app, const net::AcceptLogin& a) {
    if (a.servers.empty()) {
        phase_ = Phase::Failed;
        status_ = "Login OK, but no char-server is online";
        conn_.disconnect();
        return;
    }
    auto& s = app.session();
    s.accountId = a.accountId;
    s.loginId1 = a.loginId1;
    s.loginId2 = a.loginId2;
    s.sex = a.sex;
    const auto& cs = a.servers[0];
    s.charHost = cs.ip;
    s.charPort = cs.port;
    log::info("login: accepted (AID {}), char-server '{}' {}:{} ({} users)", a.accountId, cs.name,
              cs.ip, cs.port, cs.users);

    conn_.disconnect();
    phase_ = Phase::Done;
    app.scenes().push(app, std::make_unique<CharSelectScene>());
}

void LoginScene::pumpReplies(Application& app) {
    auto& rx = conn_.rx();
    while (rx.size() >= 2) {
        const u16 id = net::peekId(rx);
        if (id == net::PKT_AC_ACCEPT_LOGIN) {
            net::AcceptLogin a;
            const usize c = net::decodeAcceptLogin(rx.data(), rx.size(), a);
            if (c == 0) break;
            conn_.consume(c);
            onAccept(app, a);
            return;
        }
        if (id == net::PKT_AC_REFUSE_LOGIN) {
            u8 code = 0;
            std::string ban;
            const usize c = net::decodeRefuseLogin(rx.data(), rx.size(), code, ban);
            if (c == 0) break;
            conn_.consume(c);
            phase_ = Phase::Failed;
            status_ = net::loginErrorText(code);
            if (code == 6 && !ban.empty()) status_ += " (" + ban + ")";
            conn_.disconnect();
            return;
        }
        if (id == net::PKT_SC_NOTIFY_BAN) {
            u8 code = 0;
            const usize c = net::decodeNotifyBan(rx.data(), rx.size(), code);
            if (c == 0) break;
            conn_.consume(c);
            phase_ = Phase::Failed;
            status_ = net::banErrorText(code);
            conn_.disconnect();
            return;
        }
        log::warn("login: unexpected packet 0x{:04x}", id);
        conn_.disconnect();
        phase_ = Phase::Failed;
        status_ = "Unexpected response from server";
        return;
    }
}

void LoginScene::update(Application& app, double dt) {
    time_ += dt;
    InputState in = app.input();  // logical-space copy: all login UI scales with the UI-scale (#134),
    if (app.uiScale() > 1.001f) {  // and login has no 3D world-pick, so nothing needs physical coords
        in.mouseX = static_cast<int>(in.mouseX / app.uiScale());
        in.mouseY = static_cast<int>(in.mouseY / app.uiScale());
    }
    // The shared ESC menu (same component as in-game). Close returns to the login screen
    // (S.: "в menu нужна кнопка Закрыть" — there was only Exit, which quit the client). Exit
    // quits. Set once.
    if (settings_.bottomItems.empty()) {
        settings_.topItems.push_back({tr("win.close"), [this]() { settings_.open = false; }});
        settings_.bottomItems.push_back({tr("menu.exit"), [&app]() { app.requestQuit(); }});
    }

    // While the settings overlay is open it's modal: Esc closes it; all login input is swallowed so a
    // click on the overlay doesn't also hit the login window underneath (#104).
    if (settings_.open) {
        if (in.escape || in.closeRequested) settings_.open = false;
        return;
    }
    const Layout L = layout(app);
    const auto& conns = app.clientInfo().connections;

    // ESC opens the shared menu (like in-game). closeRequested (window X) still exits/backs out.
    if (in.escape) {
        settings_.open = true;
        return;
    }
    // Window-close = cancel or back: from the login form back to service select; from service
    // select, fully quit the client (S.: Exit on the login screen closes the whole app, not just
    // the scene — requestPop only pops a scene and may leave the app running under it).
    if (in.closeRequested) {
        if (uiState_ == UiState::Login) {
            conn_.disconnect();
            uiState_ = UiState::Service;
            phase_ = Phase::Input;
            status_.clear();
        } else {
            app.requestQuit();
        }
        return;
    }

    if (uiState_ == UiState::Service) {
        if (in.mousePressed) {
            for (usize i = 0; i < conns.size(); ++i) {
                const float ry = L.listY + i * L.rowH;
                if (in.hit(L.listX, ry, L.listW, L.rowH)) {
                    activeServer_ = i;
                    if (in.mouseDoubleClick) uiState_ = UiState::Login;  // double-click a row -> proceed (S.)
                }
            }
            if (in.hit(L.svcBtnX, L.svcBtnY, L.btnW, L.btnH) && !conns.empty())
                uiState_ = UiState::Login;
            if (in.hit(L.svcExitX, L.svcExitY, L.btnW, L.btnH)) {
                app.requestQuit();  // Exit -> fully close the client (S.), not just pop the scene
                return;
            }
        }
        if (in.keyDown && !conns.empty()) activeServer_ = (activeServer_ + 1) % conns.size();
        if (in.keyUp && !conns.empty())
            activeServer_ = (activeServer_ + conns.size() - 1) % conns.size();
        if (in.keyEnter && !conns.empty()) uiState_ = UiState::Login;
        // Gamepad: d-pad picks a server, A / Start proceeds to the login form, B exits.
        if (app.gamepadMode() && !conns.empty()) {
            if (in.pad.dpadDown) activeServer_ = (activeServer_ + 1) % conns.size();
            if (in.pad.dpadUp) activeServer_ = (activeServer_ + conns.size() - 1) % conns.size();
            if (in.pad.south || in.pad.start) uiState_ = UiState::Login;
            if (in.pad.east) { app.requestQuit(); return; }  // B on service select = quit client (S.)
        }
        return;
    }

    // --- Login state ---
    const bool editable = (phase_ == Phase::Input || phase_ == Phase::Failed);
    ui::osk().enabled = ui::osk().active = false;  // off unless an editable field is focused below
    if (editable) {
        if (in.mousePressed) {
            // Clicking a field focuses it AND drops the caret at the END of the existing text
            // (RO behaviour: click the pre-filled login -> caret after the name, not at the start). (S.)
            if (in.hit(L.idX, L.idY, L.idW, L.idH)) { focus_ = 0; user_.caret = user_.value.size(); }
            else if (in.hit(L.pwX, L.pwY, L.pwW, L.pwH)) { focus_ = 1; pass_.caret = pass_.value.size(); }
            if (in.hit(L.backBtnX, L.backBtnY, L.btnW, L.btnH)) {
                conn_.disconnect();
                uiState_ = UiState::Service;
                phase_ = Phase::Input;
                status_.clear();
                return;
            }
        }
        if (in.keyTab) focus_ ^= 1;
        // Gamepad: the on-screen keyboard types into the focused field; Start toggles field focus.
        // Injected keystrokes (in.text / keyBackspace / keyEnter) flow into the field updates below.
        if (app.gamepadMode()) {
            ui::osk().enabled = ui::osk().active = true;
            if (in.pad.start) focus_ ^= 1;
            ui::oskInput(in);
        }
        const bool userEnter = user_.update(in, focus_ == 0);
        const bool passEnter = pass_.update(in, focus_ == 1);
        if (userEnter) focus_ = 1;
        const bool btnClicked = in.mousePressed && in.hit(L.loginBtnX, L.loginBtnY, L.btnW, L.btnH);
        if (passEnter || btnClicked) submit(app);
    }

    conn_.update();
    if (phase_ == Phase::Connecting) {
        if (conn_.connected() && !sentLogin_) {
            const auto blob = net::buildCALogin(app.session().version, user_.value, pass_.value,
                                                app.session().clientType);
            conn_.send(blob);
            sentLogin_ = true;
            phase_ = Phase::WaitReply;
            status_ = "Authenticating ...";
        } else if (conn_.closed()) {
            phase_ = Phase::Failed;
            status_ = "Cannot reach the login server";
        }
    } else if (phase_ == Phase::WaitReply) {
        pumpReplies(app);
        if (phase_ == Phase::WaitReply && conn_.closed()) {
            phase_ = Phase::Failed;
            status_ = "Connection closed by server";
        }
    }
}

void LoginScene::renderSkinned(Application& app, const Layout& L) {
    SpriteBatch& sb = app.sprites();
    Font& font = app.font();
    ui::UiSkin& skin = app.uiSkin();
    InputState in = app.input();  // logical-space copy: all login UI scales with the UI-scale (#134),
    if (app.uiScale() > 1.001f) {  // and login has no 3D world-pick, so nothing needs physical coords
        in.mouseX = static_cast<int>(in.mouseX / app.uiScale());
        in.mouseY = static_cast<int>(in.mouseY / app.uiScale());
    }
    const auto& conns = app.clientInfo().connections;
    auto img = [&](const char* n) { return skin.get(n); };

    // Background image removed (S.: "фоновую картинку нужно убрать") -- the flat clear colour shows.

    if (uiState_ == UiState::Service) {
        if (const ui::UiImage* w = img("win_service.bmp"))
            ui::imageScaled(sb, *w, L.sx, L.sy, L.winW, L.winH);
        for (usize i = 0; i < conns.size(); ++i) {
            const float ry = L.listY + i * L.rowH;
            if (i == activeServer_)
                sb.draw(L.listX, ry, L.listW, L.rowH, ui::rgba(90, 140, 210, 120));
            const std::string& nm = conns[i].display.empty() ? conns[i].address : conns[i].display;
            font.draw(sb, L.listX + 4 * L.txt, ry + 3 * L.txt, L.txt, ui::color::kSkinText, nm);
        }
        if (conns.empty())
            font.draw(sb, L.listX + 4 * L.txt, L.listY + 4 * L.txt, L.txt, ui::color::kError,
                      "no server in sclientinfo.xml");
        ui::imageButtonScaled(sb, in, L.svcBtnX, L.svcBtnY, L.btnW, L.btnH, img("btn_connect.bmp"),
                              img("btn_connect_a.bmp"), img("btn_connect_b.bmp"));
        ui::imageButtonScaled(sb, in, L.svcExitX, L.svcExitY, L.btnW, L.btnH, img("btn_exit.bmp"),
                              img("btn_exit_a.bmp"), img("btn_exit_b.bmp"));
        return;
    }

    // Login state
    if (const ui::UiImage* w = img("win_login.bmp"))
        ui::imageScaled(sb, *w, L.lx, L.ly, L.winW, L.winH);
    user_.drawInField(sb, font, L.idX + 4 * L.txt, L.idY, L.idH, focus_ == 0, time_);
    pass_.drawInField(sb, font, L.pwX + 4 * L.txt, L.pwY, L.pwH, focus_ == 1, time_);
    ui::imageButtonScaled(sb, in, L.loginBtnX, L.loginBtnY, L.btnW, L.btnH, img("btn_connect.bmp"),
                          img("btn_connect_a.bmp"), img("btn_connect_b.bmp"));
    ui::imageButtonScaled(sb, in, L.backBtnX, L.backBtnY, L.btnW, L.btnH,
                          img("btn_cancel.bmp") ? img("btn_cancel.bmp") : img("btn_exit.bmp"),
                          img("btn_cancel_a.bmp"), img("btn_cancel_b.bmp"));

    if (activeServer_ < conns.size()) {
        const std::string& nm =
            conns[activeServer_].display.empty() ? conns[activeServer_].address
                                                 : conns[activeServer_].display;
        font.draw(sb, L.lx, L.ly - 18, 1.0f, ui::color::kWhite, nm);
    }
    if (!status_.empty()) {
        const u32 col = (phase_ == Phase::Failed) ? ui::color::kError : ui::color::kWhite;
        font.draw(sb, L.lx, L.statusY, 1.0f, col, status_);
    }
}

void LoginScene::renderFlat(Application& app, const Layout& L) {
    SpriteBatch& sb = app.sprites();
    Font& font = app.font();
    InputState in = app.input();  // logical-space copy: all login UI scales with the UI-scale (#134),
    if (app.uiScale() > 1.001f) {  // and login has no 3D world-pick, so nothing needs physical coords
        in.mouseX = static_cast<int>(in.mouseX / app.uiScale());
        in.mouseY = static_cast<int>(in.mouseY / app.uiScale());
    }
    const auto& conns = app.clientInfo().connections;

    if (uiState_ == UiState::Service) {
        ui::panel(sb, L.sx, L.sy, L.winW, L.winH, ui::color::kWinBody);
        ui::border(sb, L.sx, L.sy, L.winW, L.winH, ui::color::kWinBorder);
        font.draw(sb, L.sx + 8 * L.txt, L.sy + 6 * L.txt, 1.5f * L.txt, ui::color::kWinText,
                  tr("login.service_select"));
        for (usize i = 0; i < conns.size(); ++i) {
            const float ry = L.listY + i * L.rowH;
            if (i == activeServer_) sb.draw(L.listX, ry, L.listW, L.rowH, ui::color::kWinSelect);
            const std::string& nm = conns[i].display.empty() ? conns[i].address : conns[i].display;
            font.draw(sb, L.listX + 4 * L.txt, ry + 3 * L.txt, L.txt, ui::color::kWinText, nm);
        }
        ui::button(sb, font, in, L.svcBtnX, L.svcBtnY, L.btnW, L.btnH, tr("common.ok"), L.txt);
        ui::button(sb, font, in, L.svcExitX, L.svcExitY, L.btnW, L.btnH, tr("menu.exit"), L.txt);
        return;
    }
    ui::panel(sb, L.lx, L.ly, L.winW, L.winH, ui::color::kWinBody);
    ui::border(sb, L.lx, L.ly, L.winW, L.winH, ui::color::kWinBorder);
    font.draw(sb, L.lx + 12 * L.txt, L.idY - 16 * L.txt, 1.5f * L.txt, ui::color::kWinTextDim,
              tr("login.account"));
    user_.draw(sb, font, L.idX, L.idY, L.idW, L.idH, focus_ == 0, time_, "account");
    font.draw(sb, L.lx + 12 * L.txt, L.pwY - 16 * L.txt, 1.5f * L.txt, ui::color::kWinTextDim,
              tr("login.pass"));
    pass_.draw(sb, font, L.pwX, L.pwY, L.pwW, L.pwH, focus_ == 1, time_, "password");
    ui::button(sb, font, in, L.loginBtnX, L.loginBtnY, L.btnW, L.btnH, tr("login.login"), L.txt);
    ui::button(sb, font, in, L.backBtnX, L.backBtnY, L.btnW, L.btnH, tr("menu.back"), L.txt);
    if (!status_.empty()) {
        const u32 col = (phase_ == Phase::Failed) ? ui::color::kError : ui::color::kWinTextDim;
        font.draw(sb, L.lx, L.statusY, 1.5f, col, status_);
    }
}

void LoginScene::render(Application& app) {
    SpriteBatch& sb = app.sprites();
    if (!sb.ready()) return;
    const Layout L = layout(app);
    // Draw into the logical canvas (physical / UI-scale) so the whole login screen scales (#134).
    const float uis = app.uiScale();
    sb.begin(app.render().width(), app.render().height(), 0,
             static_cast<int>(app.render().width() / uis),
             static_cast<int>(app.render().height() / uis));
    // Light-blue background instead of the old picture (S.: "фон должен быть светло голубой").
    sb.draw(0.0f, 0.0f, L.W, L.H, ui::rgba(173, 216, 235, 255));
    if (app.uiSkin().ready())
        renderSkinned(app, L);
    else
        renderFlat(app, L);

    // Two text blocks framing the login window: one above, one below (S.). Text from the i18n table
    // (login.top / login.bottom), editable via texts/<lang>.cfg. 18px gold with a black outline (S.).
    {
        Font& font = app.font();
        const float sc = 22.0f / 16.0f;  // 22px (Font::kRenderPx is 16px at scale 1)
        const u32 black = ui::rgba(0, 0, 0, 255), gold = ui::rgba(255, 215, 0, 255);
        auto block = [&](float ty, const std::string& text) {
            const float tx = L.cx - font.width(text, sc) * 0.5f;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    if (dx || dy) font.draw(sb, tx + dx, ty + dy, sc, black, text);  // outline
            font.draw(sb, tx, ty, sc, gold, text);
        };
        const float lh = font.lineHeight(sc);
        const std::string topT = tr("login.top");
        int topLines = 1;
        for (char c : topT) if (c == '\n') ++topLines;
        block(L.ly - 16.0f - static_cast<float>(topLines) * lh, topT);  // bottom edge ~16px above window
        block(L.ly + L.winH + 26.0f, tr("login.bottom"));                // below it
        // MS-cert account rule (S.): login+password must be >= 5 chars, or account creation fails.
        // Show it under the bottom block so a reviewer doesn't try a 4-char name and get rejected.
        int botLines = 1;
        for (char c : tr("login.bottom")) if (c == '\n') ++botLines;
        block(L.ly + L.winH + 26.0f + static_cast<float>(botLines) * lh + 6.0f, tr("login.minlen"));
    }

    // Version label (bottom-left) so the Alpha mark is visible even in fullscreen (S.).
    // Version label derives from the SINGLE source of truth (Client/VERSION -> UARO_VERSION_STR), so the
    // "Alpha N" here can never drift from the window title again (S.: "почему версия осталась 0.0.11").
    // Alpha number = the patch component (3rd dotted field): 0.0.12.0 -> "Alpha12".
    {
        const std::string ver = UARO_VERSION_STR;             // e.g. "0.0.12.0"
        const std::size_t p2 = ver.find('.', ver.find('.') + 1);  // dot before the patch field
        const std::size_t p3 = (p2 == std::string::npos) ? p2 : ver.find('.', p2 + 1);
        const std::string patch =
            (p2 != std::string::npos)
                ? ver.substr(p2 + 1, (p3 == std::string::npos ? ver.size() : p3) - (p2 + 1))
                : ver;                                        // "12"
        app.font().draw(sb, 8.0f, L.H - 20.0f, 1.0f, 0xffc8c8c8u, "BornRok Alpha" + patch);
    }

    // "Menu" button (top-right) -> the Settings overlay (Sound / Video / General), so options are
    // reachable from the login screen, not only the in-game ESC menu (S. #104).
    // Scaled logical-space copy of the mouse for the Menu button + settings overlay (they live at
    // logical coords now). The settings menu keeps its own open/closed state, so a copy is fine.
    InputState uin = app.input();
    if (uis > 1.001f) {
        uin.mouseX = static_cast<int>(uin.mouseX / uis);
        uin.mouseY = static_cast<int>(uin.mouseY / uis);
    }
    const float gw = 64.0f, gh = 28.0f, gx = L.W - gw - 8.0f, gy = 8.0f;
    if (ui::button(sb, app.font(), uin, gx, gy, gw, gh, tr("login.menu"), 1.3f) && !settings_.open)
        settings_.open = true;

    // "How to create an account" button (bottom-centre) -> a popup with the account-creation rules
    // (login.howto). Moved out of the always-on login.bottom text so the screen stays clean (S.).
    {
        Font& font = app.font();
        const std::string blbl = tr("login.howto_btn");
        const float bsc = 1.2f;
        const float hbw = std::max(180.0f, font.width(blbl, bsc) + 28.0f), hbh = 30.0f;
        const float hbx = L.cx - hbw * 0.5f, hby = L.H - hbh - 12.0f;
        if (ui::button(sb, font, uin, hbx, hby, hbw, hbh, blbl, bsc) && !settings_.open)
            howtoOpen_ = !howtoOpen_;
        if (howtoOpen_) {
            // Fixed-width popup; the rules text is WORD-WRAPPED to the window width so it never runs off
            // the panel, and the body SCROLLS (wheel) when it's taller than the visible area (S.).
            const float tsc = 1.1f, lh = font.lineHeight(tsc), tpad = 16.0f, titleH = 30.0f;
            const float pw = std::min(480.0f, L.W - 40.0f);
            const float iw = pw - tpad * 2.0f;  // inner text width
            // Greedy word-wrap each explicit '\n' segment to iw.
            std::vector<std::string> lines;
            {
                std::string seg;
                const std::string body = tr("login.howto");
                auto wrap = [&](const std::string& s) {
                    std::string curL, word;
                    auto flush = [&]() {
                        if (word.empty()) return;
                        const std::string cand = curL.empty() ? word : curL + " " + word;
                        if (!curL.empty() && font.width(cand, tsc) > iw) { lines.push_back(curL); curL = word; }
                        else curL = cand;
                        word.clear();
                    };
                    for (char c : s) { if (c == ' ') flush(); else word += c; }
                    flush();
                    lines.push_back(curL);  // keep the segment's last line (even if empty)
                };
                for (char c : body) { if (c == '\n') { wrap(seg); seg.clear(); } else seg += c; }
                wrap(seg);
            }
            const float okH = 34.0f;
            const float contentH = static_cast<float>(lines.size()) * lh;
            const float maxBodyH = std::max(lh, L.H * 0.6f - titleH - okH - tpad);
            const float bodyH = std::min(contentH, maxBodyH);
            const float ph = titleH + bodyH + tpad + okH;
            const float px = L.cx - pw * 0.5f, py = L.H * 0.5f - ph * 0.5f;
            const float maxScroll = std::max(0.0f, contentH - bodyH);
            if (uin.mouseX >= px && uin.mouseX < px + pw && uin.mouseY >= py && uin.mouseY < py + ph)
                howtoScroll_ -= uin.wheel * lh * 3.0f;
            howtoScroll_ = std::clamp(howtoScroll_, 0.0f, maxScroll);
            ui::panel(sb, px - 2.0f, py - 2.0f, pw + 4.0f, ph + 4.0f, ui::rgba(20, 24, 34, 255));
            ui::panel(sb, px, py, pw, titleH, ui::rgba(46, 74, 120, 255));
            ui::panel(sb, px, py + titleH, pw, ph - titleH, ui::rgba(238, 240, 246, 255));
            font.draw(sb, px + tpad, py + 7.0f, 1.2f, ui::rgba(255, 255, 255, 255), tr("login.howto_btn"));
            // Draw only the lines within the scrolled body window.
            const float bodyTop = py + titleH + tpad * 0.5f;
            const int first = static_cast<int>(howtoScroll_ / lh);
            for (int i = first; i < static_cast<int>(lines.size()); ++i) {
                const float ly = bodyTop + static_cast<float>(i) * lh - howtoScroll_;
                if (ly > bodyTop + bodyH) break;
                font.draw(sb, px + tpad, ly, tsc, ui::rgba(30, 34, 44, 255), lines[static_cast<usize>(i)]);
            }
            // Scrollbar (right edge of the body) when the text overflows.
            if (maxScroll > 0.0f) {
                const float trackH = bodyH, trackX = px + pw - 6.0f, trackY = bodyTop;
                ui::panel(sb, trackX, trackY, 4.0f, trackH, ui::rgba(200, 204, 212, 255));
                const float thumbH = std::max(20.0f, trackH * bodyH / contentH);
                const float thumbY = trackY + (trackH - thumbH) * (howtoScroll_ / maxScroll);
                ui::panel(sb, trackX, thumbY, 4.0f, thumbH, ui::rgba(90, 110, 150, 255));
            }
            const float cbw = 100.0f, cbh = 26.0f;
            if (ui::button(sb, font, uin, px + (pw - cbw) * 0.5f, py + ph - cbh - 6.0f, cbw, cbh,
                           tr("common.ok"), 1.15f))
                howtoOpen_ = false;
            if (app.input().escape) howtoOpen_ = false;
        }
    }

    settings_.draw(app, sb, app.font(), uin, L.W, L.H);
    if (ui::osk().active) ui::oskRender(sb, app.font(), L.W, L.H);  // gamepad on-screen keyboard, on top
    sb.end();
}

} // namespace uaro
