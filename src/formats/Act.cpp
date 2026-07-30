#include "formats/Act.hpp"

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"

namespace uaro {

std::optional<Action> Action::parse(const std::vector<u8>& bytes) {
    if (bytes.size() < 16) {
        log::error("ACT: too small");
        return std::nullopt;
    }
    try {
        ByteReader r(bytes);
        if (r.u8v() != 'A' || r.u8v() != 'C') {
            log::error("ACT: bad signature");
            return std::nullopt;
        }
        Action act;
        act.version_ = r.u16le();
        const u16 v = act.version_;
        const u16 actionCount = r.u16le();
        r.skip(10);  // reserved

        // Sanity caps so a misread count cannot trigger a huge allocation.
        constexpr u32 kCap = 0xFFFF;
        act.actions_.reserve(actionCount);
        for (u16 a = 0; a < actionCount; ++a) {
            ActAction action;
            const u32 frameCount = r.u32le();
            if (frameCount > kCap) {
                log::error("ACT: implausible frame count {}", frameCount);
                return std::nullopt;
            }
            action.frames.reserve(frameCount);

            for (u32 f = 0; f < frameCount; ++f) {
                ActFrame frame;
                r.skip(32);  // per-frame reserved range box (8 * int32)
                const u32 layerCount = r.u32le();
                if (layerCount > kCap) {
                    log::error("ACT: implausible layer count {}", layerCount);
                    return std::nullopt;
                }
                frame.layers.reserve(layerCount);

                for (u32 l = 0; l < layerCount; ++l) {
                    ActLayer layer;
                    layer.x = r.i32le();
                    layer.y = r.i32le();
                    layer.sprIndex = r.i32le();
                    layer.mirror = r.i32le();
                    if (v >= 0x200) {
                        layer.r = r.u8v();
                        layer.g = r.u8v();
                        layer.b = r.u8v();
                        layer.a = r.u8v();
                        layer.scaleX = r.f32le();
                        layer.scaleY = (v >= 0x204) ? r.f32le() : layer.scaleX;
                        layer.rotation = r.i32le();
                        layer.sprType = r.i32le();
                        if (v >= 0x205) {
                            layer.width = r.i32le();
                            layer.height = r.i32le();
                        }
                    }
                    frame.layers.push_back(layer);
                }

                if (v >= 0x200) frame.eventId = r.i32le();

                if (v >= 0x203) {
                    const u32 anchorCount = r.u32le();
                    if (anchorCount > kCap) {
                        log::error("ACT: implausible anchor count {}", anchorCount);
                        return std::nullopt;
                    }
                    frame.anchors.reserve(anchorCount);
                    for (u32 an = 0; an < anchorCount; ++an) {
                        r.skip(4);  // reserved
                        i32 ax = r.i32le();
                        i32 ay = r.i32le();
                        i32 attr = r.i32le();
                        frame.anchors.push_back({ax, ay, attr});
                    }
                }
                action.frames.push_back(std::move(frame));
            }
            act.actions_.push_back(std::move(action));
        }

        // Events (named sounds), version >= 2.1.
        if (v >= 0x201) {
            const u32 eventCount = r.u32le();
            if (eventCount > kCap) {
                log::error("ACT: implausible event count {}", eventCount);
                return std::nullopt;
            }
            act.events_.reserve(eventCount);
            for (u32 e = 0; e < eventCount; ++e) {
                act.events_.push_back({r.read_cstring(40)});
            }
        }

        // Per-action animation delays, version >= 2.2.
        if (v >= 0x202) {
            for (auto& action : act.actions_) action.delay = r.f32le();
        }

        return act;
    } catch (const std::out_of_range&) {
        log::error("ACT: truncated/corrupt file");
        return std::nullopt;
    }
}

} // namespace uaro
