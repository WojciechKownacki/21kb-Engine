#include "app/EditorPaintDispatcher.hpp"

#if defined(_WIN32)

namespace kb::editor {

EditorPaintDispatcher::EditorPaintDispatcher(
    HWND& mainWindow,
    EditorDockModel& dockModel,
    EditorSceneContext& sceneContext,
    EditorTheme& theme,
    EditorMetrics& metrics,
    EditorGdiRenderer& renderer,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorPointerDragState& pointerDrag) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , sceneContext_(sceneContext)
    , theme_(theme)
    , metrics_(metrics)
    , renderer_(renderer)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , pointerDrag_(pointerDrag) {}

void EditorPaintDispatcher::Paint(HWND paintWindow) const {
    if (paintWindow == nullptr || IsMainWindow(paintWindow)) {
        renderer_.Paint(mainWindow_, dockModel_, theme_, metrics_, sceneContext_, dockController_.DropPreview(), pointerDrag_);
        return;
    }

    if (const DockPanel* panel = dockModel_.Queries().FindPanel(floatingWindows_.Queries().PanelId(paintWindow)); panel != nullptr) {
        renderer_.PaintFloating(paintWindow, *panel, theme_, metrics_, sceneContext_);
    }
}

bool EditorPaintDispatcher::IsMainWindow(HWND candidate) const noexcept {
    return candidate == mainWindow_;
}

} // namespace kb::editor

#endif
