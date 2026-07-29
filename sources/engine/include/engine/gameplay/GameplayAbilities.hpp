#pragma once

#include "engine/gameplay/GameplayModules.hpp"

#include <cstdint>
#include <unordered_map>

namespace kb::gameplay {
enum class AbilityTargetRule : std::uint8_t { Self, Any, Friendly, Hostile };
struct GameplayEffect { AttributeId attribute = 0U; float delta = 0.0F; };
struct GameplayAbilityDefinition { GameplayTagId id = 0U; float cooldownSeconds = 0.0F; AttributeId costAttribute = 0U; float cost = 0.0F; AbilityTargetRule targetRule = AbilityTargetRule::Any; GameplayEffect effect{}; };
struct ActiveGameplayAbility { GameplayTagId id = 0U; kb::scene::SceneEntity caster{}; kb::scene::SceneEntity target{}; };
// Call Advance from the owning game loop; this class creates neither a thread nor a scheduler.
class GameplayAbilities final { public: [[nodiscard]] bool Activate(const GameplayAbilityDefinition& definition, kb::scene::SceneEntity caster, GameplayIdentity casterIdentity, kb::scene::SceneEntity target, GameplayIdentity targetIdentity, GameplayModules& modules); [[nodiscard]] bool Cancel(kb::scene::SceneEntity caster) noexcept; void Advance(float deltaSeconds) noexcept; [[nodiscard]] bool IsActive(kb::scene::SceneEntity caster) const noexcept; private: [[nodiscard]] bool IsTargetAllowed(AbilityTargetRule rule, kb::scene::SceneEntity caster, GameplayIdentity casterIdentity, kb::scene::SceneEntity target, GameplayIdentity targetIdentity) const noexcept; std::unordered_map<kb::scene::SceneEntity::IdType, ActiveGameplayAbility> active_; std::unordered_map<kb::scene::SceneEntity::IdType, std::unordered_map<GameplayTagId, float>> cooldowns_; };
} // namespace kb::gameplay
