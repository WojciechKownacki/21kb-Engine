#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoAltDuplicate.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoTargetResolver.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace kb::editor {
namespace {

[[nodiscard]] bool LeftAltDown() noexcept {
    return (GetKeyState(VK_LMENU) & 0x8000) != 0;
}

} // namespace

bool EditorSceneViewportGizmoAltDuplicate::DuplicateForTranslateDrag(EditorSceneContext& sceneContext, std::optional<kb::scene::Vec3>& targetPosition) {
    if (sceneContext.Gizmo().toolMode != EditorTransformToolMode::Translate || !LeftAltDown()) {
        return true;
    }

    if (!sceneContext.DuplicateSelectedHierarchyEntities()) {
        return true;
    }

    targetPosition = EditorSceneViewportGizmoTargetResolver::SelectedTarget(sceneContext);
    return targetPosition.has_value();
}

} // namespace kb::editor

#endif
