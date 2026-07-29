#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace kb::gameplay {

enum class DamageType : std::uint8_t {
    Physical,
    Fire,
    Healing,
};

inline constexpr std::size_t kDamageTypeCount = 3U;

struct DamageEvent {
    kb::scene::SceneEntity source{};
    kb::scene::SceneEntity instigator{};
    kb::scene::SceneEntity target{};
    kb::scene::SceneEntity hitEntity{};
    DamageType type = DamageType::Physical;
    float amount = 0.0F;
};

struct DamageResistances {
    std::array<float, kDamageTypeCount> multipliers{ 1.0F, 1.0F, 1.0F };

    [[nodiscard]] std::optional<float> Multiplier(DamageType type) const noexcept {
        const std::size_t index = static_cast<std::size_t>(type);
        if (index >= multipliers.size() || !std::isfinite(multipliers[index]) || multipliers[index] < 0.0F) {
            return std::nullopt;
        }
        return multipliers[index];
    }
};

struct DamageResolution {
    DamageEvent event{};
    float healthDelta = 0.0F;
};

[[nodiscard]] inline bool IsValidDamageEvent(const DamageEvent& event) noexcept {
    return event.target.IsValid() && std::isfinite(event.amount) && event.amount > 0.0F;
}

[[nodiscard]] inline std::optional<DamageResolution> ResolveDamage(
    const DamageEvent& event,
    const DamageResistances& resistances) noexcept {
    if (!IsValidDamageEvent(event)) {
        return std::nullopt;
    }

    const std::optional<float> multiplier = resistances.Multiplier(event.type);
    if (!multiplier.has_value()) {
        return std::nullopt;
    }

    const float magnitude = event.amount * *multiplier;
    if (!std::isfinite(magnitude)) {
        return std::nullopt;
    }

    return DamageResolution{
        .event = event,
        .healthDelta = event.type == DamageType::Healing ? magnitude : -magnitude,
    };
}
} // namespace kb::gameplay
