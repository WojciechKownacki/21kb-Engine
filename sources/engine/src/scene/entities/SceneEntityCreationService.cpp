#include "scene/entities/SceneEntityCreationService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneHierarchyService.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"

#include <utility>

namespace kb::scene {

SceneObject SceneEntityCreationService::CreateObject(Scene& scene) {
    return SceneAccess::MakeObject(scene, CreateEntity(scene));
}

SceneObject SceneEntityCreationService::CreateObject(Scene& scene, SceneObjectDesc desc) {
    return SceneAccess::MakeObject(scene, CreateEntity(scene, std::move(desc)));
}

SceneEntity SceneEntityCreationService::CreateEntity(Scene& scene) {
    return CreateEntity(scene, SceneObjectDesc{});
}

SceneEntity SceneEntityCreationService::CreateEntity(Scene& scene, SceneObjectDesc desc) {
    SceneState& state = SceneAccess::State(scene);
    kb::ecs::Entity entity = desc.name.empty() ? state.world.CreateEntity() : state.world.CreateEntity(desc.name);
    state.hierarchyOrder[entity.Id()] = state.nextHierarchyOrder++;
    SceneHierarchyCache::AddRoot(state, entity);
    state.componentStorage.SetDefaults(entity, desc.transform, desc.visibility);

    if (desc.parent.EntityHandle().IsValid()) {
        [[maybe_unused]] const bool parentAssigned = SceneHierarchyService::SetParent(scene, entity, desc.parent.Entity());
    }

    return entity;
}

} // namespace kb::scene
