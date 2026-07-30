#include "app/SceneStack.hpp"

namespace uaro {

void SceneStack::push(Application& app, std::unique_ptr<Scene> scene) {
    if (!scenes_.empty()) scenes_.back()->onPause(app);
    scenes_.push_back(std::move(scene));
    scenes_.back()->onEnter(app);
}

void SceneStack::pop(Application& app) {
    if (scenes_.empty()) return;
    scenes_.back()->onExit(app);
    scenes_.pop_back();
    if (!scenes_.empty()) scenes_.back()->onResume(app);
}

void SceneStack::clear(Application& app) {
    pendingPops_ = 0;
    while (!scenes_.empty()) {
        scenes_.back()->onExit(app);
        scenes_.pop_back();
    }
}

void SceneStack::applyPending(Application& app) {
    while (pendingPops_ > 0 && !scenes_.empty()) {
        --pendingPops_;
        pop(app);
    }
    pendingPops_ = 0;
}

Scene* SceneStack::top() {
    return scenes_.empty() ? nullptr : scenes_.back().get();
}

void SceneStack::update(Application& app, double dt) {
    if (!scenes_.empty()) scenes_.back()->update(app, dt);
}

void SceneStack::render(Application& app) {
    if (!scenes_.empty()) scenes_.back()->render(app);
}

} // namespace uaro
