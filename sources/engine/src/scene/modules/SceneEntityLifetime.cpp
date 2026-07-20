#include "engine/scene/SceneEntities.hpp"

#include "scene/SceneEntityService.hpp"

namespace kb::scene {

void SceneEntities::Destroy(SceneObject object) noexcept {
    SceneEntityService::DestroyObject(scene_, object);
}

void SceneEntities::Destroy(SceneEntity entity) noexcept {
    SceneEntityService::DestroyEntity(scene_, entity);
}

void SceneEntities::Destroy(std::span<const SceneObject> objects) noexcept {
    SceneEntityService::DestroyObjects(scene_, objects);
}

void SceneEntities::QueueDeferredDestroy(SceneEntity entity) noexcept {
    SceneEntityService::QueueDeferredDestroy(scene_, entity);
}

std::size_t SceneEntities::DrainDeferredDestroys() noexcept {
    return SceneEntityService::DrainDeferredDestroys(scene_);
}

std::span<const BehaviourVariableOverride> SceneEntities::BehaviourVariableOverrides(SceneEntity entity) const noexcept {
    return SceneEntityService::BehaviourVariableOverrides(scene_, entity);
}

void SceneEntities::SetBehaviourVariableOverride(SceneEntity entity, std::string name, kb::script::ScriptValue value) {
    SceneEntityService::SetBehaviourVariableOverride(scene_, entity, std::move(name), std::move(value));
}

bool SceneEntities::RemoveBehaviourVariableOverride(SceneEntity entity, std::string_view name) noexcept {
    return SceneEntityService::RemoveBehaviourVariableOverride(scene_, entity, name);
}

void SceneEntities::ReplaceBehaviourVariableOverrides(SceneEntity entity, std::vector<BehaviourVariableOverride> overrides) {
    SceneEntityService::ReplaceBehaviourVariableOverrides(scene_, entity, std::move(overrides));
}

bool SceneEntities::SetParent(std::span<const SceneObject> objects, SceneObject parent) noexcept {
    return SceneEntityService::SetParent(scene_, objects, parent);
}

} // namespace kb::scene
