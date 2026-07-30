#pragma once
#include <string_view>
#include <utility>

#if defined(__ANDROID__)
// The Android NDK's libc++ (r26 / LLVM 17) gates the ENTIRE <format> runtime -- std::format,
// std::vformat AND std::format_string -- behind C++23, so none of it is available in this C++20 build.
// Rather than force the whole project to C++23 (or add {fmt}), format log lines with a tiny
// self-contained substituter: it replaces each "{...}" placeholder with the next argument's default
// string form, IGNORING the format spec (so "{:.2f}" / "{:04x}" print the plain value). Log readability
// on Android is slightly reduced (no precision / hex padding) but it always compiles and works.
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
#else
#include <format>
#endif

// Minimal, dependency-free logging built on std::format (C++20).
// Usage:  uaro::log::info("loaded {} files in {:.2f}s", n, secs);
namespace uaro::log {

enum class Level { Trace = 0, Debug, Info, Warn, Error, Off };

// Set the minimum level that gets written. Messages below it are dropped.
void set_level(Level level);
Level level();

// Mirror every log line to a file, TRUNCATING it first (S.: output_log.txt in the game folder, old
// log cleared on each launch). Pass "" to stop file logging. Since the release build has no console
// (WIN32 subsystem), this file is the only place the log survives. Call once at startup.
void set_log_file(std::string_view path);

// Raw sink — formats are expanded by the templated helpers below.
void write(Level level, std::string_view message);

#if defined(__ANDROID__)
namespace detail {
    template <class T>
    std::string arg_to_string(T&& v) {
        using U = std::decay_t<T>;
        if constexpr (std::is_same_v<U, bool>) return v ? "true" : "false";
        else if constexpr (std::is_same_v<U, char>) return std::string(1, v);
        else if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, std::string_view>)
            return std::string(v);
        else if constexpr (std::is_same_v<U, const char*> || std::is_same_v<U, char*>)
            return v ? std::string(v) : std::string();
        else if constexpr (std::is_arithmetic_v<U>) return std::to_string(v);
        else if constexpr (std::is_enum_v<U>)
            return std::to_string(static_cast<long long>(static_cast<std::underlying_type_t<U>>(v)));
        else { std::ostringstream os; os << v; return os.str(); }
    }

    template <class... Args>
    std::string vformat_fallback(std::string_view fmt, Args&&... args) {
        const std::vector<std::string> a{arg_to_string(std::forward<Args>(args))...};
        std::string out;
        out.reserve(fmt.size() + 16);
        std::size_t ai = 0;
        for (std::size_t i = 0; i < fmt.size(); ++i) {
            const char c = fmt[i];
            if (c == '{') {
                if (i + 1 < fmt.size() && fmt[i + 1] == '{') { out += '{'; ++i; continue; }  // {{ -> {
                const std::size_t j = fmt.find('}', i);
                if (j == std::string_view::npos) { out.append(fmt.substr(i)); break; }
                if (ai < a.size()) out += a[ai++];  // drop the spec, insert the plain value
                i = j;
            } else if (c == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
                out += '}';  // }} -> }
                ++i;
            } else {
                out += c;
            }
        }
        return out;
    }
}  // namespace detail

template <class... Args>
void emit(Level lvl, std::string_view fmt, Args&&... args) {
    if (lvl < level()) return;
    write(lvl, detail::vformat_fallback(fmt, std::forward<Args>(args)...));
}

template <class... Args> void trace(std::string_view f, Args&&... a) { emit(Level::Trace, f, std::forward<Args>(a)...); }
template <class... Args> void debug(std::string_view f, Args&&... a) { emit(Level::Debug, f, std::forward<Args>(a)...); }
template <class... Args> void info (std::string_view f, Args&&... a) { emit(Level::Info,  f, std::forward<Args>(a)...); }
template <class... Args> void warn (std::string_view f, Args&&... a) { emit(Level::Warn,  f, std::forward<Args>(a)...); }
template <class... Args> void error(std::string_view f, Args&&... a) { emit(Level::Error, f, std::forward<Args>(a)...); }

#else  // desktop / non-Android: full compile-time-checked std::format

template <class... Args>
void emit(Level lvl, std::format_string<Args...> fmt, Args&&... args) {
    if (lvl < level()) return;  // cheap early-out before formatting
    write(lvl, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args> void trace(std::format_string<Args...> f, Args&&... a) { emit(Level::Trace, f, std::forward<Args>(a)...); }
template <class... Args> void debug(std::format_string<Args...> f, Args&&... a) { emit(Level::Debug, f, std::forward<Args>(a)...); }
template <class... Args> void info (std::format_string<Args...> f, Args&&... a) { emit(Level::Info,  f, std::forward<Args>(a)...); }
template <class... Args> void warn (std::format_string<Args...> f, Args&&... a) { emit(Level::Warn,  f, std::forward<Args>(a)...); }
template <class... Args> void error(std::format_string<Args...> f, Args&&... a) { emit(Level::Error, f, std::forward<Args>(a)...); }

#endif

} // namespace uaro::log
