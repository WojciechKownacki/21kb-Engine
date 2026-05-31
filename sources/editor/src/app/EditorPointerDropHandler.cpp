#include "app/EditorPointerDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorHierarchyEntityAssetDropHandler.hpp"
#include "app/EditorHierarchyEntityHierarchyDropHandler.hpp"
#include "app/EditorPrefabAssetHierarchyDropHandler.hpp"

namespace kb::editor {

bool EditorPointerDropHandler::Drop(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    const EditorPointerDragState& drag) {
    switch (drag.kind) {
    case EditorPointerDragKind::HierarchyEntity:
        return EditorHierarchyEntityAssetDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.entity)
            || EditorHierarchyEntityHierarchyDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.entity);
    case EditorPointerDragKind::PrefabAsset:
        return EditorPrefabAssetHierarchyDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetPath);
    case EditorPointerDragKind::None:
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
