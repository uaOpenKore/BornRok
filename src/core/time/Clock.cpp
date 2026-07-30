#include "core/time/Clock.hpp"

#include <chrono>

namespace uaro {

namespace {
u64 mono_ns() {
    using namespace std::chrono;
    return static_cast<u64>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}
} // namespace

Clock::Clock() : start_ns_(mono_ns()) {}

f64 Clock::seconds() const {
    return static_cast<f64>(mono_ns() - start_ns_) / 1e9;
}

f64 Clock::restart() {
    u64 now = mono_ns();
    f64 elapsed = static_cast<f64>(now - start_ns_) / 1e9;
    start_ns_ = now;
    return elapsed;
}

f64 Clock::now_seconds() {
    return static_cast<f64>(mono_ns()) / 1e9;
}

} // namespace uaro
