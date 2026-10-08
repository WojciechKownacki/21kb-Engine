#pragma once

#include <cmath>
#include <cstddef>
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

#if defined(__SANITIZE_ADDRESS__)
#define KB_TEST_ADDRESS_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define KB_TEST_ADDRESS_SANITIZER 1
#endif
#endif

namespace kb::tests {

// Absolute time budgets are set for optimised builds. An address-sanitised build checks every memory access
// and runs several times slower, so such a budget is scaled by this.
#if defined(KB_TEST_ADDRESS_SANITIZER)
inline constexpr double kSanitizerTimeScale = 4.0;
#else
inline constexpr double kSanitizerTimeScale = 1.0;
#endif

[[nodiscard]] inline bool NearlyEqual(float lhs, float rhs) noexcept {
    return std::fabs(lhs - rhs) <= 0.0001F;
}

inline void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

// Heap allocations made on any thread between the two calls (operator new is replaced in SceneRuntimeTests.cpp).
struct AllocationTally {
    std::size_t count = 0U;
    std::size_t bytes = 0U;
    std::size_t largeCount = 0U; // 16 KB and more
};
void BeginAllocationTally() noexcept;
[[nodiscard]] AllocationTally EndAllocationTally() noexcept;

} // namespace kb::tests
