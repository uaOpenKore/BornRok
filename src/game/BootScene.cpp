#include "game/BootScene.hpp"

#include <cmath>
#include <vector>

#include "app/Application.hpp"
#include "core/Log.hpp"

namespace uaro {

namespace {
// Build a 64x64 RGBA checkerboard.
std::vector<u8> make_checker(int size, int cell) {
    std::vector<u8> px(static_cast<usize>(size) * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            bool on = ((x / cell) + (y / cell)) % 2 == 0;
            u8* p = &px[(static_cast<usize>(y) * size + x) * 4];
            u8 v = on ? 230 : 40;
            p[0] = v;          // R
            p[1] = on ? 200 : 40;  // G
            p[2] = on ? 60 : 40;   // B
            p[3] = 255;        // A
        }
    return px;
}
}  // namespace

void BootScene::onEnter(Application&) {
    auto px = make_checker(64, 8);
    checker_.create(64, 64, px.data());
    log::info("BootScene ready");
}

void BootScene::onExit(Application&) {
    checker_.destroy();
}

void BootScene::update(Application& app, double dt) {
    time_ += dt;
    // Fallback scene: ESC / window-close exits to desktop.
    if (app.input().escape || app.input().closeRequested) app.requestQuit();
}

void BootScene::render(Application& app) {
    SpriteBatch& sb = app.sprites();
    if (!sb.ready()) return;  // shaders missing -> clear colour only

    const int w = app.render().width();
    const int h = app.render().height();

    sb.begin(w, h);
    // Three solid colour quads (ABGR: 0xAABBGGRR).
    sb.draw(40.0f, 40.0f, 120.0f, 120.0f, 0xff0000ffu);   // red
    sb.draw(180.0f, 40.0f, 120.0f, 120.0f, 0xff00ff00u);  // green
    sb.draw(320.0f, 40.0f, 120.0f, 120.0f, 0xffff0000u);  // blue

    // Animated checker sprite sweeping across the screen.
    const float span = static_cast<float>(w) - 200.0f;
    const float x = 40.0f + static_cast<float>((std::sin(time_) * 0.5 + 0.5)) * (span > 0 ? span : 0);
    sb.draw(x, 220.0f, 160.0f, 160.0f, 0xffffffffu, checker_);

    sb.end();
}

} // namespace uaro
