#include "core/thread/ThreadRole.hpp"

#if defined(_WIN32)
  #include <windows.h>
#elif defined(__APPLE__)
  #include <pthread.h>
  #include <pthread/qos.h>
#elif defined(__linux__)
  #include <pthread.h>
  #include <sched.h>
  #include <unistd.h>

  #include <fstream>
  #include <vector>
#endif

namespace uaro {

#if defined(__linux__)
namespace {

// Classify CPUs by the scheduler's cpu_capacity (ARM big.LITTLE exposes it; plain
// x86 usually does not). `wantMax` true -> the highest-capacity ("performance")
// CPUs, false -> the rest ("efficiency"). Returns empty when the topology is
// uniform or capacity is unknown, in which case we leave scheduling to the kernel.
std::vector<int> coresByCapacity(bool wantMax) {
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n <= 1) return {};
    std::vector<long> cap(static_cast<size_t>(n), -1);
    long maxCap = -1, minCap = -1;
    for (long i = 0; i < n; ++i) {
        std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpu_capacity");
        long c = -1;
        if (f >> c) {
            cap[static_cast<size_t>(i)] = c;
            if (maxCap < 0 || c > maxCap) maxCap = c;
            if (minCap < 0 || c < minCap) minCap = c;
        }
    }
    if (maxCap < 0 || maxCap == minCap) return {};  // unknown or uniform topology
    const long want = wantMax ? maxCap : minCap;
    std::vector<int> out;
    for (long i = 0; i < n; ++i)
        if (cap[static_cast<size_t>(i)] == want) out.push_back(static_cast<int>(i));
    return out;
}

void pinTo(const std::vector<int>& cpus) {
    if (cpus.empty()) return;  // uniform topology: don't fight the scheduler
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c : cpus) CPU_SET(c, &set);
#if defined(__ANDROID__)
    // Bionic has no pthread_setaffinity_np; sched_setaffinity(0, ...) pins the calling thread.
    sched_setaffinity(0, sizeof(set), &set);
#else
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif
}

} // namespace
#endif

void configureCurrentThread(ThreadRole role, const std::string& name) {
#if defined(_WIN32)
    const std::wstring wname(name.begin(), name.end());
    SetThreadDescription(GetCurrentThread(), wname.c_str());
    if (role != ThreadRole::Render)  // bias background roles toward the E-cores
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__APPLE__)
    pthread_setname_np(name.c_str());
    const qos_class_t qos =
        (role == ThreadRole::Render) ? QOS_CLASS_USER_INTERACTIVE : QOS_CLASS_UTILITY;
    pthread_set_qos_class_self_np(qos, 0);
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());  // 16 bytes incl. NUL
    pinTo(coresByCapacity(/*wantMax=*/role == ThreadRole::Render));
#else
    (void)role;  // PS5 / Switch: fill in with the platform SDK's affinity API
    (void)name;
#endif
}

} // namespace uaro
