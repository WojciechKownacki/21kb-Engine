#include "AIBrainSystem.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/ecs/World.hpp"
#include "engine/gameplay/AbilityAsset.hpp"
#include "engine/math/DVec3.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/CombatantComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneGameplay.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kb::ai_plugin {
namespace {

using kb::scene::AIAction;
using kb::scene::AICondition;
using kb::scene::Vec3;

// A plain hit, for a brain told to use an empty ability slot: every fighter can at least swing.
constexpr float kPlainHitRange = 2.0F;
constexpr float kPlainHitDamage = 10.0F;
constexpr float kPlainHitInterval = 1.0F;
constexpr float kArrived = 0.35F;

[[nodiscard]] float Flat(Vec3 a, Vec3 b) noexcept {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

[[nodiscard]] Vec3 WorldPosition(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    const kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entity);
    return transform == nullptr ? Vec3{} : transform->worldPosition;
}

[[nodiscard]] float NextRandom(std::uint32_t& state) noexcept {
    state = state * 1664525U + 1013904223U;
    return static_cast<float>(state >> 8U) / static_cast<float>(1U << 24U);
}

// The ability in a slot, or none (an empty slot is the plain hit).
[[nodiscard]] kb::assets::AssetHandle<kb::gameplay::AbilityAsset> Ability(kb::scene::Scene& scene, const kb::scene::CombatantComponent* combatant, float slot) {
    const int index = static_cast<int>(slot) - 1;
    if (combatant == nullptr || index < 0 || index >= static_cast<int>(kb::scene::kCombatantAbilitySlots) || combatant->abilities[index] == 0U) return {};
    return scene.Assets().Manager().Load<kb::gameplay::AbilityAsset>(kb::assets::AssetId{ combatant->abilities[index] });
}

[[nodiscard]] bool UsesNavigation(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    const kb::scene::NavAgent* agent = scene.Components().NavAgents().TryGet(entity);
    return agent != nullptr && agent->enabled && scene.Navigation().HasNavMesh();
}

// Turns the object to look along `direction` on the ground plane.
void Face(kb::scene::Scene& scene, kb::scene::SceneEntity entity, Vec3 direction) {
    const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entity);
    if (length < 0.001F || transform == nullptr) return;
    const kb::scene::Quat world = kb::math::LookRotation(Vec3{ direction.x / length, 0.0F, direction.z / length }, Vec3{ 0.0F, 1.0F, 0.0F });
    kb::scene::Quat parentRotation{};
    if (const kb::scene::SceneEntity parent = scene.Hierarchy().Parent(entity); parent.IsValid()) {
        if (const kb::scene::TransformComponent* up = scene.Transforms().TryGet(parent)) parentRotation = up->worldRotation;
    }
    kb::scene::TransformComponent updated = *transform;
    updated.localRotation = kb::math::Normalize(kb::math::Conjugate(parentRotation) * world);
    scene.Transforms().Set(entity, updated);
}

void SetAnimatorSpeed(kb::scene::Scene& scene, kb::scene::SceneEntity entity, float speed) {
    if (scene.Animators().Exists(entity)) static_cast<void>(scene.Animators().SetFloat(entity, "Speed", speed));
}

// Walks toward `target` at `speed`: the navigation crowd steers it on a mesh, else it goes straight.
void MoveTo(kb::scene::Scene& scene, kb::scene::SceneEntity entity, Vec3 target, float speed, float deltaSeconds) {
    if (UsesNavigation(scene, entity)) {
        kb::scene::NavAgent* agent = scene.Components().NavAgents().TryGet(entity);
        const kb::math::DVec3 origin = scene.Navigation().Origin();
        agent->destination = Vec3{ static_cast<float>(target.x - origin.x), static_cast<float>(target.y - origin.y), static_cast<float>(target.z - origin.z) };
        agent->maxSpeed = speed;
        scene.Components().NavAgents().MarkModified(entity);
        SetAnimatorSpeed(scene, entity, speed);
        return;
    }
    kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entity);
    if (transform == nullptr) return;
    const Vec3 position = transform->worldPosition;
    const Vec3 toward{ target.x - position.x, 0.0F, target.z - position.z };
    const float distance = Flat(target, position);
    if (distance < 0.001F) {
        SetAnimatorSpeed(scene, entity, 0.0F);
        return;
    }
    // A physical character walks by its controller (slopes, steps, collisions), not by teleporting.
    if (scene.Components().CharacterControllers().Has(entity) &&
        kb::scene::PhysicsBackend::CharacterMove(scene, entity, Vec3{ toward.x / distance * speed, 0.0F, toward.z / distance * speed })) {
        Face(scene, entity, toward);
        SetAnimatorSpeed(scene, entity, speed);
        return;
    }
    const float step = std::min(distance, speed * deltaSeconds);
    Vec3 delta{ toward.x / distance * step, 0.0F, toward.z / distance * step };
    // The step happens in the world; a parent's turn and size are undone for the local position.
    if (const kb::scene::SceneEntity parent = scene.Hierarchy().Parent(entity); parent.IsValid()) {
        if (const kb::scene::TransformComponent* up = scene.Transforms().TryGet(parent)) {
            delta = kb::math::Rotate(kb::math::Conjugate(up->worldRotation), delta);
            delta = Vec3{ delta.x / std::max(up->worldScale.x, 0.0001F), delta.y / std::max(up->worldScale.y, 0.0001F),
                delta.z / std::max(up->worldScale.z, 0.0001F) };
        }
    }
    kb::scene::TransformComponent updated = *transform;
    updated.localPosition = Vec3{ updated.localPosition.x + delta.x, updated.localPosition.y + delta.y, updated.localPosition.z + delta.z };
    scene.Transforms().Set(entity, updated);
    Face(scene, entity, toward);
    SetAnimatorSpeed(scene, entity, speed);
}

void Stop(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    if (UsesNavigation(scene, entity)) {
        kb::scene::NavAgent* agent = scene.Components().NavAgents().TryGet(entity);
        const kb::math::DVec3 origin = scene.Navigation().Origin();
        const Vec3 position = WorldPosition(scene, entity);
        agent->destination = Vec3{ static_cast<float>(position.x - origin.x), static_cast<float>(position.y - origin.y),
            static_cast<float>(position.z - origin.z) };
        scene.Components().NavAgents().MarkModified(entity);
    } else if (scene.Components().CharacterControllers().Has(entity)) {
        static_cast<void>(kb::scene::PhysicsBackend::CharacterMove(scene, entity, Vec3{}));
    }
    SetAnimatorSpeed(scene, entity, 0.0F);
}

} // namespace

void AIBrainSystem::Think(kb::scene::Scene& scene, Mind& mind, const kb::scene::AIBrainComponent& brain) {
    const kb::scene::CombatantComponent* self = scene.Components().Combatants().TryGet(mind.entity);
    const Vec3 position = WorldPosition(scene, mind.entity);
    const std::optional<kb::scene::PerceivedCombatant> nearest = scene.Gameplay().NearestEnemy(mind.entity);
    // An enemy beyond the leash from home is let go.
    std::optional<kb::scene::PerceivedCombatant> enemy;
    if (nearest.has_value() && Flat(nearest->lastKnownPosition, mind.home) <= brain.leashDistance + brain.wanderRadius) enemy = nearest;
    // A creature of no team (a rabbit, a villager) is nobody's enemy, but anyone with a team it
    // senses is a threat to it: that is whom Flee and KeepDistance run from.
    if (!enemy.has_value() && self != nullptr && self->team == 0U) {
        for (const kb::scene::PerceivedCombatant& seen : scene.Gameplay().Perceived(mind.entity)) {
            const kb::scene::CombatantComponent* other = scene.Components().Combatants().TryGet(seen.entity);
            if (seen.sensed && other != nullptr && other->team != 0U && (!enemy.has_value() || seen.distance < enemy->distance)) enemy = seen;
        }
    }
    const float distance = enemy.has_value() ? Flat(position, WorldPosition(scene, enemy->entity)) : 0.0F;

    const auto holds = [&](const kb::scene::AIRule& rule) {
        switch (rule.when) {
        case AICondition::Never: return false;
        case AICondition::Always: return true;
        case AICondition::SeesEnemy: return enemy.has_value() && enemy->sensed;
        case AICondition::KnowsEnemy: return enemy.has_value();
        case AICondition::EnemyCloserThan: return enemy.has_value() && distance < rule.whenValue;
        case AICondition::EnemyFartherThan: return enemy.has_value() && distance > rule.whenValue;
        case AICondition::HealthBelowPercent:
            return self != nullptr && self->maximumHealth > 0.0F && self->health / self->maximumHealth * 100.0F < rule.whenValue;
        case AICondition::ManaAbovePercent:
            return self != nullptr && self->maximumMana > 0.0F && self->mana / self->maximumMana * 100.0F > rule.whenValue;
        case AICondition::AbilityReady: {
            const auto slot = static_cast<std::uint32_t>(rule.whenValue);
            return self != nullptr && slot >= 1U && slot <= kb::scene::kCombatantAbilitySlots && self->abilities[slot - 1U] != 0U &&
                scene.Gameplay().AbilityCooldown(mind.entity, slot) <= 0.0F;
        }
        case AICondition::FarFromHome: return Flat(position, mind.home) > rule.whenValue;
        }
        return false;
    };
    // Actions that need an enemy cannot be done without one; then the next rule is tried.
    const auto doable = [&](const kb::scene::AIRule& rule) {
        switch (rule.action) {
        case AIAction::Chase:
        case AIAction::UseAbility:
        case AIAction::KeepDistance:
        case AIAction::Flee: return enemy.has_value();
        default: return true;
        }
    };
    // Backing away goes on until the wanted distance is reached, so a rule that starts it at 5 m
    // and wants 8 m does not stop at 5.1 m and walk back in.
    if (mind.action == AIAction::KeepDistance && enemy.has_value() && enemy->entity == mind.enemy && distance < mind.actionValue) return;
    mind.action = AIAction::Idle;
    mind.actionValue = 0.0F;
    mind.enemy = enemy.has_value() ? enemy->entity : kb::scene::SceneEntity{};
    for (const kb::scene::AIRule& rule : brain.rules) {
        if (holds(rule) && doable(rule)) {
            mind.action = rule.action;
            mind.actionValue = rule.actionValue;
            return;
        }
    }
}

void AIBrainSystem::Act(kb::scene::Scene& scene, Mind& mind, const kb::scene::AIBrainComponent& brain, float deltaSeconds) {
    const Vec3 position = WorldPosition(scene, mind.entity);
    const bool hasEnemy = mind.enemy.IsValid() && scene.Gameplay().IsAlive(mind.enemy);
    const Vec3 enemyAt = hasEnemy ? WorldPosition(scene, mind.enemy) : Vec3{};
    const float enemyDistance = hasEnemy ? Flat(position, enemyAt) : 0.0F;
    switch (mind.action) {
    case AIAction::Idle:
        Stop(scene, mind.entity);
        return;
    case AIAction::ReturnHome:
        if (Flat(position, mind.home) > kArrived) MoveTo(scene, mind.entity, mind.home, brain.moveSpeed, deltaSeconds);
        else Stop(scene, mind.entity);
        return;
    case AIAction::Wander: {
        mind.wanderTimer -= deltaSeconds;
        if (!mind.hasWanderPoint || mind.wanderTimer <= 0.0F) {
            const float angle = NextRandom(mind.random) * 6.2831853F;
            const float reach = std::sqrt(NextRandom(mind.random)) * brain.wanderRadius;
            mind.wanderPoint = Vec3{ mind.home.x + std::cos(angle) * reach, mind.home.y, mind.home.z + std::sin(angle) * reach };
            mind.wanderTimer = 3.0F + NextRandom(mind.random) * 4.0F;
            mind.hasWanderPoint = true;
        }
        if (Flat(position, mind.wanderPoint) > kArrived) MoveTo(scene, mind.entity, mind.wanderPoint, brain.moveSpeed * 0.4F, deltaSeconds);
        else Stop(scene, mind.entity);
        return;
    }
    case AIAction::Chase:
        if (!hasEnemy) return Stop(scene, mind.entity);
        if (enemyDistance > kPlainHitRange * 0.75F) MoveTo(scene, mind.entity, enemyAt, brain.moveSpeed, deltaSeconds);
        else {
            Stop(scene, mind.entity);
            Face(scene, mind.entity, enemyAt - position);
        }
        return;
    case AIAction::UseAbility: {
        if (!hasEnemy) return Stop(scene, mind.entity);
        const kb::scene::CombatantComponent* self = scene.Components().Combatants().TryGet(mind.entity);
        const auto ability = Ability(scene, self, mind.actionValue);
        const bool plain = !ability.IsLoaded();
        const float range = plain ? kPlainHitRange : (ability->target == kb::gameplay::AbilityTarget::Self ? 1.0e9F : ability->range);
        if (enemyDistance > std::max(0.5F, range - 0.3F)) {
            MoveTo(scene, mind.entity, enemyAt, brain.moveSpeed, deltaSeconds);
            return;
        }
        Stop(scene, mind.entity);
        Face(scene, mind.entity, enemyAt - position);
        if (plain) {
            mind.plainHitTimer -= deltaSeconds;
            if (mind.plainHitTimer <= 0.0F) {
                static_cast<void>(scene.Gameplay().ApplyDamage(mind.enemy, kPlainHitDamage, kb::gameplay::DamageType::Physical, mind.entity));
                if (scene.Animators().Exists(mind.entity)) static_cast<void>(scene.Animators().SetTrigger(mind.entity, "Attack"));
                mind.plainHitTimer = kPlainHitInterval;
            }
        } else if (scene.Gameplay().AbilityCooldown(mind.entity, static_cast<std::uint32_t>(mind.actionValue)) <= 0.0F) {
            static_cast<void>(scene.Gameplay().UseAbility(mind.entity, static_cast<std::uint32_t>(mind.actionValue),
                ability->target == kb::gameplay::AbilityTarget::Enemy ? mind.enemy : kb::scene::SceneEntity{}));
        }
        return;
    }
    case AIAction::KeepDistance:
    case AIAction::Flee: {
        if (!hasEnemy) return Stop(scene, mind.entity);
        const float wanted = mind.action == AIAction::Flee ? enemyDistance + 8.0F : mind.actionValue;
        if (enemyDistance >= wanted) {
            Stop(scene, mind.entity);
            Face(scene, mind.entity, enemyAt - position);
            return;
        }
        Vec3 away{ position.x - enemyAt.x, 0.0F, position.z - enemyAt.z };
        const float length = std::max(0.001F, std::sqrt(away.x * away.x + away.z * away.z));
        away = Vec3{ away.x / length, 0.0F, away.z / length };
        const float run = wanted - enemyDistance + 1.0F;
        MoveTo(scene, mind.entity, Vec3{ position.x + away.x * run, position.y, position.z + away.z * run }, brain.moveSpeed, deltaSeconds);
        return;
    }
    }
}

void AIBrainSystem::OnUpdate(kb::scene::SceneSystemContext& context) {
    kb::scene::Scene& scene = context.GetScene();
    if (!scene.Runtime().IsPlaying()) return;
    const float deltaSeconds = context.DeltaSeconds();
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) return;
    std::vector<kb::scene::SceneEntity> brains;
    context.EcsWorld().CreateQuery<kb::scene::AIBrainComponent>().ForEach(
        [](kb::scene::SceneEntity entity, const kb::scene::AIBrainComponent&, void* list) {
            static_cast<std::vector<kb::scene::SceneEntity>*>(list)->push_back(entity);
        }, &brains);
    std::erase_if(minds_, [&scene](const auto& entry) { return !scene.Entities().IsAlive(entry.second.entity); });
    for (const kb::scene::SceneEntity entity : brains) {
        const kb::scene::AIBrainComponent* found = scene.Components().AIBrains().TryGet(entity);
        if (found == nullptr) continue;
        const kb::scene::AIBrainComponent brain = *found;
        auto [it, created] = minds_.try_emplace(entity.Id());
        Mind& mind = it->second;
        if (created) {
            mind.entity = entity;
            mind.home = WorldPosition(scene, entity);
            mind.random = static_cast<std::uint32_t>(entity.Id() * 2654435761ULL) | 1U;
            // Spread the thinking of many brains over the interval.
            mind.thinkTimer = NextRandom(mind.random) * kThinkInterval;
        }
        const kb::scene::CombatantComponent* self = scene.Components().Combatants().TryGet(entity);
        const bool dead = self != nullptr && !kb::scene::IsCombatantAlive(*self);
        if (!brain.enabled || dead || !scene.Entities().IsActive(entity)) {
            if (mind.action != AIAction::Idle) Stop(scene, entity);
            mind.action = AIAction::Idle;
            continue;
        }
        mind.thinkTimer -= deltaSeconds;
        if (mind.thinkTimer <= 0.0F) {
            Think(scene, mind, brain);
            mind.thinkTimer += kThinkInterval;
            if (mind.thinkTimer < 0.0F) mind.thinkTimer = kThinkInterval;
        }
        Act(scene, mind, brain, deltaSeconds);
    }
}

} // namespace kb::ai_plugin
