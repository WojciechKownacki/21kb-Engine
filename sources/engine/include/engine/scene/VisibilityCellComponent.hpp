#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

// Visibility cells use the RegionShapeComponent on the same entity as their
// canonical region geometry. RegionPortalComponent owns the inter-cell graph;
// consumers derive that graph from ECS instead of keeping a duplicate list.
enum class VisibilityCellMembership : std::uint8_t {
    Include = 0U,
    Exclude = 1U,
};

enum class VisibilityCellOverride : std::uint8_t {
    Automatic = 0U,
    ForceVisible = 1U,
    ForceHidden = 2U,
};

struct VisibilityCellComponent {
    static constexpr std::string_view StableId = "kb21.scene.visibility-cell";
    static constexpr std::uint32_t SchemaVersion = 1U;

    // Filter shared by member entities and visibility consumers. A non-zero
    // intersection means that this cell applies to that membership.
    std::uint32_t membershipMask = 0xFFFFFFFFU;
    VisibilityCellMembership membership = VisibilityCellMembership::Include;
    VisibilityCellOverride visibilityOverride = VisibilityCellOverride::Automatic;
    bool enabled = true;
};

[[nodiscard]] constexpr bool IsVisibilityCellMembershipValid(VisibilityCellMembership value) noexcept {
    return value == VisibilityCellMembership::Include || value == VisibilityCellMembership::Exclude;
}

[[nodiscard]] constexpr bool IsVisibilityCellOverrideValid(VisibilityCellOverride value) noexcept {
    return value == VisibilityCellOverride::Automatic || value == VisibilityCellOverride::ForceVisible ||
        value == VisibilityCellOverride::ForceHidden;
}

[[nodiscard]] constexpr bool IsVisibilityCellComponentValid(const VisibilityCellComponent& value) noexcept {
    return value.membershipMask != 0U && IsVisibilityCellMembershipValid(value.membership) &&
        IsVisibilityCellOverrideValid(value.visibilityOverride);
}

} // namespace kb::scene
