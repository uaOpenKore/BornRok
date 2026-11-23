#include "core/SysMem.hpp"

#include <fstream>
#include <sstream>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/resource.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if !defined(WINAPI_FAMILY) || WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2  // K32GetProcessMemoryInfo from kernel32: no psapi.lib to link
#endif
#include <psapi.h>
#define UARO_WIN_DESKTOP 1
#endif
#endif

namespace uaro {

namespace {
std::string slurp(const char* path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace

MemInfo parseMeminfo(const std::string& text) {
    MemInfo m;
    i64 swapTotal = -1, swapFree = -1;
    std::istringstream in(text);
    std::string key;
    i64 value = 0;
    std::string unit;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        if (!(ls >> key >> value)) continue;
        if (key == "MemAvailable:") m.availKb = value;
        else if (key == "SwapTotal:") swapTotal = value;
        else if (key == "SwapFree:") swapFree = value;
    }
    if (swapTotal >= 0 && swapFree >= 0) m.swapUsedKb = swapTotal - swapFree;
    return m;
}

i64 parseStatmRssPages(const std::string& text) {
    std::istringstream in(text);
    i64 size = 0, resident = 0;
    if (!(in >> size >> resident)) return -1;
    return resident;
}

MemInfo readMemInfo() {
#if defined(__linux__)
    return parseMeminfo(slurp("/proc/meminfo"));
#else
    return {};
#endif
}

i64 readProcessRssBytes() {
#if defined(__linux__)
    const i64 pages = parseStatmRssPages(slurp("/proc/self/statm"));
    const long page = sysconf(_SC_PAGESIZE);
    return (pages >= 0 && page > 0) ? pages * static_cast<i64>(page) : -1;
#elif defined(UARO_WIN_DESKTOP)
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<i64>(pmc.WorkingSetSize);
    return -1;
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<i64>(info.resident_size);
    return -1;
#else
    return -1;
#endif
}

double readProcessCpuSeconds() {
#if defined(__linux__) || defined(__APPLE__)
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0) return -1.0;
    return static_cast<double>(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) +
           static_cast<double>(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
#elif defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return -1.0;
    auto t = [](const FILETIME& f) {
        return static_cast<double>((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 1e7;
    };
    return t(k) + t(u);
#else
    return -1.0;
#endif
}

}  // namespace uaro
