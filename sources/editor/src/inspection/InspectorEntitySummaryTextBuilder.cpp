#include "inspection/InspectorEntitySummaryTextBuilder.hpp"

#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <cstdio>

namespace kb::editor {

std::string InspectorEntitySummaryTextBuilder::Build(const EditorSceneContext& sceneContext, kb::scene::SceneEntity entity) const {
    const kb::scene::TransformComponent transform = sceneContext.Scene().Transforms().Get(entity);
    const kb::scene::VisibilityComponent visibility = sceneContext.Scene().Components().Visibility().Get(entity);

    char buffer[1024]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "Entity: %s\nId: %llu\nVisible: %s\n\nTransform\nPosition: %.2f, %.2f, %.2f\nRotation: %.2f, %.2f, %.2f, %.2f\nScale: %.2f, %.2f, %.2f",
        sceneContext.Scene().Entities().Name(entity).c_str(),
        static_cast<unsigned long long>(entity.Id()),
        visibility.mode != kb::scene::VisibilityMode::Hidden ? "true" : "false",
        transform.localPosition.x,
        transform.localPosition.y,
        transform.localPosition.z,
        transform.localRotation.x,
        transform.localRotation.y,
        transform.localRotation.z,
        transform.localRotation.w,
        transform.localScale.x,
        transform.localScale.y,
        transform.localScale.z);

    return buffer;
}

} // namespace kb::editor
