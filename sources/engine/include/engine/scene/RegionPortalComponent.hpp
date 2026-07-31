#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class RegionPortalPurpose : std::uint32_t {
    Visibility = 1U << 0U,
    Streaming = 1U << 1U,
    Simulation = 1U << 2U,
};

using RegionPortalPurposeMask = std::uint32_t;
constexpr RegionPortalPurposeMask kRegionPortalAllPurposes = static_cast<RegionPortalPurposeMask>(RegionPortalPurpose::Visibility) |
    static_cast<RegionPortalPurposeMask>(RegionPortalPurpose::Streaming) |
    static_cast<RegionPortalPurposeMask>(RegionPortalPurpose::Simulation);

// A directional relation between two VisibilityCell entities. The RegionShape
// on this same entity is the canonical opening geometry; no consumer stores a
// duplicate portal volume or adjacency graph.
struct SceneRegionPortalComponent {
    static constexpr std::string_view StableId = "kb21.scene.region-portal";
    static constexpr std::uint32_t SchemaVersion = 1U;

    SceneEntity sourceCell{};
    SceneEntity targetCell{};
    RegionPortalPurposeMask purposes = kRegionPortalAllPurposes;
    // An author may add the component before selecting both cells. Such a
    // portal is persisted but always fails closed until it is configured.
    bool enabled = false;
};

[[nodiscard]] constexpr bool IsRegionPortalPurposeMaskValid(RegionPortalPurposeMask purposes) noexcept {
    return purposes != 0U && (purposes & ~kRegionPortalAllPurposes) == 0U;
}

[[nodiscard]] constexpr bool IsSceneRegionPortalComponentValid(const SceneRegionPortalComponent& value) noexcept {
    return value.sourceCell.IsValid() && value.targetCell.IsValid() && value.sourceCell != value.targetCell &&
        IsRegionPortalPurposeMaskValid(value.purposes);
}

} // namespace kb::scene
