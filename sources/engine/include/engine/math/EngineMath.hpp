#pragma once

namespace kb::math {

// LIB-042: the single canonical math value-type family for the engine.
// kb::scene::Vec3/Quat (TransformComponent.hpp) alias these instead of
// redefining them, and script-facing APIs (ScriptPhysicsApi, ...)
// construct/read these directly instead of keeping a private per-file
// duplicate (this replaced one such duplicate that used to live in
// ScriptPhysicsApi.cpp) — matching this section's "map onto the engine's
// own types, don't create a parallel set of vectors" rule.
//
// None of these are exposed as a single kb::script::ScriptValueType pin:
// ScriptValue is deliberately scalar-only (see its Storage variant's
// static_assert in ScriptValue.hpp), and every existing script API already
// marshals vectors as named scalar pins (originX/originY/originZ, x/y/z,
// ...). LIB-042 formalizes decompose-into-named-scalar-pins as the
// sanctioned Native/Lua/Visual Graph marshalling convention for these
// types rather than adding a composite pin type.

inline constexpr float kPi = 3.14159265358979323846F;

// LIB-044: Radians and Degrees are deliberately distinct types, not a bare
// float with a suffix in its variable name (the pattern this replaces —
// verticalFovDegrees, yawDegrees_, DegreesToRadians(float degrees) — is
// scattered across 6+ independently-reimplemented conversion helpers in
// sources/renderer and sources/editor, none type-checked against each
// other; a caller passing radians where degrees were expected fails
// silently). Construction from a bare float is `explicit` on purpose: it
// forces every call site to name its unit (`Degrees{45.0F}`, not an
// implicit `45.0F`), and there is intentionally no implicit conversion
// between Radians and Degrees — converting requires calling ToRadians/
// ToDegrees explicitly, so "which unit is this" is never silent.
class Radians final {
public:
    Radians() = default;
    explicit constexpr Radians(float value) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr float Value() const noexcept { return value_; }

private:
    float value_ = 0.0F;
};

class Degrees final {
public:
    Degrees() = default;
    explicit constexpr Degrees(float value) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr float Value() const noexcept { return value_; }

private:
    float value_ = 0.0F;
};

[[nodiscard]] constexpr Radians ToRadians(Degrees value) noexcept {
    return Radians{ value.Value() * kPi / 180.0F };
}

[[nodiscard]] constexpr Degrees ToDegrees(Radians value) noexcept {
    return Degrees{ value.Value() * 180.0F / kPi };
}

[[nodiscard]] constexpr Radians operator+(Radians lhs, Radians rhs) noexcept {
    return Radians{ lhs.Value() + rhs.Value() };
}

[[nodiscard]] constexpr Radians operator-(Radians lhs, Radians rhs) noexcept {
    return Radians{ lhs.Value() - rhs.Value() };
}

[[nodiscard]] constexpr Radians operator*(Radians lhs, float scalar) noexcept {
    return Radians{ lhs.Value() * scalar };
}

[[nodiscard]] constexpr bool operator==(Radians lhs, Radians rhs) noexcept {
    return lhs.Value() == rhs.Value();
}

[[nodiscard]] constexpr bool operator!=(Radians lhs, Radians rhs) noexcept {
    return !(lhs == rhs);
}

[[nodiscard]] constexpr bool operator<(Radians lhs, Radians rhs) noexcept {
    return lhs.Value() < rhs.Value();
}

[[nodiscard]] constexpr Degrees operator+(Degrees lhs, Degrees rhs) noexcept {
    return Degrees{ lhs.Value() + rhs.Value() };
}

[[nodiscard]] constexpr Degrees operator-(Degrees lhs, Degrees rhs) noexcept {
    return Degrees{ lhs.Value() - rhs.Value() };
}

[[nodiscard]] constexpr Degrees operator*(Degrees lhs, float scalar) noexcept {
    return Degrees{ lhs.Value() * scalar };
}

[[nodiscard]] constexpr bool operator==(Degrees lhs, Degrees rhs) noexcept {
    return lhs.Value() == rhs.Value();
}

[[nodiscard]] constexpr bool operator!=(Degrees lhs, Degrees rhs) noexcept {
    return !(lhs == rhs);
}

[[nodiscard]] constexpr bool operator<(Degrees lhs, Degrees rhs) noexcept {
    return lhs.Value() < rhs.Value();
}

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Vec4 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

struct IVec2 {
    int x = 0;
    int y = 0;
};

// LIB-043: left-handed, Y-up — the engine's explicit coordinate/rotation
// convention. Previously implicit: every existing bx::mtxLookAt/mtxProj/
// mtxOrtho call site (the renderer's underlying bgfx/bx math, see
// third_party/bgfx.cmake/bx/include/bx/math.h) omits the `_handedness`
// argument and so already used bx's Handedness::Left default; this makes
// that default an explicit, documented engine decision rather than an
// accident of an omitted parameter. Positive rotation around an axis
// follows the left-hand rule (thumb along the axis, fingers curl toward
// the rotation direction). kb::math::Quat/Mat3/Mat4 and every function
// below are defined against this convention.
//
// Rotation, imaginary xyz + real w. Identity = {0,0,0,1}.
struct Quat {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

// LIB-043: 3x3 rotation/scale matrix, column-major (columns[0]/[1]/[2] are
// the transformed X/Y/Z basis vectors) — matches kb::math::Mat4 and
// kb::scene::WorldTransformAffine3x4's existing column-major layout, not a
// second convention.
struct Mat3 {
    Vec3 columns[3]{
        Vec3{ 1.0F, 0.0F, 0.0F },
        Vec3{ 0.0F, 1.0F, 0.0F },
        Vec3{ 0.0F, 0.0F, 1.0F },
    };
};

// LIB-043: 4x4 matrix, column-major (translation lives in columns[3].xyz)
// — the same layout bx::mtx* (the renderer's existing matrix math) and
// kb::scene::WorldTransformAffine3x4 already use, not a parallel
// convention invented for this type.
struct Mat4 {
    Vec4 columns[4]{
        Vec4{ 1.0F, 0.0F, 0.0F, 0.0F },
        Vec4{ 0.0F, 1.0F, 0.0F, 0.0F },
        Vec4{ 0.0F, 0.0F, 1.0F, 0.0F },
        Vec4{ 0.0F, 0.0F, 0.0F, 1.0F },
    };
};

// Linear-space RGBA, intentionally unclamped: HDR values above 1 are
// valid inputs (the renderer decides tone mapping, not this type). Default
// is opaque white — the identity value for multiplicative tinting, not an
// arbitrary zeroed struct.
struct Color {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

// Axis-aligned 2D rectangle: (x, y) is the min corner; width/height extend
// in the positive direction.
struct Rect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

// Axis-aligned bounding box as center + half-extents — matches
// kb::scene::ColliderComponent's existing center/boxSize convention
// (ColliderComponent.hpp) rather than introducing a second, min/max-corner
// AABB convention.
struct Bounds {
    Vec3 center{};
    Vec3 extents{};
};

// origin + direction. direction is expected unit-length by convention
// (not enforced by this type — callers that consume a Ray are responsible
// for normalizing, the same way Physics.Raycast already normalizes its
// decomposed direction pins internally today).
struct Ray {
    Vec3 origin{};
    Vec3 direction{ 0.0F, 0.0F, 1.0F };
};

// normal (expected unit-length by convention) + signed distance from the
// origin along that normal: a point p lies on the plane when
// Dot(normal, p) == distance.
struct Plane {
    Vec3 normal{ 0.0F, 1.0F, 0.0F };
    float distance = 0.0F;
};

// Position + rotation, no scale — the shape World.Spawn/Transform.WorldPose
// (section 6) read and write.
struct Pose {
    Vec3 position{};
    Quat rotation{};
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{ lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 lhs, float rhs) noexcept {
    return Vec3{ lhs.x * rhs, lhs.y * rhs, lhs.z * rhs };
}

[[nodiscard]] constexpr float Dot(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] constexpr Vec3 Abs(Vec3 value) noexcept;
[[nodiscard]] constexpr Vec3 Max(Vec3 lhs, Vec3 rhs) noexcept;
[[nodiscard]] float Length(Vec3 value) noexcept;
[[nodiscard]] Vec3 Normalize(Vec3 value) noexcept;

constexpr Vec3 Abs(Vec3 value) noexcept {
    return Vec3{
        value.x < 0.0F ? -value.x : value.x,
        value.y < 0.0F ? -value.y : value.y,
        value.z < 0.0F ? -value.z : value.z,
    };
}

constexpr Vec3 Max(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{
        lhs.x > rhs.x ? lhs.x : rhs.x,
        lhs.y > rhs.y ? lhs.y : rhs.y,
        lhs.z > rhs.z ? lhs.z : rhs.z,
    };
}

[[nodiscard]] constexpr Vec3 Cross(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

// Hamilton product. lhs*rhs applies rhs first, then lhs — i.e. matches the
// existing parent-composes-child convention (a world rotation is
// `worldRotation = parentRotation * localRotation`, not the reverse).
[[nodiscard]] constexpr Quat operator*(Quat lhs, Quat rhs) noexcept {
    return Quat{
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

[[nodiscard]] Quat Normalize(Quat value) noexcept;

// Rotates `value` by `rotation` (Rodrigues' rotation formula generalized
// to quaternions — exact for any quaternion, not just unit-length ones,
// unlike a formula that substitutes w²+|xyz|²=1).
[[nodiscard]] constexpr Vec3 Rotate(Quat rotation, Vec3 value) noexcept {
    const Vec3 u{ rotation.x, rotation.y, rotation.z };
    const float s = rotation.w;
    return u * (2.0F * Dot(u, value)) + value * (s * s - Dot(u, u)) + Cross(u, value) * (2.0F * s);
}

[[nodiscard]] constexpr Vec4 operator*(const Mat4& lhs, const Vec4& rhs) noexcept {
    return Vec4{
        lhs.columns[0].x * rhs.x + lhs.columns[1].x * rhs.y + lhs.columns[2].x * rhs.z + lhs.columns[3].x * rhs.w,
        lhs.columns[0].y * rhs.x + lhs.columns[1].y * rhs.y + lhs.columns[2].y * rhs.z + lhs.columns[3].y * rhs.w,
        lhs.columns[0].z * rhs.x + lhs.columns[1].z * rhs.y + lhs.columns[2].z * rhs.z + lhs.columns[3].z * rhs.w,
        lhs.columns[0].w * rhs.x + lhs.columns[1].w * rhs.y + lhs.columns[2].w * rhs.z + lhs.columns[3].w * rhs.w,
    };
}

[[nodiscard]] constexpr Mat4 operator*(const Mat4& lhs, const Mat4& rhs) noexcept {
    return Mat4{ {
        lhs * rhs.columns[0],
        lhs * rhs.columns[1],
        lhs * rhs.columns[2],
        lhs * rhs.columns[3],
    } };
}

// Builds the column-major Translate * Rotate * Scale matrix (apply scale,
// then rotation, then translation to a point) — the standard TRS
// composition order kb::scene::TransformComponent's
// position/rotation/scale fields already imply.
[[nodiscard]] Mat4 FromTRS(Vec3 translation, Quat rotation, Vec3 scale) noexcept;

} // namespace kb::math
