#include "scene/SceneTransformMatrices.hpp"

#include <array>

namespace kb::render {
namespace {

struct Basis {
    float xx = 1.0F;
    float xy = 0.0F;
    float xz = 0.0F;
    float yx = 0.0F;
    float yy = 1.0F;
    float yz = 0.0F;
    float zx = 0.0F;
    float zy = 0.0F;
    float zz = 1.0F;
};

[[nodiscard]] Basis BasisFromQuat(const kb::scene::Quat& q) noexcept {
    const float x2 = q.x + q.x;
    const float y2 = q.y + q.y;
    const float z2 = q.z + q.z;
    const float xx = q.x * x2;
    const float xy = q.x * y2;
    const float xz = q.x * z2;
    const float yy = q.y * y2;
    const float yz = q.y * z2;
    const float zz = q.z * z2;
    const float wx = q.w * x2;
    const float wy = q.w * y2;
    const float wz = q.w * z2;

    return Basis{
        .xx = 1.0F - (yy + zz),
        .xy = xy + wz,
        .xz = xz - wy,
        .yx = xy - wz,
        .yy = 1.0F - (xx + zz),
        .yz = yz + wx,
        .zx = xz + wy,
        .zy = yz - wx,
        .zz = 1.0F - (xx + yy),
    };
}

} // namespace

std::array<float, 16> SceneTransformMatrices::Model(const kb::scene::TransformComponent& transform) noexcept {
    const Basis basis = BasisFromQuat(transform.worldRotation);
    const kb::scene::Vec3& scale = transform.worldScale;
    const kb::scene::Vec3& position = transform.worldPosition;

    return {
        basis.xx * scale.x,
        basis.xy * scale.x,
        basis.xz * scale.x,
        0.0F,
        basis.yx * scale.y,
        basis.yy * scale.y,
        basis.yz * scale.y,
        0.0F,
        basis.zx * scale.z,
        basis.zy * scale.z,
        basis.zz * scale.z,
        0.0F,
        position.x,
        position.y,
        position.z,
        1.0F,
    };
}

float SceneTransformMatrices::ForwardX(const kb::scene::Quat& rotation) noexcept {
    return BasisFromQuat(rotation).zx;
}

float SceneTransformMatrices::ForwardY(const kb::scene::Quat& rotation) noexcept {
    return BasisFromQuat(rotation).zy;
}

float SceneTransformMatrices::ForwardZ(const kb::scene::Quat& rotation) noexcept {
    return BasisFromQuat(rotation).zz;
}

float SceneTransformMatrices::UpX(const kb::scene::Quat& rotation) noexcept {
    return BasisFromQuat(rotation).yx;
}

float SceneTransformMatrices::UpY(const kb::scene::Quat& rotation) noexcept {
    return BasisFromQuat(rotation).yy;
}

float SceneTransformMatrices::UpZ(const kb::scene::Quat& rotation) noexcept {
    return BasisFromQuat(rotation).yz;
}

} // namespace kb::render
