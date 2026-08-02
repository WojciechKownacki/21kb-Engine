#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::scene {

inline constexpr std::uint32_t kMaxDeformedGeometryMaterialSlotOverrides = 8U;

// The pose source is either the component owner (the default) or one explicit
// SkeletonBinding owner. Evaluated poses, morph weights, bounds, and GPU
// palette handles are derived runtime data and must never be stored here.
struct DrawD3DeformedGeometryComponent {
    static constexpr std::string_view StableId = "kb21.draw.d3.deformed-geometry";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t skeletalMeshAssetId = 0U;
    std::array<std::uint64_t, kMaxDeformedGeometryMaterialSlotOverrides> materialSlotAssetIds{};
    std::uint32_t materialSlotOverrideCount = 0U;
    // A zero entity selects the component owner as the pose source.
    SceneEntity poseSource{};
    std::int32_t lodBias = 0;
    bool lodEnabled = true;
    // Renderer code may only select a fixed bound when this explicit authored
    // policy is enabled; otherwise SkeletalMeshAsset conservative bounds apply.
    bool fixedBounds = false;
    bool castsShadow = true;
    bool receivesShadow = true;
    std::uint32_t layer = 1U;
    bool enabled = true;
};

[[nodiscard]] constexpr bool IsDrawD3DeformedGeometryComponentValid(
    const DrawD3DeformedGeometryComponent& value) noexcept {
    return value.skeletalMeshAssetId != 0U &&
        value.materialSlotOverrideCount <= kMaxDeformedGeometryMaterialSlotOverrides;
}

} // namespace kb::scene
