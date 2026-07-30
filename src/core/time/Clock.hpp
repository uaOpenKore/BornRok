#pragma once
#include "core/Types.hpp"

namespace uaro {

// Monotonic high-resolution stopwatch used for frame timing.
class Clock {
public:
    Clock();                  // starts counting from construction

    f64 seconds() const;      // elapsed seconds since start/restart
    f64 restart();            // returns elapsed seconds, then resets to now

    static f64 now_seconds(); // monotonic timestamp (arbitrary epoch)

private:
    u64 start_ns_;
};

} // namespace uaro
