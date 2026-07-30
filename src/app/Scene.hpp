#pragma once

namespace uaro {

class Application;  // scenes access engine services through the app

// A screen/state in the game (boot, login, char-select, in-game, ...).
// v0 ships a single BootScene; the state machine grows in v5.
class Scene {
public:
    virtual ~Scene() = default;

    virtual void onEnter(Application&) {}
    virtual void onExit(Application&) {}
    // Called when another scene is pushed on top of / popped back to this one.
    virtual void onPause(Application&) {}
    virtual void onResume(Application&) {}
    virtual void update(Application&, double dt) { (void)dt; }
    virtual void render(Application&) {}
};

} // namespace uaro
