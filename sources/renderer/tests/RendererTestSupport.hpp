#pragma once

#include <cmath>
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

} // namespace kb::render::tests
