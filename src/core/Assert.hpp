#pragma once
#include <cstdlib>

#include "core/Log.hpp"

// Lightweight assertion that routes through our logger before aborting.
// Active in all builds for now (foundation phase); can be made debug-only later.
#define UARO_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::uaro::log::error("Assertion failed: {}  at {}:{}", #cond,        \
                               __FILE__, __LINE__);                            \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

#define UARO_ASSERT_MSG(cond, ...)                                             \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::uaro::log::error("Assertion failed: {}  at {}:{}", #cond,        \
                               __FILE__, __LINE__);                            \
            ::uaro::log::error(__VA_ARGS__);                                   \
            std::abort();                                                      \
        }                                                                      \
    } while (0)
