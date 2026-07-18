#include "engine/math/EngineMath.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace kb::math {

float Length(Vec3 value) noexcept {
    return std::sqrt(Dot(value, value));
}

// LIB-042: Vec2 magnitude, same definition as Length(Vec3) one dimension
// down — a finite result for every input, 0 for the zero vector.
float Length(Vec2 value) noexcept {
    return std::sqrt(Dot(value, value));
}

// LIB-042: ray-vs-plane. denom = Dot(rayDir, planeNormal) is the rate the
// ray approaches the plane; when it is ~0 the ray is parallel and never
// meets the plane (hit = false). Otherwise t = -SignedDistance(origin) /
// denom is the parameter of the intersection; a negative t means the plane
// is behind the ray origin, which we report as a miss (hit = false) rather
// than returning a point "behind" the ray — the same defined-degenerate
// -result rule the header documents.
RayPlaneIntersection Intersect(const Ray& ray, const Plane& plane) noexcept {
    const float denom = Dot(ray.direction, plane.normal);
    if (denom <= 0.000001F && denom >= -0.000001F) {
        return {};
    }
    const float t = -SignedDistance(plane, ray.origin) / denom;
    if (t < 0.0F) {
        return {};
    }
    return RayPlaneIntersection{ .hit = true, .t = t, .point = PointAt(ray, t) };
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

Quat Inverse(Quat value) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (lengthSquared <= 0.000001F) {
        return Quat{};
    }
    const Quat conjugate = Conjugate(value);
    const float invLengthSquared = 1.0F / lengthSquared;
    return Quat{ conjugate.x * invLengthSquared, conjugate.y * invLengthSquared, conjugate.z * invLengthSquared, conjugate.w * invLengthSquared };
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

Mat3 InverseTranspose(const Mat3& m) noexcept {
    const Vec3 col0 = Cross(m.columns[1], m.columns[2]);
    const Vec3 col1 = Cross(m.columns[2], m.columns[0]);
    const Vec3 col2 = Cross(m.columns[0], m.columns[1]);
    const float det = Dot(m.columns[0], col0);
    if (det <= 0.000001F && det >= -0.000001F) {
        return Mat3{};
    }
    const float inv = 1.0F / det;
    return Mat3{ { col0 * inv, col1 * inv, col2 * inv } };
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
    if (divisor == 0.0F) {
        return value;
    }
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

// LIB-054: a lattice coordinate is `static_cast<int32_t>(Floor(coord))`.
// Casting a float that is NaN, +/-infinity, or simply larger in magnitude
// than INT32_MAX to int32_t is undefined behaviour. Non-finite inputs are
// screened out before this is ever called (Noise3D returns 0 for them);
// this additionally clamps a finite but out-of-int32-range floored value
// to the representable range (leaving headroom for the `+ 1` neighbour
// lattice index) so the cast is always defined. A coordinate past
// +/-2.1e9 is astronomically far from any real sampling; saturating there
// is a defined, benign result rather than UB.
[[nodiscard]] std::int32_t FloorToLatticeInt(float flooredCoord) noexcept {
    constexpr float kMaxLattice = 2147483646.0F; // INT32_MAX - 1, exactly representable as float
    constexpr float kMinLattice = -2147483648.0F; // INT32_MIN, exactly representable as float
    const float clamped = flooredCoord > kMaxLattice ? kMaxLattice : (flooredCoord < kMinLattice ? kMinLattice : flooredCoord);
    return static_cast<std::int32_t>(clamped);
}

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
    // LIB-054: gradient noise has no defined meaning at a NaN or infinite
    // coordinate, and flooring-then-casting one to int32 (below) would be
    // UB. Return the neutral 0.0F (the same value the noise takes at every
    // integer lattice point) instead of computing on undefined data.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return 0.0F;
    }
    const float floorX = Floor(x);
    const float floorY = Floor(y);
    const float floorZ = Floor(z);
    const std::int32_t xi = FloorToLatticeInt(floorX);
    const std::int32_t yi = FloorToLatticeInt(floorY);
    const std::int32_t zi = FloorToLatticeInt(floorZ);
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

const char* ToString(Easing easing) noexcept {
    switch (easing) {
    case Easing::Linear:
        return "Linear";
    case Easing::InSine:
        return "InSine";
    case Easing::OutSine:
        return "OutSine";
    case Easing::InOutSine:
        return "InOutSine";
    case Easing::InQuad:
        return "InQuad";
    case Easing::OutQuad:
        return "OutQuad";
    case Easing::InOutQuad:
        return "InOutQuad";
    case Easing::InCubic:
        return "InCubic";
    case Easing::OutCubic:
        return "OutCubic";
    case Easing::InOutCubic:
        return "InOutCubic";
    case Easing::InQuart:
        return "InQuart";
    case Easing::OutQuart:
        return "OutQuart";
    case Easing::InOutQuart:
        return "InOutQuart";
    case Easing::InQuint:
        return "InQuint";
    case Easing::OutQuint:
        return "OutQuint";
    case Easing::InOutQuint:
        return "InOutQuint";
    case Easing::InExpo:
        return "InExpo";
    case Easing::OutExpo:
        return "OutExpo";
    case Easing::InOutExpo:
        return "InOutExpo";
    case Easing::InCirc:
        return "InCirc";
    case Easing::OutCirc:
        return "OutCirc";
    case Easing::InOutCirc:
        return "InOutCirc";
    case Easing::InBack:
        return "InBack";
    case Easing::OutBack:
        return "OutBack";
    case Easing::InOutBack:
        return "InOutBack";
    case Easing::InElastic:
        return "InElastic";
    case Easing::OutElastic:
        return "OutElastic";
    case Easing::InOutElastic:
        return "InOutElastic";
    case Easing::InBounce:
        return "InBounce";
    case Easing::OutBounce:
        return "OutBounce";
    case Easing::InOutBounce:
        return "InOutBounce";
    }
    return "Linear";
}

namespace {

// The "Robert Penner" easing formulas, widely reproduced under that name
// (https://easings.net is the commonly cited reference implementation).

[[nodiscard]] float OutBounceImpl(float t) noexcept {
    constexpr float n1 = 7.5625F;
    constexpr float d1 = 2.75F;
    if (t < 1.0F / d1) {
        return n1 * t * t;
    }
    if (t < 2.0F / d1) {
        t -= 1.5F / d1;
        return n1 * t * t + 0.75F;
    }
    if (t < 2.5F / d1) {
        t -= 2.25F / d1;
        return n1 * t * t + 0.9375F;
    }
    t -= 2.625F / d1;
    return n1 * t * t + 0.984375F;
}

[[nodiscard]] float InBounceImpl(float t) noexcept {
    return 1.0F - OutBounceImpl(1.0F - t);
}

} // namespace

float Evaluate(Easing easing, float t) noexcept {
    t = Clamp(t, 0.0F, 1.0F);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::InSine:
        return 1.0F - std::cos(t * kPi / 2.0F);
    case Easing::OutSine:
        return std::sin(t * kPi / 2.0F);
    case Easing::InOutSine:
        return -(std::cos(kPi * t) - 1.0F) / 2.0F;
    case Easing::InQuad:
        return t * t;
    case Easing::OutQuad:
        return 1.0F - (1.0F - t) * (1.0F - t);
    case Easing::InOutQuad:
        return t < 0.5F ? 2.0F * t * t : 1.0F - std::pow(-2.0F * t + 2.0F, 2.0F) / 2.0F;
    case Easing::InCubic:
        return t * t * t;
    case Easing::OutCubic:
        return 1.0F - std::pow(1.0F - t, 3.0F);
    case Easing::InOutCubic:
        return t < 0.5F ? 4.0F * t * t * t : 1.0F - std::pow(-2.0F * t + 2.0F, 3.0F) / 2.0F;
    case Easing::InQuart:
        return t * t * t * t;
    case Easing::OutQuart:
        return 1.0F - std::pow(1.0F - t, 4.0F);
    case Easing::InOutQuart:
        return t < 0.5F ? 8.0F * t * t * t * t : 1.0F - std::pow(-2.0F * t + 2.0F, 4.0F) / 2.0F;
    case Easing::InQuint:
        return t * t * t * t * t;
    case Easing::OutQuint:
        return 1.0F - std::pow(1.0F - t, 5.0F);
    case Easing::InOutQuint:
        return t < 0.5F ? 16.0F * t * t * t * t * t : 1.0F - std::pow(-2.0F * t + 2.0F, 5.0F) / 2.0F;
    case Easing::InExpo:
        return t <= 0.0F ? 0.0F : std::pow(2.0F, 10.0F * t - 10.0F);
    case Easing::OutExpo:
        return t >= 1.0F ? 1.0F : 1.0F - std::pow(2.0F, -10.0F * t);
    case Easing::InOutExpo:
        if (t <= 0.0F) {
            return 0.0F;
        }
        if (t >= 1.0F) {
            return 1.0F;
        }
        return t < 0.5F ? std::pow(2.0F, 20.0F * t - 10.0F) / 2.0F : (2.0F - std::pow(2.0F, -20.0F * t + 10.0F)) / 2.0F;
    case Easing::InCirc:
        return 1.0F - std::sqrt(1.0F - t * t);
    case Easing::OutCirc:
        return std::sqrt(1.0F - (t - 1.0F) * (t - 1.0F));
    case Easing::InOutCirc:
        return t < 0.5F
            ? (1.0F - std::sqrt(1.0F - std::pow(2.0F * t, 2.0F))) / 2.0F
            : (std::sqrt(1.0F - std::pow(-2.0F * t + 2.0F, 2.0F)) + 1.0F) / 2.0F;
    case Easing::InBack: {
        constexpr float c1 = 1.70158F;
        constexpr float c3 = c1 + 1.0F;
        return c3 * t * t * t - c1 * t * t;
    }
    case Easing::OutBack: {
        constexpr float c1 = 1.70158F;
        constexpr float c3 = c1 + 1.0F;
        return 1.0F + c3 * std::pow(t - 1.0F, 3.0F) + c1 * std::pow(t - 1.0F, 2.0F);
    }
    case Easing::InOutBack: {
        constexpr float c1 = 1.70158F;
        constexpr float c2 = c1 * 1.525F;
        return t < 0.5F
            ? (std::pow(2.0F * t, 2.0F) * ((c2 + 1.0F) * 2.0F * t - c2)) / 2.0F
            : (std::pow(2.0F * t - 2.0F, 2.0F) * ((c2 + 1.0F) * (t * 2.0F - 2.0F) + c2) + 2.0F) / 2.0F;
    }
    case Easing::InElastic: {
        constexpr float c4 = (2.0F * kPi) / 3.0F;
        if (t <= 0.0F) {
            return 0.0F;
        }
        if (t >= 1.0F) {
            return 1.0F;
        }
        return -std::pow(2.0F, 10.0F * t - 10.0F) * std::sin((t * 10.0F - 10.75F) * c4);
    }
    case Easing::OutElastic: {
        constexpr float c4 = (2.0F * kPi) / 3.0F;
        if (t <= 0.0F) {
            return 0.0F;
        }
        if (t >= 1.0F) {
            return 1.0F;
        }
        return std::pow(2.0F, -10.0F * t) * std::sin((t * 10.0F - 0.75F) * c4) + 1.0F;
    }
    case Easing::InOutElastic: {
        constexpr float c5 = (2.0F * kPi) / 4.5F;
        if (t <= 0.0F) {
            return 0.0F;
        }
        if (t >= 1.0F) {
            return 1.0F;
        }
        return t < 0.5F
            ? -(std::pow(2.0F, 20.0F * t - 10.0F) * std::sin((20.0F * t - 11.125F) * c5)) / 2.0F
            : (std::pow(2.0F, -20.0F * t + 10.0F) * std::sin((20.0F * t - 11.125F) * c5)) / 2.0F + 1.0F;
    }
    case Easing::InBounce:
        return InBounceImpl(t);
    case Easing::OutBounce:
        return OutBounceImpl(t);
    case Easing::InOutBounce:
        return t < 0.5F ? (1.0F - OutBounceImpl(1.0F - 2.0F * t)) / 2.0F : (1.0F + OutBounceImpl(2.0F * t - 1.0F)) / 2.0F;
    }
    return t;
}

float Evaluate(const Curve& curve, float t) noexcept {
    if (curve.keyframes.empty()) {
        return 0.0F;
    }
    if (curve.keyframes.size() == 1U || t <= curve.keyframes.front().time) {
        return curve.keyframes.front().value;
    }
    if (t >= curve.keyframes.back().time) {
        return curve.keyframes.back().value;
    }
    for (std::size_t i = 0U; i + 1U < curve.keyframes.size(); ++i) {
        const CurveKeyframe& a = curve.keyframes[i];
        const CurveKeyframe& b = curve.keyframes[i + 1U];
        if (t <= b.time) {
            const float localT = InverseLerp(a.time, b.time, t);
            return Lerp(a.value, b.value, Evaluate(a.easing, localT));
        }
    }
    return curve.keyframes.back().value;
}

Color Evaluate(const Gradient& gradient, float t) noexcept {
    if (gradient.stops.empty()) {
        return Color{};
    }
    if (gradient.stops.size() == 1U || t <= gradient.stops.front().time) {
        return gradient.stops.front().color;
    }
    if (t >= gradient.stops.back().time) {
        return gradient.stops.back().color;
    }
    for (std::size_t i = 0U; i + 1U < gradient.stops.size(); ++i) {
        const GradientStop& a = gradient.stops[i];
        const GradientStop& b = gradient.stops[i + 1U];
        if (t <= b.time) {
            const float localT = InverseLerp(a.time, b.time, t);
            return Color{
                Lerp(a.color.r, b.color.r, localT),
                Lerp(a.color.g, b.color.g, localT),
                Lerp(a.color.b, b.color.b, localT),
                Lerp(a.color.a, b.color.a, localT),
            };
        }
    }
    return gradient.stops.back().color;
}

namespace {

constexpr std::uint32_t kCurveMagic = 0x4B435256U; // "KCRV"
constexpr std::uint32_t kGradientMagic = 0x4B475244U; // "KGRD"

// LIB-053: the exact on-disk size of one element, used to reject a
// data-controlled element count that the remaining payload cannot possibly
// contain BEFORE reserving for it — a few-byte file declaring count =
// 0xFFFFFFFF must never force a multi-gigabyte allocation. A CurveKeyframe
// serializes as time(4) + value(4) + easing(1); a GradientStop as time(4) +
// rgba(4*4).
constexpr std::size_t kCurveKeyframeBytes = 9U;
constexpr std::size_t kGradientStopBytes = 20U;

// True when `bytes[offset..]` is large enough to hold `count` elements of
// `elementBytes` each. Uses division (not count*elementBytes) so it cannot
// itself overflow for a hostile count near 2^32.
[[nodiscard]] bool PayloadHoldsElements(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint32_t count, std::size_t elementBytes) noexcept {
    const std::size_t remaining = bytes.size() - offset;
    return count <= remaining / elementBytes;
}

void AppendUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    std::uint8_t buffer[4];
    std::memcpy(buffer, &value, sizeof(buffer));
    bytes.insert(bytes.end(), buffer, buffer + sizeof(buffer));
}

void AppendFloat(std::vector<std::uint8_t>& bytes, float value) {
    std::uint8_t buffer[4];
    std::memcpy(buffer, &value, sizeof(buffer));
    bytes.insert(bytes.end(), buffer, buffer + sizeof(buffer));
}

[[nodiscard]] bool ReadUInt32(std::span<const std::uint8_t> bytes, std::size_t& offset, std::uint32_t& outValue) noexcept {
    if (offset + sizeof(outValue) > bytes.size()) {
        return false;
    }
    std::memcpy(&outValue, bytes.data() + offset, sizeof(outValue));
    offset += sizeof(outValue);
    return true;
}

[[nodiscard]] bool ReadFloat(std::span<const std::uint8_t> bytes, std::size_t& offset, float& outValue) noexcept {
    if (offset + sizeof(outValue) > bytes.size()) {
        return false;
    }
    std::memcpy(&outValue, bytes.data() + offset, sizeof(outValue));
    offset += sizeof(outValue);
    return true;
}

[[nodiscard]] bool ReadUInt8(std::span<const std::uint8_t> bytes, std::size_t& offset, std::uint8_t& outValue) noexcept {
    if (offset >= bytes.size()) {
        return false;
    }
    outValue = bytes[offset];
    offset += 1U;
    return true;
}

} // namespace

std::vector<std::uint8_t> Serialize(const Curve& curve) {
    std::vector<std::uint8_t> bytes;
    AppendUInt32(bytes, kCurveMagic);
    AppendUInt32(bytes, static_cast<std::uint32_t>(curve.keyframes.size()));
    for (const CurveKeyframe& keyframe : curve.keyframes) {
        AppendFloat(bytes, keyframe.time);
        AppendFloat(bytes, keyframe.value);
        bytes.push_back(static_cast<std::uint8_t>(keyframe.easing));
    }
    return bytes;
}

bool Deserialize(std::span<const std::uint8_t> bytes, Curve& outCurve) {
    std::size_t offset = 0U;
    std::uint32_t magic = 0U;
    std::uint32_t count = 0U;
    if (!ReadUInt32(bytes, offset, magic) || magic != kCurveMagic || !ReadUInt32(bytes, offset, count)) {
        return false;
    }
    // LIB-053: reject a count the payload cannot back BEFORE reserving, so a
    // tiny hostile file cannot force a huge allocation. The per-element
    // reads in the loop below already catch a truncated payload, but only
    // after reserve(count) has already tried to allocate for the fabricated
    // count — this guard is what makes the reserve safe.
    if (!PayloadHoldsElements(bytes, offset, count, kCurveKeyframeBytes)) {
        return false;
    }
    std::vector<CurveKeyframe> keyframes;
    keyframes.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
        CurveKeyframe keyframe;
        std::uint8_t easingOrdinal = 0U;
        if (!ReadFloat(bytes, offset, keyframe.time) || !ReadFloat(bytes, offset, keyframe.value) || !ReadUInt8(bytes, offset, easingOrdinal) ||
            easingOrdinal > static_cast<std::uint8_t>(Easing::InOutBounce)) {
            return false;
        }
        keyframe.easing = static_cast<Easing>(easingOrdinal);
        keyframes.push_back(keyframe);
    }
    outCurve.keyframes = std::move(keyframes);
    return true;
}

std::vector<std::uint8_t> Serialize(const Gradient& gradient) {
    std::vector<std::uint8_t> bytes;
    AppendUInt32(bytes, kGradientMagic);
    AppendUInt32(bytes, static_cast<std::uint32_t>(gradient.stops.size()));
    for (const GradientStop& stop : gradient.stops) {
        AppendFloat(bytes, stop.time);
        AppendFloat(bytes, stop.color.r);
        AppendFloat(bytes, stop.color.g);
        AppendFloat(bytes, stop.color.b);
        AppendFloat(bytes, stop.color.a);
    }
    return bytes;
}

bool Deserialize(std::span<const std::uint8_t> bytes, Gradient& outGradient) {
    std::size_t offset = 0U;
    std::uint32_t magic = 0U;
    std::uint32_t count = 0U;
    if (!ReadUInt32(bytes, offset, magic) || magic != kGradientMagic || !ReadUInt32(bytes, offset, count)) {
        return false;
    }
    // LIB-053: see the Curve Deserialize above — reject an unbackable count
    // before reserving so a few-byte hostile file cannot force a huge
    // allocation.
    if (!PayloadHoldsElements(bytes, offset, count, kGradientStopBytes)) {
        return false;
    }
    std::vector<GradientStop> stops;
    stops.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
        GradientStop stop;
        if (!ReadFloat(bytes, offset, stop.time) || !ReadFloat(bytes, offset, stop.color.r) || !ReadFloat(bytes, offset, stop.color.g) ||
            !ReadFloat(bytes, offset, stop.color.b) || !ReadFloat(bytes, offset, stop.color.a)) {
            return false;
        }
        stops.push_back(stop);
    }
    outGradient.stops = std::move(stops);
    return true;
}

} // namespace kb::math
