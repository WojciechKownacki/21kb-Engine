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

// Rotation, imaginary xyz + real w. Identity = {0,0,0,1}.
struct Quat {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
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

} // namespace kb::math
