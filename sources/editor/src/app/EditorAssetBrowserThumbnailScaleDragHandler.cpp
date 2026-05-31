#include "app/EditorAssetBrowserThumbnailScaleDragHandler.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerPanelResolver.hpp"
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserLayout.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>

namespace kb::editor {

bool EditorAssetBrowserThumbnailScaleDragHandler::HandlePointerMove(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (!state.IsThumbnailScaleDragging()) {
        return false;
    }

    const std::optional<RECT> content = EditorAssetBrowserPointerPanelResolver::ResolveContent(sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!content.has_value()) {
        return false;
    }

    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(*content);
    state.SetThumbnailScale(EditorAssetBrowserGeometry::SliderValueAt(layout, x));
    return true;
}

bool EditorAssetBrowserThumbnailScaleDragHandler::HandlePointerUp(EditorSceneContext& sceneContext) noexcept {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (!state.IsThumbnailScaleDragging()) {
        return false;
    }

    state.EndThumbnailScaleDrag();
    return true;
}

} // namespace kb::editor

#endif
