#include "engine/scene/SceneEntities.hpp"

#include "scene/SceneEntityService.hpp"

namespace kb::scene {

bool SceneEntityQueries::IsAlive(SceneObject object) const noexcept {
    return SceneEntityService::IsAlive(scene_, object);
}

bool SceneEntityQueries::IsAlive(SceneEntity entity) const noexcept {
    return SceneEntityService::IsAlive(scene_, entity);
}

std::string SceneEntityQueries::Name(SceneObject object) const {
    return SceneEntityService::Name(scene_, object);
}

std::string SceneEntityQueries::Name(SceneEntity entity) const {
    return SceneEntityService::Name(scene_, entity);
}

bool SceneEntityQueries::IsActive(SceneObject object) const noexcept {
    return SceneEntityService::IsActive(scene_, object);
}

bool SceneEntityQueries::IsActive(SceneEntity entity) const noexcept {
    return SceneEntityService::IsActive(scene_, entity);
}

std::size_t SceneEntityQueries::Count() const {
    return SceneEntityService::Count(scene_);
}

bool SceneEntities::IsAlive(SceneObject object) const noexcept {
    return SceneEntityService::IsAlive(scene_, object);
}

bool SceneEntities::IsAlive(SceneEntity entity) const noexcept {
    return SceneEntityService::IsAlive(scene_, entity);
}

SceneObject SceneEntities::Object(SceneEntity entity) const noexcept {
    return SceneEntityService::Object(scene_, entity);
}

bool SceneEntities::IsActive(SceneObject object) const noexcept {
    return SceneEntityService::IsActive(scene_, object);
}

bool SceneEntities::IsActive(SceneEntity entity) const noexcept {
    return SceneEntityService::IsActive(scene_, entity);
}

void SceneEntities::SetActive(SceneObject object, bool active) noexcept {
    SceneEntityService::SetActive(scene_, object, active);
}

void SceneEntities::SetActive(SceneEntity entity, bool active) noexcept {
    SceneEntityService::SetActive(scene_, entity, active);
}

std::size_t SceneEntities::Count() const {
    return SceneEntityService::Count(scene_);
}

} // namespace kb::scene
