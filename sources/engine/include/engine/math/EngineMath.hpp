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

[[nodiscard]] Quat Normalize(Quat value) noexcept;

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
// a near-zero sin(omega).
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
// resolution Ogre3D/Unity's equivalent functions use.
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

// LIB-045: scalar math foundation — the plain-Float functions LIB-046
// through LIB-049's Vec3/Quat-level operations (Dot, Cross, Slerp, ...)
// build on. These are the same functions `sources/script/ScriptMathApi`
// registers as `Math.Clamp`/`Math.Lerp`/etc for Native/Lua/Visual Graph;
// this header is their native implementation, not a second copy of it.

[[nodiscard]] constexpr float Clamp(float value, float min, float max) noexcept {
    return value < min ? min : (value > max ? max : value);
}

// Clamps t to [0,1] before interpolating (matches the game-engine
// convention this API otherwise follows — e.g. Unity's Mathf.Lerp — so a
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
// past target (Unity's Mathf.MoveTowards) — unlike Lerp, this is
// frame-rate-independent when maxDelta = speed * deltaTime.
[[nodiscard]] constexpr float MoveTowards(float current, float target, float maxDelta) noexcept {
    const float difference = target - current;
    if (difference <= maxDelta && difference >= -maxDelta) {
        return target;
    }
    return current + (difference < 0.0F ? -maxDelta : maxDelta);
}

struct DampResult {
    float value = 0.0F;
    float velocity = 0.0F;
};

// Critically-damped spring smoothing (Unity's Mathf.SmoothDamp / Game
// Programming Gems 4's "critically damped ease-in ease-out" formula).
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
// direction).
[[nodiscard]] constexpr float Sign(float value) noexcept {
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
// is not (fmod(-1, 4) is -1, not 3).
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

} // namespace kb::math
