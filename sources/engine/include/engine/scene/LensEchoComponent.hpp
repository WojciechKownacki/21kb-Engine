#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class LensEchoOcclusionRule : std::uint8_t {
    DepthTested = 0U,
    AlwaysVisible = 1U,
};

// Authored policy only. The renderer resolves the source transform and owns
// every transient billboard proxy required to present the optical echo.
struct LensEchoComponent {
    static constexpr std::string_view StableId = "kb21.draw.d3.lens-echo";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t sourceEntityId = 0U;
    std::uint64_t profileMaterialAssetId = 0U;
    float intensity = 1.0F;
    float size = 1.0F;
    std::uint32_t layer = 1U;
    LensEchoOcclusionRule occlusionRule = LensEchoOcclusionRule::DepthTested;
    bool enabled = false;
};

[[nodiscard]] constexpr bool IsLensEchoOcclusionRuleValid(LensEchoOcclusionRule value) noexcept {
    return value == LensEchoOcclusionRule::DepthTested || value == LensEchoOcclusionRule::AlwaysVisible;
}

[[nodiscard]] inline bool IsLensEchoComponentValid(const LensEchoComponent& value) noexcept {
    return value.sourceEntityId != 0U && value.profileMaterialAssetId != 0U &&
        std::isfinite(value.intensity) && value.intensity >= 0.0F && value.intensity <= 100000.0F &&
        std::isfinite(value.size) && value.size > 0.0F && value.size <= 100000.0F && value.layer != 0U &&
        IsLensEchoOcclusionRuleValid(value.occlusionRule);
}

[[nodiscard]] inline bool IsLensEchoComponentPersistable(const LensEchoComponent& value) noexcept {
    return IsLensEchoComponentValid(value) ||
        (!value.enabled && std::isfinite(value.intensity) && value.intensity >= 0.0F && value.intensity <= 100000.0F &&
            std::isfinite(value.size) && value.size > 0.0F && value.size <= 100000.0F && value.layer != 0U &&
            IsLensEchoOcclusionRuleValid(value.occlusionRule));
}

} // namespace kb::scene
