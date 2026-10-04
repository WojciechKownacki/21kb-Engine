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

// Names, orders and roots the entities just created with their transform and visibility, then parents them.
void RegisterCreatedEntities(
    Scene& scene,
    SceneState& state,
    std::span<const SceneEntity> entities,
    std::span<const SceneObjectDesc> descs,
    std::span<const VisibilityComponent> visibilities) {
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
    const TransformComponent transform = SceneTransformComponentStore::Written(nullptr, desc.transform);
    const VisibilityComponent visibility = NormalizedVisibility(desc.visibility);
    // Born in its final archetype like a bulk-created object, without the chunks of the empty and transform-only ones.
    const std::array<kb::ecs::World::BulkComponentView, 2U> components{
        kb::ecs::World::MakeBulkComponentView(std::span<const TransformComponent>{ &transform, 1U }),
        kb::ecs::World::MakeBulkComponentView(std::span<const VisibilityComponent>{ &visibility, 1U }),
    };
    const SceneEntity entity = state.world.CreateEntity(components);
    try {
        RegisterCreatedEntities(scene, state, { &entity, 1U }, { &desc, 1U }, { &visibility, 1U });
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
        RegisterCreatedEntities(scene, state, entities, descs, visibilities);
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
