#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

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
// LIB-054: a vector shorter than 0.000001 (including the exact zero
// vector) has no well-defined direction, so this returns the zero vector
// instead of a NaN-filled one from dividing by a near-zero length — every
// caller of Normalize (Angle, SignedAngle, FromToRotation, ...) inherits
// this guard rather than needing its own.
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

// LIB-048: Distance/Project/Reflect/Refract — built on the Dot/Cross/
// Length/Normalize this file already defines (LIB-042), not a second,
// independently-derived set of vector formulas.

[[nodiscard]] float Distance(Vec3 a, Vec3 b) noexcept;

// The component of `value` parallel to `onto` (vector projection, not
// scalar projection). Returns the zero vector if `onto` is the zero
// vector, the same zero-length guard Normalize already uses, rather than
// dividing by zero.
[[nodiscard]] Vec3 Project(Vec3 value, Vec3 onto) noexcept;

// Reflects `incident` off a surface with the given `normal` (GLSL's
// `reflect` convention). `normal` is expected unit-length, the same
// convention kb::math::Plane already documents for its own normal field.
[[nodiscard]] constexpr Vec3 Reflect(Vec3 incident, Vec3 normal) noexcept {
    return incident - normal * (2.0F * Dot(incident, normal));
}

// Refracts `incident` through a surface with the given unit `normal` and
// `eta` (ratio of the incident medium's index of refraction to the
// transmitted medium's — GLSL's `refract` convention). Returns the zero
// vector on total internal reflection (when the refraction angle would
// exceed 90 degrees) rather than a NaN-producing sqrt of a negative
// number — mirroring LIB-047's "a real, defined result for the boundary
// case" rule, here at the native level since total internal reflection is
// a legitimate physical outcome, not an invalid input to reject.
[[nodiscard]] Vec3 Refract(Vec3 incident, Vec3 normal, float eta) noexcept;

// LIB-049: the unsigned angle between two vectors (always >= 0). Returns
// Radians (LIB-044/047's convention: a function whose result IS an angle
// returns a typed angle, not a bare float). Internally clamps the dot
// product of the two normalized vectors to [-1,1] before calling Acos —
// unlike LIB-047's Math.Asin/Acos (which reject an out-of-domain value the
// SCRIPT CALLER supplied), this clamp corrects float round-off in an
// internal computation that is mathematically guaranteed to be in
// [-1,1] for genuine unit vectors; it is not hiding a caller error.
//
// LIB-054: a zero-length `a` or `b` inherits Normalize's zero-vector
// guard (Normalize(zero) == zero), so Dot(normalizedA, normalizedB) == 0
// and Angle returns exactly pi/2 (90 degrees) — a defined, non-NaN result,
// not a meaningful geometric answer (a zero vector has no direction to
// measure an angle from). Callers that pass a genuinely zero vector should
// not rely on this value beyond "it's a real number, not NaN".
[[nodiscard]] Radians Angle(Vec3 a, Vec3 b) noexcept;
// Signed angle from a to b around the given reference axis (positive when
// the rotation from a to b is counterclockwise looking down -axis, the
// same left-handed convention this file documents next to Quat).
[[nodiscard]] Radians SignedAngle(Vec3 a, Vec3 b, Vec3 axis) noexcept;

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

// LIB-054: same zero-length guard as Vec3's Normalize — a quaternion
// shorter than 0.000001 (including the exact zero quaternion, which is
// not a valid rotation) returns the identity rotation rather than a
// NaN-filled result from dividing by a near-zero length.
[[nodiscard]] Quat Normalize(Quat value) noexcept;

// LIB-085: negates the imaginary (x,y,z) part, leaves w unchanged. For a
// UNIT quaternion (already true for every rotation this engine composes —
// Compose/Normalize keep world/local rotations normalized) this IS the
// rotation's inverse: the cheap, no-division common case, kept separate
// from Inverse() below (the general form, safe for a non-unit quaternion
// too) the same way this file already keeps Rotate constexpr-cheap
// alongside heavier, non-constexpr operations like Slerp.
[[nodiscard]] constexpr Quat Conjugate(Quat value) noexcept {
    return Quat{ -value.x, -value.y, -value.z, value.w };
}

// The general inverse: Conjugate divided by squared length. Same
// near-zero-length guard convention as Normalize (LIB-054) — a quaternion
// shorter than 0.000001 has no well-defined inverse, so this returns the
// identity rotation rather than a NaN-filled result from dividing by a
// near-zero length. Added for LIB-085's Transform.SetWorldPose (world ->
// local pose back-solve needs to undo a parent's world rotation), the
// first caller in this codebase that needs to invert an arbitrary
// quaternion rather than just compose/rotate with one.
[[nodiscard]] Quat Inverse(Quat value) noexcept;

// Rotates `value` by `rotation` (Rodrigues' rotation formula generalized
// to quaternions — exact for any quaternion, not just unit-length ones,
// unlike a formula that substitutes w²+|xyz|²=1).
[[nodiscard]] constexpr Vec3 Rotate(Quat rotation, Vec3 value) noexcept {
    const Vec3 u{ rotation.x, rotation.y, rotation.z };
    const float s = rotation.w;
    return u * (2.0F * Dot(u, value)) + value * (s * s - Dot(u, u)) + Cross(u, value) * (2.0F * s);
}

// LIB-049: spherical linear interpolation between two rotations. Takes
// the shorter of the two paths around the 4D unit sphere (negating `b`
// when the quaternions are more than 90 degrees apart as 4D points, since
// q and -q represent the same rotation) and falls back to a plain linear
// interpolation when a and b are nearly identical, to avoid dividing by
// a near-zero sin(omega). LIB-054: the result also always passes through
// Normalize before returning, a second safety net that absorbs even a
// literal zero quaternion input into a defined (identity) result.
[[nodiscard]] Quat Slerp(Quat a, Quat b, float t) noexcept;

// Builds the rotation that points local +Z (this file's forward
// convention — see Ray's default direction) along `forward`, with `up` as
// a hint for which way local +Y should point. If `forward` is parallel to
// `up` (a degenerate basis), falls back to an arbitrary up hint rather
// than producing a NaN/zero basis.
[[nodiscard]] Quat LookRotation(Vec3 forward, Vec3 up) noexcept;

// The shortest rotation that takes the direction `from` to the direction
// `to`. `from` and `to` exactly opposite (180 degrees apart) is a
// genuinely underdetermined case (infinitely many valid rotation axes) —
// resolved by picking an arbitrary axis perpendicular to `from`, the same
// resolution the conventional implementation of this uses.
[[nodiscard]] Quat FromToRotation(Vec3 from, Vec3 to) noexcept;

// Rotates from `from` toward `to` by at most `maxDelta`, without
// overshooting (the Quat analog of MoveTowards). Uses the shortest-path
// angle between the two rotations (q and -q are the same rotation, so the
// angle is derived from |Dot(from,to)|, not the raw dot product).
[[nodiscard]] Quat RotateTowards(Quat from, Quat to, Radians maxDelta) noexcept;

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

// LIB-043: Mat3 operations. columns[] are the transformed basis vectors, so
// m * v is the linear combination of the columns weighted by v — the same
// column-major convention Mat4's operator* above uses.
[[nodiscard]] constexpr Vec3 operator*(const Mat3& lhs, Vec3 rhs) noexcept {
    return lhs.columns[0] * rhs.x + lhs.columns[1] * rhs.y + lhs.columns[2] * rhs.z;
}

// Determinant as the scalar triple product of the columns — det = c0 · (c1 ×
// c2). Built on the Dot/Cross this file already defines, not a separately
// transcribed expansion.
[[nodiscard]] constexpr float Determinant(const Mat3& m) noexcept {
    return Dot(m.columns[0], Cross(m.columns[1], m.columns[2]));
}

// The inverse-transpose (M^-1)^T — the "normal matrix" that transforms
// surface normals correctly under a non-uniform scale/shear, where the plain
// matrix would skew them off the surface. For a column matrix [c0 c1 c2],
// (M^-1)^T has columns (c1×c2, c2×c0, c0×c1)/det — derived once from Cross/
// Dot rather than hand-expanding nine cofactors. A singular matrix (|det| ~
// 0) has no well-defined normal matrix, so this returns identity (leaving
// normals unchanged) instead of dividing by zero — the same defined-
// degenerate-result rule Normalize/Intersect follow (LIB-054).
[[nodiscard]] Mat3 InverseTranspose(const Mat3& m) noexcept;

// LIB-045: scalar math foundation — the plain-Float functions LIB-046
// through LIB-049's Vec3/Quat-level operations (Dot, Cross, Slerp, ...)
// build on. These are the same functions `sources/script/ScriptMathApi`
// registers as `Math.Clamp`/`Math.Lerp`/etc for Native/Lua/Visual Graph;
// this header is their native implementation, not a second copy of it.
//
// LIB-054's NaN/infinity/zero-length contract for this whole file:
// - NaN/Infinity INPUT is, by default, allowed to flow through untouched
//   (Clamp/Lerp/InverseLerp/Remap/SmoothStep/Frac/Floor/Ceil/Round/Sqrt/
//   Pow/Exp/Log/Mat4 arithmetic/Easing::Evaluate/Curve-Gradient::Evaluate
//   all do this) — this is the honest, standard IEEE-754 contract, the
//   same "NaN is the documented result of invalid input, not a silent
//   fallback" precedent Asin/Acos already establish for their real
//   restricted domain. A function that does something DIFFERENT (returns
//   a defined non-NaN value instead) documents that choice at its own
//   declaration — see Normalize, Project, Refract, InverseLerp, Mod,
//   Sign, MoveTowards, Damp, Slerp, LookRotation, FromToRotation,
//   NextIntRange below.
// - Zero-length vectors/quaternions and other structurally-degenerate
//   inputs (a zero `onto` in Project, a==b in InverseLerp, divisor==0 in
//   Mod, an empty Curve/Gradient) always return a REAL, defined value —
//   never NaN, never a crash/out-of-bounds read — because these are
//   reachable, unexceptional runtime states (an uninitialized Vec3, an
//   empty authored curve), not caller mistakes to reject the way
//   Asin/Acos reject an out-of-[-1,1] value at the script boundary.

[[nodiscard]] constexpr float Clamp(float value, float min, float max) noexcept {
    return value < min ? min : (value > max ? max : value);
}

// Clamps t to [0,1] before interpolating (matches the game-engine
// convention this API otherwise follows — clamped, not free — so a
// caller can't overshoot past `b` by accident; Remap below is built on
// this and inherits the same clamped behavior).
[[nodiscard]] constexpr float Lerp(float a, float b, float t) noexcept {
    const float clampedT = Clamp(t, 0.0F, 1.0F);
    return a + (b - a) * clampedT;
}

// Inverse of Lerp: returns the t in [0,1] such that Lerp(a, b, t) == value
// (clamped, so a value outside [a,b] reports 0 or 1 rather than
// extrapolating). a == b (zero-width range) returns 0 rather than
// dividing by zero.
[[nodiscard]] constexpr float InverseLerp(float a, float b, float value) noexcept {
    if (a == b) {
        return 0.0F;
    }
    return Clamp((value - a) / (b - a), 0.0F, 1.0F);
}

// Maps value from [inMin, inMax] to [outMin, outMax], built directly on
// InverseLerp+Lerp (one source of truth for the clamping behavior) rather
// than a second, independently-derived formula.
[[nodiscard]] constexpr float Remap(float value, float inMin, float inMax, float outMin, float outMax) noexcept {
    return Lerp(outMin, outMax, InverseLerp(inMin, inMax, value));
}

// Standard smoothstep: 0 at/before edge0, 1 at/after edge1, cubic
// (3t²-2t³) ease in between.
[[nodiscard]] constexpr float SmoothStep(float edge0, float edge1, float x) noexcept {
    const float t = InverseLerp(edge0, edge1, x);
    return t * t * (3.0F - 2.0F * t);
}

// Moves current toward target by at most maxDelta, without overshooting
// past target (a constant step, not a ratio) — unlike Lerp, this is
// frame-rate-independent when maxDelta = speed * deltaTime. LIB-054: a NaN
// `target` (or `current`) is checked and propagated explicitly — without
// that check, `difference < 0.0F` is false for a NaN difference, so the
// unguarded arithmetic below would silently return `current + maxDelta`,
// a concrete, deceptively valid-looking result instead of surfacing the
// invalid input.
[[nodiscard]] constexpr float MoveTowards(float current, float target, float maxDelta) noexcept {
    const float difference = target - current;
    if (difference != difference) {
        return difference;
    }
    if (difference <= maxDelta && difference >= -maxDelta) {
        return target;
    }
    return current + (difference < 0.0F ? -maxDelta : maxDelta);
}

struct DampResult {
    float value = 0.0F;
    float velocity = 0.0F;
};

// Critically-damped spring smoothing (Game Programming Gems 4's
// "critically damped ease-in ease-out" formula).
// `velocity` is state the caller owns and threads back in every call
// (there is no by-reference output across the script boundary — LIB-032
// forbids that — so this returns {value, velocity} instead of taking
// velocity by reference); maxSpeed caps the rate of change (pass a very
// large value for effectively unclamped).
[[nodiscard]] DampResult Damp(float current, float target, float velocity, float smoothTime, float deltaTime, float maxSpeed) noexcept;

// LIB-046: scalar Min/Max/Abs/Sign/Floor/Ceil/Round/Frac/Mod/Sqrt/Pow/Exp/
// Log — the same functions ScriptMathApi registers as `Math.Min`/etc.
// Min/Max/Abs overload the existing Vec3 versions above rather than using
// a different name for the scalar case.

[[nodiscard]] constexpr float Min(float lhs, float rhs) noexcept {
    return lhs < rhs ? lhs : rhs;
}

[[nodiscard]] constexpr float Max(float lhs, float rhs) noexcept {
    return lhs > rhs ? lhs : rhs;
}

[[nodiscard]] constexpr float Abs(float value) noexcept {
    return value < 0.0F ? -value : value;
}

// -1 for negative, 1 for positive, 0 for exactly zero (not "0 counts as
// positive" — a caller multiplying by Sign(0) should get 0, not flip
// direction). LIB-054: propagates NaN rather than silently reporting 0 —
// `value != value` is true only for NaN (a well-known IEEE-754 property),
// checked directly instead of via std::isnan so this stays constexpr; a
// naive `value > 0 ? 1 : (value < 0 ? -1 : 0)` would answer "0" for NaN,
// which looks like a valid, deliberate result instead of the honest "this
// input was invalid" signal every other function in this file gives NaN
// input (Clamp/Lerp/Mat4 math/Frac/Floor/Ceil/Round all let NaN flow
// through unchanged, matching Asin/Acos's "NaN is the native IEEE-754
// contract for invalid input" precedent).
[[nodiscard]] constexpr float Sign(float value) noexcept {
    if (value != value) {
        return value;
    }
    return value > 0.0F ? 1.0F : (value < 0.0F ? -1.0F : 0.0F);
}

[[nodiscard]] float Floor(float value) noexcept;
[[nodiscard]] float Ceil(float value) noexcept;
// Ties round away from zero (std::round's convention), not to-even.
[[nodiscard]] float Round(float value) noexcept;
// Always in [0, 1) — the fractional part after Floor, not after
// truncation (Frac(-1.25) is 0.75, not -0.25).
[[nodiscard]] float Frac(float value) noexcept;
// Floor-based modulo (GLSL's `mod`, not C's `fmod`): the result always has
// the same sign as `divisor` and stays in [0, divisor) for divisor > 0 —
// the convention that makes wrapping an angle or a repeating coordinate
// well-defined for a negative dividend, which C's truncation-based fmod
// is not (fmod(-1, 4) is -1, not 3). LIB-054: divisor == 0 returns value
// unchanged ("no wrap is possible") instead of letting value/0 -> inf and
// 0*inf -> NaN propagate silently — the same "a real, defined result for
// a degenerate divisor" choice InverseLerp already makes for a==b.
[[nodiscard]] float Mod(float value, float divisor) noexcept;
[[nodiscard]] float Sqrt(float value) noexcept;
[[nodiscard]] float Pow(float base, float exponent) noexcept;
[[nodiscard]] float Exp(float value) noexcept;
// Natural logarithm (base e), not base-10 or base-2.
[[nodiscard]] float Log(float value) noexcept;

// LIB-047: trigonometric functions take/return kb::math::Radians (not a
// bare float) — angle unit confusion (passing degrees to a function that
// expects radians) is exactly the bug class LIB-044 exists to prevent,
// and a trig function is the most common place that mistake happens.
// Sin/Cos/Tan accept any angle (their domain is all real radian values,
// including magnitudes past a full turn); Asin/Acos have a genuinely
// restricted input DOMAIN ([-1,1]) — that domain error is defined at the
// script boundary (ScriptMathApi.cpp's Math.Asin/Math.Acos return a real
// ScriptFunctionCallResult error for |value|>1) rather than here, where
// letting std::asin/std::acos's standard IEEE-754 NaN-on-out-of-domain
// behavior through is the honest, well-understood native C++ contract —
// not a silent fallback, since NaN is the documented result of this
// exact input, and every native caller already knows to check for it.
[[nodiscard]] float Sin(Radians angle) noexcept;
[[nodiscard]] float Cos(Radians angle) noexcept;
[[nodiscard]] float Tan(Radians angle) noexcept;
[[nodiscard]] Radians Asin(float value) noexcept;
[[nodiscard]] Radians Acos(float value) noexcept;
[[nodiscard]] Radians Atan(float value) noexcept;
// Two-argument arctangent: resolves the correct quadrant from the signs
// of both y and x (unlike Atan(y/x), which cannot distinguish (1,1) from
// (-1,-1)). Atan2(0, 0) is conventionally 0, not an error.
[[nodiscard]] Radians Atan2(float y, float x) noexcept;

// LIB-050: deterministic, seed-parameterized noise and random — every
// function here is a PURE function of its explicit `seed` (and, for
// Random01, `index`) argument. There is no hidden global generator to
// advance or reset: calling the same function with the same arguments
// twice always returns the same result, and the caller owns the seed the
// same way Damp's caller owns `velocity` — no implicit call-order
// dependency the way a global rand()-style stream would have.

// 32-bit integer hash (a standard bit-mixing finalizer, not
// cryptographic) — the shared building block Noise/Random01 use instead
// of each reimplementing their own scrambling.
[[nodiscard]] constexpr std::uint32_t Hash32(std::int32_t value, std::uint32_t seed) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(value) ^ seed;
    h ^= h >> 16;
    h *= 0x85ebca6bU;
    h ^= h >> 13;
    h *= 0xc2b2ae35U;
    h ^= h >> 16;
    return h;
}

// Deterministic pseudo-random value in [0, 1], purely a function of
// (seed, index) — call it with a different `index` each time (e.g. a
// counter the caller owns) to get a sequence, exactly like Damp's caller
// owns `velocity` between calls.
[[nodiscard]] float Random01(std::uint32_t seed, std::uint32_t index) noexcept;

// Perlin-style gradient (not value) noise: zero at every integer lattice
// point (Noise3D(n,m,k,seed) == 0 for any integers n,m,k, any seed — the
// distance vector from a lattice point to itself is zero, so every
// corner's gradient contributes nothing there), smooth (C2-continuous
// fade curve) in between. Noise1D/Noise2D reuse Noise3D with the unused
// axes pinned to 0 rather than duplicating the algorithm — the fade
// curve's u=v=w=0 term on a pinned-zero axis makes that axis' corners
// collapse to a single value with no contribution from the "next" lattice
// cell along it, i.e. genuinely reduces to lower-dimensional noise, not
// an approximation of it.
[[nodiscard]] float Noise3D(float x, float y, float z, std::uint32_t seed) noexcept;
[[nodiscard]] float Noise2D(float x, float y, std::uint32_t seed) noexcept;
[[nodiscard]] float Noise1D(float x, std::uint32_t seed) noexcept;

// LIB-051: a counter-based pseudo-random stream — {seed, counter} is the
// ENTIRE state, deliberately transparent rather than an opaque handle, so
// "snapshot the state" (the plan's own requirement) is exact and free: a
// snapshot is just a copy of this value type, and restoring it is just
// assigning it back. There is no mutable generator object to advance in
// place; every Next* function takes a RandomStream BY VALUE and returns
// the advanced stream alongside the result (the same {value, newState}
// pattern Damp already established for LIB-032's "no references across
// the script boundary" rule) — the caller re-threads the returned stream
// into its next call, the same way Damp's caller re-threads `velocity`.
struct RandomStream {
    std::uint32_t seed = 0U;
    std::uint32_t counter = 0U;
};

[[nodiscard]] constexpr RandomStream MakeRandomStream(std::uint32_t seed) noexcept {
    return RandomStream{ seed, 0U };
}

struct RandomStreamUInt32Result {
    std::uint32_t value = 0U;
    RandomStream stream;
};

// The raw building block every other Next* function is built on: hashes
// the stream's counter (using the same kb::math::Hash32 LIB-050's
// Random01/Noise already use — one scrambling source, not a second
// independently-tuned one) and advances the counter by one.
[[nodiscard]] constexpr RandomStreamUInt32Result NextUInt32(RandomStream stream) noexcept {
    const std::uint32_t value = Hash32(static_cast<std::int32_t>(stream.counter), stream.seed);
    return RandomStreamUInt32Result{ value, RandomStream{ stream.seed, stream.counter + 1U } };
}

struct RandomStreamFloatResult {
    float value = 0.0F;
    RandomStream stream;
};

// A value in [0, 1).
[[nodiscard]] RandomStreamFloatResult NextFloat01(RandomStream stream) noexcept;

struct RandomStreamRangeResult {
    float value = 0.0F;
    RandomStream stream;
};

// A value in [min, max).
[[nodiscard]] RandomStreamRangeResult NextRange(RandomStream stream, float min, float max) noexcept;

struct RandomStreamIntRangeResult {
    std::int32_t value = 0;
    RandomStream stream;
};

// An integer in [min, max) (max <= min is a degenerate, zero-width range:
// returns min unchanged, but the stream still advances, so a caller
// looping over several ranges — some possibly degenerate — gets the same
// sequence for the following calls regardless of which ranges happened to
// be empty).
[[nodiscard]] RandomStreamIntRangeResult NextIntRange(RandomStream stream, std::int32_t min, std::int32_t max) noexcept;

// Fisher-Yates shuffle, in place. Native-only (a template over an
// arbitrary C++ type, `std::span` never crosses the script boundary) —
// LIB-051's "shuffle" requirement is met here for native callers; a
// script-facing Math.Shuffle needs a script-visible collection type
// (Array<T>, LIB-058, not implemented yet) to shuffle, so that surface is
// intentionally deferred rather than faked with a 0-or-1-element stand-in.
template <typename T>
[[nodiscard]] RandomStream Shuffle(std::span<T> items, RandomStream stream) noexcept {
    for (std::size_t i = items.size(); i > 1U; --i) {
        const RandomStreamIntRangeResult picked = NextIntRange(stream, 0, static_cast<std::int32_t>(i));
        stream = picked.stream;
        std::swap(items[i - 1U], items[static_cast<std::size_t>(picked.value)]);
    }
    return stream;
}

// LIB-052: Easing is a value-type enum + a pure evaluation function
// (kb::script::ScriptValueType, kb::input::InputActionValueType, and
// kb::library::LibraryOwnership are the same pattern already used
// throughout this codebase) — deliberately NOT std::function/a callback
// table, which would allocate on the heap the moment a caller captured
// any state, and is exactly what the plan's "bez allocacji callbacków w
// hot path" forbids. Selecting a curve is a plain enum comparison/switch,
// zero allocation, the same cost as any other enum dispatch already used
// here (ScriptValueType's ToString, etc).
//
// The standard "Robert Penner" easing catalog (widely reproduced under
// that name; https://easings.net is the commonly cited reference) — the
// plan does not name a specific subset, and this is the conventional
// complete set every game engine/animation library of this kind exposes.
enum class Easing : std::uint8_t {
    Linear,
    InSine,
    OutSine,
    InOutSine,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    InQuart,
    OutQuart,
    InOutQuart,
    InQuint,
    OutQuint,
    InOutQuint,
    InExpo,
    OutExpo,
    InOutExpo,
    InCirc,
    OutCirc,
    InOutCirc,
    InBack,
    OutBack,
    InOutBack,
    InElastic,
    OutElastic,
    InOutElastic,
    InBounce,
    OutBounce,
    InOutBounce,
};

[[nodiscard]] const char* ToString(Easing easing) noexcept;

// Evaluates `easing` at `t` (clamped to [0,1] before evaluation, matching
// Lerp's convention — LIB-045). The OUTPUT is intentionally NOT clamped:
// InBack/OutBack/InOutBack and the Elastic family are specifically
// designed to overshoot outside [0,1] (that's the visual "anticipation"/
// "overshoot" effect that makes them useful), so clamping the result
// would silently break the curve's defining characteristic.
[[nodiscard]] float Evaluate(Easing easing, float t) noexcept;

// LIB-042: geometry operations on the Vec2/IVec2/Rect/Plane/Ray value types
// this file already defines — the operations that make those types usable
// data rather than inert structs. Every function here is built on the
// Vec3 Dot/operators above (one source of truth), not a re-derived formula,
// and is the native implementation the Math.* script bindings
// (ScriptMathApi) decompose into scalar pins and call — so these types have
// real consumers on the Native/Lua/Visual Graph path, not orphaned API.

[[nodiscard]] constexpr Vec2 operator+(Vec2 lhs, Vec2 rhs) noexcept {
    return Vec2{ lhs.x + rhs.x, lhs.y + rhs.y };
}

[[nodiscard]] constexpr Vec2 operator-(Vec2 lhs, Vec2 rhs) noexcept {
    return Vec2{ lhs.x - rhs.x, lhs.y - rhs.y };
}

[[nodiscard]] constexpr Vec2 operator*(Vec2 lhs, float rhs) noexcept {
    return Vec2{ lhs.x * rhs, lhs.y * rhs };
}

[[nodiscard]] constexpr float Dot(Vec2 lhs, Vec2 rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

// Same zero-length contract as Vec3's Length (LIB-054): a real, finite
// magnitude for every input, including the zero vector (which returns 0).
[[nodiscard]] float Length(Vec2 value) noexcept;

[[nodiscard]] constexpr IVec2 operator+(IVec2 lhs, IVec2 rhs) noexcept {
    return IVec2{ lhs.x + rhs.x, lhs.y + rhs.y };
}

[[nodiscard]] constexpr IVec2 operator-(IVec2 lhs, IVec2 rhs) noexcept {
    return IVec2{ lhs.x - rhs.x, lhs.y - rhs.y };
}

[[nodiscard]] constexpr bool operator==(IVec2 lhs, IVec2 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] constexpr bool operator!=(IVec2 lhs, IVec2 rhs) noexcept {
    return !(lhs == rhs);
}

// L1 (grid/taxicab) distance between two integer lattice points — the
// natural metric for the tile/grid coordinates IVec2 exists to carry.
[[nodiscard]] constexpr int ManhattanDistance(IVec2 lhs, IVec2 rhs) noexcept {
    const int dx = lhs.x > rhs.x ? lhs.x - rhs.x : rhs.x - lhs.x;
    const int dy = lhs.y > rhs.y ? lhs.y - rhs.y : rhs.y - lhs.y;
    return dx + dy;
}

// A point lies inside the rectangle when it is within [x, x+width) on the
// horizontal axis and [y, y+height) on the vertical — half-open so two
// edge-sharing rects partition the plane without double-counting the seam
// (the same convention DockRect/MaterialGraphCanvas hit-testing already
// use). A rect with zero or negative extent contains nothing.
[[nodiscard]] constexpr bool Contains(const Rect& rect, Vec2 point) noexcept {
    return point.x >= rect.x && point.x < rect.x + rect.width &&
           point.y >= rect.y && point.y < rect.y + rect.height;
}

// Two axis-aligned rectangles overlap when they overlap on BOTH axes.
// Touching-only edges (a.x+a.width == b.x) do not count as an overlap,
// matching Contains' half-open convention.
[[nodiscard]] constexpr bool Intersects(const Rect& lhs, const Rect& rhs) noexcept {
    return lhs.x < rhs.x + rhs.width && rhs.x < lhs.x + lhs.width &&
           lhs.y < rhs.y + rhs.height && rhs.y < lhs.y + lhs.height;
}

// Signed distance from `point` to the plane along its normal: positive on
// the side the normal points toward, negative behind, zero exactly on the
// plane. Uses the same "Dot(normal, p) == distance defines the plane"
// convention documented on the Plane type.
[[nodiscard]] constexpr float SignedDistance(const Plane& plane, Vec3 point) noexcept {
    return Dot(plane.normal, point) - plane.distance;
}

// The point a parameter `t` along the ray: origin + direction * t. With a
// unit-length direction (Ray's documented convention) `t` is a distance.
[[nodiscard]] constexpr Vec3 PointAt(const Ray& ray, float t) noexcept {
    return ray.origin + ray.direction * t;
}

// Result of intersecting a ray with a plane. `hit` is false when the ray
// is parallel to the plane (or points away from it, i.e. a negative `t`) —
// in that case `t`/`point` are left at their defaults rather than holding a
// NaN or a behind-the-origin solution, the same "defined result for the
// degenerate case" rule the rest of this file follows (LIB-047/054).
struct RayPlaneIntersection {
    bool hit = false;
    float t = 0.0F;
    Vec3 point{};
};

[[nodiscard]] RayPlaneIntersection Intersect(const Ray& ray, const Plane& plane) noexcept;

// LIB-053: Curve/Gradient — deterministic keyframe interpolation, reusing
// Easing (LIB-052) as the per-segment interpolation mode rather than
// inventing a second, parallel curve-shape enum.
//
// Keyframes must be sorted by ascending `time` (not enforced by the type
// itself — the same "caller-maintained invariant, not type-enforced"
// choice ScriptFunctionRegistry makes for e.g. pin ordering). Evaluate()
// CLAMPS t outside the keyframe range to the first/last keyframe's value
// rather than extrapolating — the same "don't silently guess past defined
// data" rule InverseLerp/Lerp already apply to their own t.

struct CurveKeyframe {
    float time = 0.0F;
    float value = 0.0F;
    // Interpolation used between THIS keyframe and the next one.
    Easing easing = Easing::Linear;
};

struct Curve {
    std::vector<CurveKeyframe> keyframes;
};

[[nodiscard]] float Evaluate(const Curve& curve, float t) noexcept;

struct GradientStop {
    float time = 0.0F;
    Color color{};
};

struct Gradient {
    std::vector<GradientStop> stops;
};

[[nodiscard]] Color Evaluate(const Gradient& gradient, float t) noexcept;

// LIB-053's "serializacja assetowa": a minimal, self-contained binary
// (de)serialization pair, NOT a reuse of kb::scene's SceneAssetBinaryIO —
// that header is private to kb::scene (sources/engine/src/private/...),
// and kb::math is a zero-dependency foundational module every other type
// in this file already keeps free of upward dependencies on scene/asset
// internals. Any asset codec that wants to embed a Curve/Gradient inside
// its own file format calls these and copies the resulting bytes into its
// own buffer, the same way SceneAssetBinaryIO's own helpers are meant to
// be reused generically.
//
// A full standalone IAssetLoader-registered "load a .curve file" asset
// type is intentionally NOT implemented here: nothing in the engine
// consumes a top-level Curve/Gradient asset yet (no particle system,
// animation curve field, etc. exists to reference one) — building that
// loader/registration chain now would be speculative infrastructure with
// zero real callers, which this codebase's own rules forbid. Deterministic
// evaluation and a real byte-level round-trip are what LIB-053 asks for;
// wiring a specific consumer's IAssetLoader is that future consumer's job.
[[nodiscard]] std::vector<std::uint8_t> Serialize(const Curve& curve);
[[nodiscard]] bool Deserialize(std::span<const std::uint8_t> bytes, Curve& outCurve);

[[nodiscard]] std::vector<std::uint8_t> Serialize(const Gradient& gradient);
[[nodiscard]] bool Deserialize(std::span<const std::uint8_t> bytes, Gradient& outGradient);

} // namespace kb::math
