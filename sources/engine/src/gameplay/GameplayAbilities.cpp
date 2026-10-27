#include "engine/gameplay/GameplayAbilities.hpp"

#include <cmath>

namespace kb::gameplay {
bool GameplayAbilities::IsTargetAllowed(AbilityTargetRule rule, kb::scene::SceneEntity caster, GameplayIdentity casterIdentity, kb::scene::SceneEntity target, GameplayIdentity targetIdentity) const noexcept { if(!caster.IsValid()||!target.IsValid())return false; switch(rule){case AbilityTargetRule::Self:return caster==target; case AbilityTargetRule::Any:return true; case AbilityTargetRule::Friendly:return IsFriendly(casterIdentity,targetIdentity); case AbilityTargetRule::Hostile:return !IsFriendly(casterIdentity,targetIdentity);} return false; }
bool GameplayAbilities::Activate(const GameplayAbilityDefinition& definition, kb::scene::SceneEntity caster,
    GameplayIdentity casterIdentity, kb::scene::SceneEntity target, GameplayIdentity targetIdentity, GameplayModules& modules) {
    if (definition.id == 0U || !std::isfinite(definition.cooldownSeconds) || definition.cooldownSeconds < 0.0F ||
        !std::isfinite(definition.cost) || definition.cost < 0.0F || !std::isfinite(definition.effect.delta) ||
        std::isnan(definition.durationSeconds) || definition.durationSeconds < 0.0F || active_.contains(caster.Id()) ||
        !IsTargetAllowed(definition.targetRule, caster, casterIdentity, target, targetIdentity)) return false;
    if (CooldownRemaining(caster, definition.id) > 0.0F) return false;

    std::optional<AttributeState> costState;
    if (definition.cost > 0.0F) {
        costState = modules.Attribute(caster, definition.costAttribute);
        if (!costState || costState->current < definition.cost) return false;
        costState->current -= definition.cost;
        if (!std::isfinite(costState->current) || costState->current < costState->minimum) return false;
    }
    const bool sameAttribute = costState && caster == target && definition.costAttribute == definition.effect.attribute;
    std::optional<AttributeState> effectState;
    if (definition.effect.attribute != 0U) {
        effectState = sameAttribute ? costState : modules.Attribute(target, definition.effect.attribute);
        if (!effectState) return false;
        effectState->current += definition.effect.delta;
        if (!std::isfinite(effectState->current) || effectState->current < effectState->minimum ||
            effectState->current > effectState->maximum) return false;
    }
    // Validate both writes before committing either one. In particular, an effect
    // on the cost attribute must not be applied before a failing resource debit.
    // Reserve lifetime state before the non-allocating updates of existing attributes.
    cooldowns_[caster.Id()].insert_or_assign(definition.id, 0.0F);
    active_.emplace(caster.Id(), ActiveGameplayAbility{ .id = definition.id, .caster = caster, .target = target,
        .remainingSeconds = definition.durationSeconds, .cooldownSeconds = definition.cooldownSeconds });
    if (costState && !sameAttribute) static_cast<void>(modules.SetAttribute(caster, definition.costAttribute, *costState));
    if (effectState) static_cast<void>(modules.SetAttribute(target, definition.effect.attribute, *effectState));
    return true;
}

bool GameplayAbilities::Remove(kb::scene::SceneEntity entity) noexcept {
    bool removed = active_.erase(entity.Id()) != 0U;
    removed = cooldowns_.erase(entity.Id()) != 0U || removed;
    for (auto ability = active_.begin(); ability != active_.end();) {
        if (ability->second.target != entity) { ++ability; continue; }
        StartCooldown(ability->first, ability->second, 0.0F);
        ability = active_.erase(ability);
        removed = true;
    }
    return removed;
}

// Activate reserves the cooldown slot, so ending an ability never allocates.
void GameplayAbilities::StartCooldown(kb::scene::SceneEntity::IdType caster, const ActiveGameplayAbility& ability, float elapsedAfterEnd) noexcept { const auto owner=cooldowns_.find(caster); if(owner==cooldowns_.end())return; const auto slot=owner->second.find(ability.id); if(slot!=owner->second.end())slot->second=ability.cooldownSeconds>elapsedAfterEnd?ability.cooldownSeconds-elapsedAfterEnd:0.0F; }
bool GameplayAbilities::End(kb::scene::SceneEntity::IdType caster, float elapsedAfterEnd) noexcept { const auto found=active_.find(caster); if(found==active_.end())return false; StartCooldown(caster,found->second,elapsedAfterEnd); active_.erase(found); return true; }
bool GameplayAbilities::Cancel(kb::scene::SceneEntity caster) noexcept { return End(caster.Id(),0.0F); }
bool GameplayAbilities::Complete(kb::scene::SceneEntity caster) noexcept { return End(caster.Id(),0.0F); }
void GameplayAbilities::Advance(float deltaSeconds) noexcept { if(!std::isfinite(deltaSeconds)||deltaSeconds<0.0F)return; for(auto& [owner,byAbility]:cooldowns_){(void)owner;for(auto& [ability,remaining]:byAbility){(void)ability;remaining=remaining>deltaSeconds?remaining-deltaSeconds:0.0F;}} for(auto ability=active_.begin();ability!=active_.end();){ActiveGameplayAbility& state=ability->second; if(state.remainingSeconds>deltaSeconds){state.remainingSeconds-=deltaSeconds;++ability;continue;} StartCooldown(ability->first,state,deltaSeconds-state.remainingSeconds); ability=active_.erase(ability);} }
bool GameplayAbilities::IsActive(kb::scene::SceneEntity caster) const noexcept { return active_.contains(caster.Id()); }
std::optional<ActiveGameplayAbility> GameplayAbilities::Active(kb::scene::SceneEntity caster) const noexcept { const auto found=active_.find(caster.Id()); return found==active_.end()?std::nullopt:std::optional<ActiveGameplayAbility>{found->second}; }
float GameplayAbilities::CooldownRemaining(kb::scene::SceneEntity caster, GameplayTagId ability) const noexcept { const auto owner=cooldowns_.find(caster.Id()); if(owner==cooldowns_.end())return 0.0F; const auto found=owner->second.find(ability); return found==owner->second.end()?0.0F:found->second; }
} // namespace kb::gameplay
