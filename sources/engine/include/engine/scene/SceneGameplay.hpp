#pragma once

#include "engine/gameplay/Damage.hpp"
#include "engine/scene/CombatantComponent.hpp"
#include "engine/scene/Perception.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kb::scene {

class Scene;

enum class GameplayEventKind : std::uint8_t {
    Damaged,      // target lost `amount` health to source
    Healed,       // target gained `amount` health
    Died,         // target's health reached 0 (once)
    Revived,      // target came back with `amount` health
    AbilityUsed,  // source used `ability` on target
    Spotted,      // source (an observer) started to sense the hostile target
    Lost,         // source lost every sense of the hostile target and forgot it
};

// Everything that happened in fights, in order; read them with DrainEvents (C++), Gameplay.Events
// (scripts) or let the UI react to them. The engine also fires the Animator triggers "Hit" and
// "Die" (and sets the bool "Dead") on combatants whose controller declares them.
struct GameplayEvent {
    GameplayEventKind kind = GameplayEventKind::Damaged;
    SceneEntity source{};
    SceneEntity target{};
    float amount = 0.0F;
    kb::gameplay::DamageType damageType = kb::gameplay::DamageType::Physical;
    std::uint64_t ability = 0U;
    // Counts every event of the scene from 1: a reader that keeps the last one it saw reads each
    // event once without taking it from the others (scripts do; DrainEvents is for C++ owners).
    std::uint64_t sequence = 0U;
};

struct DamageOutcome {
    bool applied = false;
    // Health actually lost (or gained by Heal/Revive), after resistances and limits.
    float amount = 0.0F;
    bool killed = false;
    std::string reason;
};

struct AbilityOutcome {
    bool used = false;
    std::string reason;
    SceneEntity target{};
    std::uint32_t hits = 0U;
};

// A combatant an observer senses now (`sensed`), or sensed less than a few seconds ago and still
// remembers where (`lastKnownPosition`).
struct PerceivedCombatant {
    SceneEntity entity{};
    Vec3 lastKnownPosition{};
    float distance = 0.0F;
    float secondsSinceSensed = 0.0F;
    PerceptionSense sense = PerceptionSense::Sight;
    bool sensed = false;
    bool hostile = false;
};

class SceneGameplayQueries {
public:
    explicit SceneGameplayQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool IsAlive(SceneEntity entity) const noexcept;
    [[nodiscard]] bool AreHostile(SceneEntity first, SceneEntity second) const noexcept;
    // Seconds until the ability in `slot` (1-8) can be used again; 0 when ready.
    [[nodiscard]] float AbilityCooldown(SceneEntity caster, std::uint32_t slot) const noexcept;
    [[nodiscard]] std::span<const PerceivedCombatant> Perceived(SceneEntity observer) const noexcept;
    // The closest hostile combatant the observer senses now (else the closest remembered one).
    [[nodiscard]] std::optional<PerceivedCombatant> NearestEnemy(SceneEntity observer) const noexcept;
    [[nodiscard]] std::span<const GameplayEvent> PendingEvents() const noexcept;

private:
    const Scene& scene_;
};

// Fights in a scene: damage and healing through each Combatant's resistances, abilities with
// mana, cooldowns, range and targets, and what every combatant sees and hears. All calls are
// for the game thread; they apply at once and leave events behind.
class SceneGameplay {
public:
    static constexpr std::size_t kMaxPendingEvents = 8192U;
    explicit SceneGameplay(Scene& scene) noexcept;

    DamageOutcome ApplyDamage(SceneEntity target, float amount,
        kb::gameplay::DamageType type = kb::gameplay::DamageType::Physical, SceneEntity source = {});
    DamageOutcome Heal(SceneEntity target, float amount, SceneEntity source = {});
    // Brings a dead combatant back with `health` (its maximum when not positive).
    DamageOutcome Revive(SceneEntity target, float health = 0.0F);
    // Uses the ability in `slot` (1-8) of the caster's Combatant. Without a target an Enemy or Ally
    // ability takes the nearest fitting combatant the caster perceives within range.
    AbilityOutcome UseAbility(SceneEntity caster, std::uint32_t slot, SceneEntity target = {});

    [[nodiscard]] bool IsAlive(SceneEntity entity) const noexcept;
    [[nodiscard]] bool AreHostile(SceneEntity first, SceneEntity second) const noexcept;
    [[nodiscard]] float AbilityCooldown(SceneEntity caster, std::uint32_t slot) const noexcept;
    [[nodiscard]] std::span<const PerceivedCombatant> Perceived(SceneEntity observer) const noexcept;
    [[nodiscard]] std::optional<PerceivedCombatant> NearestEnemy(SceneEntity observer) const noexcept;
    [[nodiscard]] std::span<const GameplayEvent> PendingEvents() const noexcept;
    [[nodiscard]] std::vector<GameplayEvent> DrainEvents();

private:
    Scene& scene_;
};

} // namespace kb::scene
