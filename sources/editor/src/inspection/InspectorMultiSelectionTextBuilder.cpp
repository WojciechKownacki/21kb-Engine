#include "inspection/InspectorMultiSelectionTextBuilder.hpp"

#include "engine/scene/SceneEntities.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"

#include <cstdio>
#include <optional>
#include <vector>

namespace kb::editor {

std::string InspectorMultiSelectionTextBuilder::Build(const EditorSceneContext& sceneContext) const {
    const std::vector<kb::scene::SceneEntity>& selected = sceneContext.SelectedHierarchyEntities();
    const kb::scene::SceneEntity primary = sceneContext.SelectedEntity();
    const std::string primaryName = sceneContext.Scene().Entities().IsAlive(primary)
        ? sceneContext.Scene().Entities().Name(primary)
        : std::string{ "(none)" };
    const std::optional<kb::scene::Vec3> pivot = EditorSceneSelectionPivot::Resolve(sceneContext.Scene(), selected, primary);

    char buffer[512]{};
    if (pivot.has_value()) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "Selection\nEntities: %zu\nPrimary: %s\nPrimary Id: %llu\n\nPivot\nPosition: %.2f, %.2f, %.2f",
            selected.size(),
            primaryName.c_str(),
            static_cast<unsigned long long>(primary.Id()),
            pivot->x,
            pivot->y,
            pivot->z);
    } else {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "Selection\nEntities: %zu\nPrimary: %s\nPrimary Id: %llu",
            selected.size(),
            primaryName.c_str(),
            static_cast<unsigned long long>(primary.Id()));
    }
    return buffer;
}

} // namespace kb::editor
