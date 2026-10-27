#include "scene/SceneGameplayService.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/gameplay/AbilityAsset.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityCounter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace kb::scene {
namespace {

using kb::gameplay::DamageType;

// Where eyes and bodies are, above the object's origin (characters stand on their origin).
constexpr float kEyeHeight = 1.5F;
constexpr float kBodyHeight = 1.0F;
// Range checks forgive a little, so "in range" at the edge of a swing does not flicker.
constexpr float kRangeTolerance = 0.25F;

[[nodiscard]] GameplayRuntimeState& State(Scene& scene) noexcept { return SceneAccess::State(scene).gameplay; }
[[nodiscard]] const GameplayRuntimeState& State(const Scene& scene) noexcept { return SceneAccess::State(scene).gameplay; }

[[nodiscard]] CombatantComponent* Combatant(Scene& scene, SceneEntity entity) noexcept {
    return scene.Entities().IsAlive(entity) ? SceneAccess::State(scene).componentStorage.Combatants().TryGet(entity) : nullptr;
}

[[nodiscard]] const CombatantComponent* Combatant(const Scene& scene, SceneEntity entity) noexcept {
    return scene.Entities().IsAlive(entity) ? SceneAccess::State(scene).componentStorage.Combatants().TryGet(entity) : nullptr;
}

void Touch(Scene& scene, SceneEntity entity) noexcept { SceneAccess::State(scene).componentStorage.Combatants().MarkModified(entity); }

void Emit(Scene& scene, GameplayEvent event) {
    std::vector<GameplayEvent>& events = State(scene).events;
    // Nobody read them for a long time: keep the newest half.
    if (events.size() >= SceneGameplay::kMaxPendingEvents) events.erase(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(events.size() / 2U));
    event.sequence = State(scene).nextEventSequence++;
    events.push_back(event);
}

[[nodiscard]] Vec3 Position(const Scene& scene, SceneEntity entity) noexcept {
    const TransformComponent* transform = scene.Transforms().TryGet(entity);
    return transform == nullptr ? Vec3{} : transform->worldPosition;
}

[[nodiscard]] float GameplayDistance(Vec3 a, Vec3 b) noexcept {
    const Vec3 d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

[[nodiscard]] bool IsBelowOrSelf(const Scene& scene, SceneEntity entity, SceneEntity ancestor) noexcept {
    for (SceneEntity current = entity; current.IsValid(); current = scene.Hierarchy().Parent(current)) {
        if (current == ancestor) return true;
    }
    return false;
}

[[nodiscard]] bool UsableCombatant(const CombatantComponent* combatant) noexcept {
    return combatant != nullptr && combatant->enabled && IsCombatantAlive(*combatant);
}

// Nothing but the target (or what it carries) between the eyes and the target's body. Colliders
// of the observer's own body and gear do not block its view.
[[nodiscard]] bool InLineOfSight(Scene& scene, SceneEntity observer, Vec3 eye, SceneEntity target, Vec3 body) {
    if (!PhysicsBackend::HasBackend(scene)) return true;
    const Vec3 delta = body - eye;
    const float distance = GameplayDistance(body, eye);
    if (distance <= 0.001F) return true;
    const Vec3 direction{ delta.x / distance, delta.y / distance, delta.z / distance };
    std::array<PhysicsCastResult, 16> storage{};
    kb::library::ArrayNonAlloc<PhysicsCastResult> hits{ std::span<PhysicsCastResult>{ storage } };
    RaycastAllNonAlloc(scene, eye, direction, distance, kPhysicsAllLayers, hits);
    const PhysicsCastResult* nearest = nullptr;
    for (const PhysicsCastResult& hit : hits) {
        if (!hit.hit || IsBelowOrSelf(scene, hit.entity, observer)) continue;
        if (nearest == nullptr || hit.distance < nearest->distance) nearest = &hit;
    }
    return nearest == nullptr || IsBelowOrSelf(scene, nearest->entity, target);
}

void FireAnimatorTrigger(Scene& scene, SceneEntity entity, std::string_view trigger) {
    if (!trigger.empty() && scene.Animators().Exists(entity)) static_cast<void>(scene.Animators().SetTrigger(entity, trigger));
}

[[nodiscard]] std::vector<SceneEntity> AllCombatants(Scene& scene) {
    struct Collect { std::vector<SceneEntity> entities; };
    Collect collected;
    SceneAccess::State(scene).world.CreateQuery<CombatantComponent>().ForEach(
        [](SceneEntity entity, const CombatantComponent&, void* context) {
            static_cast<Collect*>(context)->entities.push_back(entity);
        }, &collected);
    return std::move(collected.entities);
}

[[nodiscard]] bool RelationFits(kb::gameplay::AbilityTarget rule, const CombatantComponent& caster, SceneEntity casterEntity,
    const CombatantComponent& target, SceneEntity targetEntity) noexcept {
    const bool hostile = AreCombatantTeamsHostile(caster.team, target.team);
    switch (rule) {
    case kb::gameplay::AbilityTarget::Enemy: return hostile && casterEntity != targetEntity;
    case kb::gameplay::AbilityTarget::Ally: return !hostile;
    case kb::gameplay::AbilityTarget::Self: return casterEntity == targetEntity;
    }
    return false;
}

} // namespace

DamageOutcome SceneGameplayService::ApplyDamage(Scene& scene, SceneEntity target, float amount, DamageType type, SceneEntity source) {
    if (type == DamageType::Healing) return Heal(scene, target, amount, source);
    CombatantComponent* combatant = Combatant(scene, target);
    if (combatant == nullptr || !combatant->enabled) return DamageOutcome{ .reason = "the target is not an enabled Combatant" };
    if (!std::isfinite(amount) || amount <= 0.0F) return DamageOutcome{ .reason = "damage must be a positive number" };
    if (!IsCombatantAlive(*combatant)) return DamageOutcome{ .reason = "the target is already dead" };
    if (combatant->invulnerable) return DamageOutcome{ .reason = "the target is invulnerable" };
    const float resistance = combatant->resistances[static_cast<std::size_t>(type)];
    const float effective = std::max(0.0F, amount * (1.0F - resistance / 100.0F));
    const float lost = std::min(combatant->health, effective);
    combatant->health = std::max(0.0F, combatant->health - lost);
    const bool killed = combatant->health <= 0.0F;
    Touch(scene, target);
    Emit(scene, GameplayEvent{ .kind = GameplayEventKind::Damaged, .source = source, .target = target, .amount = lost, .damageType = type });
    if (killed) {
        State(scene).dead.insert(target.Id());
        Emit(scene, GameplayEvent{ .kind = GameplayEventKind::Died, .source = source, .target = target, .damageType = type });
        FireAnimatorTrigger(scene, target, "Die");
        if (scene.Animators().Exists(target)) static_cast<void>(scene.Animators().SetBool(target, "Dead", true));
    } else if (lost > 0.0F) {
        FireAnimatorTrigger(scene, target, "Hit");
    }
    return DamageOutcome{ .applied = true, .amount = lost, .killed = killed };
}

DamageOutcome SceneGameplayService::Heal(Scene& scene, SceneEntity target, float amount, SceneEntity source) {
    CombatantComponent* combatant = Combatant(scene, target);
    if (combatant == nullptr || !combatant->enabled) return DamageOutcome{ .reason = "the target is not an enabled Combatant" };
    if (!std::isfinite(amount) || amount <= 0.0F) return DamageOutcome{ .reason = "healing must be a positive number" };
    if (!IsCombatantAlive(*combatant)) return DamageOutcome{ .reason = "the dead are brought back with Revive, not Heal" };
    const float gained = std::min(amount, combatant->maximumHealth - combatant->health);
    combatant->health += gained;
    Touch(scene, target);
    Emit(scene, GameplayEvent{ .kind = GameplayEventKind::Healed, .source = source, .target = target, .amount = gained,
        .damageType = DamageType::Healing });
    return DamageOutcome{ .applied = true, .amount = gained };
}

DamageOutcome SceneGameplayService::Revive(Scene& scene, SceneEntity target, float health) {
    CombatantComponent* combatant = Combatant(scene, target);
    if (combatant == nullptr) return DamageOutcome{ .reason = "the target is not a Combatant" };
    if (IsCombatantAlive(*combatant)) return DamageOutcome{ .reason = "the target is alive" };
    if (!std::isfinite(health)) return DamageOutcome{ .reason = "health must be a number" };
    combatant->health = health > 0.0F ? std::min(health, combatant->maximumHealth) : combatant->maximumHealth;
    if (combatant->health <= 0.0F) return DamageOutcome{ .reason = "the combatant's maximum health is 0" };
    State(scene).dead.erase(target.Id());
    Touch(scene, target);
    if (scene.Animators().Exists(target)) static_cast<void>(scene.Animators().SetBool(target, "Dead", false));
    Emit(scene, GameplayEvent{ .kind = GameplayEventKind::Revived, .target = target, .amount = combatant->health });
    return DamageOutcome{ .applied = true, .amount = combatant->health };
}

AbilityOutcome SceneGameplayService::UseAbility(Scene& scene, SceneEntity caster, std::uint32_t slot, SceneEntity target) {
    CombatantComponent* self = Combatant(scene, caster);
    if (!UsableCombatant(self)) return AbilityOutcome{ .reason = "the caster is not an enabled, living Combatant" };
    if (slot == 0U || slot > kCombatantAbilitySlots) return AbilityOutcome{ .reason = "ability slots are 1 to 8" };
    const std::uint64_t abilityId = self->abilities[slot - 1U];
    if (abilityId == 0U) return AbilityOutcome{ .reason = "ability slot " + std::to_string(slot) + " is empty" };
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetHandle<kb::gameplay::AbilityAsset> ability = manager.Load<kb::gameplay::AbilityAsset>(kb::assets::AssetId{ abilityId });
    if (!ability.IsLoaded()) return AbilityOutcome{ .reason = "the ability in slot " + std::to_string(slot) + " could not be loaded: " + manager.LastError() };
    GameplayRuntimeState& state = State(scene);
    std::array<float, kCombatantAbilitySlots>& cooldowns = state.cooldowns[caster.Id()];
    if (cooldowns[slot - 1U] > 0.0F) {
        return AbilityOutcome{ .reason = "the ability is cooling down (" + std::to_string(cooldowns[slot - 1U]) + " s left)" };
    }
    if (self->mana < ability->manaCost) return AbilityOutcome{ .reason = "not enough mana" };

    const Vec3 casterPosition = Position(scene, caster);
    const auto inRange = [&](SceneEntity other) { return GameplayDistance(casterPosition, Position(scene, other)) <= ability->range + kRangeTolerance; };
    SceneEntity chosen{};
    if (ability->target == kb::gameplay::AbilityTarget::Self) {
        chosen = caster;
    } else if (target.IsValid()) {
        const CombatantComponent* other = Combatant(scene, target);
        if (!UsableCombatant(other)) return AbilityOutcome{ .reason = "the target is not an enabled, living Combatant" };
        if (!RelationFits(ability->target, *self, caster, *other, target)) {
            return AbilityOutcome{ .reason = ability->target == kb::gameplay::AbilityTarget::Enemy
                ? "the target is not an enemy (give the two different non-zero teams)" : "the target is an enemy" };
        }
        if (!inRange(target)) return AbilityOutcome{ .reason = "the target is out of range" };
        chosen = target;
    } else {
        // The nearest fitting combatant the caster perceives, in range; an Ally ability falls back to the caster.
        float best = std::numeric_limits<float>::infinity();
        for (const PerceivedCombatant& seen : Perceived(scene, caster)) {
            const CombatantComponent* other = Combatant(scene, seen.entity);
            if (!seen.sensed || !UsableCombatant(other) || !RelationFits(ability->target, *self, caster, *other, seen.entity) || !inRange(seen.entity)) continue;
            const float distance = GameplayDistance(casterPosition, Position(scene, seen.entity));
            if (distance < best) {
                best = distance;
                chosen = seen.entity;
            }
        }
        if (!chosen.IsValid() && ability->target == kb::gameplay::AbilityTarget::Ally) chosen = caster;
        if (!chosen.IsValid()) return AbilityOutcome{ .reason = "no enemy in range" };
    }

    self->mana -= ability->manaCost;
    Touch(scene, caster);
    cooldowns[slot - 1U] = ability->cooldownSeconds;
    FireAnimatorTrigger(scene, caster, ability->animatorTrigger);
    Emit(scene, GameplayEvent{ .kind = GameplayEventKind::AbilityUsed, .source = caster, .target = chosen, .damageType = ability->damageType,
        .ability = abilityId });

    std::vector<SceneEntity> victims;
    if (ability->radius > 0.0F) {
        const Vec3 center = Position(scene, chosen);
        // A Self ability with a radius reaches whom its damage type is for: enemies, or allies when it heals.
        const kb::gameplay::AbilityTarget rule = ability->target == kb::gameplay::AbilityTarget::Self
            ? (ability->damageType == DamageType::Healing ? kb::gameplay::AbilityTarget::Ally : kb::gameplay::AbilityTarget::Enemy)
            : ability->target;
        for (const SceneEntity other : AllCombatants(scene)) {
            const CombatantComponent* candidate = Combatant(scene, other);
            if (UsableCombatant(candidate) && RelationFits(rule, *self, caster, *candidate, other) &&
                GameplayDistance(center, Position(scene, other)) <= ability->radius) {
                victims.push_back(other);
            }
        }
    } else {
        victims.push_back(chosen);
    }
    std::uint32_t hits = 0U;
    for (const SceneEntity victim : victims) {
        if (ability->amount <= 0.0F) break;
        const DamageOutcome outcome = ability->damageType == DamageType::Healing
            ? Heal(scene, victim, ability->amount, caster)
            : ApplyDamage(scene, victim, ability->amount, ability->damageType, caster);
        if (outcome.applied) ++hits;
    }
    return AbilityOutcome{ .used = true, .target = chosen, .hits = hits };
}

bool SceneGameplayService::IsAlive(const Scene& scene, SceneEntity entity) noexcept {
    const CombatantComponent* combatant = Combatant(scene, entity);
    return combatant != nullptr && IsCombatantAlive(*combatant);
}

bool SceneGameplayService::AreHostile(const Scene& scene, SceneEntity first, SceneEntity second) noexcept {
    const CombatantComponent* a = Combatant(scene, first);
    const CombatantComponent* b = Combatant(scene, second);
    return a != nullptr && b != nullptr && first != second && AreCombatantTeamsHostile(a->team, b->team);
}

float SceneGameplayService::AbilityCooldown(const Scene& scene, SceneEntity caster, std::uint32_t slot) noexcept {
    if (slot == 0U || slot > kCombatantAbilitySlots) return 0.0F;
    const auto found = State(scene).cooldowns.find(caster.Id());
    return found == State(scene).cooldowns.end() ? 0.0F : std::max(0.0F, found->second[slot - 1U]);
}

std::span<const PerceivedCombatant> SceneGameplayService::Perceived(const Scene& scene, SceneEntity observer) noexcept {
    const auto found = State(scene).perceived.find(observer.Id());
    return found == State(scene).perceived.end() ? std::span<const PerceivedCombatant>{} : std::span<const PerceivedCombatant>{ found->second };
}

std::optional<PerceivedCombatant> SceneGameplayService::NearestEnemy(const Scene& scene, SceneEntity observer) noexcept {
    std::optional<PerceivedCombatant> best;
    for (const PerceivedCombatant& seen : Perceived(scene, observer)) {
        if (!seen.hostile || !IsAlive(scene, seen.entity)) continue;
        // Sensed beats remembered; then nearer beats farther.
        if (!best.has_value() || (seen.sensed && !best->sensed) || (seen.sensed == best->sensed && seen.distance < best->distance)) best = seen;
    }
    return best;
}

std::span<const GameplayEvent> SceneGameplayService::PendingEvents(const Scene& scene) noexcept { return State(scene).events; }

std::vector<GameplayEvent> SceneGameplayService::DrainEvents(Scene& scene) {
    std::vector<GameplayEvent> drained;
    drained.swap(State(scene).events);
    return drained;
}

void SceneGameplayService::RefreshPerception(Scene& scene, float elapsedSeconds) {
    GameplayRuntimeState& state = State(scene);
    const std::vector<SceneEntity> combatants = AllCombatants(scene);
    struct Subject { SceneEntity entity; Vec3 position; std::uint32_t team; };
    std::vector<Subject> subjects;
    subjects.reserve(combatants.size());
    std::unordered_set<std::uint64_t> living;
    living.reserve(combatants.size());
    for (const SceneEntity entity : combatants) {
        const CombatantComponent* component = Combatant(scene, entity);
        if (component != nullptr && IsCombatantAlive(*component)) living.insert(entity.Id());
        if (UsableCombatant(component) && scene.Entities().IsActive(entity)) {
            subjects.push_back({ entity, Position(scene, entity), component->team });
        }
    }
    // Sample ECS state once per refresh; inner perception loops only read this
    // snapshot. Reuse the lookup allocation across observers instead of scanning
    // the entire remembered list for every visible subject.
    std::unordered_map<std::uint64_t, std::size_t> remembered;
    for (const SceneEntity observer : combatants) {
        const CombatantComponent* eyes = Combatant(scene, observer);
        std::vector<PerceivedCombatant>& memory = state.perceived[observer.Id()];
        if (!UsableCombatant(eyes) || !scene.Entities().IsActive(observer)) {
            memory.clear();
            continue;
        }
        const TransformComponent* transform = scene.Transforms().TryGet(observer);
        const Vec3 position = transform == nullptr ? Vec3{} : transform->worldPosition;
        const Vec3 forward = transform == nullptr ? Vec3{ 0.0F, 0.0F, 1.0F } : kb::math::Rotate(transform->worldRotation, Vec3{ 0.0F, 0.0F, 1.0F });
        const Vec3 eye{ position.x, position.y + kEyeHeight, position.z };
        const float halfAngle = eyes->sightAngleDegrees * 0.5F;
        const float forwardFlat = std::sqrt(forward.x * forward.x + forward.z * forward.z);
        const float coneCosine = std::cos(halfAngle * 3.14159265358979F / 180.0F);
        const float maximumRange = std::max(eyes->hearingRange, eyes->sightRange);
        remembered.clear();
        remembered.reserve(memory.size());
        for (PerceivedCombatant& entry : memory) {
            remembered.emplace(entry.entity.Id(), static_cast<std::size_t>(&entry - memory.data()));
            entry.sensed = false;
            entry.secondsSinceSensed += elapsedSeconds;
        }
        for (const Subject& subject : subjects) {
            const SceneEntity other = subject.entity;
            if (other == observer) continue;
            const Vec3 at = subject.position;
            const Vec3 offset = at - position;
            if (offset.x * offset.x + offset.y * offset.y + offset.z * offset.z > maximumRange * maximumRange) continue;
            const float distance = GameplayDistance(position, at);
            std::optional<PerceptionSense> sense;
            if (distance <= eyes->hearingRange) sense = PerceptionSense::Hearing;
            if (distance <= eyes->sightRange) {
                // Sight is a cone on the ground plane (a tall foe right above still counts as seen ahead).
                const float dx = at.x - position.x;
                const float dz = at.z - position.z;
                const float flat = std::sqrt(dx * dx + dz * dz);
                bool inCone = halfAngle >= 180.0F || flat <= 0.001F;
                if (!inCone && forwardFlat > 0.001F) {
                    const float cosine = (dx * forward.x + dz * forward.z) / (flat * forwardFlat);
                    inCone = cosine >= coneCosine;
                }
                if (inCone && (!eyes->requireLineOfSight ||
                               InLineOfSight(scene, observer, eye, other, Vec3{ at.x, at.y + kBodyHeight, at.z }))) {
                    sense = PerceptionSense::Sight;
                }
            }
            if (!sense.has_value()) continue;
            const bool hostile = AreCombatantTeamsHostile(eyes->team, subject.team);
            const auto [known, added] = remembered.try_emplace(other.Id(), memory.size());
            if (added) {
                memory.push_back(PerceivedCombatant{ .entity = other });
                if (hostile) Emit(scene, GameplayEvent{ .kind = GameplayEventKind::Spotted, .source = observer, .target = other });
            }
            memory[known->second] = PerceivedCombatant{ .entity = other, .lastKnownPosition = at, .distance = distance, .secondsSinceSensed = 0.0F,
                .sense = *sense, .sensed = true, .hostile = hostile };
        }
        // Forget the dead, the gone, and whoever was not sensed for kMemorySeconds.
        std::erase_if(memory, [&](const PerceivedCombatant& entry) {
            if (!living.contains(entry.entity.Id())) return true;
            if (entry.secondsSinceSensed < kMemorySeconds) return false;
            if (entry.hostile) Emit(scene, GameplayEvent{ .kind = GameplayEventKind::Lost, .source = observer, .target = entry.entity });
            return true;
        });
        for (PerceivedCombatant& entry : memory) {
            if (!entry.sensed) entry.distance = GameplayDistance(position, entry.lastKnownPosition);
        }
        std::ranges::sort(memory, [](const PerceivedCombatant& a, const PerceivedCombatant& b) { return a.distance < b.distance; });
    }
}

void SceneGameplayService::Advance(Scene& scene, float deltaSeconds) {
    SceneState& sceneState = SceneAccess::State(scene);
    GameplayRuntimeState& state = sceneState.gameplay;
    if (SceneEntityCounter::CountWithComponent(sceneState.world, sceneState.components.CombatantComponentId()) == 0U) {
        state.cooldowns.clear();
        state.perceived.clear();
        state.dead.clear();
        return;
    }
    if (!sceneState.isPlaying || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) return;
    const std::vector<SceneEntity> combatants = AllCombatants(scene);
    for (const SceneEntity entity : combatants) {
        CombatantComponent* combatant = Combatant(scene, entity);
        if (combatant == nullptr || !combatant->enabled) continue;
        if (!IsCombatantAlive(*combatant)) {
            // Authored dead, or set to 0 by a script: it dies once like any other.
            if (state.dead.insert(entity.Id()).second) {
                Emit(scene, GameplayEvent{ .kind = GameplayEventKind::Died, .target = entity });
                FireAnimatorTrigger(scene, entity, "Die");
                if (scene.Animators().Exists(entity)) static_cast<void>(scene.Animators().SetBool(entity, "Dead", true));
            }
            continue;
        }
        state.dead.erase(entity.Id());
        const float health = std::clamp(combatant->health + combatant->healthRegeneration * deltaSeconds, 0.0F, combatant->maximumHealth);
        const float mana = std::clamp(combatant->mana + combatant->manaRegeneration * deltaSeconds, 0.0F, combatant->maximumMana);
        if (health != combatant->health || mana != combatant->mana) {
            combatant->health = health;
            combatant->mana = mana;
            Touch(scene, entity);
        }
    }
    for (auto it = state.cooldowns.begin(); it != state.cooldowns.end();) {
        if (!scene.Entities().IsAlive(SceneEntity{ it->first })) {
            it = state.cooldowns.erase(it);
            continue;
        }
        for (float& cooldown : it->second) cooldown = std::max(0.0F, cooldown - deltaSeconds);
        ++it;
    }
    std::erase_if(state.perceived, [&scene](const auto& entry) { return !scene.Entities().IsAlive(SceneEntity{ entry.first }); });
    state.perceptionAccumulator += deltaSeconds;
    if (state.perceptionAccumulator >= kPerceptionInterval) {
        RefreshPerception(scene, state.perceptionAccumulator);
        state.perceptionAccumulator = 0.0F;
    }
}

SceneGameplayQueries::SceneGameplayQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneGameplayQueries::IsAlive(SceneEntity entity) const noexcept { return SceneGameplayService::IsAlive(scene_, entity); }
bool SceneGameplayQueries::AreHostile(SceneEntity first, SceneEntity second) const noexcept { return SceneGameplayService::AreHostile(scene_, first, second); }
float SceneGameplayQueries::AbilityCooldown(SceneEntity caster, std::uint32_t slot) const noexcept { return SceneGameplayService::AbilityCooldown(scene_, caster, slot); }
std::span<const PerceivedCombatant> SceneGameplayQueries::Perceived(SceneEntity observer) const noexcept { return SceneGameplayService::Perceived(scene_, observer); }
std::optional<PerceivedCombatant> SceneGameplayQueries::NearestEnemy(SceneEntity observer) const noexcept { return SceneGameplayService::NearestEnemy(scene_, observer); }
std::span<const GameplayEvent> SceneGameplayQueries::PendingEvents() const noexcept { return SceneGameplayService::PendingEvents(scene_); }

SceneGameplay::SceneGameplay(Scene& scene) noexcept : scene_(scene) {}
DamageOutcome SceneGameplay::ApplyDamage(SceneEntity target, float amount, DamageType type, SceneEntity source) { return SceneGameplayService::ApplyDamage(scene_, target, amount, type, source); }
DamageOutcome SceneGameplay::Heal(SceneEntity target, float amount, SceneEntity source) { return SceneGameplayService::Heal(scene_, target, amount, source); }
DamageOutcome SceneGameplay::Revive(SceneEntity target, float health) { return SceneGameplayService::Revive(scene_, target, health); }
AbilityOutcome SceneGameplay::UseAbility(SceneEntity caster, std::uint32_t slot, SceneEntity target) { return SceneGameplayService::UseAbility(scene_, caster, slot, target); }
bool SceneGameplay::IsAlive(SceneEntity entity) const noexcept { return SceneGameplayService::IsAlive(scene_, entity); }
bool SceneGameplay::AreHostile(SceneEntity first, SceneEntity second) const noexcept { return SceneGameplayService::AreHostile(scene_, first, second); }
float SceneGameplay::AbilityCooldown(SceneEntity caster, std::uint32_t slot) const noexcept { return SceneGameplayService::AbilityCooldown(scene_, caster, slot); }
std::span<const PerceivedCombatant> SceneGameplay::Perceived(SceneEntity observer) const noexcept { return SceneGameplayService::Perceived(scene_, observer); }
std::optional<PerceivedCombatant> SceneGameplay::NearestEnemy(SceneEntity observer) const noexcept { return SceneGameplayService::NearestEnemy(scene_, observer); }
std::span<const GameplayEvent> SceneGameplay::PendingEvents() const noexcept { return SceneGameplayService::PendingEvents(scene_); }
std::vector<GameplayEvent> SceneGameplay::DrainEvents() { return SceneGameplayService::DrainEvents(scene_); }

} // namespace kb::scene
