#pragma once
// microtest — a ~40-line, zero-dependency test harness.
// Lets core/ be verified with nothing but a C++20 compiler. Replaceable with
// doctest/Catch2 later without touching the test bodies (same CHECK* spelling).
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace microtest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}
inline int& checks() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline void report(const char* file, int line, const char* expr) {
    ++failures();
    std::printf("    FAIL %s:%d  %s\n", file, line, expr);
}

inline bool approx(double a, double b, double eps) {
    double d = a - b;
    return (d < 0 ? -d : d) <= eps;
}

inline int run_all() {
    int failed_tests = 0;
    for (auto& tc : registry()) {
        int before = failures();
        tc.fn();
        bool ok = failures() == before;
        if (!ok) ++failed_tests;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", tc.name);
    }
    std::printf("\n%zu test(s), %d failed, %d checks performed\n",
                registry().size(), failed_tests, checks());
    return failed_tests == 0 ? 0 : 1;
}

} // namespace microtest

#define TEST_CASE(name)                                                        \
    static void name();                                                        \
    static ::microtest::Registrar microtest_reg_##name(#name, name);           \
    static void name()

#define CHECK(expr)                                                            \
    do {                                                                       \
        ++::microtest::checks();                                               \
        if (!(expr)) ::microtest::report(__FILE__, __LINE__, "CHECK(" #expr ")");\
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NEAR(a, b, eps)                                                  \
    do {                                                                       \
        ++::microtest::checks();                                               \
        if (!::microtest::approx((a), (b), (eps)))                             \
            ::microtest::report(__FILE__, __LINE__, "CHECK_NEAR(" #a ", " #b ")");\
    } while (0)
