#pragma once
#include <memory>
#include <vector>

#include "app/Scene.hpp"

namespace uaro {

class Application;

// LIFO stack of scenes. Only the top scene receives update()/render(); this is
// enough for push/pop screen flow and modal overlays later.
class SceneStack {
public:
    void push(Application& app, std::unique_ptr<Scene> scene);
    void pop(Application& app);
    void clear(Application& app);

    // Defer a pop until the end of the frame. A scene must not pop itself while
    // its own update() is running (it would destroy the live object); it calls
    // requestPop() instead, and Application drains it via applyPending().
    void requestPop() { ++pendingPops_; }
    void applyPending(Application& app);

    Scene* top();
    bool empty() const { return scenes_.empty(); }

    void update(Application& app, double dt);
    void render(Application& app);

private:
    std::vector<std::unique_ptr<Scene>> scenes_;
    int pendingPops_ = 0;
};

} // namespace uaro
