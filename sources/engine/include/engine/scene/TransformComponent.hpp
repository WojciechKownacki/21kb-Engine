#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>

namespace kb::scene {

// LIB-042: Vec3/Quat are aliases to the single canonical kb::math family,
// not a second definition — every existing kb::scene::Vec3/Quat call site
// (LocalTransform, WorldTransform, TransformComponent, ColliderComponent,
// Vec3Math, QuatMath, ...) keeps compiling unchanged.
using Vec3 = kb::math::Vec3;
using Quat = kb::math::Quat;

struct LocalTransform {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{ 1.0F, 1.0F, 1.0F };
};

struct WorldTransform {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{ 1.0F, 1.0F, 1.0F };
};

struct WorldTransformAffine3x4 {
    float values[12]{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 0.0F,
    };
};

// 32-bit counters keep the stored transform row at 96 bytes; each counts the writes of one entity.
struct TransformVersionMetadata {
    std::uint32_t localVersion = 1;
    std::uint32_t parentVersion = 0;
    std::uint32_t worldVersion = 0;
    bool worldDirty = true;
};

struct TransformHierarchyRelation {
    std::uint64_t parentEntityId = 0;
    std::uint64_t topologyVersion = 0;
};

struct TransformComponent {
    Vec3 localPosition{};
    Quat localRotation{};
    Vec3 localScale{ 1.0F, 1.0F, 1.0F };
    Vec3 worldPosition{};
    Quat worldRotation{};
    Vec3 worldScale{ 1.0F, 1.0F, 1.0F };
    std::uint32_t localVersion = 1;
    std::uint32_t parentVersion = 0;
    std::uint32_t worldVersion = 0;
    bool worldDirty = true;

    [[nodiscard]] constexpr LocalTransform LocalPayload() const noexcept {
        return LocalTransform{ .position = localPosition, .rotation = localRotation, .scale = localScale };
    }

    [[nodiscard]] constexpr WorldTransform WorldPayload() const noexcept {
        return WorldTransform{ .position = worldPosition, .rotation = worldRotation, .scale = worldScale };
    }

    [[nodiscard]] constexpr TransformVersionMetadata VersionMetadata() const noexcept {
        return TransformVersionMetadata{
            .localVersion = localVersion,
            .parentVersion = parentVersion,
            .worldVersion = worldVersion,
            .worldDirty = worldDirty,
        };
    }

    static constexpr TransformComponent FromPayloads(
        const LocalTransform& local,
        const WorldTransform& world = WorldTransform{},
        const TransformVersionMetadata& metadata = TransformVersionMetadata{}) noexcept {
        return TransformComponent{
            .localPosition = local.position,
            .localRotation = local.rotation,
            .localScale = local.scale,
            .worldPosition = world.position,
            .worldRotation = world.rotation,
            .worldScale = world.scale,
            .localVersion = metadata.localVersion,
            .parentVersion = metadata.parentVersion,
            .worldVersion = metadata.worldVersion,
            .worldDirty = metadata.worldDirty,
        };
    }
};

static_assert(sizeof(TransformComponent) == 96U, "The stored transform row is 96 bytes");

// The world transform as the column-major affine the renderer consumes. The scene's render-proxy lists and the
// renderer's transform pull both build it here, so the two agree bit for bit.
[[nodiscard]] inline WorldTransformAffine3x4 BuildWorldAffine3x4(const TransformComponent& transform) noexcept {
    if (transform.worldRotation.x == 0.0F &&
        transform.worldRotation.y == 0.0F &&
        transform.worldRotation.z == 0.0F &&
        transform.worldRotation.w == 1.0F) {
        WorldTransformAffine3x4 affine;
        affine.values[0] = transform.worldScale.x;
        affine.values[1] = 0.0F;
        affine.values[2] = 0.0F;
        affine.values[3] = 0.0F;
        affine.values[4] = transform.worldScale.y;
        affine.values[5] = 0.0F;
        affine.values[6] = 0.0F;
        affine.values[7] = 0.0F;
        affine.values[8] = transform.worldScale.z;
        affine.values[9] = transform.worldPosition.x;
        affine.values[10] = transform.worldPosition.y;
        affine.values[11] = transform.worldPosition.z;
        return affine;
    }

    const float x = transform.worldRotation.x;
    const float y = transform.worldRotation.y;
    const float z = transform.worldRotation.z;
    const float w = transform.worldRotation.w;
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;
    const float sx = transform.worldScale.x;
    const float sy = transform.worldScale.y;
    const float sz = transform.worldScale.z;

    WorldTransformAffine3x4 affine;
    affine.values[0] = (1.0F - 2.0F * (yy + zz)) * sx;
    affine.values[1] = (2.0F * (xy + wz)) * sx;
    affine.values[2] = (2.0F * (xz - wy)) * sx;
    affine.values[3] = (2.0F * (xy - wz)) * sy;
    affine.values[4] = (1.0F - 2.0F * (xx + zz)) * sy;
    affine.values[5] = (2.0F * (yz + wx)) * sy;
    affine.values[6] = (2.0F * (xz + wy)) * sz;
    affine.values[7] = (2.0F * (yz - wx)) * sz;
    affine.values[8] = (1.0F - 2.0F * (xx + yy)) * sz;
    affine.values[9] = transform.worldPosition.x;
    affine.values[10] = transform.worldPosition.y;
    affine.values[11] = transform.worldPosition.z;
    return affine;
}

} // namespace kb::scene
