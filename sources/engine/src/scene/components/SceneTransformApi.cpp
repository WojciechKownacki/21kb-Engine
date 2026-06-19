#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneTransformService.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"
#include "scene/transform/SceneTransformDirtyFrontier.hpp"

namespace kb::scene {

TransformComponent SceneTransformService::Get(const Scene& scene, SceneObject object) {
    return SceneEntityService::IsAlive(scene, object) ? Get(scene, object.Entity()) : TransformComponent{};
}

TransformComponent SceneTransformService::Get(const Scene& scene, SceneEntity entity) {
    const TransformComponent* transform = TryGet(scene, entity);
    return transform == nullptr ? TransformComponent{} : *transform;
}

const TransformComponent* SceneTransformService::TryGet(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Transforms().TryGet(entity) : nullptr;
}

TransformComponent* SceneTransformService::TryGet(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Transforms().TryGet(entity) : nullptr;
}

void SceneTransformService::Set(Scene& scene, SceneObject object, const TransformComponent& transform) {
    if (SceneEntityService::IsAlive(scene, object)) {
        Set(scene, object.Entity(), transform);
    }
}

void SceneTransformService::Set(Scene& scene, SceneEntity entity, const TransformComponent& transform) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Transforms().Set(entity, transform);
        EnqueueSceneTransformDirtyFrontier(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneTransformService::MarkModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Transforms().MarkModified(entity);
        EnqueueSceneTransformDirtyFrontier(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneTransformService::MarkParentModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Transforms().MarkParentModified(entity);
        EnqueueSceneTransformDirtyFrontier(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
