#include "game/MapScene.hpp"

#include <cmath>

#include "app/Application.hpp"

namespace uaro {

MapScene::MapScene(std::string mapName) : mapName_(std::move(mapName)) {}

void MapScene::onEnter(Application& app) { renderer_.load(app, mapName_); }

void MapScene::onExit(Application&) { renderer_.destroy(); }

void MapScene::update(Application& app, double dt) {
    time_ += dt;
    // Dev map viewer: ESC / window-close exits to desktop (no scene flow here).
    if (app.input().escape || app.input().closeRequested) app.requestQuit();
}

void MapScene::render(Application& app) {
    if (!renderer_.ready()) return;

    const int w = app.render().width();
    const int h = app.render().height();
    const float aspect = h > 0 ? static_cast<float>(w) / h : 1.0f;

    const Vec3 c = renderer_.center();
    const float r = renderer_.radius();
    const double ang = time_ * 0.15;  // slow orbit
    const Vec3 eye{c.x + static_cast<float>(std::cos(ang)) * r * 1.6f, c.y + r * 1.2f,
                   c.z + static_cast<float>(std::sin(ang)) * r * 1.6f};
    const Mat4 view = Mat4::lookAt(eye, c, Vec3{0, 1, 0});
    const bool homog = bgfx::getCaps()->homogeneousDepth;
    const Mat4 proj = Mat4::perspective(radians(60.0f), aspect, 1.0f, r * 12.0f + 2000.0f, homog);
    renderer_.render(view, proj);
}

} // namespace uaro
