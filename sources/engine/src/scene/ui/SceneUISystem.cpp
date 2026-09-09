#include "scene/ui/SceneUISystem.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneUI.hpp"

#include <string>

namespace kb::scene {

void SceneUISystem::OnUpdate(SceneSystemContext& context) {
    Scene& scene = context.GetScene();
    if (scene.UI().UpdateFromInput(context.DeltaSeconds())) {
        return;
    }

    // An update can decline for reasons that are not faults - no viewport published yet, a
    // pointer position that has not arrived. Only a frame the builder actually refused names
    // an entity, and only that is worth reporting; anything else would be noise every frame
    // before the first present.
    const SceneUIFrameRefusal& refusal = scene.UI().Frame().refusal;
    if (!refusal.HasValue()) {
        return;
    }
    context.ReportError("UI frame refused: " + std::string{refusal.reason} + " (entity " +
                        std::to_string(refusal.entity.Id()) + ")");
}

} // namespace kb::scene
