#pragma once

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace kb::editor::tests {

[[nodiscard]] inline bool NearlyEqual(double lhs, double rhs) noexcept {
    return std::fabs(lhs - rhs) <= 0.0001;
}

inline void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

} // namespace kb::editor::tests
