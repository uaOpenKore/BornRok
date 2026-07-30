#pragma once
#include "app/Scene.hpp"
#include "render/Texture.hpp"

namespace uaro {

// The v0 "it renders" scene: a few coloured quads plus an animated, checker-
// textured sprite. Proves the window + device + shader + texture + batch path
// end to end. Esc quits (handled by the platform event pump).
class BootScene : public Scene {
public:
    void onEnter(Application& app) override;
    void onExit(Application& app) override;
    void update(Application& app, double dt) override;
    void render(Application& app) override;

private:
    Texture checker_;
    double time_ = 0.0;
};

} // namespace uaro
