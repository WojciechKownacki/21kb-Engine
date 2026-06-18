#include "scene/entities/SceneEntityNameService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityNaming.hpp"

namespace kb::scene {

std::string SceneEntityNameService::Name(const Scene& scene, SceneObject object) {
    return SceneEntityService::IsAlive(scene, object) ? Name(scene, object.Entity()) : std::string{};
}

std::string SceneEntityNameService::Name(const Scene& scene, SceneEntity entity) {
    return SceneEntityService::IsAlive(scene, entity) ? SceneEntityNaming::Name(SceneAccess::State(scene), entity) : std::string{};
}

void SceneEntityNameService::SetName(Scene& scene, SceneObject object, std::string_view name) {
    if (SceneEntityService::IsAlive(scene, object)) {
        SetName(scene, object.Entity(), name);
    }
}

void SceneEntityNameService::SetName(Scene& scene, SceneEntity entity, std::string_view name) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneEntityNaming::SetName(SceneAccess::State(scene), entity, name);
    }
}

} // namespace kb::scene
