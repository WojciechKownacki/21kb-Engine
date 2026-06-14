#include "app/scene_viewport/EditorSceneViewportToolbarPointerController.hpp"

#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] bool SelectDropdownValue(EditorViewportPreviewState& preview, const SceneViewportToolbarRects& toolbar, int x, int y) noexcept {
    const EditorViewportToolbarDropdown dropdown = preview.ToolbarDropdown();
    if (dropdown == EditorViewportToolbarDropdown::None) {
        return false;
    }

    std::size_t count = EditorViewportSnapStepOptionCount();
    if (dropdown == EditorViewportToolbarDropdown::GridSpacing) {
        count = EditorViewportGridSpacingOptionCount();
    } else if (dropdown == EditorViewportToolbarDropdown::RotationSnap) {
        count = EditorViewportRotationSnapOptionCount();
    }
    for (std::size_t index = 0; index < count && index < toolbar.dropdownItems.size(); ++index) {
        if (!PointInRect(toolbar.dropdownItems[index], x, y)) {
            continue;
        }
        if (dropdown == EditorViewportToolbarDropdown::GridSpacing) {
            preview.SetGridSpacing(EditorViewportGridSpacingOption(index));
        } else if (dropdown == EditorViewportToolbarDropdown::SnapStep) {
            preview.SetSnapStep(EditorViewportSnapStepOption(index));
        } else if (dropdown == EditorViewportToolbarDropdown::RotationSnap) {
            preview.SetRotationSnapDegrees(EditorViewportRotationSnapOption(index));
        }
        preview.CloseToolbarDropdown();
        return true;
    }
    return false;
}

} // namespace

EditorSceneViewportToolbarPointerController::EditorSceneViewportToolbarPointerController(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) noexcept
    : sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport) {}

bool EditorSceneViewportToolbarPointerController::HandlePointerDown(const EditorResolvedPanelContent& panelContent, int x, int y) {
    EditorViewportPreviewState& preview = sceneContext_.ViewportPreview(panelContent.panelId);
    const SceneViewportToolbarRects toolbar = SceneViewportToolbarRenderer::Resolve(panelContent.content, preview);
    if (SelectDropdownValue(preview, toolbar, x, y)) {
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.renderProfileButton, x, y)) {
        preview.CloseToolbarDropdown();
        preview.CycleRenderProfile();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.gridToggleButton, x, y)) {
        preview.CloseToolbarDropdown();
        preview.ToggleGridVisible();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.gridStepButton, x, y)) {
        preview.ToggleToolbarDropdown(EditorViewportToolbarDropdown::GridSpacing);
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.snapToggleButton, x, y)) {
        preview.CloseToolbarDropdown();
        preview.ToggleSnapEnabled();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.snapStepButton, x, y)) {
        preview.ToggleToolbarDropdown(EditorViewportToolbarDropdown::SnapStep);
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.rotationSnapButton, x, y)) {
        preview.ToggleToolbarDropdown(EditorViewportToolbarDropdown::RotationSnap);
        sceneViewport_.RequestPresent();
        return true;
    }
    if (preview.ToolbarDropdown() != EditorViewportToolbarDropdown::None) {
        preview.CloseToolbarDropdown();
        sceneViewport_.RequestPresent();
        return true;
    }
    return false;
}

} // namespace kb::editor
