#include "engine/math/EngineMath.hpp"

#include <cmath>

namespace kb::math {

float Length(Vec3 value) noexcept {
    return std::sqrt(Dot(value, value));
}

Vec3 Normalize(Vec3 value) noexcept {
    const float length = Length(value);
    if (length <= 0.000001F) {
        return {};
    }
    return value * (1.0F / length);
}

} // namespace kb::math
