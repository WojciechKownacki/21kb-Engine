#include "engine/math/EngineMath.hpp"

#include <cmath>
#include <limits>

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

float Distance(Vec3 a, Vec3 b) noexcept {
    return Length(a - b);
}

Vec3 Project(Vec3 value, Vec3 onto) noexcept {
    const float ontoLengthSquared = Dot(onto, onto);
    if (ontoLengthSquared <= 0.000001F) {
        return {};
    }
    return onto * (Dot(value, onto) / ontoLengthSquared);
}

Vec3 Refract(Vec3 incident, Vec3 normal, float eta) noexcept {
    const float cosIncident = Dot(normal, incident);
    const float k = 1.0F - eta * eta * (1.0F - cosIncident * cosIncident);
    if (k < 0.0F) {
        return {};
    }
    return incident * eta - normal * (eta * cosIncident + std::sqrt(k));
}

Radians Angle(Vec3 a, Vec3 b) noexcept {
    const Vec3 normalizedA = Normalize(a);
    const Vec3 normalizedB = Normalize(b);
    const float cosAngle = Clamp(Dot(normalizedA, normalizedB), -1.0F, 1.0F);
    return Radians{ std::acos(cosAngle) };
}

Radians SignedAngle(Vec3 a, Vec3 b, Vec3 axis) noexcept {
    const Radians unsignedAngle = Angle(a, b);
    const float direction = Dot(Cross(a, b), axis);
    return Radians{ direction < 0.0F ? -unsignedAngle.Value() : unsignedAngle.Value() };
}

Quat Normalize(Quat value) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (lengthSquared <= 0.000001F) {
        return Quat{};
    }
    const float invLength = 1.0F / std::sqrt(lengthSquared);
    return Quat{ value.x * invLength, value.y * invLength, value.z * invLength, value.w * invLength };
}

Quat Slerp(Quat a, Quat b, float t) noexcept {
    const float clampedT = Clamp(t, 0.0F, 1.0F);
    float cosOmega = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat adjustedB = b;
    if (cosOmega < 0.0F) {
        adjustedB = Quat{ -b.x, -b.y, -b.z, -b.w };
        cosOmega = -cosOmega;
    }

    float scaleA = 0.0F;
    float scaleB = 0.0F;
    if (cosOmega < 0.9995F) {
        const float omega = std::acos(Clamp(cosOmega, -1.0F, 1.0F));
        const float sinOmega = std::sin(omega);
        scaleA = std::sin((1.0F - clampedT) * omega) / sinOmega;
        scaleB = std::sin(clampedT * omega) / sinOmega;
    } else {
        // a and b are nearly identical: fall back to linear interpolation
        // rather than dividing by a near-zero sinOmega.
        scaleA = 1.0F - clampedT;
        scaleB = clampedT;
    }

    return Normalize(Quat{
        scaleA * a.x + scaleB * adjustedB.x,
        scaleA * a.y + scaleB * adjustedB.y,
        scaleA * a.z + scaleB * adjustedB.z,
        scaleA * a.w + scaleB * adjustedB.w,
    });
}

Quat LookRotation(Vec3 forward, Vec3 up) noexcept {
    const Vec3 forwardAxis = Normalize(forward);
    Vec3 rightAxis = Cross(up, forwardAxis);
    if (Dot(rightAxis, rightAxis) <= 0.000001F) {
        // forward is parallel to up (a degenerate basis) — pick an
        // arbitrary up hint that cannot be parallel to forwardAxis.
        const Vec3 fallbackUp = std::abs(forwardAxis.y) < 0.999F ? Vec3{ 0.0F, 1.0F, 0.0F } : Vec3{ 1.0F, 0.0F, 0.0F };
        rightAxis = Cross(fallbackUp, forwardAxis);
    }
    rightAxis = Normalize(rightAxis);
    const Vec3 upAxis = Cross(forwardAxis, rightAxis);

    // Standard rotation-matrix-to-quaternion conversion (Shepperd's
    // method), applied to the orthonormal basis {rightAxis, upAxis,
    // forwardAxis} built above — the matrix columns are that basis, not a
    // second, independently-verified formula.
    const float m00 = rightAxis.x, m10 = rightAxis.y, m20 = rightAxis.z;
    const float m01 = upAxis.x, m11 = upAxis.y, m21 = upAxis.z;
    const float m02 = forwardAxis.x, m12 = forwardAxis.y, m22 = forwardAxis.z;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        return Normalize(Quat{ (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25F * s });
    }
    if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0F + m00 - m11 - m22) * 2.0F;
        return Normalize(Quat{ 0.25F * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s });
    }
    if (m11 > m22) {
        const float s = std::sqrt(1.0F + m11 - m00 - m22) * 2.0F;
        return Normalize(Quat{ (m01 + m10) / s, 0.25F * s, (m12 + m21) / s, (m02 - m20) / s });
    }
    const float s = std::sqrt(1.0F + m22 - m00 - m11) * 2.0F;
    return Normalize(Quat{ (m02 + m20) / s, (m12 + m21) / s, 0.25F * s, (m10 - m01) / s });
}

Quat FromToRotation(Vec3 from, Vec3 to) noexcept {
    const Vec3 normalizedFrom = Normalize(from);
    const Vec3 normalizedTo = Normalize(to);
    const float cosAngle = Clamp(Dot(normalizedFrom, normalizedTo), -1.0F, 1.0F);

    if (cosAngle > 0.9999F) {
        return Quat{};
    }
    if (cosAngle < -0.9999F) {
        // Exactly opposite: any axis perpendicular to normalizedFrom is a
        // valid 180-degree rotation. Try +X first, fall back to +Y if
        // normalizedFrom happens to already be parallel to +X.
        Vec3 axis = Cross(Vec3{ 1.0F, 0.0F, 0.0F }, normalizedFrom);
        if (Dot(axis, axis) <= 0.000001F) {
            axis = Cross(Vec3{ 0.0F, 1.0F, 0.0F }, normalizedFrom);
        }
        axis = Normalize(axis);
        return Quat{ axis.x, axis.y, axis.z, 0.0F };
    }

    const Vec3 axis = Cross(normalizedFrom, normalizedTo);
    const float s = std::sqrt((1.0F + cosAngle) * 2.0F);
    const float invS = 1.0F / s;
    return Normalize(Quat{ axis.x * invS, axis.y * invS, axis.z * invS, s * 0.5F });
}

Quat RotateTowards(Quat from, Quat to, Radians maxDelta) noexcept {
    const float cosOmega = from.x * to.x + from.y * to.y + from.z * to.z + from.w * to.w;
    // q and -q represent the same rotation, so the shortest-path angle
    // comes from the absolute value of the dot product, not the raw one.
    const float angle = 2.0F * std::acos(Clamp(std::abs(cosOmega), 0.0F, 1.0F));
    if (angle <= maxDelta.Value() || angle <= 0.000001F) {
        return to;
    }
    return Slerp(from, to, maxDelta.Value() / angle);
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

float Floor(float value) noexcept {
    return std::floor(value);
}

float Ceil(float value) noexcept {
    return std::ceil(value);
}

float Round(float value) noexcept {
    return std::round(value);
}

float Frac(float value) noexcept {
    return value - std::floor(value);
}

float Mod(float value, float divisor) noexcept {
    return value - divisor * std::floor(value / divisor);
}

float Sqrt(float value) noexcept {
    return std::sqrt(value);
}

float Pow(float base, float exponent) noexcept {
    return std::pow(base, exponent);
}

float Exp(float value) noexcept {
    return std::exp(value);
}

float Log(float value) noexcept {
    return std::log(value);
}

float Sin(Radians angle) noexcept {
    return std::sin(angle.Value());
}

float Cos(Radians angle) noexcept {
    return std::cos(angle.Value());
}

float Tan(Radians angle) noexcept {
    return std::tan(angle.Value());
}

Radians Asin(float value) noexcept {
    return Radians{ std::asin(value) };
}

Radians Acos(float value) noexcept {
    return Radians{ std::acos(value) };
}

Radians Atan(float value) noexcept {
    return Radians{ std::atan(value) };
}

Radians Atan2(float y, float x) noexcept {
    return Radians{ std::atan2(y, x) };
}

namespace {

[[nodiscard]] constexpr std::uint32_t CornerHash(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) noexcept {
    std::uint32_t h = Hash32(x, seed);
    h = Hash32(y, h);
    h = Hash32(z, h);
    return h;
}

// Ken Perlin's "improved noise" (2002) gradient selection: the low 4 bits
// of the hash pick one of 12 gradient directions (edge midpoints of a
// cube), and the dot product with the distance vector is computed without
// ever materializing the gradient vector itself.
[[nodiscard]] constexpr float Grad(std::uint32_t hash, float x, float y, float z) noexcept {
    const std::uint32_t h = hash & 15U;
    const float u = h < 8U ? x : y;
    const float v = h < 4U ? y : ((h == 12U || h == 14U) ? x : z);
    return ((h & 1U) != 0U ? -u : u) + ((h & 2U) != 0U ? -v : v);
}

[[nodiscard]] constexpr float Fade(float t) noexcept {
    return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
}

} // namespace

float Random01(std::uint32_t seed, std::uint32_t index) noexcept {
    const std::uint32_t h = Hash32(static_cast<std::int32_t>(index), seed);
    return static_cast<float>(h) / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
}

float Noise3D(float x, float y, float z, std::uint32_t seed) noexcept {
    const float floorX = Floor(x);
    const float floorY = Floor(y);
    const float floorZ = Floor(z);
    const std::int32_t xi = static_cast<std::int32_t>(floorX);
    const std::int32_t yi = static_cast<std::int32_t>(floorY);
    const std::int32_t zi = static_cast<std::int32_t>(floorZ);
    const float fx = x - floorX;
    const float fy = y - floorY;
    const float fz = z - floorZ;
    const float u = Fade(fx);
    const float v = Fade(fy);
    const float w = Fade(fz);

    const float x00 = Lerp(
        Grad(CornerHash(xi, yi, zi, seed), fx, fy, fz),
        Grad(CornerHash(xi + 1, yi, zi, seed), fx - 1.0F, fy, fz),
        u);
    const float x10 = Lerp(
        Grad(CornerHash(xi, yi + 1, zi, seed), fx, fy - 1.0F, fz),
        Grad(CornerHash(xi + 1, yi + 1, zi, seed), fx - 1.0F, fy - 1.0F, fz),
        u);
    const float x01 = Lerp(
        Grad(CornerHash(xi, yi, zi + 1, seed), fx, fy, fz - 1.0F),
        Grad(CornerHash(xi + 1, yi, zi + 1, seed), fx - 1.0F, fy, fz - 1.0F),
        u);
    const float x11 = Lerp(
        Grad(CornerHash(xi, yi + 1, zi + 1, seed), fx, fy - 1.0F, fz - 1.0F),
        Grad(CornerHash(xi + 1, yi + 1, zi + 1, seed), fx - 1.0F, fy - 1.0F, fz - 1.0F),
        u);

    const float y0 = Lerp(x00, x10, v);
    const float y1 = Lerp(x01, x11, v);
    return Lerp(y0, y1, w);
}

float Noise2D(float x, float y, std::uint32_t seed) noexcept {
    return Noise3D(x, y, 0.0F, seed);
}

float Noise1D(float x, std::uint32_t seed) noexcept {
    return Noise3D(x, 0.0F, 0.0F, seed);
}

RandomStreamFloatResult NextFloat01(RandomStream stream) noexcept {
    const RandomStreamUInt32Result raw = NextUInt32(stream);
    return RandomStreamFloatResult{ static_cast<float>(raw.value) / static_cast<float>(std::numeric_limits<std::uint32_t>::max()), raw.stream };
}

RandomStreamRangeResult NextRange(RandomStream stream, float min, float max) noexcept {
    const RandomStreamFloatResult unit = NextFloat01(stream);
    return RandomStreamRangeResult{ Lerp(min, max, unit.value), unit.stream };
}

RandomStreamIntRangeResult NextIntRange(RandomStream stream, std::int32_t min, std::int32_t max) noexcept {
    if (max <= min) {
        return RandomStreamIntRangeResult{ min, NextUInt32(stream).stream };
    }
    const RandomStreamUInt32Result raw = NextUInt32(stream);
    const std::uint32_t span = static_cast<std::uint32_t>(max - min);
    return RandomStreamIntRangeResult{ min + static_cast<std::int32_t>(raw.value % span), raw.stream };
}

} // namespace kb::math
