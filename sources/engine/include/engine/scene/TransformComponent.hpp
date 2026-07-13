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

struct TransformVersionMetadata {
    std::uint64_t localVersion = 1;
    std::uint64_t parentVersion = 0;
    std::uint64_t worldVersion = 0;
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
    std::uint64_t localVersion = 1;
    std::uint64_t parentVersion = 0;
    std::uint64_t worldVersion = 0;
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

} // namespace kb::scene
