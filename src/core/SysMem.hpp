#pragma once
#include <string>

#include "core/Types.hpp"

namespace uaro {

// System / process memory for the periodic perf log line. On a 4 GB device shared by the OS, the GPU
// and a co-hosted game server (Switch under L4T), "is it swapping" is the first question; these answer
// it from /proc. Every value is -1 when unknown (non-Linux, unreadable, missing field).
struct MemInfo {
    i64 availKb = -1;     // MemAvailable
    i64 swapUsedKb = -1;  // SwapTotal - SwapFree
};

// Parsers for the /proc text formats (kept separate from the readers so they can be unit-tested).
MemInfo parseMeminfo(const std::string& meminfoText);
i64 parseStatmRssPages(const std::string& statmText);  // 2nd field of /proc/self/statm

MemInfo readMemInfo();       // /proc/meminfo
i64 readProcessRssBytes();   // /proc/self/statm resident pages * page size

}  // namespace uaro
