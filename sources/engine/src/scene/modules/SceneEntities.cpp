#include "engine/scene/SceneEntities.hpp"

#include "scene/SceneEntityService.hpp"

#include <utility>

namespace kb::scene {

SceneEntityQueries::SceneEntityQueries(const Scene& scene) noexcept
    : scene_(scene) {}

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

std::size_t SceneEntityQueries::Count() const {
    return SceneEntityService::Count(scene_);
}

SceneEntities::SceneEntities(Scene& scene) noexcept
    : scene_(scene) {}

SceneObject SceneEntities::CreateObject() {
    return SceneEntityService::CreateObject(scene_);
}

SceneObject SceneEntities::CreateObject(SceneObjectDesc desc) {
    return SceneEntityService::CreateObject(scene_, std::move(desc));
}

SceneEntity SceneEntities::CreateEntity() {
    return SceneEntityService::CreateEntity(scene_);
}

SceneEntity SceneEntities::CreateEntity(SceneObjectDesc desc) {
    return SceneEntityService::CreateEntity(scene_, std::move(desc));
}

void SceneEntities::Destroy(SceneObject object) noexcept {
    SceneEntityService::DestroyObject(scene_, object);
}

void SceneEntities::Destroy(SceneEntity entity) noexcept {
    SceneEntityService::DestroyEntity(scene_, entity);
}

bool SceneEntities::IsAlive(SceneObject object) const noexcept {
    return SceneEntityService::IsAlive(scene_, object);
}

bool SceneEntities::IsAlive(SceneEntity entity) const noexcept {
    return SceneEntityService::IsAlive(scene_, entity);
}

std::string SceneEntities::Name(SceneObject object) const {
    return SceneEntityService::Name(scene_, object);
}

std::string SceneEntities::Name(SceneEntity entity) const {
    return SceneEntityService::Name(scene_, entity);
}

void SceneEntities::SetName(SceneObject object, std::string_view name) {
    SceneEntityService::SetName(scene_, object, name);
}

void SceneEntities::SetName(SceneEntity entity, std::string_view name) {
    SceneEntityService::SetName(scene_, entity, name);
}

std::size_t SceneEntities::Count() const {
    return SceneEntityService::Count(scene_);
}

} // namespace kb::scene
