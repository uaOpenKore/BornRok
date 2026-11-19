#include "core/SysMem.hpp"

#include <fstream>
#include <sstream>

#if defined(__linux__)
#include <unistd.h>
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
#else
    return -1;
#endif
}

}  // namespace uaro
