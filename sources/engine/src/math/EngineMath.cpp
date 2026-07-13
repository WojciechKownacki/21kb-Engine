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

DampResult Damp(float current, float target, float velocity, float smoothTime, float deltaTime, float maxSpeed) noexcept {
    // Guard against smoothTime <= 0 the same way Unity's Mathf.SmoothDamp
    // does: rather than dividing by zero, clamp to a tiny positive value so
    // the result still converges (extremely fast) instead of producing NaN.
    const float safeSmoothTime = smoothTime > 0.0001F ? smoothTime : 0.0001F;
    const float omega = 2.0F / safeSmoothTime;
    const float x = omega * deltaTime;
    const float exponent = 1.0F / (1.0F + x + 0.48F * x * x + 0.235F * x * x * x);

    float change = current - target;
    const float originalTarget = target;
    const float maxChange = maxSpeed * safeSmoothTime;
    change = Clamp(change, -maxChange, maxChange);
    target = current - change;

    const float temp = (velocity + omega * change) * deltaTime;
    float newVelocity = (velocity - omega * temp) * exponent;
    float output = target + (change + temp) * exponent;

    // Prevent overshooting the target: if we started below the target and
    // the unclamped result would land above it (or vice versa), snap to
    // the target instead.
    if ((originalTarget - current > 0.0F) == (output > originalTarget)) {
        output = originalTarget;
        newVelocity = (output - originalTarget) / (deltaTime > 0.0F ? deltaTime : 1.0F);
    }

    return DampResult{ .value = output, .velocity = newVelocity };
}

} // namespace kb::math
