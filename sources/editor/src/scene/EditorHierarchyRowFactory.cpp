#include "scene/EditorHierarchyRowFactory.hpp"

#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldInspection.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool IsCollapsed(const EditorHierarchyRowBuilder::CollapsedEntitySet& collapsedEntities, kb::scene::SceneEntity entity) noexcept {
    return collapsedEntities.contains(entity.Id());
}

} // namespace

EditorHierarchyRow EditorHierarchyRowFactory::Make(
    const kb::scene::Scene& scene,
    const EditorHierarchyRowBuilder::CollapsedEntitySet& collapsedEntities,
    kb::scene::SceneEntity entity,
    std::uint32_t depth) {
    const std::vector<kb::scene::SceneEntity> children = scene.Hierarchy().ChildEntities(entity);
    const kb::ecs::EntityInspection inspection = scene.Runtime().EcsWorld().InspectEntity(entity);
    const kb::scene::VisibilityComponent visibility = scene.Components().Visibility().Get(entity);
    const bool hasChildren = !children.empty();
    const bool prefabRoot = scene.Prefabs().RootInstance(entity).IsValid();

    return EditorHierarchyRow{
        .entity = entity,
        .depth = depth,
        .name = scene.Entities().Name(entity),
        .componentCount = inspection.components.size(),
        .hasChildren = hasChildren,
        .expanded = hasChildren && !IsCollapsed(collapsedEntities, entity),
        .visible = visibility.visible,
        .prefabRoot = prefabRoot,
    };
}

} // namespace kb::editor
