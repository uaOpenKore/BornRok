#pragma once
#include <string>

namespace uaro {

// What a thread is for. Drives core affinity / scheduling QoS so the planned
// split lands on the right kind of core: the render thread wants a performance
// core, the rest are fine on efficiency cores (hybrid CPUs / big.LITTLE).
enum class ThreadRole {
    Render,   // performance core: bgfx submit + present
    Logic,    // efficiency core: input + network receive + game simulation
    Audio,    // efficiency core: audio output (placeholder until an audio system exists)
    NetSend,  // efficiency core: drain the outgoing packet queue to the socket
};

// Name the current thread (for debuggers/profilers) and bias its scheduling toward
// the kind of core its role wants. Best-effort and platform-specific:
//   Linux/Android : pin to performance/efficiency CPUs by cpu_capacity (no-op if
//                   the topology is uniform, e.g. plain desktop x86)
//   Windows       : thread description + a below-normal priority for background
//                   roles (the hybrid scheduler then prefers E-cores for them)
//   macOS/iOS     : QoS class (USER_INTERACTIVE vs UTILITY); the OS picks P/E
//   PS5/Switch    : no-op until each console SDK backend is filled in
void configureCurrentThread(ThreadRole role, const std::string& name);

} // namespace uaro
