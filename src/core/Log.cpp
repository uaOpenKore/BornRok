#include "core/Log.hpp"

#include <cstdio>
#include <string>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace uaro::log {

namespace {
// Info by default so the tester log shows real Info/Warn/Error, not the Debug/Trace spam (missing
// content resources, per-texture diagnostics) that buried actual problems (S.: "много варнингов на
// bmp/act/spr мешает видеть реальные ошибки"). Devs can drop to Debug/Trace via set_level().
Level g_level = Level::Info;
std::FILE* g_file = nullptr;   // optional file sink (output_log.txt); the only sink in a no-console build

const char* tag(Level l) {
    switch (l) {
        case Level::Trace: return "TRC";
        case Level::Debug: return "DBG";
        case Level::Info:  return "INF";
        case Level::Warn:  return "WRN";
        case Level::Error: return "ERR";
        case Level::Off:   return "OFF";
    }
    return "???";
}
} // namespace

void set_level(Level level) { g_level = level; }
Level level() { return g_level; }

void set_log_file(std::string_view path) {
    if (g_file) { std::fclose(g_file); g_file = nullptr; }
    if (path.empty()) return;
    std::string p(path);
    g_file = std::fopen(p.c_str(), "w");  // "w" truncates -> the old log is cleared on each launch (S.)
}

void write(Level level, std::string_view message) {
    if (level < g_level) return;
    std::fprintf(stderr, "[%s] %.*s\n", tag(level),
                 static_cast<int>(message.size()), message.data());
    if (g_file) {
        std::fprintf(g_file, "[%s] %.*s\n", tag(level),
                     static_cast<int>(message.size()), message.data());
        std::fflush(g_file);  // flush each line so a crash still leaves the log on disk
    }
#if defined(__ANDROID__)
    // Also emit to Android logcat (tag "uaRO") -- stderr isn't reliably visible there, and this is
    // the only way to see why the client behaves as it does on device. (S.)
    int prio = ANDROID_LOG_INFO;
    switch (level) {
        case Level::Trace: case Level::Debug: prio = ANDROID_LOG_DEBUG; break;
        case Level::Info:  prio = ANDROID_LOG_INFO;  break;
        case Level::Warn:  prio = ANDROID_LOG_WARN;  break;
        case Level::Error: prio = ANDROID_LOG_ERROR; break;
        default: break;
    }
    __android_log_print(prio, "uaRO", "%.*s", static_cast<int>(message.size()), message.data());
#endif
}

} // namespace uaro::log
