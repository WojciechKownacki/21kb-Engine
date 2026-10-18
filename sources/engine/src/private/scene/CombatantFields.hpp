#pragma once

#include "engine/scene/CombatantComponent.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace kb::scene {

// Combatant fields by the names prefabs, overrides and scripts use, read and written as text.
struct CombatantTextField {
    std::string_view name;
    std::string (*get)(const CombatantComponent&);
    bool (*set)(CombatantComponent&, std::string_view);
};

namespace combatant_fields {

[[nodiscard]] inline std::string FloatText(float value) {
    char buffer[32]{};
    const auto [end, code] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return code == std::errc{} ? std::string{ buffer, end } : std::string{ "0" };
}

[[nodiscard]] inline bool ParseFloat(std::string_view text, float& output) {
    float value = 0.0F;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (code != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) return false;
    output = value;
    return true;
}

template <typename T>
[[nodiscard]] inline bool ParseInteger(std::string_view text, T& output) {
    T value{};
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (code != std::errc{} || end != text.data() + text.size()) return false;
    output = value;
    return true;
}

[[nodiscard]] inline bool ParseBool(std::string_view text, bool& output) {
    if (text == "1" || text == "true") { output = true; return true; }
    if (text == "0" || text == "false") { output = false; return true; }
    return false;
}

} // namespace combatant_fields

#define KB_COMBATANT_FLOAT(field) \
    CombatantTextField{ #field, [](const CombatantComponent& c) { return combatant_fields::FloatText(c.field); }, \
        [](CombatantComponent& c, std::string_view t) { return combatant_fields::ParseFloat(t, c.field); } }
#define KB_COMBATANT_RESISTANCE(name, type) \
    CombatantTextField{ name, [](const CombatantComponent& c) { return combatant_fields::FloatText(c.resistances[static_cast<std::size_t>(type)]); }, \
        [](CombatantComponent& c, std::string_view t) { return combatant_fields::ParseFloat(t, c.resistances[static_cast<std::size_t>(type)]); } }
#define KB_COMBATANT_BOOL(field) \
    CombatantTextField{ #field, [](const CombatantComponent& c) { return std::string{ c.field ? "1" : "0" }; }, \
        [](CombatantComponent& c, std::string_view t) { return combatant_fields::ParseBool(t, c.field); } }
#define KB_COMBATANT_ABILITY(slot) \
    CombatantTextField{ "ability" #slot, [](const CombatantComponent& c) { return std::to_string(c.abilities[slot - 1]); }, \
        [](CombatantComponent& c, std::string_view t) { return combatant_fields::ParseInteger(t, c.abilities[slot - 1]); } }

[[nodiscard]] inline std::span<const CombatantTextField> CombatantTextFields() {
    using kb::gameplay::DamageType;
    static const CombatantTextField kFields[] = {
        KB_COMBATANT_FLOAT(maximumHealth),
        KB_COMBATANT_FLOAT(health),
        KB_COMBATANT_FLOAT(healthRegeneration),
        KB_COMBATANT_FLOAT(maximumMana),
        KB_COMBATANT_FLOAT(mana),
        KB_COMBATANT_FLOAT(manaRegeneration),
        KB_COMBATANT_RESISTANCE("physicalResistance", DamageType::Physical),
        KB_COMBATANT_RESISTANCE("fireResistance", DamageType::Fire),
        KB_COMBATANT_RESISTANCE("coldResistance", DamageType::Cold),
        KB_COMBATANT_RESISTANCE("lightningResistance", DamageType::Lightning),
        KB_COMBATANT_RESISTANCE("poisonResistance", DamageType::Poison),
        CombatantTextField{ "team", [](const CombatantComponent& c) { return std::to_string(c.team); },
            [](CombatantComponent& c, std::string_view t) { return combatant_fields::ParseInteger(t, c.team); } },
        KB_COMBATANT_FLOAT(sightRange),
        KB_COMBATANT_FLOAT(sightAngleDegrees),
        KB_COMBATANT_FLOAT(hearingRange),
        KB_COMBATANT_BOOL(requireLineOfSight),
        KB_COMBATANT_ABILITY(1), KB_COMBATANT_ABILITY(2), KB_COMBATANT_ABILITY(3), KB_COMBATANT_ABILITY(4),
        KB_COMBATANT_ABILITY(5), KB_COMBATANT_ABILITY(6), KB_COMBATANT_ABILITY(7), KB_COMBATANT_ABILITY(8),
        KB_COMBATANT_BOOL(invulnerable),
        KB_COMBATANT_BOOL(enabled),
    };
    return kFields;
}

#undef KB_COMBATANT_FLOAT
#undef KB_COMBATANT_RESISTANCE
#undef KB_COMBATANT_BOOL
#undef KB_COMBATANT_ABILITY

} // namespace kb::scene
