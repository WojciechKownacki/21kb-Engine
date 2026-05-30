#include "scene/transform/QuatMath.hpp"

#include <cmath>

namespace kb::scene {
namespace {

constexpr float kEpsilon = 0.000001F;

} // namespace

Quat QuatMath::Multiply(Quat lhs, Quat rhs) noexcept {
    return Quat{
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

Quat QuatMath::Normalize(Quat value) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (lengthSquared <= kEpsilon) {
        return Quat{};
    }

    const float invLength = 1.0F / std::sqrt(lengthSquared);
    return Quat{ value.x * invLength, value.y * invLength, value.z * invLength, value.w * invLength };
}

} // namespace kb::scene
