#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cmath>
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
}

} // namespace

void RunEngineMathTests() {
    RunSceneAliasIdentityTest();
    RunDefaultValueConventionsTest();
    RunVec3MathTest();
    RunQuatAndMatrixMathTest();
}

} // namespace kb::tests
