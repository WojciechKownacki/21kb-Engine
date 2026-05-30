#include "scene/transform/Vec3Math.hpp"

#include "scene/transform/QuatMath.hpp"

namespace kb::scene {

Vec3 Vec3Math::Add(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

Vec3 Vec3Math::Multiply(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{ lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z };
}

Vec3 Vec3Math::Rotate(Quat rotation, Vec3 value) noexcept {
    const Quat normalized = QuatMath::Normalize(rotation);
    const Vec3 q{ normalized.x, normalized.y, normalized.z };
    const Vec3 uv{
        q.y * value.z - q.z * value.y,
        q.z * value.x - q.x * value.z,
        q.x * value.y - q.y * value.x,
    };
    const Vec3 uuv{
        q.y * uv.z - q.z * uv.y,
        q.z * uv.x - q.x * uv.z,
        q.x * uv.y - q.y * uv.x,
    };

    return Vec3{
        value.x + ((uv.x * normalized.w) + uuv.x) * 2.0F,
        value.y + ((uv.y * normalized.w) + uuv.y) * 2.0F,
        value.z + ((uv.z * normalized.w) + uuv.z) * 2.0F,
    };
}

} // namespace kb::scene
