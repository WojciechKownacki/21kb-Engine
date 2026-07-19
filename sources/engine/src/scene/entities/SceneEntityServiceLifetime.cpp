#include "scene/SceneEntityService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityDestructionService.hpp"

#include <algorithm>

namespace kb::scene {

void SceneEntityService::DestroyObject(Scene& scene, SceneObject object) noexcept {
    SceneEntityDestructionService::DestroyObject(scene, object);
}

void SceneEntityService::DestroyEntity(Scene& scene, SceneEntity entity) noexcept {
    SceneEntityDestructionService::DestroyEntity(scene, entity);
}

void SceneEntityService::QueueDeferredDestroy(Scene& scene, SceneEntity entity) noexcept {
    if (!entity.IsValid() || !IsAlive(scene, entity)) {
        return;
    }
    std::vector<SceneEntity>& queue = SceneAccess::State(scene).pendingDeferredDestroys;
    if (std::ranges::find(queue, entity) == queue.end()) {
        queue.push_back(entity);
    }
}

std::size_t SceneEntityService::DrainDeferredDestroys(Scene& scene) noexcept {
    // Swap out first so a deferred destroy queued from within DestroyEntity's
    // own cascade (were that ever to happen) lands in a fresh queue for the
    // next drain rather than being iterated here mid-loop.
    std::vector<SceneEntity> queued;
    queued.swap(SceneAccess::State(scene).pendingDeferredDestroys);
    std::size_t destroyed = 0U;
    for (const SceneEntity entity : queued) {
        if (IsAlive(scene, entity)) {
            DestroyEntity(scene, entity);
            ++destroyed;
        }
    }
    return destroyed;
}

std::span<const BehaviourVariableOverride> SceneEntityService::BehaviourVariableOverrides(const Scene& scene, SceneEntity entity) noexcept {
    const std::unordered_map<SceneEntity::IdType, std::vector<BehaviourVariableOverride>>& table =
        SceneAccess::State(scene).behaviourVariableOverrides;
    const auto iter = table.find(entity.Id());
    if (iter == table.end()) {
        return {};
    }
    return iter->second;
}

void SceneEntityService::SetBehaviourVariableOverride(Scene& scene, SceneEntity entity, std::string name, kb::script::ScriptValue value) {
    if (!entity.IsValid() || name.empty()) {
        return;
    }
    std::vector<BehaviourVariableOverride>& overrides = SceneAccess::State(scene).behaviourVariableOverrides[entity.Id()];
    for (BehaviourVariableOverride& existing : overrides) {
        if (existing.name == name) {
            existing.value = std::move(value);
            return;
        }
    }
    overrides.push_back(BehaviourVariableOverride{ .name = std::move(name), .value = std::move(value) });
}

bool SceneEntityService::RemoveBehaviourVariableOverride(Scene& scene, SceneEntity entity, std::string_view name) noexcept {
    std::unordered_map<SceneEntity::IdType, std::vector<BehaviourVariableOverride>>& table =
        SceneAccess::State(scene).behaviourVariableOverrides;
    const auto iter = table.find(entity.Id());
    if (iter == table.end()) {
        return false;
    }
    std::vector<BehaviourVariableOverride>& overrides = iter->second;
    const auto pos = std::ranges::find_if(overrides, [name](const BehaviourVariableOverride& candidate) {
        return candidate.name == name;
    });
    if (pos == overrides.end()) {
        return false;
    }
    overrides.erase(pos);
    if (overrides.empty()) {
        table.erase(iter);
    }
    return true;
}

void SceneEntityService::ReplaceBehaviourVariableOverrides(Scene& scene, SceneEntity entity, std::vector<BehaviourVariableOverride> overrides) {
    if (!entity.IsValid()) {
        return;
    }
    std::unordered_map<SceneEntity::IdType, std::vector<BehaviourVariableOverride>>& table =
        SceneAccess::State(scene).behaviourVariableOverrides;
    if (overrides.empty()) {
        static_cast<void>(table.erase(entity.Id()));
        return;
    }
    table[entity.Id()] = std::move(overrides);
}

} // namespace kb::scene
