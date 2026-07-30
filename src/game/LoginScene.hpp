#pragma once
#include <string>

#include "app/Scene.hpp"
#include "core/Types.hpp"
#include "game/SettingsMenu.hpp"
#include "net/Connection.hpp"
#include "ui/Widgets.hpp"

namespace uaro {

namespace net { struct AcceptLogin; }

// The login screen, skinned to the original client: a service-select step (pick a
// server from clientinfo) then the login window (account/password). On success it
// stores the auth ids + char-server address and pushes the char-select screen.
// Falls back to flat panels when the original UI bitmaps are unavailable.
class LoginScene : public Scene {
public:
    void onEnter(Application& app) override;
    void onResume(Application& app) override;
    void update(Application& app, double dt) override;
    void render(Application& app) override;

private:
    enum class UiState { Service, Login };          // which screen is shown
    enum class Phase { Input, Connecting, WaitReply, Failed, Done };  // network state

    // Layout rectangles, computed from the window size (shared by update/render so
    // hit-testing and drawing stay in sync). Both skin and flat modes use these.
    struct Layout {
        float W, H, cx;
        float lx, ly;                       // login window (280x120) origin
        float idX, idY, idW, idH;
        float pwX, pwY, pwW, pwH;
        float loginBtnX, loginBtnY, backBtnX, backBtnY;
        float sx, sy;                       // service window (280x120) origin
        float listX, listY, listW, rowH;
        float svcBtnX, svcBtnY, svcExitX, svcExitY;
        float statusY;
        float winW, winH;                   // drawn window size (skin scaled down, S.: 0.5x)
        float btnW, btnH;                   // drawn button size (scaled with the window)
        float txt;                          // text scale for labels/fields inside the scaled window
    };
    Layout layout(Application& app) const;

    void submit(Application& app);
    void pumpReplies(Application& app);
    void onAccept(Application& app, const net::AcceptLogin& a);
    void renderSkinned(Application& app, const Layout& L);
    void renderFlat(Application& app, const Layout& L);

    net::Connection conn_;
    ui::TextField user_;
    ui::TextField pass_;
    UiState uiState_ = UiState::Service;
    int focus_ = 0;           // 0 = account, 1 = password
    usize activeServer_ = 0;  // selected clientinfo connection
    Phase phase_ = Phase::Input;
    bool sentLogin_ = false;
    std::string status_;
    double time_ = 0.0;
    SettingsMenu settings_;  // gear-button settings overlay (Sound/Video/General) (#104)
    bool howtoOpen_ = false;  // "How to create an account" popup (S.): rules from login.howto
    float howtoScroll_ = 0.0f;  // scroll offset (px) for the howto popup body
};

} // namespace uaro
