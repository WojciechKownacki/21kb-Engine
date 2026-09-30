#include "scene/EditorHierarchyRowFactory.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
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
    const kb::scene::VisibilityComponent visibility = scene.Components().Visibility().Get(entity);
    const bool hasChildren = scene.Hierarchy().ChildCount(entity) != 0U;
    const bool prefabRoot = scene.Prefabs().RootInstance(entity).IsValid();

    return EditorHierarchyRow{
        .entity = entity,
        .depth = depth,
        .name = scene.Entities().Name(entity),
        .hasChildren = hasChildren,
        .expanded = hasChildren && !IsCollapsed(collapsedEntities, entity),
        .visible = visibility.mode != kb::scene::VisibilityMode::Hidden,
        .prefabRoot = prefabRoot,
        .hasCamera = scene.Components().Cameras().Has(entity),
    };
}

} // namespace kb::editor
