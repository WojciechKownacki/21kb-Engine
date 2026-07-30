#include "scene/entities/SceneEntityCreationService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneHierarchyService.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/entities/SceneEntityNaming.hpp"
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
    kb::ecs::Entity entity = state.world.CreateEntity();
    if (!desc.name.empty()) {
        SceneEntityNaming::SetName(state, entity, desc.name);
    }
    SceneHierarchyCache::AssignOrder(state, entity);
    SceneHierarchyCache::AddRoot(state, entity);
    VisibilityComponent visibility = desc.visibility;
    if (!IsVisibilityModeValid(visibility.mode)) {
        visibility.mode = visibility.visible ? VisibilityMode::Visible : VisibilityMode::Hidden;
    }
    if (!visibility.visible) {
        visibility.mode = VisibilityMode::Hidden;
    }
    visibility.visible = visibility.mode != VisibilityMode::Hidden;
    state.componentStorage.SetDefaults(entity, desc.transform, visibility);
    if (visibility.mode == VisibilityMode::Hidden) {
        SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Hidden);
    }

    if (desc.parent.EntityHandle().IsValid()) {
        [[maybe_unused]] const bool parentAssigned = SceneHierarchyService::SetParent(scene, entity, desc.parent.Entity());
    }

    return entity;
}

} // namespace kb::scene
