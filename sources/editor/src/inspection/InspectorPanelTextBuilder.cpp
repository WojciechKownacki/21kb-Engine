#include "inspection/InspectorPanelTextBuilder.hpp"

#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"

#include "inspection/InspectorCameraTextBuilder.hpp"
#include "inspection/InspectorEntitySummaryTextBuilder.hpp"
#include "inspection/InspectorLightTextBuilder.hpp"
#include "inspection/InspectorMeshRendererTextBuilder.hpp"

namespace kb::editor {

std::optional<std::string> InspectorPanelTextBuilder::Build(const EditorSceneContext& sceneContext) const {
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        return std::nullopt;
    }

    std::string text = InspectorEntitySummaryTextBuilder{}.Build(sceneContext, selected);

    if (const kb::scene::CameraComponent* camera = sceneContext.Scene().Components().Cameras().TryGet(selected); camera != nullptr) {
        InspectorCameraTextBuilder{}.Append(text, *camera);
    }

    if (const kb::scene::MeshRendererComponent* renderer = sceneContext.Scene().Components().MeshRenderers().TryGet(selected); renderer != nullptr) {
        InspectorMeshRendererTextBuilder{}.Append(text, *renderer);
    }

    if (const kb::scene::LightComponent* light = sceneContext.Scene().Components().Lights().TryGet(selected); light != nullptr) {
        InspectorLightTextBuilder{}.Append(text, *light);
    }

    return text;
}

} // namespace kb::editor
