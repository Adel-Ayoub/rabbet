#pragma once

#include <cstdio>

namespace rbtest {

inline int g_checks = 0;
inline int g_failures = 0;

inline void record(bool ok, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  [FAIL] %s:%d  %s\n", file, line, expr);
    }
}

inline int summary(const char* suite) {
    if (g_failures == 0) {
        std::fprintf(stderr, "[ ok ] %s: %d checks passed\n", suite, g_checks);
        return 0;
    }
    std::fprintf(stderr, "[FAIL] %s: %d of %d checks failed\n", suite, g_failures, g_checks);
    return 1;
}

} // namespace rbtest

#define CHECK(cond) ::rbtest::record(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
