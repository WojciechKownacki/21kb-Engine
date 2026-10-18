#pragma once

#include "engine/scene/SceneGameplay.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kb::scene {

class Scene;

// Runtime state of fights, kept beside the Combatant components (which hold what is authored and
// persisted: health, mana, team, senses, ability slots).
struct GameplayRuntimeState {
    std::unordered_map<std::uint64_t, std::array<float, kCombatantAbilitySlots>> cooldowns;
    std::unordered_map<std::uint64_t, std::vector<PerceivedCombatant>> perceived;
    std::unordered_set<std::uint64_t> dead;
    std::vector<GameplayEvent> events;
    float perceptionAccumulator = 0.0F;
    std::uint64_t nextEventSequence = 1U;
};

class SceneGameplayService final {
public:
    SceneGameplayService() = delete;
    // Seconds between two looks of every observer, and how long a lost combatant is remembered.
    static constexpr float kPerceptionInterval = 0.2F;
    static constexpr float kMemorySeconds = 5.0F;

    static DamageOutcome ApplyDamage(Scene& scene, SceneEntity target, float amount, kb::gameplay::DamageType type, SceneEntity source);
    static DamageOutcome Heal(Scene& scene, SceneEntity target, float amount, SceneEntity source);
    static DamageOutcome Revive(Scene& scene, SceneEntity target, float health);
    static AbilityOutcome UseAbility(Scene& scene, SceneEntity caster, std::uint32_t slot, SceneEntity target);
    [[nodiscard]] static bool IsAlive(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool AreHostile(const Scene& scene, SceneEntity first, SceneEntity second) noexcept;
    [[nodiscard]] static float AbilityCooldown(const Scene& scene, SceneEntity caster, std::uint32_t slot) noexcept;
    [[nodiscard]] static std::span<const PerceivedCombatant> Perceived(const Scene& scene, SceneEntity observer) noexcept;
    [[nodiscard]] static std::optional<PerceivedCombatant> NearestEnemy(const Scene& scene, SceneEntity observer) noexcept;
    [[nodiscard]] static std::span<const GameplayEvent> PendingEvents(const Scene& scene) noexcept;
    [[nodiscard]] static std::vector<GameplayEvent> DrainEvents(Scene& scene);
    // Regeneration, cooldowns and senses for one frame; only while the scene plays.
    static void Advance(Scene& scene, float deltaSeconds);
    // Every observer looks and listens now (Advance does it every kPerceptionInterval).
    static void RefreshPerception(Scene& scene, float elapsedSeconds);
};

} // namespace kb::scene
