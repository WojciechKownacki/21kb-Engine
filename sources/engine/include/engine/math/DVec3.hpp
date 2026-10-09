#pragma once

#include "engine/math/EngineMath.hpp"

#include <cmath>

namespace kb::math {

// A double-precision 3D vector for absolute world positions. Single-precision Vec3 stays the type for
// directions, offsets, scales and everything near the origin; DVec3 carries a position wherever it may lie
// kilometres away (transform translations, physics queries, render and audio origins). The conversions are
// explicit so a narrowing to float is always visible at the call site.
struct DVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] constexpr DVec3 ToDVec3(Vec3 value) noexcept {
    return DVec3{ static_cast<double>(value.x), static_cast<double>(value.y), static_cast<double>(value.z) };
}

// Rounds to the nearest float per component.
[[nodiscard]] constexpr Vec3 ToVec3(const DVec3& value) noexcept {
    return Vec3{ static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z) };
}

[[nodiscard]] constexpr DVec3 operator+(const DVec3& lhs, const DVec3& rhs) noexcept {
    return DVec3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

[[nodiscard]] constexpr DVec3 operator-(const DVec3& lhs, const DVec3& rhs) noexcept {
    return DVec3{ lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

[[nodiscard]] constexpr DVec3 operator+(const DVec3& lhs, Vec3 rhs) noexcept {
    return DVec3{ lhs.x + static_cast<double>(rhs.x), lhs.y + static_cast<double>(rhs.y), lhs.z + static_cast<double>(rhs.z) };
}

[[nodiscard]] constexpr DVec3 operator-(const DVec3& lhs, Vec3 rhs) noexcept {
    return DVec3{ lhs.x - static_cast<double>(rhs.x), lhs.y - static_cast<double>(rhs.y), lhs.z - static_cast<double>(rhs.z) };
}

[[nodiscard]] constexpr DVec3 operator*(const DVec3& lhs, double rhs) noexcept {
    return DVec3{ lhs.x * rhs, lhs.y * rhs, lhs.z * rhs };
}

[[nodiscard]] constexpr bool operator==(const DVec3& lhs, const DVec3& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] constexpr bool operator!=(const DVec3& lhs, const DVec3& rhs) noexcept {
    return !(lhs == rhs);
}

[[nodiscard]] inline double Length(const DVec3& value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

[[nodiscard]] inline double Distance(const DVec3& a, const DVec3& b) noexcept {
    return Length(a - b);
}

// `position - origin` rounded to float: the offset of a world position from a nearby origin (a render, audio
// or simulation origin). Exact to float precision of the offset, however far both lie from the world origin.
[[nodiscard]] constexpr Vec3 RelativeTo(const DVec3& position, const DVec3& origin) noexcept {
    return ToVec3(position - origin);
}

// Rotates a double-precision vector by a (normalized) single-precision rotation, in double precision.
[[nodiscard]] constexpr DVec3 RotateDouble(Quat rotation, const DVec3& value) noexcept {
    const double qx = rotation.x;
    const double qy = rotation.y;
    const double qz = rotation.z;
    const double qw = rotation.w;
    const double uvx = qy * value.z - qz * value.y;
    const double uvy = qz * value.x - qx * value.z;
    const double uvz = qx * value.y - qy * value.x;
    const double uuvx = qy * uvz - qz * uvy;
    const double uuvy = qz * uvx - qx * uvz;
    const double uuvz = qx * uvy - qy * uvx;
    return DVec3{
        value.x + ((uvx * qw) + uuvx) * 2.0,
        value.y + ((uvy * qw) + uuvy) * 2.0,
        value.z + ((uvz * qw) + uuvz) * 2.0,
    };
}

// A double split into the float nearest to it (`view`) and the float nearest to what remains (`residual`).
// view + residual carries about 48 bits of mantissa: 1e-7 m at 1e7 m.
struct SplitFloat {
    float view = 0.0F;
    float residual = 0.0F;
};

[[nodiscard]] constexpr SplitFloat SplitDouble(double value) noexcept {
    const float view = static_cast<float>(value);
    return SplitFloat{ view, static_cast<float>(value - static_cast<double>(view)) };
}

// The residual a float view honours: one no larger than half the spacing of floats at the view's magnitude
// (|residual| <= |view| * 2^-24). A residual that does not fit belongs to an earlier value of the view (the
// view was written on its own since) and counts as zero.
[[nodiscard]] constexpr float FittingResidual(float view, float residual) noexcept {
    const float bound = (view < 0.0F ? -view : view) * 0x1p-24F;
    return residual <= bound && residual >= -bound ? residual : 0.0F;
}

// FittingResidual per axis.
[[nodiscard]] constexpr Vec3 FittingResidual(Vec3 view, Vec3 residual) noexcept {
    return Vec3{ FittingResidual(view.x, residual.x), FittingResidual(view.y, residual.y), FittingResidual(view.z, residual.z) };
}

[[nodiscard]] constexpr double JoinFloat(float view, float residual) noexcept {
    return static_cast<double>(view) + static_cast<double>(FittingResidual(view, residual));
}

} // namespace kb::math
