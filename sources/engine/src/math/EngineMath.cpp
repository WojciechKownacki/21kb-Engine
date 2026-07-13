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

Quat Normalize(Quat value) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (lengthSquared <= 0.000001F) {
        return Quat{};
    }
    const float invLength = 1.0F / std::sqrt(lengthSquared);
    return Quat{ value.x * invLength, value.y * invLength, value.z * invLength, value.w * invLength };
}

Mat4 FromTRS(Vec3 translation, Quat rotation, Vec3 scale) noexcept {
    const Vec3 xAxis = Rotate(rotation, Vec3{ scale.x, 0.0F, 0.0F });
    const Vec3 yAxis = Rotate(rotation, Vec3{ 0.0F, scale.y, 0.0F });
    const Vec3 zAxis = Rotate(rotation, Vec3{ 0.0F, 0.0F, scale.z });
    return Mat4{ {
        Vec4{ xAxis.x, xAxis.y, xAxis.z, 0.0F },
        Vec4{ yAxis.x, yAxis.y, yAxis.z, 0.0F },
        Vec4{ zAxis.x, zAxis.y, zAxis.z, 0.0F },
        Vec4{ translation.x, translation.y, translation.z, 1.0F },
    } };
}

} // namespace kb::math
