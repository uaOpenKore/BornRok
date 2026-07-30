#pragma once
// Pre-game patcher phase: runs PatchRunner on a worker thread and shows a status
// screen (light-blue bg, white-with-outline text), then hands off to LoginScene.
// This is the functional first cut using the normal bgfx UI; the A(1) static/
// SDL-software packaging is a later refinement.
#include <atomic>
#include <mutex>
#include <thread>

#include "app/Scene.hpp"
#include "patcher/PatchRunner.hpp"

namespace uaro {

class PatcherScene : public Scene {
public:
    ~PatcherScene() override;
    void onEnter(Application& app) override;
    void update(Application& app, double dt) override;
    void render(Application& app) override;

private:
    std::thread worker_;
    std::mutex mu_;
    PatchProgress prog_;          // guarded by mu_
    std::atomic<bool> done_{false};
    PatchSummary summary_;        // written once before done_ flips
    double quitAt_ = -1.0;        // >0: self-updated; close the client at this time (S.)
    bool started_ = false;
    bool transitioned_ = false;
    double doneAt_ = -1.0;        // time the patch finished (brief "done" hold before login)
    double time_ = 0.0;
};

}  // namespace uaro
