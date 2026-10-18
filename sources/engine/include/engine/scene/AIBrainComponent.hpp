#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// AI as a short list of rules anyone can read: "when <condition>, do <action>", checked from the
// top every few tenths of a second; the first rule whose condition holds and whose action can be
// done right now wins. A preset fills the list for the usual kinds of creature, so making a
// monster is picking "Melee". The Gameplay.AI plugin (on by default) runs it, using the object's
// Combatant (senses, abilities, team), its NavAgent when the scene has a navigation mesh, and its
// Animator (the float "Speed" follows its movement, the trigger "Attack" fires on a plain hit).
enum class AICondition : std::uint8_t {
    Never,              // the rule is off
    Always,
    SeesEnemy,          // sees or hears an enemy now
    KnowsEnemy,         // senses one now or still remembers where it was
    EnemyCloserThan,    // value: meters
    EnemyFartherThan,   // value: meters
    HealthBelowPercent, // value: percent of maximum health
    ManaAbovePercent,   // value: percent of maximum mana
    AbilityReady,       // value: ability slot 1-8
    FarFromHome,        // value: meters from where it started
};
inline constexpr std::size_t kAIConditionCount = 10U;
inline constexpr std::array<std::string_view, kAIConditionCount> kAIConditionNames{
    "Never", "Always", "SeesEnemy", "KnowsEnemy", "EnemyCloserThan", "EnemyFartherThan",
    "HealthBelowPercent", "ManaAbovePercent", "AbilityReady", "FarFromHome" };

enum class AIAction : std::uint8_t {
    Idle,           // stands still
    Wander,         // strolls around home within the wander radius
    ReturnHome,
    Chase,          // runs to the enemy
    UseAbility,     // value: slot 1-8; walks into its range, faces the enemy and uses it (an empty slot is a plain 10-damage hit)
    KeepDistance,   // value: meters; backs away when the enemy is closer
    Flee,           // runs away from the enemy
};
inline constexpr std::size_t kAIActionCount = 7U;
inline constexpr std::array<std::string_view, kAIActionCount> kAIActionNames{
    "Idle", "Wander", "ReturnHome", "Chase", "UseAbility", "KeepDistance", "Flee" };

enum class AIBrainPreset : std::uint8_t {
    Custom,     // the rules as written
    Melee,      // fights up close: attacks with slot 1, goes home when it loses the enemy, wanders
    Ranged,     // a caster or archer: keeps 8 m away and uses slot 1
    Guard,      // stands at its post, attacks what comes near, never wanders
    Coward,     // runs from enemies (with no team: from anyone with one), wanders otherwise (animals, villagers)
    Wanderer,   // only strolls around (critters, ambient life)
};
inline constexpr std::size_t kAIBrainPresetCount = 6U;
inline constexpr std::array<std::string_view, kAIBrainPresetCount> kAIBrainPresetNames{
    "Custom", "Melee", "Ranged", "Guard", "Coward", "Wanderer" };

[[nodiscard]] constexpr bool AIConditionTakesValue(AICondition condition) noexcept {
    return condition == AICondition::EnemyCloserThan || condition == AICondition::EnemyFartherThan ||
        condition == AICondition::HealthBelowPercent || condition == AICondition::ManaAbovePercent ||
        condition == AICondition::AbilityReady || condition == AICondition::FarFromHome;
}

[[nodiscard]] constexpr bool AIActionTakesValue(AIAction action) noexcept {
    return action == AIAction::UseAbility || action == AIAction::KeepDistance;
}

struct AIRule {
    AICondition when = AICondition::Never;
    float whenValue = 0.0F;
    AIAction action = AIAction::Idle;
    float actionValue = 0.0F;
};

inline constexpr std::size_t kAIBrainRuleCount = 8U;

struct AIBrainComponent {
    static constexpr std::string_view StableId = "kb21.gameplay.ai-brain";
    static constexpr std::uint32_t SchemaVersion = 1U;

    AIBrainPreset preset = AIBrainPreset::Melee;
    float moveSpeed = 3.5F;
    float wanderRadius = 6.0F;
    // How far from home it follows an enemy before it gives up and goes back.
    float leashDistance = 30.0F;
    std::array<AIRule, kAIBrainRuleCount> rules{ {
        { AICondition::KnowsEnemy, 0.0F, AIAction::UseAbility, 1.0F },
        { AICondition::FarFromHome, 1.0F, AIAction::ReturnHome, 0.0F },
        { AICondition::Always, 0.0F, AIAction::Wander, 0.0F },
    } };
    bool enabled = true;
};

// The rules (and leash) of a preset; Custom leaves the brain as it is.
inline void ApplyAIBrainPreset(AIBrainComponent& brain, AIBrainPreset preset) noexcept {
    brain.preset = preset;
    if (preset == AIBrainPreset::Custom) return;
    brain.rules = {};
    switch (preset) {
    case AIBrainPreset::Melee:
        brain.rules[0] = { AICondition::KnowsEnemy, 0.0F, AIAction::UseAbility, 1.0F };
        brain.rules[1] = { AICondition::FarFromHome, 1.0F, AIAction::ReturnHome, 0.0F };
        brain.rules[2] = { AICondition::Always, 0.0F, AIAction::Wander, 0.0F };
        brain.leashDistance = 30.0F;
        break;
    case AIBrainPreset::Ranged:
        brain.rules[0] = { AICondition::EnemyCloserThan, 5.0F, AIAction::KeepDistance, 8.0F };
        brain.rules[1] = { AICondition::KnowsEnemy, 0.0F, AIAction::UseAbility, 1.0F };
        brain.rules[2] = { AICondition::FarFromHome, 1.0F, AIAction::ReturnHome, 0.0F };
        brain.rules[3] = { AICondition::Always, 0.0F, AIAction::Idle, 0.0F };
        brain.leashDistance = 30.0F;
        break;
    case AIBrainPreset::Guard:
        brain.rules[0] = { AICondition::EnemyCloserThan, 12.0F, AIAction::UseAbility, 1.0F };
        brain.rules[1] = { AICondition::FarFromHome, 0.5F, AIAction::ReturnHome, 0.0F };
        brain.rules[2] = { AICondition::Always, 0.0F, AIAction::Idle, 0.0F };
        brain.leashDistance = 15.0F;
        break;
    case AIBrainPreset::Coward:
        brain.rules[0] = { AICondition::KnowsEnemy, 0.0F, AIAction::Flee, 0.0F };
        brain.rules[1] = { AICondition::Always, 0.0F, AIAction::Wander, 0.0F };
        break;
    case AIBrainPreset::Wanderer:
        brain.rules[0] = { AICondition::Always, 0.0F, AIAction::Wander, 0.0F };
        break;
    case AIBrainPreset::Custom:
        break;
    }
}

[[nodiscard]] inline bool IsAIBrainComponentValid(const AIBrainComponent& brain) noexcept {
    const auto finiteNonNegative = [](float value) { return std::isfinite(value) && value >= 0.0F; };
    if (static_cast<std::size_t>(brain.preset) >= kAIBrainPresetCount || !finiteNonNegative(brain.moveSpeed) ||
        !finiteNonNegative(brain.wanderRadius) || !finiteNonNegative(brain.leashDistance)) return false;
    for (const AIRule& rule : brain.rules) {
        if (static_cast<std::size_t>(rule.when) >= kAIConditionCount || static_cast<std::size_t>(rule.action) >= kAIActionCount ||
            !finiteNonNegative(rule.whenValue) || !finiteNonNegative(rule.actionValue)) return false;
        if (rule.action == AIAction::UseAbility && (rule.actionValue < 1.0F || rule.actionValue > 8.0F)) return false;
        if (rule.when == AICondition::AbilityReady && (rule.whenValue < 1.0F || rule.whenValue > 8.0F)) return false;
    }
    return true;
}

} // namespace kb::scene
