#pragma once
#include <mutex>
#include <utility>

#include "core/Types.hpp"

namespace uaro {

// A single-slot hand-off of the latest published value from one thread to another:
// the logic thread publishes a fresh render snapshot each tick, the render thread
// consumes the most recent complete one. The consumer never sees a torn value and
// always gets the newest; snapshots the consumer was too slow to read are simply
// overwritten. Versioned so the consumer skips the copy when nothing changed.
//
// This is the hand-off the render-thread split of the 4-thread architecture needs
// (Phase 2): render() reads live game state today, which is why it can't run on its
// own thread yet — publishing an immutable snapshot here breaks that coupling.
//
// Mutex-guarded: correct and simple, good enough for one-snapshot-per-frame rates.
// If a hot path ever needs it, swap for a lock-free triple buffer behind this same
// interface (the same note ThreadSafeQueue carries).
template <typename T>
class SnapshotMailbox {
public:
    // Producer side: publish a new snapshot, overwriting any unconsumed one.
    void publish(T v) {
        std::lock_guard<std::mutex> lk(m_);
        value_ = std::move(v);
        ++version_;
    }

    // Consumer side: copy the latest snapshot into `out` if it is newer than
    // `seenVersion` (which is then advanced to it). Returns true if `out` was updated,
    // false if nothing changed since (the consumer should keep its previous snapshot).
    // `seenVersion` starts at 0; version 0 means nothing has been published yet.
    bool consume(T& out, u64& seenVersion) const {
        std::lock_guard<std::mutex> lk(m_);
        if (version_ == seenVersion) return false;
        out = value_;
        seenVersion = version_;
        return true;
    }

    // The current published version (0 = nothing published yet).
    u64 version() const {
        std::lock_guard<std::mutex> lk(m_);
        return version_;
    }

private:
    mutable std::mutex m_;
    T value_{};
    u64 version_ = 0;  // bumped on each publish
};

} // namespace uaro
