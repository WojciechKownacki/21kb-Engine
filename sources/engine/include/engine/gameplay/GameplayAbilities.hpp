#pragma once

#include "engine/gameplay/GameplayModules.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

namespace kb::gameplay {
enum class AbilityTargetRule : std::uint8_t { Self, Any, Friendly, Hostile };
// Duration value for an ability that stays active until Complete() or Cancel() ends it.
inline constexpr float kGameplayAbilityUntilCompleted = std::numeric_limits<float>::infinity();
struct GameplayEffect { AttributeId attribute = 0U; float delta = 0.0F; };
// Activation commits cost and effect immediately. The ability then stays active for durationSeconds
// (0 = it ends on the next Advance) or until Complete()/Cancel(); its cooldown starts when it ends.
struct GameplayAbilityDefinition { GameplayTagId id = 0U; float cooldownSeconds = 0.0F; AttributeId costAttribute = 0U; float cost = 0.0F; AbilityTargetRule targetRule = AbilityTargetRule::Any; GameplayEffect effect{}; float durationSeconds = 0.0F; };
struct ActiveGameplayAbility { GameplayTagId id = 0U; kb::scene::SceneEntity caster{}; kb::scene::SceneEntity target{}; float remainingSeconds = 0.0F; float cooldownSeconds = 0.0F; };
// Call Advance from the owning game loop; this class creates neither a thread nor a scheduler.
class GameplayAbilities final {
public:
    [[nodiscard]] bool Activate(const GameplayAbilityDefinition& definition, kb::scene::SceneEntity caster, GameplayIdentity casterIdentity, kb::scene::SceneEntity target, GameplayIdentity targetIdentity, GameplayModules& modules);
    // Ends the caster's active ability early and starts its cooldown.
    [[nodiscard]] bool Cancel(kb::scene::SceneEntity caster) noexcept;
    // Called by the ability's owner when its work finished before (or without) a fixed duration.
    [[nodiscard]] bool Complete(kb::scene::SceneEntity caster) noexcept;
    // Ticks cooldowns, then ends every active ability whose duration elapsed and starts its cooldown
    // with the time left over from this step.
    void Advance(float deltaSeconds) noexcept;
    [[nodiscard]] bool IsActive(kb::scene::SceneEntity caster) const noexcept;
    [[nodiscard]] std::optional<ActiveGameplayAbility> Active(kb::scene::SceneEntity caster) const noexcept;
    [[nodiscard]] float CooldownRemaining(kb::scene::SceneEntity caster, GameplayTagId ability) const noexcept;
private:
    [[nodiscard]] bool IsTargetAllowed(AbilityTargetRule rule, kb::scene::SceneEntity caster, GameplayIdentity casterIdentity, kb::scene::SceneEntity target, GameplayIdentity targetIdentity) const noexcept;
    void StartCooldown(kb::scene::SceneEntity::IdType caster, const ActiveGameplayAbility& ability, float elapsedAfterEnd) noexcept;
    bool End(kb::scene::SceneEntity::IdType caster, float elapsedAfterEnd) noexcept;
    std::unordered_map<kb::scene::SceneEntity::IdType, ActiveGameplayAbility> active_;
    std::unordered_map<kb::scene::SceneEntity::IdType, std::unordered_map<GameplayTagId, float>> cooldowns_;
};
} // namespace kb::gameplay
