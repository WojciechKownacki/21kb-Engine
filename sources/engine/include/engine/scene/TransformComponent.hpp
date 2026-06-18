#pragma once

#include <cstdint>

namespace kb::scene {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Quat {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

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
