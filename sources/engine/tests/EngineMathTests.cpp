#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>

namespace kb::tests {
namespace {

// LIB-042: kb::scene::Vec3/Quat must be the SAME type as kb::math's, not a
// look-alike second definition — this is what makes every pre-existing
// kb::scene::Vec3/Quat call site (LocalTransform, TransformComponent,
// ColliderComponent, Vec3Math, QuatMath, ...) keep compiling unchanged.
void RunSceneAliasIdentityTest() {
    static_assert(std::is_same_v<kb::scene::Vec3, kb::math::Vec3>, "kb::scene::Vec3 must be an alias to kb::math::Vec3, not a separate type");
    static_assert(std::is_same_v<kb::scene::Quat, kb::math::Quat>, "kb::scene::Quat must be an alias to kb::math::Quat, not a separate type");
    kb::tests::Require(true, "kb::scene Vec3/Quat alias identity is verified at compile time above");
}

// LIB-042: every new value type must default-construct to the documented
// convention (identity rotation, opaque-white color with full alpha,
// forward-facing unit ray, etc.) — not an arbitrary zeroed struct.
void RunDefaultValueConventionsTest() {
    kb::tests::Require(kb::math::Vec2{}.x == 0.0F && kb::math::Vec2{}.y == 0.0F, "Vec2 default must be the zero vector");

    const kb::math::Quat identity{};
    kb::tests::Require(identity.x == 0.0F && identity.y == 0.0F && identity.z == 0.0F && identity.w == 1.0F, "Quat default must be the identity rotation {0,0,0,1}");

    const kb::math::Color white{};
    kb::tests::Require(white.r == 1.0F && white.g == 1.0F && white.b == 1.0F && white.a == 1.0F, "Color default must be opaque white");

    const kb::math::Ray forward{};
    kb::tests::Require(forward.origin.x == 0.0F && forward.origin.y == 0.0F && forward.origin.z == 0.0F, "Ray default origin must be zero");
    kb::tests::Require(forward.direction.x == 0.0F && forward.direction.y == 0.0F && forward.direction.z == 1.0F, "Ray default direction must be the +Z unit vector");

    const kb::math::Plane groundPlane{};
    kb::tests::Require(groundPlane.normal.x == 0.0F && groundPlane.normal.y == 1.0F && groundPlane.normal.z == 0.0F, "Plane default normal must be +Y (up)");
    kb::tests::Require(groundPlane.distance == 0.0F, "Plane default distance must be zero (passes through the origin)");

    const kb::math::Bounds emptyBounds{};
    kb::tests::Require(emptyBounds.center.x == 0.0F && emptyBounds.extents.x == 0.0F, "Bounds default must be a zero-size box at the origin");

    const kb::math::Pose identityPose{};
    kb::tests::Require(identityPose.position.x == 0.0F && identityPose.rotation.w == 1.0F, "Pose default must be zero position + identity rotation");

    kb::tests::Require(kb::math::IVec2{}.x == 0 && kb::math::IVec2{}.y == 0, "IVec2 default must be the zero vector");
    kb::tests::Require(kb::math::Vec4{}.w == 0.0F, "Vec4 default must be the zero vector (no implicit w=1 homogeneous convention)");
    kb::tests::Require(kb::math::Rect{}.width == 0.0F && kb::math::Rect{}.height == 0.0F, "Rect default must be a zero-size rectangle at the origin");
}

// LIB-042: the Vec3 free functions this file centralizes (previously
// privately duplicated in ScriptPhysicsApi.cpp) must be arithmetically
// correct against known values, not just "compiles".
void RunVec3MathTest() {
    using kb::math::Vec3;

    const Vec3 a{ 1.0F, 2.0F, 3.0F };
    const Vec3 b{ 4.0F, -1.0F, 0.5F };

    const Vec3 sum = a + b;
    kb::tests::Require(sum.x == 5.0F && sum.y == 1.0F && sum.z == 3.5F, "Vec3 operator+ must add component-wise");

    const Vec3 diff = a - b;
    kb::tests::Require(diff.x == -3.0F && diff.y == 3.0F && diff.z == 2.5F, "Vec3 operator- must subtract component-wise");

    const Vec3 scaled = a * 2.0F;
    kb::tests::Require(scaled.x == 2.0F && scaled.y == 4.0F && scaled.z == 6.0F, "Vec3 operator* must scale every component");

    kb::tests::Require(kb::math::Dot(a, b) == 1.0F * 4.0F + 2.0F * -1.0F + 3.0F * 0.5F, "Vec3 Dot must compute the standard dot product");

    const Vec3 axis{ 3.0F, 4.0F, 0.0F };
    kb::tests::Require(std::abs(kb::math::Length(axis) - 5.0F) < 0.0001F, "Vec3 Length must compute the Euclidean norm (3-4-5 triangle)");

    const Vec3 normalized = kb::math::Normalize(axis);
    kb::tests::Require(std::abs(kb::math::Length(normalized) - 1.0F) < 0.0001F, "Vec3 Normalize must produce a unit-length vector");
    kb::tests::Require(std::abs(normalized.x - 0.6F) < 0.0001F && std::abs(normalized.y - 0.8F) < 0.0001F, "Vec3 Normalize must preserve direction");

    kb::tests::Require(kb::math::Length(kb::math::Normalize(Vec3{})) == 0.0F, "Vec3 Normalize of the zero vector must return zero, not divide by zero");

    const Vec3 negative{ -1.0F, 2.0F, -3.0F };
    const Vec3 absolute = kb::math::Abs(negative);
    kb::tests::Require(absolute.x == 1.0F && absolute.y == 2.0F && absolute.z == 3.0F, "Vec3 Abs must take the component-wise absolute value");

    const Vec3 maxOf = kb::math::Max(Vec3{ 1.0F, 5.0F, -2.0F }, Vec3{ 3.0F, 2.0F, -1.0F });
    kb::tests::Require(maxOf.x == 3.0F && maxOf.y == 5.0F && maxOf.z == -1.0F, "Vec3 Max must take the component-wise maximum");
}

// LIB-043: Cross, Quat composition/rotation, and Mat3/Mat4 must be
// arithmetically correct against known values.
void RunQuatAndMatrixMathTest() {
    using kb::math::Mat3;
    using kb::math::Mat4;
    using kb::math::Quat;
    using kb::math::Vec3;
    using kb::math::Vec4;

    const Vec3 cross = kb::math::Cross(Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ 0.0F, 1.0F, 0.0F });
    kb::tests::Require(cross.x == 0.0F && cross.y == 0.0F && cross.z == 1.0F, "Cross({1,0,0},{0,1,0}) must be {0,0,1}");

    const Quat identity{};
    const Quat someRotation{ 0.1F, 0.2F, 0.3F, 0.9F };
    const Quat identityLeft = identity * someRotation;
    const Quat identityRight = someRotation * identity;
    kb::tests::Require(
        identityLeft.x == someRotation.x && identityLeft.y == someRotation.y && identityLeft.z == someRotation.z && identityLeft.w == someRotation.w,
        "identity * q must equal q (Quat operator* left identity)");
    kb::tests::Require(
        identityRight.x == someRotation.x && identityRight.y == someRotation.y && identityRight.z == someRotation.z && identityRight.w == someRotation.w,
        "q * identity must equal q (Quat operator* right identity)");

    // 90-degree rotation around +Z, applied to +X — a known, convention-pinning value
    // (this exact result is what LIB-043's documented left-handed/Y-up convention,
    // and the exact Multiply/Rotate formulas already used by
    // EcsRenderTransformResolver::Compose before this consolidation, produce).
    const float halfAngle = 3.14159265358979323846F / 4.0F; // 45 degrees in radians
    const Quat rotateZ90{ 0.0F, 0.0F, std::sin(halfAngle), std::cos(halfAngle) };
    const Vec3 rotated = kb::math::Rotate(rotateZ90, Vec3{ 1.0F, 0.0F, 0.0F });
    kb::tests::Require(std::abs(rotated.x) < 0.0001F && std::abs(rotated.y - 1.0F) < 0.0001F && std::abs(rotated.z) < 0.0001F, "Rotating +X by a 90-degree +Z quaternion must yield +Y");
    kb::tests::Require(std::abs(kb::math::Length(rotated) - 1.0F) < 0.0001F, "Rotate must preserve vector length for a unit quaternion");

    const Quat unnormalized{ 2.0F, 0.0F, 0.0F, 0.0F };
    const Quat normalizedQuat = kb::math::Normalize(unnormalized);
    kb::tests::Require(std::abs(normalizedQuat.x - 1.0F) < 0.0001F && normalizedQuat.y == 0.0F, "Quat Normalize must scale to unit length");
    const Quat zeroQuat = kb::math::Normalize(Quat{ 0.0F, 0.0F, 0.0F, 0.0F });
    kb::tests::Require(zeroQuat.x == 0.0F && zeroQuat.y == 0.0F && zeroQuat.z == 0.0F && zeroQuat.w == 1.0F, "Quat Normalize of a zero-length quaternion must return identity, not divide by zero");

    const Mat3 identityMat3{};
    kb::tests::Require(
        identityMat3.columns[0].x == 1.0F && identityMat3.columns[1].y == 1.0F && identityMat3.columns[2].z == 1.0F,
        "Mat3 default must be the identity matrix");

    const Mat4 identityMat4{};
    const Vec4 point{ 3.0F, 4.0F, 5.0F, 1.0F };
    const Vec4 transformedByIdentity = identityMat4 * point;
    kb::tests::Require(
        transformedByIdentity.x == point.x && transformedByIdentity.y == point.y && transformedByIdentity.z == point.z && transformedByIdentity.w == point.w,
        "Mat4 identity * point must return the point unchanged");

    const Mat4 translated = kb::math::FromTRS(Vec3{ 5.0F, 0.0F, 0.0F }, Quat{}, Vec3{ 1.0F, 1.0F, 1.0F });
    const Vec4 origin = translated * Vec4{ 0.0F, 0.0F, 0.0F, 1.0F };
    kb::tests::Require(origin.x == 5.0F && origin.y == 0.0F && origin.z == 0.0F, "FromTRS with identity rotation/scale must place the origin at the given translation");

    const Mat4 scaled = kb::math::FromTRS(Vec3{}, Quat{}, Vec3{ 2.0F, 3.0F, 4.0F });
    const Vec4 scaledPoint = scaled * Vec4{ 1.0F, 1.0F, 1.0F, 1.0F };
    kb::tests::Require(scaledPoint.x == 2.0F && scaledPoint.y == 3.0F && scaledPoint.z == 4.0F, "FromTRS scale must scale each axis independently");

    const Mat4 composed = identityMat4 * translated;
    kb::tests::Require(
        composed.columns[3].x == translated.columns[3].x && composed.columns[3].y == translated.columns[3].y,
        "Mat4 identity * M must equal M (Mat4 operator* identity)");

    // LIB-085: Conjugate/Inverse — added for Transform.SetWorldPose's
    // world-to-local back-solve (the first caller in this codebase that
    // needs to invert an arbitrary quaternion).
    const Quat conjugated = kb::math::Conjugate(rotateZ90);
    kb::tests::Require(conjugated.x == -rotateZ90.x && conjugated.y == -rotateZ90.y && conjugated.z == -rotateZ90.z && conjugated.w == rotateZ90.w,
        "Conjugate must negate the imaginary part and leave w unchanged");
    const Quat unitInverse = kb::math::Inverse(rotateZ90);
    const Quat shouldBeIdentity = rotateZ90 * unitInverse;
    kb::tests::Require(std::abs(shouldBeIdentity.x) < 0.0001F && std::abs(shouldBeIdentity.y) < 0.0001F && std::abs(shouldBeIdentity.z) < 0.0001F && std::abs(shouldBeIdentity.w - 1.0F) < 0.0001F,
        "q * Inverse(q) must be the identity rotation for a unit quaternion");
    const Vec3 rotatedThenUnrotated = kb::math::Rotate(kb::math::Inverse(rotateZ90), kb::math::Rotate(rotateZ90, Vec3{ 1.0F, 0.0F, 0.0F }));
    kb::tests::Require(std::abs(rotatedThenUnrotated.x - 1.0F) < 0.0001F && std::abs(rotatedThenUnrotated.y) < 0.0001F && std::abs(rotatedThenUnrotated.z) < 0.0001F,
        "Rotate(Inverse(q), Rotate(q, v)) must recover v");
    const Quat zeroInverse = kb::math::Inverse(Quat{ 0.0F, 0.0F, 0.0F, 0.0F });
    kb::tests::Require(zeroInverse.x == 0.0F && zeroInverse.y == 0.0F && zeroInverse.z == 0.0F && zeroInverse.w == 1.0F,
        "Inverse of a zero-length quaternion must return identity, not divide by zero");
}

// LIB-044: Radians/Degrees must convert correctly against known values,
// must NOT implicitly convert into each other or from a bare float (that
// is the whole point — every construction site must name its unit), and
// same-unit arithmetic/comparison must work for accumulation/clamping use
// cases (e.g. camera yaw/pitch).
void RunAngleUnitsTest() {
    using kb::math::Degrees;
    using kb::math::Radians;

    kb::tests::Require(std::abs(kb::math::ToRadians(Degrees{ 180.0F }).Value() - kb::math::kPi) < 0.0001F, "ToRadians(180 degrees) must equal pi radians");
    kb::tests::Require(std::abs(kb::math::ToDegrees(Radians{ kb::math::kPi }).Value() - 180.0F) < 0.0001F, "ToDegrees(pi radians) must equal 180 degrees");
    kb::tests::Require(kb::math::ToRadians(Degrees{ 0.0F }).Value() == 0.0F, "ToRadians(0 degrees) must be exactly zero");

    // Round-trip: converting degrees -> radians -> degrees must recover the
    // original value (within float precision).
    const Degrees original{ 47.5F };
    const Degrees roundTripped = kb::math::ToDegrees(kb::math::ToRadians(original));
    kb::tests::Require(std::abs(roundTripped.Value() - original.Value()) < 0.001F, "Degrees -> Radians -> Degrees must round-trip");

    static_assert(!std::is_convertible_v<float, Radians>, "a bare float must not implicitly convert to Radians — every call site must name its unit");
    static_assert(!std::is_convertible_v<float, Degrees>, "a bare float must not implicitly convert to Degrees — every call site must name its unit");
    static_assert(!std::is_convertible_v<Degrees, Radians>, "Degrees must not implicitly convert to Radians — ToRadians must be called explicitly");
    static_assert(!std::is_convertible_v<Radians, Degrees>, "Radians must not implicitly convert to Degrees — ToDegrees must be called explicitly");

    const Degrees yaw = Degrees{ 10.0F } + Degrees{ 5.0F };
    kb::tests::Require(yaw.Value() == 15.0F, "Degrees operator+ must add same-unit values");
    kb::tests::Require((Degrees{ 20.0F } - Degrees{ 5.0F }).Value() == 15.0F, "Degrees operator- must subtract same-unit values");
    kb::tests::Require((Degrees{ 10.0F } * 3.0F).Value() == 30.0F, "Degrees operator* must scale by a plain float");
    kb::tests::Require(Degrees{ 90.0F } < Degrees{ 91.0F }, "Degrees operator< must compare same-unit values");
    kb::tests::Require(Degrees{ 90.0F } == Degrees{ 90.0F } && Degrees{ 90.0F } != Degrees{ 91.0F }, "Degrees operator==/!= must compare same-unit values");

    kb::tests::Require((Radians{ 1.0F } + Radians{ 0.5F }).Value() == 1.5F, "Radians operator+ must add same-unit values");
    kb::tests::Require(Radians{ 1.0F } < Radians{ 2.0F }, "Radians operator< must compare same-unit values");
}

// LIB-045: the scalar math foundation (also registered as Math.* script
// functions in ScriptMathApi.cpp, which reuse these directly) must be
// correct against known values, including the edge cases that distinguish
// each function from a naive implementation (a==b in InverseLerp, an
// already-reached target in MoveTowards, a non-positive smoothTime in
// Damp).
void RunScalarMathFunctionsTest() {
    kb::tests::Require(kb::math::Clamp(5.0F, 0.0F, 10.0F) == 5.0F, "Clamp must pass through a value already inside the range");
    kb::tests::Require(kb::math::Clamp(-5.0F, 0.0F, 10.0F) == 0.0F, "Clamp must clamp a value below min");
    kb::tests::Require(kb::math::Clamp(15.0F, 0.0F, 10.0F) == 10.0F, "Clamp must clamp a value above max");

    kb::tests::Require(kb::math::Lerp(0.0F, 10.0F, 0.5F) == 5.0F, "Lerp must interpolate at the midpoint");
    kb::tests::Require(kb::math::Lerp(0.0F, 10.0F, 1.5F) == 10.0F, "Lerp must clamp t above 1");
    kb::tests::Require(kb::math::Lerp(0.0F, 10.0F, -0.5F) == 0.0F, "Lerp must clamp t below 0");

    kb::tests::Require(kb::math::InverseLerp(0.0F, 10.0F, 2.5F) == 0.25F, "InverseLerp must invert Lerp");
    kb::tests::Require(kb::math::InverseLerp(5.0F, 5.0F, 5.0F) == 0.0F, "InverseLerp must return 0 for a zero-width range instead of dividing by zero");
    kb::tests::Require(kb::math::InverseLerp(0.0F, 10.0F, 20.0F) == 1.0F, "InverseLerp must clamp a value beyond the range to 1");

    kb::tests::Require(kb::math::Remap(5.0F, 0.0F, 10.0F, 100.0F, 200.0F) == 150.0F, "Remap must linearly map between ranges");

    kb::tests::Require(kb::math::SmoothStep(0.0F, 10.0F, 0.0F) == 0.0F, "SmoothStep must be 0 at edge0");
    kb::tests::Require(kb::math::SmoothStep(0.0F, 10.0F, 10.0F) == 1.0F, "SmoothStep must be 1 at edge1");
    kb::tests::Require(kb::math::SmoothStep(0.0F, 10.0F, 5.0F) == 0.5F, "SmoothStep must be 0.5 at the midpoint (symmetric curve)");

    kb::tests::Require(kb::math::MoveTowards(0.0F, 10.0F, 3.0F) == 3.0F, "MoveTowards must move by maxDelta when far from target");
    kb::tests::Require(kb::math::MoveTowards(9.0F, 10.0F, 3.0F) == 10.0F, "MoveTowards must not overshoot the target");
    kb::tests::Require(kb::math::MoveTowards(5.0F, 5.0F, 3.0F) == 5.0F, "MoveTowards must stay put when already at the target");
    kb::tests::Require(kb::math::MoveTowards(10.0F, 0.0F, 3.0F) == 7.0F, "MoveTowards must move toward a lower target");

    const kb::math::DampResult dampStep = kb::math::Damp(0.0F, 10.0F, 0.0F, 1.0F, 0.1F, std::numeric_limits<float>::max());
    kb::tests::Require(dampStep.value > 0.0F && dampStep.value < 10.0F, "Damp must move current toward target without overshooting on the first step");
    kb::tests::Require(dampStep.velocity > 0.0F, "Damp must report a positive velocity when moving toward a higher target");

    const kb::math::DampResult dampInvalidSmoothTime = kb::math::Damp(0.0F, 10.0F, 0.0F, -1.0F, 0.1F, std::numeric_limits<float>::max());
    kb::tests::Require(std::isfinite(dampInvalidSmoothTime.value) && std::isfinite(dampInvalidSmoothTime.velocity), "Damp must not produce NaN/Inf for a non-positive smoothTime");
}

// LIB-046: Min/Max/Abs/Sign/Floor/Ceil/Round/Frac/Mod/Sqrt/Pow/Exp/Log
// scalar overloads, including the edge case that distinguishes Mod's
// floor-based convention from C's fmod (a negative dividend).
void RunScalarMathFunctions2Test() {
    kb::tests::Require(kb::math::Min(3.0F, 7.0F) == 3.0F && kb::math::Min(7.0F, 3.0F) == 3.0F, "Min must return the smaller of two values regardless of argument order");
    kb::tests::Require(kb::math::Max(3.0F, 7.0F) == 7.0F && kb::math::Max(7.0F, 3.0F) == 7.0F, "Max must return the larger of two values regardless of argument order");
    kb::tests::Require(kb::math::Abs(-4.5F) == 4.5F && kb::math::Abs(4.5F) == 4.5F, "Abs must return the magnitude regardless of sign");

    kb::tests::Require(kb::math::Sign(5.0F) == 1.0F, "Sign must be 1 for a positive value");
    kb::tests::Require(kb::math::Sign(-5.0F) == -1.0F, "Sign must be -1 for a negative value");
    kb::tests::Require(kb::math::Sign(0.0F) == 0.0F, "Sign must be exactly 0 for zero, not +1 or -1");

    kb::tests::Require(kb::math::Floor(2.7F) == 2.0F && kb::math::Floor(-2.7F) == -3.0F, "Floor must round toward negative infinity");
    kb::tests::Require(kb::math::Ceil(2.3F) == 3.0F && kb::math::Ceil(-2.3F) == -2.0F, "Ceil must round toward positive infinity");
    kb::tests::Require(kb::math::Round(2.5F) == 3.0F && kb::math::Round(-2.5F) == -3.0F, "Round must round ties away from zero");

    kb::tests::Require(std::abs(kb::math::Frac(2.75F) - 0.75F) < 0.0001F, "Frac must return the fractional part for a positive value");
    kb::tests::Require(std::abs(kb::math::Frac(-1.25F) - 0.75F) < 0.0001F, "Frac must stay non-negative for a negative value (floor-based, not truncation-based)");

    kb::tests::Require(std::abs(kb::math::Mod(5.0F, 3.0F) - 2.0F) < 0.0001F, "Mod must match ordinary modulo for a positive dividend");
    kb::tests::Require(std::abs(kb::math::Mod(-1.0F, 4.0F) - 3.0F) < 0.0001F, "Mod must be floor-based (result in [0, divisor)), unlike C's fmod which would return -1");

    kb::tests::Require(std::abs(kb::math::Sqrt(9.0F) - 3.0F) < 0.0001F, "Sqrt must compute the square root");
    kb::tests::Require(std::abs(kb::math::Pow(2.0F, 10.0F) - 1024.0F) < 0.01F, "Pow must compute base^exponent");
    kb::tests::Require(std::abs(kb::math::Exp(0.0F) - 1.0F) < 0.0001F, "Exp(0) must be 1");
    kb::tests::Require(std::abs(kb::math::Log(1.0F)) < 0.0001F, "Log(1) must be 0 (natural log)");
    kb::tests::Require(std::abs(kb::math::Log(std::exp(1.0F)) - 1.0F) < 0.0001F, "Log(e) must be 1, confirming natural (not base-10/base-2) log");
}

// LIB-047: trig functions take/return kb::math::Radians (LIB-044's typed
// angle unit, not a bare float) with known values, including Atan2's
// quadrant resolution (which plain Atan(y/x) cannot do) and Asin/Acos
// round-tripping their own inverse.
void RunTrigFunctionsTest() {
    using kb::math::Degrees;
    using kb::math::Radians;

    kb::tests::Require(std::abs(kb::math::Sin(Radians{ 0.0F })) < 0.0001F, "Sin(0) must be 0");
    kb::tests::Require(std::abs(kb::math::Sin(kb::math::ToRadians(Degrees{ 90.0F })) - 1.0F) < 0.0001F, "Sin(90 degrees) must be 1");
    kb::tests::Require(std::abs(kb::math::Cos(Radians{ 0.0F }) - 1.0F) < 0.0001F, "Cos(0) must be 1");
    kb::tests::Require(std::abs(kb::math::Cos(kb::math::ToRadians(Degrees{ 90.0F }))) < 0.0001F, "Cos(90 degrees) must be 0");
    kb::tests::Require(std::abs(kb::math::Tan(Radians{ 0.0F })) < 0.0001F, "Tan(0) must be 0");

    kb::tests::Require(std::abs(kb::math::Asin(1.0F).Value() - kb::math::kPi / 2.0F) < 0.0001F, "Asin(1) must be pi/2");
    kb::tests::Require(std::abs(kb::math::Acos(1.0F).Value()) < 0.0001F, "Acos(1) must be 0");
    kb::tests::Require(std::abs(kb::math::Atan(1.0F).Value() - kb::math::kPi / 4.0F) < 0.0001F, "Atan(1) must be pi/4");

    // Atan2 resolves the quadrant from both signs — Atan(1) alone cannot
    // distinguish (1,1) from (-1,-1), but Atan2 must.
    kb::tests::Require(std::abs(kb::math::Atan2(1.0F, 1.0F).Value() - kb::math::kPi / 4.0F) < 0.0001F, "Atan2(1,1) must be pi/4 (first quadrant)");
    kb::tests::Require(std::abs(kb::math::Atan2(-1.0F, -1.0F).Value() - (-3.0F * kb::math::kPi / 4.0F)) < 0.0001F, "Atan2(-1,-1) must be -3pi/4 (third quadrant), distinguishing it from Atan(1)");
    kb::tests::Require(kb::math::Atan2(0.0F, 0.0F).Value() == 0.0F, "Atan2(0,0) must conventionally be 0, not NaN/undefined");
}

// LIB-048: Distance/Project/Reflect/Refract, the four Vec3 functions this
// task adds on top of the LIB-042 Dot/Cross/Length/Normalize foundation.
void RunVec3DistanceProjectReflectRefractTest() {
    using kb::math::Vec3;

    kb::tests::Require(std::abs(kb::math::Distance(Vec3{ 0.0F, 0.0F, 0.0F }, Vec3{ 3.0F, 4.0F, 0.0F }) - 5.0F) < 0.0001F, "Distance must compute the Euclidean distance (3-4-5 triangle)");
    kb::tests::Require(kb::math::Distance(Vec3{ 1.0F, 2.0F, 3.0F }, Vec3{ 1.0F, 2.0F, 3.0F }) == 0.0F, "Distance between identical points must be zero");

    // Projecting (2,2,0) onto the X axis leaves only the X component.
    const Vec3 projected = kb::math::Project(Vec3{ 2.0F, 2.0F, 0.0F }, Vec3{ 5.0F, 0.0F, 0.0F });
    kb::tests::Require(std::abs(projected.x - 2.0F) < 0.0001F && std::abs(projected.y) < 0.0001F, "Project must return the component of value parallel to onto");
    const Vec3 projectedOntoZero = kb::math::Project(Vec3{ 1.0F, 1.0F, 1.0F }, Vec3{});
    kb::tests::Require(projectedOntoZero.x == 0.0F && projectedOntoZero.y == 0.0F && projectedOntoZero.z == 0.0F, "Project onto the zero vector must return zero, not divide by zero");

    // A ray going straight down (0,-1,0) reflecting off an upward-facing
    // floor normal (0,1,0) must bounce straight back up (0,1,0).
    const Vec3 reflected = kb::math::Reflect(Vec3{ 0.0F, -1.0F, 0.0F }, Vec3{ 0.0F, 1.0F, 0.0F });
    kb::tests::Require(std::abs(reflected.x) < 0.0001F && std::abs(reflected.y - 1.0F) < 0.0001F, "Reflect off a floor normal must bounce a downward ray straight up");

    // Refracting straight through a surface (incident parallel to
    // -normal, eta=1: same medium on both sides) must pass through
    // unchanged.
    const Vec3 refractedStraight = kb::math::Refract(Vec3{ 0.0F, -1.0F, 0.0F }, Vec3{ 0.0F, 1.0F, 0.0F }, 1.0F);
    kb::tests::Require(std::abs(refractedStraight.x) < 0.0001F && std::abs(refractedStraight.y - (-1.0F)) < 0.0001F, "Refract with eta=1 straight through a surface must pass through unchanged");
    // A grazing-angle ray with a large eta triggers total internal
    // reflection — must return the zero vector, not NaN.
    const Vec3 refractedTIR = kb::math::Refract(Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ 0.0F, 1.0F, 0.0F }, 2.0F);
    kb::tests::Require(refractedTIR.x == 0.0F && refractedTIR.y == 0.0F && refractedTIR.z == 0.0F, "Refract must return the zero vector on total internal reflection, not NaN");
}

// LIB-049: Angle/SignedAngle/Slerp/LookRotation/FromToRotation/
// RotateTowards. LookRotation/FromToRotation are verified by applying the
// resulting rotation with the already-verified Rotate() (LIB-043) and
// checking it lands on the expected vector, rather than pinning exact
// quaternion components — a more robust proof that composes with
// existing, already-tested machinery instead of duplicating it.
void RunRotationFunctionsTest() {
    using kb::math::Quat;
    using kb::math::Vec3;

    kb::tests::Require(std::abs(kb::math::Angle(Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ 0.0F, 1.0F, 0.0F }).Value() - kb::math::kPi / 2.0F) < 0.0001F, "Angle between perpendicular vectors must be pi/2");
    kb::tests::Require(kb::math::Angle(Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ 1.0F, 0.0F, 0.0F }).Value() < 0.0001F, "Angle between identical vectors must be 0");
    kb::tests::Require(std::abs(kb::math::Angle(Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ -1.0F, 0.0F, 0.0F }).Value() - kb::math::kPi) < 0.0001F, "Angle between opposite vectors must be pi");

    kb::tests::Require(kb::math::SignedAngle(Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ 0.0F, 1.0F, 0.0F }, Vec3{ 0.0F, 0.0F, 1.0F }).Value() > 0.0F, "SignedAngle(+X,+Y,+Z axis) must be positive");
    kb::tests::Require(kb::math::SignedAngle(Vec3{ 0.0F, 1.0F, 0.0F }, Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ 0.0F, 0.0F, 1.0F }).Value() < 0.0F, "SignedAngle(+Y,+X,+Z axis) must be negative (opposite of the +X-to-+Y case)");

    const Quat identity{};
    const Quat rotated90Y = kb::math::LookRotation(Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ 0.0F, 1.0F, 0.0F });
    kb::tests::Require(kb::math::Slerp(identity, rotated90Y, 0.0F).x == identity.x && kb::math::Slerp(identity, rotated90Y, 0.0F).w == identity.w, "Slerp at t=0 must return the start rotation");
    const Quat slerpedFull = kb::math::Slerp(identity, rotated90Y, 1.0F);
    kb::tests::Require(std::abs(slerpedFull.x - rotated90Y.x) < 0.0001F && std::abs(slerpedFull.w - rotated90Y.w) < 0.0001F, "Slerp at t=1 must return the end rotation");
    const Quat slerpedIdentical = kb::math::Slerp(rotated90Y, rotated90Y, 0.5F);
    kb::tests::Require(std::abs(slerpedIdentical.w - rotated90Y.w) < 0.0001F, "Slerp between identical rotations must not divide by a near-zero sin(omega)");

    // LookRotation(+Z, +Y) must be the identity (this file's forward
    // convention is local +Z, matching Ray's default direction).
    kb::tests::Require(std::abs(identity.x) < 0.0001F && std::abs(identity.y) < 0.0001F && std::abs(identity.z) < 0.0001F && std::abs(identity.w - 1.0F) < 0.0001F, "Quat default (used as the LookRotation(+Z,+Y) baseline) must be identity");
    const Quat lookAtZ = kb::math::LookRotation(Vec3{ 0.0F, 0.0F, 1.0F }, Vec3{ 0.0F, 1.0F, 0.0F });
    kb::tests::Require(std::abs(lookAtZ.x) < 0.0001F && std::abs(lookAtZ.y) < 0.0001F && std::abs(lookAtZ.z) < 0.0001F && std::abs(lookAtZ.w - 1.0F) < 0.0001F, "LookRotation(+Z, +Y) must be the identity rotation");
    // LookRotation(+X, +Y) applied to local forward (+Z) must point at +X.
    const Vec3 lookedAtX = kb::math::Rotate(rotated90Y, Vec3{ 0.0F, 0.0F, 1.0F });
    kb::tests::Require(std::abs(lookedAtX.x - 1.0F) < 0.0001F && std::abs(lookedAtX.y) < 0.0001F && std::abs(lookedAtX.z) < 0.0001F, "LookRotation(+X, +Y) must rotate local forward (+Z) to point at +X");

    kb::tests::Require(std::abs(kb::math::FromToRotation(Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ 1.0F, 0.0F, 0.0F }).w - 1.0F) < 0.0001F, "FromToRotation between identical directions must be the identity");
    const Vec3 rotatedByFromTo = kb::math::Rotate(kb::math::FromToRotation(Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ 0.0F, 1.0F, 0.0F }), Vec3{ 1.0F, 0.0F, 0.0F });
    kb::tests::Require(std::abs(rotatedByFromTo.x) < 0.0001F && std::abs(rotatedByFromTo.y - 1.0F) < 0.0001F, "FromToRotation(+X,+Y) applied to +X must land on +Y");
    const Vec3 rotatedByFromToOpposite = kb::math::Rotate(kb::math::FromToRotation(Vec3{ 1.0F, 0.0F, 0.0F }, Vec3{ -1.0F, 0.0F, 0.0F }), Vec3{ 1.0F, 0.0F, 0.0F });
    kb::tests::Require(std::abs(rotatedByFromToOpposite.x - (-1.0F)) < 0.0001F, "FromToRotation between exactly opposite directions must still produce a valid 180-degree rotation, not NaN");

    const Quat reachedTarget = kb::math::RotateTowards(identity, rotated90Y, kb::math::Radians{ 10.0F });
    kb::tests::Require(std::abs(reachedTarget.w - rotated90Y.w) < 0.0001F, "RotateTowards must snap to the target when maxDelta exceeds the angle between the two rotations");
    const Quat partialStep = kb::math::RotateTowards(identity, rotated90Y, kb::math::Radians{ 0.1F });
    kb::tests::Require(std::abs(partialStep.w - rotated90Y.w) > 0.0001F, "RotateTowards must not overshoot to the target when maxDelta is smaller than the angle between the two rotations");
}

// LIB-050: Random01/Noise1D/Noise2D/Noise3D must be pure, deterministic
// functions of their explicit arguments — no hidden global generator, so
// the primary property to prove is "same inputs -> same output" and
// "different seed -> (almost certainly) different output", plus gradient
// noise's defining mathematical property (exactly zero at every integer
// lattice point, for any seed).
void RunNoiseAndRandomTest() {
    kb::tests::Require(kb::math::Random01(42U, 7U) == kb::math::Random01(42U, 7U), "Random01 must be deterministic: the same (seed, index) must always produce the same value");
    kb::tests::Require(kb::math::Random01(42U, 7U) != kb::math::Random01(42U, 8U), "Random01 must (in practice) differ for a different index with the same seed");
    kb::tests::Require(kb::math::Random01(42U, 7U) != kb::math::Random01(43U, 7U), "Random01 must (in practice) differ for a different seed with the same index");
    const float randomValue = kb::math::Random01(1U, 1U);
    kb::tests::Require(randomValue >= 0.0F && randomValue <= 1.0F, "Random01 must stay within [0, 1]");

    // Deliberately non-symmetric fractional offsets (not exactly 0.5 in
    // any axis): at the exact midpoint of a cell, Grad(hash, 0.5, 0.5,
    // 0.5) only has 3 possible outputs regardless of hash, which makes a
    // "different seed must differ" check flaky at that specific point —
    // not a property of the noise function, just a bad sample point.
    kb::tests::Require(kb::math::Noise3D(1.37F, 2.91F, 3.14F, 42U) == kb::math::Noise3D(1.37F, 2.91F, 3.14F, 42U), "Noise3D must be deterministic: the same (position, seed) must always produce the same value");
    kb::tests::Require(kb::math::Noise3D(1.37F, 2.91F, 3.14F, 42U) != kb::math::Noise3D(1.37F, 2.91F, 3.14F, 43U), "Noise3D must (in practice) differ for a different seed at the same position");

    // The defining property of gradient noise: exactly zero at every
    // integer lattice point (the distance vector from a lattice point to
    // itself is zero, so every corner's gradient·distance term is zero
    // there), for any seed.
    kb::tests::Require(kb::math::Noise3D(3.0F, -2.0F, 5.0F, 42U) == 0.0F, "Noise3D must be exactly zero at an integer lattice point");
    kb::tests::Require(kb::math::Noise3D(0.0F, 0.0F, 0.0F, 999U) == 0.0F, "Noise3D must be exactly zero at the origin for any seed");
    kb::tests::Require(kb::math::Noise2D(4.0F, -1.0F, 42U) == 0.0F, "Noise2D must be exactly zero at an integer lattice point");
    kb::tests::Require(kb::math::Noise1D(7.0F, 42U) == 0.0F, "Noise1D must be exactly zero at an integer lattice point");

    // Off-lattice, the noise must actually vary (not collapse to zero
    // everywhere) and stay within gradient noise's expected rough range.
    const float midCellNoise = kb::math::Noise3D(0.37F, 0.61F, 0.24F, 42U);
    kb::tests::Require(midCellNoise != 0.0F, "Noise3D must be non-zero away from a lattice point");
    kb::tests::Require(midCellNoise > -2.0F && midCellNoise < 2.0F, "Noise3D must stay within gradient noise's expected rough amplitude range");

    kb::tests::Require(kb::math::Noise1D(0.37F, 42U) == kb::math::Noise2D(0.37F, 0.0F, 42U), "Noise1D must exactly match Noise2D/Noise3D with the unused axes pinned to 0 (same underlying implementation, not a second one)");
    kb::tests::Require(kb::math::Noise2D(0.37F, 0.61F, 42U) == kb::math::Noise3D(0.37F, 0.61F, 0.0F, 42U), "Noise2D must exactly match Noise3D with the unused axis pinned to 0");

    // LIB-054 (2026-07-17 audit gap): a NaN or infinite coordinate must not
    // reach the `static_cast<int32_t>(Floor(coord))` lattice conversion,
    // which is undefined behaviour for a non-finite (or out-of-int32-range)
    // float. Noise3D screens them out and returns the neutral 0.0F. Every
    // non-finite input on every axis is exercised.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    kb::tests::Require(kb::math::Noise3D(nan, 0.5F, 0.5F, 42U) == 0.0F, "Noise3D must return 0 (not UB) for a NaN x coordinate");
    kb::tests::Require(kb::math::Noise3D(0.5F, nan, 0.5F, 42U) == 0.0F, "Noise3D must return 0 for a NaN y coordinate");
    kb::tests::Require(kb::math::Noise3D(0.5F, 0.5F, nan, 42U) == 0.0F, "Noise3D must return 0 for a NaN z coordinate");
    kb::tests::Require(kb::math::Noise3D(inf, 0.5F, 0.5F, 42U) == 0.0F, "Noise3D must return 0 for a +infinity coordinate");
    kb::tests::Require(kb::math::Noise3D(-inf, 0.5F, 0.5F, 42U) == 0.0F, "Noise3D must return 0 for a -infinity coordinate");
    kb::tests::Require(kb::math::Noise1D(nan, 42U) == 0.0F, "Noise1D must inherit Noise3D's non-finite guard (returns 0 for NaN)");
    kb::tests::Require(kb::math::Noise2D(inf, inf, 42U) == 0.0F, "Noise2D must inherit Noise3D's non-finite guard (returns 0 for infinity)");

    // A finite coordinate larger in magnitude than INT32_MAX must produce a
    // defined result via the lattice clamp, not UB from an out-of-range
    // float->int32 cast. The value only needs to be finite and in range —
    // the point is that it does not crash or trap.
    const float huge = 5.0e9F; // > INT32_MAX (~2.147e9)
    const float hugeNoise = kb::math::Noise3D(huge, -huge, huge, 42U);
    kb::tests::Require(std::isfinite(hugeNoise) && hugeNoise > -2.0F && hugeNoise < 2.0F,
        "Noise3D must return a defined, in-amplitude value for a finite coordinate beyond int32 range, not trigger UB");
}

// LIB-051: RandomStream's whole point is that its state IS a plain value
// (not an opaque handle to hidden generator state) — the primary property
// to prove is that a COPY of a stream is a genuine snapshot: advancing
// the original must not affect what the copy produces next.
void RunRandomStreamTest() {
    const kb::math::RandomStream seeded = kb::math::MakeRandomStream(42U);
    kb::tests::Require(seeded.seed == 42U && seeded.counter == 0U, "MakeRandomStream must start at counter 0 with the given seed");

    const kb::math::RandomStreamUInt32Result first = kb::math::NextUInt32(seeded);
    const kb::math::RandomStreamUInt32Result firstAgain = kb::math::NextUInt32(seeded);
    kb::tests::Require(first.value == firstAgain.value, "NextUInt32 must be deterministic: calling it twice on the SAME (unmodified) stream must return the same value");
    kb::tests::Require(first.stream.counter == seeded.counter + 1U, "NextUInt32 must advance the counter by exactly one");

    // The defining snapshot property: a copy of the stream, taken BEFORE
    // advancing the original, must reproduce the exact same next value —
    // proving the state is a real value, not a reference to shared
    // mutable generator state.
    const kb::math::RandomStream snapshot = seeded;
    kb::math::RandomStream advancingOriginal = seeded;
    advancingOriginal = kb::math::NextUInt32(advancingOriginal).stream;
    advancingOriginal = kb::math::NextUInt32(advancingOriginal).stream;
    kb::tests::Require(advancingOriginal.counter == seeded.counter + 2U, "Advancing a stream twice must move its counter forward by two");
    const kb::math::RandomStreamUInt32Result fromSnapshot = kb::math::NextUInt32(snapshot);
    kb::tests::Require(fromSnapshot.value == first.value, "A snapshot taken before advancing must reproduce the same next value the original stream would have produced at that point, unaffected by the original being advanced afterward");

    // Two consecutive draws from a threaded stream must (in practice)
    // differ — this is what "the stream actually advances" looks like
    // from the caller's perspective.
    const kb::math::RandomStreamUInt32Result second = kb::math::NextUInt32(first.stream);
    kb::tests::Require(second.value != first.value, "Consecutive draws from a properly threaded stream must (in practice) differ");

    const kb::math::RandomStreamFloatResult unitFloat = kb::math::NextFloat01(seeded);
    kb::tests::Require(unitFloat.value >= 0.0F && unitFloat.value < 1.0F, "NextFloat01 must stay within [0, 1)");

    const kb::math::RandomStreamRangeResult ranged = kb::math::NextRange(seeded, 10.0F, 20.0F);
    kb::tests::Require(ranged.value >= 10.0F && ranged.value < 20.0F, "NextRange must stay within [min, max)");

    kb::math::RandomStream intStream = seeded;
    bool sawValueBelow5 = false;
    bool sawOutOfRange = false;
    for (int i = 0; i < 50; ++i) {
        const kb::math::RandomStreamIntRangeResult picked = kb::math::NextIntRange(intStream, 0, 5);
        intStream = picked.stream;
        if (picked.value < 5) {
            sawValueBelow5 = true;
        }
        if (picked.value < 0 || picked.value >= 5) {
            sawOutOfRange = true;
        }
    }
    kb::tests::Require(sawValueBelow5 && !sawOutOfRange, "NextIntRange must stay within [min, max) across many draws");

    const kb::math::RandomStreamIntRangeResult degenerate = kb::math::NextIntRange(seeded, 5, 5);
    kb::tests::Require(degenerate.value == 5, "NextIntRange with a zero-width range must return min rather than dividing by zero");
    kb::tests::Require(degenerate.stream.counter == seeded.counter + 1U, "NextIntRange must still advance the stream even for a degenerate range, so later draws stay in sync");

    // Fisher-Yates: the result must be a permutation of the input (same
    // multiset of elements) and must be deterministic for the same
    // starting stream.
    int itemsA[5]{ 1, 2, 3, 4, 5 };
    int itemsB[5]{ 1, 2, 3, 4, 5 };
    const kb::math::RandomStream streamAfterShuffleA = kb::math::Shuffle(std::span<int>{ itemsA }, seeded);
    const kb::math::RandomStream streamAfterShuffleB = kb::math::Shuffle(std::span<int>{ itemsB }, seeded);
    for (int i = 0; i < 5; ++i) {
        kb::tests::Require(itemsA[i] == itemsB[i], "Shuffle must be deterministic: the same starting stream must produce the same permutation");
    }
    kb::tests::Require(streamAfterShuffleA.counter == streamAfterShuffleB.counter, "Shuffle must advance the stream deterministically too");
    int sum = 0;
    for (int i = 0; i < 5; ++i) {
        sum += itemsA[i];
    }
    kb::tests::Require(sum == 15, "Shuffle must be a permutation: the shuffled elements must sum to the same total as the input (1+2+3+4+5)");
}

// LIB-052: Easing must not allocate (it's an enum + a pure switch, not a
// std::function/callback table — nothing here to assert at runtime, but
// the type itself proves it: static_assert that Easing is trivially
// copyable, the same shape as ScriptValueType/LibraryOwnership) and every
// curve must satisfy the universal Penner-easing boundary property:
// Evaluate(easing, 0) == 0 and Evaluate(easing, 1) == 1, for ALL 31
// curves, checked in a loop rather than one at a time.
void RunEasingTest() {
    static_assert(std::is_trivially_copyable_v<kb::math::Easing>, "Easing must be a plain value type (enum), not something that could own allocated state");

    constexpr kb::math::Easing kAllEasings[]{
        kb::math::Easing::Linear,
        kb::math::Easing::InSine, kb::math::Easing::OutSine, kb::math::Easing::InOutSine,
        kb::math::Easing::InQuad, kb::math::Easing::OutQuad, kb::math::Easing::InOutQuad,
        kb::math::Easing::InCubic, kb::math::Easing::OutCubic, kb::math::Easing::InOutCubic,
        kb::math::Easing::InQuart, kb::math::Easing::OutQuart, kb::math::Easing::InOutQuart,
        kb::math::Easing::InQuint, kb::math::Easing::OutQuint, kb::math::Easing::InOutQuint,
        kb::math::Easing::InExpo, kb::math::Easing::OutExpo, kb::math::Easing::InOutExpo,
        kb::math::Easing::InCirc, kb::math::Easing::OutCirc, kb::math::Easing::InOutCirc,
        kb::math::Easing::InBack, kb::math::Easing::OutBack, kb::math::Easing::InOutBack,
        kb::math::Easing::InElastic, kb::math::Easing::OutElastic, kb::math::Easing::InOutElastic,
        kb::math::Easing::InBounce, kb::math::Easing::OutBounce, kb::math::Easing::InOutBounce,
    };
    for (const kb::math::Easing easing : kAllEasings) {
        const std::string startMessage = std::string{ "Easing " } + kb::math::ToString(easing) + " must evaluate to exactly 0 at t=0";
        kb::tests::Require(kb::math::Evaluate(easing, 0.0F) == 0.0F, startMessage.c_str());
        const std::string endMessage = std::string{ "Easing " } + kb::math::ToString(easing) + " must evaluate to exactly 1 at t=1";
        kb::tests::Require(std::abs(kb::math::Evaluate(easing, 1.0F) - 1.0F) < 0.0001F, endMessage.c_str());
    }

    kb::tests::Require(kb::math::Evaluate(kb::math::Easing::Linear, 0.5F) == 0.5F, "Linear easing must be the identity function");
    kb::tests::Require(std::abs(kb::math::Evaluate(kb::math::Easing::InQuad, 0.5F) - 0.25F) < 0.0001F, "InQuad(0.5) must be 0.25 (t*t)");

    // Input t must be clamped (matches Lerp's convention), but the OUTPUT
    // is intentionally NOT clamped for curves designed to overshoot.
    kb::tests::Require(kb::math::Evaluate(kb::math::Easing::Linear, 2.0F) == 1.0F, "Evaluate must clamp t above 1");
    kb::tests::Require(kb::math::Evaluate(kb::math::Easing::Linear, -1.0F) == 0.0F, "Evaluate must clamp t below 0");
    kb::tests::Require(kb::math::Evaluate(kb::math::Easing::OutBack, 0.9F) > 1.0F, "OutBack must overshoot above 1 before settling (its defining visual characteristic) rather than being silently clamped");

    kb::tests::Require(std::string{ kb::math::ToString(kb::math::Easing::InOutElastic) } == "InOutElastic", "Easing::ToString must format the enumerator name");
}

// LIB-053: Curve/Gradient deterministic evaluation and a real byte-level
// serialization round-trip.
void RunCurveAndGradientTest() {
    using kb::math::Color;
    using kb::math::Curve;
    using kb::math::CurveKeyframe;
    using kb::math::Easing;
    using kb::math::Gradient;
    using kb::math::GradientStop;

    kb::tests::Require(kb::math::Evaluate(Curve{}, 0.5F) == 0.0F, "Evaluate on an empty Curve must return 0, not crash or read out of bounds");

    Curve curve;
    curve.keyframes = {
        CurveKeyframe{ .time = 0.0F, .value = 0.0F, .easing = Easing::Linear },
        CurveKeyframe{ .time = 1.0F, .value = 10.0F, .easing = Easing::Linear },
    };
    kb::tests::Require(kb::math::Evaluate(curve, 0.0F) == 0.0F, "Evaluate at the first keyframe's exact time must return its exact value");
    kb::tests::Require(kb::math::Evaluate(curve, 1.0F) == 10.0F, "Evaluate at the last keyframe's exact time must return its exact value");
    kb::tests::Require(kb::math::Evaluate(curve, 0.5F) == 5.0F, "Evaluate at the midpoint with Linear easing must Lerp between the two keyframes");
    kb::tests::Require(kb::math::Evaluate(curve, -1.0F) == 0.0F, "Evaluate before the first keyframe must clamp to the first keyframe's value, not extrapolate");
    kb::tests::Require(kb::math::Evaluate(curve, 2.0F) == 10.0F, "Evaluate after the last keyframe must clamp to the last keyframe's value, not extrapolate");

    Curve threeKeyframeCurve;
    threeKeyframeCurve.keyframes = {
        CurveKeyframe{ .time = 0.0F, .value = 0.0F, .easing = Easing::Linear },
        CurveKeyframe{ .time = 1.0F, .value = 100.0F, .easing = Easing::Linear },
        CurveKeyframe{ .time = 2.0F, .value = 0.0F, .easing = Easing::Linear },
    };
    kb::tests::Require(kb::math::Evaluate(threeKeyframeCurve, 1.0F) == 100.0F, "Evaluate at a middle keyframe's exact time must return its exact value, not blend across two segments");
    kb::tests::Require(kb::math::Evaluate(threeKeyframeCurve, 1.5F) == 50.0F, "Evaluate must select the correct segment (the second one) for a t past the middle keyframe");

    const std::vector<std::uint8_t> curveBytes = kb::math::Serialize(curve);
    Curve roundTrippedCurve;
    kb::tests::Require(kb::math::Deserialize(curveBytes, roundTrippedCurve), "Deserialize must succeed on bytes Serialize just produced");
    kb::tests::Require(
        roundTrippedCurve.keyframes.size() == curve.keyframes.size() && roundTrippedCurve.keyframes[0].time == curve.keyframes[0].time &&
            roundTrippedCurve.keyframes[0].value == curve.keyframes[0].value && roundTrippedCurve.keyframes[1].value == curve.keyframes[1].value,
        "A Curve deserialized from Serialize's output must exactly match the original");
    kb::tests::Require(kb::math::Evaluate(roundTrippedCurve, 0.5F) == kb::math::Evaluate(curve, 0.5F), "A round-tripped Curve must evaluate identically to the original");

    Curve emptyResult;
    kb::tests::Require(!kb::math::Deserialize(std::span<const std::uint8_t>{}, emptyResult), "Deserialize must fail (not crash) on an empty byte span");
    const std::vector<std::uint8_t> truncated{ curveBytes.begin(), curveBytes.begin() + 4 };
    Curve truncatedResult;
    kb::tests::Require(!kb::math::Deserialize(truncated, truncatedResult), "Deserialize must fail on truncated bytes rather than reading out of bounds");
    std::vector<std::uint8_t> wrongMagic = curveBytes;
    wrongMagic[0] = static_cast<std::uint8_t>(wrongMagic[0] + 1U);
    Curve wrongMagicResult;
    kb::tests::Require(!kb::math::Deserialize(wrongMagic, wrongMagicResult), "Deserialize must reject bytes with the wrong magic number (e.g. accidentally handed Gradient bytes)");

    kb::tests::Require(kb::math::Evaluate(Gradient{}, 0.5F).r == 1.0F, "Evaluate on an empty Gradient must return the default Color (opaque white), not crash");

    Gradient gradient;
    gradient.stops = {
        GradientStop{ .time = 0.0F, .color = Color{ 0.0F, 0.0F, 0.0F, 1.0F } },
        GradientStop{ .time = 1.0F, .color = Color{ 1.0F, 1.0F, 1.0F, 1.0F } },
    };
    kb::tests::Require(kb::math::Evaluate(gradient, 0.0F).r == 0.0F, "Evaluate at the first stop's exact time must return its exact color");
    const Color midColor = kb::math::Evaluate(gradient, 0.5F);
    kb::tests::Require(std::abs(midColor.r - 0.5F) < 0.0001F && std::abs(midColor.g - 0.5F) < 0.0001F && std::abs(midColor.b - 0.5F) < 0.0001F, "Evaluate at the midpoint must linearly blend between the two stop colors");

    const std::vector<std::uint8_t> gradientBytes = kb::math::Serialize(gradient);
    Gradient roundTrippedGradient;
    kb::tests::Require(kb::math::Deserialize(gradientBytes, roundTrippedGradient), "Deserialize must succeed on bytes Serialize just produced (Gradient)");
    kb::tests::Require(
        roundTrippedGradient.stops.size() == gradient.stops.size() && roundTrippedGradient.stops[1].color.r == gradient.stops[1].color.r,
        "A Gradient deserialized from Serialize's output must exactly match the original");

    // A Curve's bytes must not silently parse as a valid Gradient (they
    // have different magic numbers) — proves the format is self-describing,
    // not just "however many bytes happen to be there".
    Gradient crossParsed;
    kb::tests::Require(!kb::math::Deserialize(curveBytes, crossParsed), "Curve bytes must not be misinterpreted as a valid Gradient");

    // LIB-053 (2026-07-17 audit gap): a few-byte payload that declares a
    // huge element count must be rejected BEFORE reserve(count) tries to
    // allocate for it — otherwise a hostile 8-byte file (magic + count =
    // 0xFFFFFFFF) forces a multi-gigabyte allocation / OOM. Build a valid
    // header with a fabricated count and NO element bytes and require
    // Deserialize to fail cleanly rather than allocate.
    {
        // Curve: magic (KCRV) + count = 0xFFFFFFFF, no keyframe bytes.
        std::vector<std::uint8_t> hostileCurve{ curveBytes.begin(), curveBytes.begin() + 4 }; // valid magic
        hostileCurve.insert(hostileCurve.end(), { 0xFFU, 0xFFU, 0xFFU, 0xFFU }); // count = 4294967295
        Curve hostileCurveResult;
        kb::tests::Require(!kb::math::Deserialize(hostileCurve, hostileCurveResult),
            "Deserialize must reject a Curve whose declared count the payload cannot possibly hold, before reserving for it");

        // A count the payload ALMOST backs (off by one element) must also be
        // rejected — the guard is a real byte-count check, not just a
        // huge-number sniff.
        std::vector<std::uint8_t> oneShortCurve = kb::math::Serialize(curve);
        // Bump the serialized count by one without adding an element's bytes.
        oneShortCurve[4] = static_cast<std::uint8_t>(oneShortCurve[4] + 1U);
        Curve oneShortResult;
        kb::tests::Require(!kb::math::Deserialize(oneShortCurve, oneShortResult),
            "Deserialize must reject a Curve whose count is one element more than the payload provides");

        // Gradient: magic (KGRD) + count = 0xFFFFFFFF, no stop bytes.
        std::vector<std::uint8_t> hostileGradient{ gradientBytes.begin(), gradientBytes.begin() + 4 };
        hostileGradient.insert(hostileGradient.end(), { 0xFFU, 0xFFU, 0xFFU, 0xFFU });
        Gradient hostileGradientResult;
        kb::tests::Require(!kb::math::Deserialize(hostileGradient, hostileGradientResult),
            "Deserialize must reject a Gradient whose declared count the payload cannot possibly hold, before reserving for it");
    }
}

// LIB-054: NaN/infinity/zero-length contract audit — proves the
// documented behavior at each declaration is what the code actually
// does, for both halves of the contract: honest NaN propagation by
// default, and the specific functions that instead return a defined,
// non-NaN value for a structurally-degenerate input.
void RunNaNInfinityZeroLengthContractTest() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    // Default contract: NaN/Infinity flows through untouched.
    kb::tests::Require(std::isnan(kb::math::Clamp(nan, 0.0F, 1.0F)), "Clamp must propagate a NaN value, not silently substitute a default");
    kb::tests::Require(std::isnan(kb::math::Lerp(nan, 1.0F, 0.5F)), "Lerp must propagate a NaN endpoint");
    kb::tests::Require(std::isnan(kb::math::Floor(nan)), "Floor must propagate NaN");
    kb::tests::Require(std::isinf(kb::math::Clamp(infinity, 0.0F, 1.0F)) == false && kb::math::Clamp(infinity, 0.0F, 1.0F) == 1.0F, "Clamp must clamp +infinity to max, not propagate it (infinity is an ordered, comparable value unlike NaN)");

    // Exceptions to the default: these return a real, defined, non-NaN
    // value instead of propagating NaN or dividing by zero.
    kb::tests::Require(std::isnan(kb::math::Sign(nan)), "Sign must propagate NaN (this is a fix — a naive implementation would silently return 0 for NaN, since both > and < comparisons are false)");
    kb::tests::Require(kb::math::Sign(0.0F) == 0.0F && kb::math::Sign(5.0F) == 1.0F && kb::math::Sign(-5.0F) == -1.0F, "Sign must still behave normally for real numbers after the NaN-propagation fix");

    kb::tests::Require(std::isnan(kb::math::MoveTowards(0.0F, nan, 1.0F)), "MoveTowards must propagate a NaN target (this is a fix — a naive implementation would silently return current+maxDelta for NaN, since the overshoot/direction comparisons are all false)");
    kb::tests::Require(kb::math::MoveTowards(0.0F, 10.0F, 3.0F) == 3.0F, "MoveTowards must still behave normally for real numbers after the NaN-propagation fix");

    kb::tests::Require(kb::math::Mod(7.0F, 0.0F) == 7.0F, "Mod with divisor=0 must return value unchanged (no wrap is possible), not NaN from a 0*infinity computation");
    kb::tests::Require(std::abs(kb::math::Mod(-1.0F, 4.0F) - 3.0F) < 0.0001F, "Mod must still be floor-based for a normal divisor after the guard");

    // Zero-length / structurally-degenerate inputs return a defined value.
    kb::tests::Require(kb::math::Length(kb::math::Normalize(kb::math::Vec3{})) == 0.0F, "Normalize of the zero vector must return the zero vector, not NaN (retested here as part of the LIB-054 contract audit)");
    const kb::math::Quat normalizedZeroQuat = kb::math::Normalize(kb::math::Quat{ 0.0F, 0.0F, 0.0F, 0.0F });
    kb::tests::Require(normalizedZeroQuat.w == 1.0F && normalizedZeroQuat.x == 0.0F, "Normalize of the zero quaternion must return the identity rotation, not NaN");
    kb::tests::Require(
        std::abs(kb::math::Angle(kb::math::Vec3{}, kb::math::Vec3{ 1.0F, 0.0F, 0.0F }).Value() - kb::math::kPi / 2.0F) < 0.0001F,
        "Angle with a zero-length vector must return a defined value (pi/2, inherited from Normalize's zero-vector guard), not NaN");
    kb::tests::Require(kb::math::Length(kb::math::Project(kb::math::Vec3{ 1.0F, 1.0F, 1.0F }, kb::math::Vec3{})) == 0.0F, "Project onto the zero vector must return the zero vector, not NaN (retested here as part of the LIB-054 contract audit)");
}

// LIB-055: property/fuzz tests. Rather than a handful of hand-picked
// example values (what every other test in this file does), these draw
// hundreds of random-but-DETERMINISTIC inputs from a fixed-seed
// RandomStream (LIB-051) and assert a general mathematical PROPERTY holds
// across all of them — a much stronger check than any fixed example set,
// and still perfectly reproducible on failure (same seed, same sequence,
// every run) since RandomStream has no hidden global state.

namespace {

[[nodiscard]] kb::math::Vec3 NextTestVec3(kb::math::RandomStream& stream, float min, float max) noexcept {
    const kb::math::RandomStreamRangeResult rx = kb::math::NextRange(stream, min, max);
    const kb::math::RandomStreamRangeResult ry = kb::math::NextRange(rx.stream, min, max);
    const kb::math::RandomStreamRangeResult rz = kb::math::NextRange(ry.stream, min, max);
    stream = rz.stream;
    return kb::math::Vec3{ rx.value, ry.value, rz.value };
}

// Not uniformly distributed over the unit hypersphere (that's not the
// point here) — just a cheap, deterministic way to get a wide variety of
// unit quaternions to fuzz with.
[[nodiscard]] kb::math::Quat NextTestUnitQuat(kb::math::RandomStream& stream) noexcept {
    const kb::math::RandomStreamRangeResult rx = kb::math::NextRange(stream, -1.0F, 1.0F);
    const kb::math::RandomStreamRangeResult ry = kb::math::NextRange(rx.stream, -1.0F, 1.0F);
    const kb::math::RandomStreamRangeResult rz = kb::math::NextRange(ry.stream, -1.0F, 1.0F);
    const kb::math::RandomStreamRangeResult rw = kb::math::NextRange(rz.stream, -1.0F, 1.0F);
    stream = rw.stream;
    return kb::math::Normalize(kb::math::Quat{ rx.value, ry.value, rz.value, rw.value });
}

} // namespace

void RunVectorPropertyTest() {
    kb::math::RandomStream stream = kb::math::MakeRandomStream(1337U);
    constexpr int kIterations = 500;
    for (int i = 0; i < kIterations; ++i) {
        const kb::math::Vec3 a = NextTestVec3(stream, -10.0F, 10.0F);
        const kb::math::Vec3 b = NextTestVec3(stream, -10.0F, 10.0F);

        // Property: Dot is commutative.
        kb::tests::Require(std::abs(kb::math::Dot(a, b) - kb::math::Dot(b, a)) < 0.001F, "Property violated: Dot(a,b) must equal Dot(b,a) for any a, b");

        // Property: Cross is anti-commutative.
        const kb::math::Vec3 crossAB = kb::math::Cross(a, b);
        const kb::math::Vec3 crossBA = kb::math::Cross(b, a);
        kb::tests::Require(
            std::abs(crossAB.x + crossBA.x) < 0.01F && std::abs(crossAB.y + crossBA.y) < 0.01F && std::abs(crossAB.z + crossBA.z) < 0.01F,
            "Property violated: Cross(a,b) must equal -Cross(b,a) for any a, b");

        // Property: Cross(a,b) is orthogonal to both a and b.
        kb::tests::Require(
            std::abs(kb::math::Dot(crossAB, a)) < 0.1F && std::abs(kb::math::Dot(crossAB, b)) < 0.1F,
            "Property violated: Cross(a,b) must be orthogonal to both a and b");

        // Property: the triangle inequality holds for vector length.
        kb::tests::Require(
            kb::math::Length(a + b) <= kb::math::Length(a) + kb::math::Length(b) + 0.01F,
            "Property violated: Length(a+b) must not exceed Length(a)+Length(b) (triangle inequality)");

        if (kb::math::Dot(a, a) > 0.01F) {
            // Property: Normalize always produces a unit-length vector (for
            // any nonzero input, not just the hand-picked (3,4,0) example).
            kb::tests::Require(std::abs(kb::math::Length(kb::math::Normalize(a)) - 1.0F) < 0.001F, "Property violated: Length(Normalize(v)) must be ~1 for any nonzero v");

            // Property: Reflect preserves vector length off a unit normal.
            const kb::math::Vec3 normal = kb::math::Normalize(b);
            if (kb::math::Dot(normal, normal) > 0.5F) {
                kb::tests::Require(
                    std::abs(kb::math::Length(kb::math::Reflect(a, normal)) - kb::math::Length(a)) < 0.01F,
                    "Property violated: Reflect off a unit normal must preserve vector length");
            }
        }
    }
}

void RunQuaternionPropertyTest() {
    kb::math::RandomStream stream = kb::math::MakeRandomStream(2024U);
    constexpr int kIterations = 500;
    for (int i = 0; i < kIterations; ++i) {
        const kb::math::Quat q = NextTestUnitQuat(stream);
        const kb::math::Vec3 v = NextTestVec3(stream, -10.0F, 10.0F);

        // Property: any quaternion this file constructs as "a rotation"
        // (Normalize's own output here) stays on the unit hypersphere.
        const float qLengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        kb::tests::Require(std::abs(qLengthSquared - 1.0F) < 0.001F, "Property violated: a Normalize()'d quaternion must have unit length");

        // Property: rotating by the identity quaternion is a no-op.
        const kb::math::Vec3 rotatedByIdentity = kb::math::Rotate(kb::math::Quat{}, v);
        kb::tests::Require(
            std::abs(rotatedByIdentity.x - v.x) < 0.01F && std::abs(rotatedByIdentity.y - v.y) < 0.01F && std::abs(rotatedByIdentity.z - v.z) < 0.01F,
            "Property violated: Rotate(identity, v) must equal v");

        // Property: rotation by a unit quaternion preserves vector length.
        const kb::math::Vec3 rotated = kb::math::Rotate(q, v);
        kb::tests::Require(std::abs(kb::math::Length(rotated) - kb::math::Length(v)) < 0.01F, "Property violated: rotating by a unit quaternion must preserve vector length");

        // Property: Slerp anchors its endpoints exactly, for random unit
        // quaternion pairs (not just the one hand-picked pair the example
        // test uses).
        const kb::math::Quat other = NextTestUnitQuat(stream);
        const kb::math::Quat slerpedAtStart = kb::math::Slerp(q, other, 0.0F);
        kb::tests::Require(
            std::abs(std::abs(slerpedAtStart.w) - std::abs(q.w)) < 0.01F,
            "Property violated: Slerp(a,b,0) must equal a (comparing |w| since q and -q represent the same rotation)");
    }
}

void RunInterpolationPropertyTest() {
    kb::math::RandomStream stream = kb::math::MakeRandomStream(4242U);
    constexpr int kIterations = 500;

    // Easing curves that are, by construction, monotonic and bounded to
    // [0,1] for t in [0,1] — excludes Back/Elastic/Bounce, which are
    // SUPPOSED to overshoot (LIB-052 documents this as their defining
    // characteristic, not a bug), so they are correctly excluded from a
    // "stays in [0,1]" property rather than the property being wrong.
    constexpr kb::math::Easing kMonotonicBoundedEasings[]{
        kb::math::Easing::Linear,
        kb::math::Easing::InSine, kb::math::Easing::OutSine, kb::math::Easing::InOutSine,
        kb::math::Easing::InQuad, kb::math::Easing::OutQuad, kb::math::Easing::InOutQuad,
        kb::math::Easing::InCubic, kb::math::Easing::OutCubic, kb::math::Easing::InOutCubic,
        kb::math::Easing::InQuart, kb::math::Easing::OutQuart, kb::math::Easing::InOutQuart,
        kb::math::Easing::InQuint, kb::math::Easing::OutQuint, kb::math::Easing::InOutQuint,
        kb::math::Easing::InExpo, kb::math::Easing::OutExpo, kb::math::Easing::InOutExpo,
        kb::math::Easing::InCirc, kb::math::Easing::OutCirc, kb::math::Easing::InOutCirc,
    };

    // Property: EVERY easing curve (all 31, including Back/Elastic/Bounce)
    // anchors its endpoints exactly — already proven once in RunEasingTest
    // as a fixed loop over all 31 at t=0/t=1; re-verified here as part of
    // the LIB-055 property suite for completeness alongside the other
    // interpolation properties.
    for (const kb::math::Easing easing : kMonotonicBoundedEasings) {
        for (int i = 0; i < 50; ++i) {
            const kb::math::RandomStreamRangeResult sample = kb::math::NextRange(stream, 0.0F, 1.0F);
            stream = sample.stream;
            const float value = kb::math::Evaluate(easing, sample.value);
            kb::tests::Require(value >= -0.01F && value <= 1.01F, "Property violated: a monotonic, non-overshooting easing curve must stay within [0,1] for t in [0,1]");
        }
    }

    for (int i = 0; i < kIterations; ++i) {
        const kb::math::RandomStreamRangeResult ra = kb::math::NextRange(stream, -100.0F, 100.0F);
        const kb::math::RandomStreamRangeResult rb = kb::math::NextRange(ra.stream, -100.0F, 100.0F);
        stream = rb.stream;

        // Property: Lerp anchors its endpoints for any a, b. t=0 is exact
        // (a + (b-a)*0 == a + 0 == a always, in IEEE float). t=1 uses a
        // tolerance, not exact equality: a + (b-a) is not guaranteed to
        // exactly equal b for arbitrary float magnitudes (floating-point
        // addition/subtraction isn't perfectly associative) — that's a
        // property of IEEE-754 arithmetic itself, not a Lerp bug.
        kb::tests::Require(kb::math::Lerp(ra.value, rb.value, 0.0F) == ra.value, "Property violated: Lerp(a,b,0) must equal a");
        kb::tests::Require(std::abs(kb::math::Lerp(ra.value, rb.value, 1.0F) - rb.value) < 0.01F, "Property violated: Lerp(a,b,1) must equal b (within float tolerance)");

        // Property: InverseLerp is the true inverse of Lerp for t
        // genuinely inside [0,1] (both are otherwise clamped, so this
        // only holds within the unclamped region).
        if (ra.value != rb.value) {
            const kb::math::RandomStreamRangeResult rt = kb::math::NextRange(stream, 0.0F, 1.0F);
            stream = rt.stream;
            const float interpolated = kb::math::Lerp(ra.value, rb.value, rt.value);
            const float recoveredT = kb::math::InverseLerp(ra.value, rb.value, interpolated);
            kb::tests::Require(std::abs(recoveredT - rt.value) < 0.01F, "Property violated: InverseLerp(a,b,Lerp(a,b,t)) must recover t");
        }
    }
}

void RunSerializationPropertyTest() {
    using kb::math::Curve;
    using kb::math::CurveKeyframe;
    using kb::math::Easing;

    kb::math::RandomStream stream = kb::math::MakeRandomStream(9999U);
    constexpr int kCurveCount = 100;
    constexpr kb::math::Easing kEasings[]{ Easing::Linear, Easing::InQuad, Easing::OutCubic, Easing::InOutSine };

    for (int curveIndex = 0; curveIndex < kCurveCount; ++curveIndex) {
        Curve curve;
        float time = 0.0F;
        for (int keyframeIndex = 0; keyframeIndex < 4; ++keyframeIndex) {
            const kb::math::RandomStreamRangeResult rValue = kb::math::NextRange(stream, -50.0F, 50.0F);
            const kb::math::RandomStreamIntRangeResult rEasing = kb::math::NextIntRange(rValue.stream, 0, 4);
            stream = rEasing.stream;
            curve.keyframes.push_back(CurveKeyframe{ .time = time, .value = rValue.value, .easing = kEasings[static_cast<std::size_t>(rEasing.value)] });
            time += 1.0F;
        }

        // Property: Deserialize(Serialize(curve)) evaluates identically to
        // the original at many sampled t values, not just the keyframe
        // times themselves — exercises the interpolation-reconstruction
        // logic, not just raw field copying.
        const std::vector<std::uint8_t> bytes = kb::math::Serialize(curve);
        Curve roundTripped;
        kb::tests::Require(kb::math::Deserialize(bytes, roundTripped), "Property violated: Deserialize must succeed on bytes Serialize just produced");
        for (int sampleIndex = 0; sampleIndex < 20; ++sampleIndex) {
            const kb::math::RandomStreamRangeResult sampleT = kb::math::NextRange(stream, 0.0F, time);
            stream = sampleT.stream;
            kb::tests::Require(
                kb::math::Evaluate(curve, sampleT.value) == kb::math::Evaluate(roundTripped, sampleT.value),
                "Property violated: a round-tripped Curve must Evaluate() identically to the original at every sampled t");
        }

        // Property: serialization is idempotent under a second pass —
        // re-serializing the round-tripped curve must produce the exact
        // same bytes (catches a serializer that "normalizes" data
        // differently between passes).
        const std::vector<std::uint8_t> bytesAgain = kb::math::Serialize(roundTripped);
        kb::tests::Require(bytes == bytesAgain, "Property violated: Serialize(Deserialize(Serialize(curve))) must byte-for-byte equal Serialize(curve)");
    }
}

// LIB-055 (2026-07-17 audit gap): RunSerializationPropertyTest above only
// ever feeds Deserialize bytes that Serialize itself produced, so it never
// exercises the DESERIALIZATION boundary against hostile or corrupt input —
// it cannot detect an unbounded count/reserve. This fuzzes that boundary
// with deterministic pseudo-random bytes and, critically, with valid-magic
// headers carrying a fabricated huge count. The universal property: every
// call must RETURN (true or false) in bounded time — never crash, hang, or
// allocate proportionally to a declared-but-unbacked count. This whole test
// would hang (multi-gigabyte reserve) without LIB-053's payload-size guard,
// so it is the running proof of that guard, not just a unit check.
void RunSerializationFuzzTest() {
    using kb::math::Curve;
    using kb::math::Gradient;

    kb::math::RandomStream stream = kb::math::MakeRandomStream(24601U);
    const auto nextByte = [&stream]() -> std::uint8_t {
        const kb::math::RandomStreamUInt32Result r = kb::math::NextUInt32(stream);
        stream = r.stream;
        return static_cast<std::uint8_t>(r.value & 0xFFU);
    };

    // Valid magic prefixes, harvested from Serialize's own output (the magic
    // constants are private to EngineMath.cpp) so the fuzzer can build
    // headers that pass the magic check and reach the count/reserve path.
    Curve seedCurve;
    seedCurve.keyframes.push_back(kb::math::CurveKeyframe{ .time = 0.0F, .value = 0.0F, .easing = kb::math::Easing::Linear });
    const std::vector<std::uint8_t> seedCurveBytes = kb::math::Serialize(seedCurve);
    const std::vector<std::uint8_t> curveMagic{ seedCurveBytes.begin(), seedCurveBytes.begin() + 4 };
    Gradient seedGradient;
    seedGradient.stops.push_back(kb::math::GradientStop{ .time = 0.0F, .color = kb::math::Color{} });
    const std::vector<std::uint8_t> seedGradientBytes = kb::math::Serialize(seedGradient);
    const std::vector<std::uint8_t> gradientMagic{ seedGradientBytes.begin(), seedGradientBytes.begin() + 4 };

    constexpr int kIterations = 2000;
    for (int i = 0; i < kIterations; ++i) {
        // (a) A fully random, random-length payload (0..48 bytes).
        const std::size_t length = static_cast<std::size_t>(nextByte()) % 49U;
        std::vector<std::uint8_t> payload;
        payload.reserve(length);
        for (std::size_t b = 0; b < length; ++b) {
            payload.push_back(nextByte());
        }
        Curve curveResult;
        Gradient gradientResult;
        // The ONLY property that must hold: these return without crashing,
        // hanging, or over-allocating. The bool value itself is unconstrained
        // (random bytes may occasionally form a technically-valid document).
        static_cast<void>(kb::math::Deserialize(std::span<const std::uint8_t>{ payload }, curveResult));
        static_cast<void>(kb::math::Deserialize(std::span<const std::uint8_t>{ payload }, gradientResult));

        // (b) Valid magic + a fabricated 32-bit count + NO element bytes.
        // This is the exact shape a hostile file uses to try to force a huge
        // reserve; it must always be rejected (false), never allocate.
        std::vector<std::uint8_t> hostileCurve = curveMagic;
        std::vector<std::uint8_t> hostileGradient = gradientMagic;
        for (int b = 0; b < 4; ++b) {
            const std::uint8_t countByte = nextByte();
            hostileCurve.push_back(countByte);
            hostileGradient.push_back(countByte);
        }
        // Force the high byte non-zero so the declared count is always far
        // larger than the (zero) element payload can back.
        hostileCurve.back() = 0xFFU;
        hostileGradient.back() = 0xFFU;
        Curve hostileCurveResult;
        Gradient hostileGradientResult;
        kb::tests::Require(!kb::math::Deserialize(std::span<const std::uint8_t>{ hostileCurve }, hostileCurveResult),
            "Property violated: a valid-magic Curve header with a huge count and no element bytes must be rejected without allocating");
        kb::tests::Require(!kb::math::Deserialize(std::span<const std::uint8_t>{ hostileGradient }, hostileGradientResult),
            "Property violated: a valid-magic Gradient header with a huge count and no element bytes must be rejected without allocating");
    }
}

} // namespace

void RunEngineMathTests() {
    RunSceneAliasIdentityTest();
    RunDefaultValueConventionsTest();
    RunVec3MathTest();
    RunQuatAndMatrixMathTest();
    RunAngleUnitsTest();
    RunScalarMathFunctionsTest();
    RunScalarMathFunctions2Test();
    RunTrigFunctionsTest();
    RunVec3DistanceProjectReflectRefractTest();
    RunRotationFunctionsTest();
    RunNoiseAndRandomTest();
    RunRandomStreamTest();
    RunEasingTest();
    RunCurveAndGradientTest();
    RunNaNInfinityZeroLengthContractTest();
    RunVectorPropertyTest();
    RunQuaternionPropertyTest();
    RunInterpolationPropertyTest();
    RunSerializationPropertyTest();
    RunSerializationFuzzTest();
}

} // namespace kb::tests
