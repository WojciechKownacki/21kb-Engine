#include "app/EditorPointerDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorAssetFolderProjectFilesDropHandler.hpp"
#include "app/EditorBehaviourAssetHierarchyDropHandler.hpp"
#include "app/EditorHierarchyEntityAssetDropHandler.hpp"
#include "app/EditorHierarchyEntityHierarchyDropHandler.hpp"
#include "app/EditorPrefabAssetHierarchyDropHandler.hpp"
#include "app/EditorPrefabAssetProjectFilesDropHandler.hpp"

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
        return EditorPrefabAssetProjectFilesDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId)
            || (drag.assetAddsBehaviour && EditorBehaviourAssetHierarchyDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetInstantiatesPrefab && EditorPrefabAssetHierarchyDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetPath));
    case EditorPointerDragKind::AssetFolder:
        return EditorAssetFolderProjectFilesDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetFolderPath);
    case EditorPointerDragKind::None:
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
