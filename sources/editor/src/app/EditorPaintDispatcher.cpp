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
    EditorDockController& dockController) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , sceneContext_(sceneContext)
    , theme_(theme)
    , metrics_(metrics)
    , renderer_(renderer)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController) {}

void EditorPaintDispatcher::Paint(HWND paintWindow) const {
    if (paintWindow == nullptr || IsMainWindow(paintWindow)) {
        renderer_.Paint(mainWindow_, dockModel_, theme_, metrics_, sceneContext_, dockController_.DropPreview());
        return;
    }

    if (const DockPanel* panel = dockModel_.FindPanel(floatingWindows_.PanelId(paintWindow)); panel != nullptr) {
        renderer_.PaintFloating(paintWindow, *panel, theme_, metrics_, sceneContext_);
    }
}

bool EditorPaintDispatcher::IsMainWindow(HWND candidate) const noexcept {
    return candidate == mainWindow_;
}

} // namespace kb::editor

#endif
