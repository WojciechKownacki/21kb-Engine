#include "scene/entities/SceneEntityCreationService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/components/SceneTransformComponentStore.hpp"
#include "scene/SceneHierarchyService.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/entities/SceneEntityNaming.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"

#include <array>
#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] VisibilityComponent NormalizedVisibility(VisibilityComponent visibility) noexcept {
    if (!IsVisibilityModeValid(visibility.mode)) {
        visibility.mode = visibility.visible ? VisibilityMode::Visible : VisibilityMode::Hidden;
    }
    if (!visibility.visible) {
        visibility.mode = VisibilityMode::Hidden;
    }
    visibility.visible = visibility.mode != VisibilityMode::Hidden;
    return visibility;
}

void RollBackCreatedEntity(SceneState& state, SceneEntity entity) {
    SceneHierarchyCache::Remove(state, entity, SceneHierarchyCache::Parent(state, entity));
    SceneEntityNaming::ClearName(state, entity);
    ClearSceneRenderProxyComponentMask(state, entity);
    state.world.DestroyEntity(entity);
}

} // namespace

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
    try {
        if (!desc.name.empty()) {
            SceneEntityNaming::SetName(state, entity, desc.name);
        }
        SceneHierarchyCache::AssignOrder(state, entity);
        SceneHierarchyCache::AddRoot(state, entity);
        const VisibilityComponent visibility = NormalizedVisibility(desc.visibility);
        state.componentStorage.SetDefaults(entity, desc.transform, visibility);
        if (visibility.mode == VisibilityMode::Hidden) {
            SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Hidden);
        }

        if (desc.parent.EntityHandle().IsValid()) {
            [[maybe_unused]] const bool parentAssigned = SceneHierarchyService::SetParent(scene, entity, desc.parent.Entity());
        }
    } catch (...) {
        RollBackCreatedEntity(state, entity);
        throw;
    }

    return entity;
}

std::vector<SceneObject> SceneEntityCreationService::CreateObjects(Scene& scene, std::span<const SceneObjectDesc> descs) {
    SceneState& state = SceneAccess::State(scene);
    std::vector<TransformComponent> transforms(descs.size());
    std::vector<VisibilityComponent> visibilities(descs.size());
    for (std::size_t index = 0U; index < descs.size(); ++index) {
        transforms[index] = SceneTransformComponentStore::Written(nullptr, descs[index].transform);
        visibilities[index] = NormalizedVisibility(descs[index].visibility);
    }
    // The entities are born in their final archetype instead of passing through the empty and transform-only ones.
    const std::array<kb::ecs::World::BulkComponentView, 2U> components{
        kb::ecs::World::MakeBulkComponentView(std::span<const TransformComponent>{ transforms }),
        kb::ecs::World::MakeBulkComponentView(std::span<const VisibilityComponent>{ visibilities }),
    };
    const std::vector<SceneEntity> entities = state.world.CreateEntities(descs.size(), components);
    try {
        for (std::size_t index = 0U; index < descs.size(); ++index) {
            if (!descs[index].name.empty()) {
                SceneEntityNaming::SetName(state, entities[index], descs[index].name);
            }
        }
        SceneHierarchyCache::AssignOrderRange(state, entities);
        SceneHierarchyCache::AddRoots(state, entities);
        for (std::size_t index = 0U; index < descs.size(); ++index) {
            if (visibilities[index].mode == VisibilityMode::Hidden) {
                SetSceneRenderProxyComponentMask(state, entities[index], SceneRenderProxyComponentMask::Hidden);
            }
            if (descs[index].parent.EntityHandle().IsValid()) {
                [[maybe_unused]] const bool parentAssigned = SceneHierarchyService::SetParent(scene, entities[index], descs[index].parent.Entity());
            }
        }
    } catch (...) {
        for (const SceneEntity entity : entities) {
            RollBackCreatedEntity(state, entity);
        }
        throw;
    }

    std::vector<SceneObject> created;
    created.reserve(entities.size());
    for (const SceneEntity entity : entities) {
        created.push_back(SceneAccess::MakeObject(scene, entity));
    }
    return created;
}

} // namespace kb::scene
