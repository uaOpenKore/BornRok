#pragma once
#include <algorithm>

namespace uaro {

// Exponential back-off for a "retry until it works" loop: the first retry comes `first` seconds after a
// failure, each further failure doubles the wait up to `cap`. A transient failure (GRF/GPU contention
// during the map-entry burst) recovers within a frame or two; a permanent one (a sprite that doesn't
// exist) costs one attempt every `cap` seconds instead of one per frame.
struct RetryBackoff {
    double nextAt = 0.0;  // earliest time of the next attempt
    double delay = 0.0;   // current wait; 0 = no failure yet

    bool due(double now) const { return now >= nextAt; }
    void failed(double now, double first = 0.1, double cap = 2.0) {
        delay = delay <= 0.0 ? first : std::min(cap, delay * 2.0);
        nextAt = now + delay;
    }
    void reset() {
        nextAt = 0.0;
        delay = 0.0;
    }
};

}  // namespace uaro
