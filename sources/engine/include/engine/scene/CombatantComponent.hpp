#pragma once

#include "engine/gameplay/Damage.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

inline constexpr std::size_t kCombatantAbilitySlots = 8U;
using CombatantTeamId = std::uint32_t;

// Everything that makes an object take part in a fight, in one place: it can be hurt and healed,
// spends mana on abilities, belongs to a team, and sees and hears the others. A player, a monster,
// a destructible barrel and a guard tower are all combatants with different numbers.
//
// Teams: 0 belongs to nobody and fights nobody (a barrel); combatants of two different non-zero
// teams are enemies (players 1, monsters 2). Resistances are percents: 50 halves a damage type,
// 100 makes immune, a negative value makes weak. Senses drive AI and Gameplay::Perceived.
struct CombatantComponent {
    static constexpr std::string_view StableId = "kb21.gameplay.combatant";
    static constexpr std::uint32_t SchemaVersion = 1U;

    float maximumHealth = 100.0F;
    float health = 100.0F;
    float healthRegeneration = 0.0F;
    float maximumMana = 0.0F;
    float mana = 0.0F;
    float manaRegeneration = 0.0F;
    std::array<float, kb::gameplay::kDamageTypeCount> resistances{};
    CombatantTeamId team = 0U;
    float sightRange = 15.0F;
    float sightAngleDegrees = 120.0F;
    float hearingRange = 8.0F;
    bool requireLineOfSight = true;
    // Ability assets (.kbability) in slot order: slot 1 is the first.
    std::array<std::uint64_t, kCombatantAbilitySlots> abilities{};
    bool invulnerable = false;
    bool enabled = true;
};

[[nodiscard]] inline bool IsCombatantComponentValid(const CombatantComponent& value) noexcept {
    const auto finiteNonNegative = [](float number) { return std::isfinite(number) && number >= 0.0F; };
    for (const float resistance : value.resistances) {
        if (!std::isfinite(resistance) || resistance > 100.0F || resistance < -1000.0F) return false;
    }
    return finiteNonNegative(value.maximumHealth) && finiteNonNegative(value.health) && value.health <= value.maximumHealth &&
        std::isfinite(value.healthRegeneration) &&
        finiteNonNegative(value.maximumMana) && finiteNonNegative(value.mana) && value.mana <= value.maximumMana &&
        std::isfinite(value.manaRegeneration) &&
        finiteNonNegative(value.sightRange) && finiteNonNegative(value.hearingRange) &&
        std::isfinite(value.sightAngleDegrees) && value.sightAngleDegrees >= 0.0F && value.sightAngleDegrees <= 360.0F;
}

[[nodiscard]] inline bool IsCombatantAlive(const CombatantComponent& value) noexcept { return value.health > 0.0F; }

// Two combatants fight when both have a team and the teams differ.
[[nodiscard]] constexpr bool AreCombatantTeamsHostile(CombatantTeamId first, CombatantTeamId second) noexcept {
    return first != 0U && second != 0U && first != second;
}

} // namespace kb::scene
