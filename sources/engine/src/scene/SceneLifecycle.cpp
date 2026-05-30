#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityCreationService.hpp"
#include "scene/entities/SceneEntityDestructionService.hpp"
#include "scene/entities/SceneEntityNameService.hpp"
#include "scene/entities/SceneEntityStatsService.hpp"

#include <utility>

namespace kb::scene {

SceneObject SceneEntityService::CreateObject(Scene& scene) {
    return SceneEntityCreationService::CreateObject(scene);
}

SceneObject SceneEntityService::CreateObject(Scene& scene, SceneObjectDesc desc) {
    return SceneEntityCreationService::CreateObject(scene, std::move(desc));
}

SceneEntity SceneEntityService::CreateEntity(Scene& scene) {
    return SceneEntityCreationService::CreateEntity(scene);
}

SceneEntity SceneEntityService::CreateEntity(Scene& scene, SceneObjectDesc desc) {
    return SceneEntityCreationService::CreateEntity(scene, std::move(desc));
}

void SceneEntityService::DestroyObject(Scene& scene, SceneObject object) noexcept {
    SceneEntityDestructionService::DestroyObject(scene, object);
}

void SceneEntityService::DestroyEntity(Scene& scene, SceneEntity entity) noexcept {
    SceneEntityDestructionService::DestroyEntity(scene, entity);
}

bool SceneEntityService::IsAlive(const Scene& scene, SceneObject object) noexcept {
    return SceneAccess::BelongsTo(scene, object) && IsAlive(scene, object.Entity());
}

bool SceneEntityService::IsAlive(const Scene& scene, SceneEntity entity) noexcept {
    return SceneAccess::State(scene).world.IsAlive(entity);
}

std::string SceneEntityService::Name(const Scene& scene, SceneObject object) {
    return SceneEntityNameService::Name(scene, object);
}

std::string SceneEntityService::Name(const Scene& scene, SceneEntity entity) {
    return SceneEntityNameService::Name(scene, entity);
}

void SceneEntityService::SetName(Scene& scene, SceneObject object, std::string_view name) {
    SceneEntityNameService::SetName(scene, object, name);
}

void SceneEntityService::SetName(Scene& scene, SceneEntity entity, std::string_view name) {
    SceneEntityNameService::SetName(scene, entity, name);
}

std::size_t SceneEntityService::Count(const Scene& scene) {
    return SceneEntityStatsService::Count(scene);
}

} // namespace kb::scene
