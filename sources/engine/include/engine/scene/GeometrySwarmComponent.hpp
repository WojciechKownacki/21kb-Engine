#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// Authoring input for a renderer-derived regular swarm.  The ECS component is
// deliberately compact: individual render instances are generated
// deterministically from this descriptor and never become ECS entities.
struct GeometrySwarmComponent {
    static constexpr std::string_view StableId = "kb21.draw.d3.geometry-swarm";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t meshAssetId = 0U;
    std::uint64_t materialAssetId = 0U;
    std::uint32_t instanceCount = 1U;
    // The generated lattice is filled X-first, then Y and Z.  A zero extent
    // keeps the descriptor editable while disabled, but can never render.
    std::uint16_t columns = 1U;
    std::uint16_t rows = 1U;
    std::uint16_t layers = 1U;
    Vec3 spacing{ 1.0F, 1.0F, 1.0F };
    float instanceScale = 1.0F;
    std::uint32_t layer = 1U;
    bool castsShadow = true;
    bool receivesShadow = true;
    bool enabled = false;
};

[[nodiscard]] inline bool IsGeometrySwarmComponentValid(const GeometrySwarmComponent& value) noexcept {
    constexpr std::uint32_t kMaximumInstances = 1U << 20U;
    const std::uint64_t capacity = static_cast<std::uint64_t>(value.columns) * value.rows * value.layers;
    return value.meshAssetId != 0U && value.instanceCount > 0U && value.instanceCount <= kMaximumInstances &&
        capacity >= value.instanceCount && value.columns > 0U && value.rows > 0U && value.layers > 0U &&
        std::isfinite(value.spacing.x) && std::isfinite(value.spacing.y) && std::isfinite(value.spacing.z) &&
        std::isfinite(value.instanceScale) && value.instanceScale > 0.0F;
}

[[nodiscard]] inline bool IsGeometrySwarmComponentPersistable(const GeometrySwarmComponent& value) noexcept {
    if (IsGeometrySwarmComponentValid(value)) return true;
    return !value.enabled && value.instanceCount > 0U && value.instanceCount <= (1U << 20U) &&
        value.columns > 0U && value.rows > 0U && value.layers > 0U &&
        std::isfinite(value.spacing.x) && std::isfinite(value.spacing.y) && std::isfinite(value.spacing.z) &&
        std::isfinite(value.instanceScale) && value.instanceScale > 0.0F;
}

} // namespace kb::scene
