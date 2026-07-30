#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// One sprite clip within an animation frame: which SPR frame to draw and how.
struct ActLayer {
    i32 x = 0;
    i32 y = 0;
    i32 sprIndex = -1;  // index into the paired sprite's frames (-1 = none)
    i32 mirror = 0;     // flip horizontally
    u8 r = 255, g = 255, b = 255, a = 255;
    f32 scaleX = 1.0f;
    f32 scaleY = 1.0f;
    i32 rotation = 0;
    i32 sprType = 0;    // 0 = indexed (palette) frame, 1 = truecolor frame
    i32 width = 0;
    i32 height = 0;
};

// One animation step. Anchors are stored as (x, y, attr).
struct ActFrame {
    std::vector<ActLayer> layers;
    i32 eventId = -1;
    std::vector<std::array<i32, 3>> anchors;
};

struct ActAction {
    std::vector<ActFrame> frames;
    f32 delay = 0.0f;  // animation speed (lower = faster)
};

struct ActEvent {
    std::string name;  // sound/event name
};

// Parsed RO ACT file (sprite actions/animations). Pairs with a Sprite by frame
// index; playback comes later (v3/v5).
class Action {
public:
    static std::optional<Action> parse(const std::vector<u8>& bytes);

    // Build an in-memory action set (no .act file behind it) — used by the PNG-sprite
    // loader (#109) to synthesize the standard action*8+dir layout from a manifest.
    static Action fromParts(std::vector<ActAction> actions, std::vector<ActEvent> events) {
        Action a;
        a.version_ = 0x300;  // synthetic marker
        a.actions_ = std::move(actions);
        a.events_ = std::move(events);
        return a;
    }

    u16 version() const { return version_; }
    const std::vector<ActAction>& actions() const { return actions_; }
    const std::vector<ActEvent>& events() const { return events_; }

private:
    u16 version_ = 0;
    std::vector<ActAction> actions_;
    std::vector<ActEvent> events_;
};

} // namespace uaro
