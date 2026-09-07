#include "scene/ui/SceneUISystem.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneUI.hpp"

namespace kb::scene {

void SceneUISystem::OnUpdate(SceneSystemContext& context) {
    static_cast<void>(context.GetScene().UI().UpdateFromInput(context.DeltaSeconds()));
}

} // namespace kb::scene
