#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

// Authored policy only. The time-stamped samples and generated ribbon instances
// are owned by the renderer's runtime system and are never written into ECS.
struct HistoryRibbonComponent {
    static constexpr std::string_view StableId = "kb21.draw.d3.history-ribbon";
    static constexpr std::uint32_t SchemaVersion = 1U;
    static constexpr std::uint32_t MaxSamples = 128U;

    std::uint64_t meshAssetId = 0U;
    std::uint64_t materialAssetId = 0U;
    float lifetimeSeconds = 1.0F;
    float width = 0.1F;
    float sampleIntervalSeconds = 1.0F / 30.0F;
    std::uint32_t layer = 1U;
    bool castsShadow = false;
    bool receivesShadow = true;
    bool enabled = false;
};

[[nodiscard]] constexpr bool IsHistoryRibbonComponentValid(const HistoryRibbonComponent& value) noexcept {
    return value.meshAssetId != 0U && value.lifetimeSeconds > 0.0F && value.lifetimeSeconds <= 3600.0F &&
        value.width > 0.0F && value.width <= 100000.0F && value.sampleIntervalSeconds > 0.0F &&
        value.sampleIntervalSeconds <= value.lifetimeSeconds &&
        value.lifetimeSeconds / value.sampleIntervalSeconds < static_cast<float>(HistoryRibbonComponent::MaxSamples) && value.layer != 0U;
}

[[nodiscard]] constexpr bool IsHistoryRibbonComponentPersistable(const HistoryRibbonComponent& value) noexcept {
    return IsHistoryRibbonComponentValid(value) ||
        (!value.enabled && value.lifetimeSeconds > 0.0F && value.lifetimeSeconds <= 3600.0F &&
            value.width > 0.0F && value.width <= 100000.0F && value.sampleIntervalSeconds > 0.0F &&
            value.sampleIntervalSeconds <= value.lifetimeSeconds &&
            value.lifetimeSeconds / value.sampleIntervalSeconds < static_cast<float>(HistoryRibbonComponent::MaxSamples) && value.layer != 0U);
}

} // namespace kb::scene
