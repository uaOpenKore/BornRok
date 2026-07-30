#pragma once
#include <string>
#include <unordered_map>

#include "core/Types.hpp"
#include "render/Texture.hpp"

namespace uaro {

class Vfs;

namespace ui {

// One loaded UI bitmap: the GPU texture plus its native pixel size (needed for
// layout). Texture has no destructor, so copies are cheap handle copies and the
// owning UiSkin frees each handle exactly once.
struct UiImage {
    Texture tex;
    int w = 0;
    int h = 0;
    bool valid() const { return tex.valid(); }
};

// Loads the original client's login UI bitmaps (data/texture/<유저인터페이스>/
// login_interface/*.bmp) and exposes them by filename. RO uses magenta (255,0,255)
// as the transparency key, which is applied at load. If the assets are missing
// (no GRF / different pack) ready() is false and scenes fall back to flat panels.
class UiSkin {
public:
    void init(const Vfs& vfs);
    void destroy();
    bool ready() const { return ready_; }

    // Returns the named image (e.g. "win_login.bmp") or nullptr if not loaded.
    const UiImage* get(const std::string& name) const;

private:
    std::unordered_map<std::string, UiImage> imgs_;
    bool ready_ = false;
};

} // namespace ui
} // namespace uaro
