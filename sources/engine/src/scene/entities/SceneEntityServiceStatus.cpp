#include "scene/SceneEntityService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

bool SceneEntityService::IsAlive(const Scene& scene, SceneObject object) noexcept {
    return SceneAccess::BelongsTo(scene, object) && IsAlive(scene, object.Entity());
}

bool SceneEntityService::IsAlive(const Scene& scene, SceneEntity entity) noexcept {
    return SceneAccess::State(scene).world.IsAlive(entity);
}

SceneObject SceneEntityService::Object(Scene& scene, SceneEntity entity) noexcept {
    return IsAlive(scene, entity) ? SceneAccess::MakeObject(scene, entity) : SceneObject{};
}

bool SceneEntityService::IsActive(const Scene& scene, SceneObject object) noexcept {
    return SceneAccess::BelongsTo(scene, object) && IsActive(scene, object.Entity());
}

bool SceneEntityService::IsActive(const Scene& scene, SceneEntity entity) noexcept {
    return IsAlive(scene, entity) && !SceneAccess::State(scene).inactiveEntities.contains(entity.Id());
}

void SceneEntityService::SetActive(Scene& scene, SceneObject object, bool active) noexcept {
    if (SceneAccess::BelongsTo(scene, object)) {
        SetActive(scene, object.Entity(), active);
    }
}

void SceneEntityService::SetActive(Scene& scene, SceneEntity entity, bool active) noexcept {
    if (!IsAlive(scene, entity)) {
        return;
    }
    SceneState& state = SceneAccess::State(scene);
    if (active) {
        state.inactiveEntities.erase(entity.Id());
    } else {
        state.inactiveEntities.insert(entity.Id());
    }
}

bool SceneEntityService::IsPersistent(const Scene& scene, SceneObject object) noexcept {
    return SceneAccess::BelongsTo(scene, object) && IsPersistent(scene, object.Entity());
}

bool SceneEntityService::IsPersistent(const Scene& scene, SceneEntity entity) noexcept {
    return IsAlive(scene, entity) && SceneAccess::State(scene).persistentEntities.contains(entity.Id());
}

void SceneEntityService::SetPersistent(Scene& scene, SceneObject object, bool persistent) noexcept {
    if (SceneAccess::BelongsTo(scene, object)) {
        SetPersistent(scene, object.Entity(), persistent);
    }
}

void SceneEntityService::SetPersistent(Scene& scene, SceneEntity entity, bool persistent) noexcept {
    if (!IsAlive(scene, entity)) {
        return;
    }
    SceneState& state = SceneAccess::State(scene);
    if (persistent) {
        state.persistentEntities.insert(entity.Id());
    } else {
        state.persistentEntities.erase(entity.Id());
    }
}

} // namespace kb::scene
