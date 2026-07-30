#pragma once

#include <cstdint>
#include <cmath>
#include <string_view>

namespace kb::scene {

enum class StreamLoadMask : std::uint32_t {
    None = 0U,
    Prefab = 1U << 0U,
    Subscene = 1U << 1U,
    WorldFragment = 1U << 2U,
    All = (1U << 0U) | (1U << 1U) | (1U << 2U),
};

[[nodiscard]] constexpr StreamLoadMask operator|(StreamLoadMask left, StreamLoadMask right) noexcept {
    return static_cast<StreamLoadMask>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr bool ContainsStreamLoadMask(StreamLoadMask mask, StreamLoadMask value) noexcept {
    return (static_cast<std::uint32_t>(mask) & static_cast<std::uint32_t>(value)) != 0U;
}

[[nodiscard]] constexpr bool IsStreamLoadMaskValid(StreamLoadMask value) noexcept {
    return (static_cast<std::uint32_t>(value) & ~static_cast<std::uint32_t>(StreamLoadMask::All)) == 0U;
}

// The focus position is the owning entity's canonical world transform. This
// component only stores the authored stream policy and never duplicates pose.
struct StreamFocusComponent {
    static constexpr std::string_view StableId = "kb21.scene.stream-focus";
    static constexpr std::uint32_t SchemaVersion = 1U;

    float innerRadius = 24.0F;
    float outerRadius = 48.0F;
    std::int32_t priority = 0;
    StreamLoadMask loadMask = StreamLoadMask::All;
    bool enabled = true;
};

[[nodiscard]] inline bool IsStreamFocusValid(const StreamFocusComponent& value) noexcept {
    return std::isfinite(value.innerRadius) && std::isfinite(value.outerRadius) && value.innerRadius >= 0.0F
        && value.outerRadius >= value.innerRadius && IsStreamLoadMaskValid(value.loadMask);
}

} // namespace kb::scene
