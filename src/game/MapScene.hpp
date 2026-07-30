#pragma once
#include <string>

#include "app/Scene.hpp"
#include "game/MapRenderer.hpp"

namespace uaro {

// Offline map viewer: loads a map by name through the VFS and renders its ground
// with a slow orbiting camera. The dev path (RoLegacy <map>); the online path
// is GameScene. Models/water/UI come in later increments.
class MapScene : public Scene {
public:
    explicit MapScene(std::string mapName);

    void onEnter(Application& app) override;
    void onExit(Application& app) override;
    void update(Application& app, double dt) override;
    void render(Application& app) override;

private:
    std::string mapName_;
    MapRenderer renderer_;
    double time_ = 0.0;
};

} // namespace uaro
