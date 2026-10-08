#pragma once

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace kb::render::tests {

[[nodiscard]] inline bool NearlyEqual(float lhs, float rhs) noexcept {
    return std::fabs(lhs - rhs) <= 0.0001F;
}

inline void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

// The most memory this process has had committed at once, in bytes; 0 where the
// platform does not say. A step that must not allocate for an untrusted size can
// be held to a bound by comparing this before and after it.
[[nodiscard]] std::size_t PeakCommittedBytes() noexcept;

} // namespace kb::render::tests
