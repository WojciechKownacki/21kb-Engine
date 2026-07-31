#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// Coordinates the existing mesh LODs for every entity carrying the same
// non-zero group id. The selected level is renderer-derived frame state; this
// component stores only authored policy.
struct SceneDetailSwitchComponent {
    static constexpr std::string_view StableId = "kb21.scene.detail-switch";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t groupId = 0U;
    std::uint32_t minimumLod = 0U;
    std::uint32_t maximumLod = 255U;
    // Moving closer promotes detail only at or above this coverage. Moving
    // away demotes only below demoteCoverage, preventing LOD oscillation.
    float promoteCoverage = 0.20F;
    float demoteCoverage = 0.15F;
    bool enabled = true;
};

[[nodiscard]] inline bool IsSceneDetailSwitchComponentValid(const SceneDetailSwitchComponent& value) noexcept {
    return value.minimumLod <= value.maximumLod &&
        std::isfinite(value.promoteCoverage) && std::isfinite(value.demoteCoverage) &&
        value.promoteCoverage >= 0.0F && value.promoteCoverage <= 1.0F &&
        value.demoteCoverage >= 0.0F && value.demoteCoverage <= 1.0F &&
        value.demoteCoverage < value.promoteCoverage;
}

} // namespace kb::scene
