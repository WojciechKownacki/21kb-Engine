#pragma once

#include <cmath>
#include <cstdlib>
#include <iostream>

#if defined(__clang__)
#define KB_TEST_SUPPRESS_DEPRECATED_PUSH                                                                                         \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"")
#define KB_TEST_SUPPRESS_DEPRECATED_POP _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
#define KB_TEST_SUPPRESS_DEPRECATED_PUSH                                                                                         \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define KB_TEST_SUPPRESS_DEPRECATED_POP _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#define KB_TEST_SUPPRESS_DEPRECATED_PUSH __pragma(warning(push)) __pragma(warning(disable : 4996))
#define KB_TEST_SUPPRESS_DEPRECATED_POP __pragma(warning(pop))
#else
#define KB_TEST_SUPPRESS_DEPRECATED_PUSH
#define KB_TEST_SUPPRESS_DEPRECATED_POP
#endif

namespace kb::tests {

[[nodiscard]] inline bool NearlyEqual(float lhs, float rhs) noexcept {
    return std::fabs(lhs - rhs) <= 0.0001F;
}

inline void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace kb::tests
