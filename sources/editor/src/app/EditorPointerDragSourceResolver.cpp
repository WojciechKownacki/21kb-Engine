#include "app/EditorPointerDragSourceResolver.hpp"

#if defined(_WIN32)
#include "project/EditorProjectPanelHitTester.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "scene/EditorHierarchyRowPicker.hpp"

namespace kb::editor {

void EditorPointerDragSourceResolver::Resolve(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    const EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    drag.Clear();
    drag.x = x;
    drag.y = y;

    if (const std::optional<RECT> hierarchy = EditorPanelContentResolver::Resolve(DockPanelKind::Hierarchy, sourceWindow, mainWindow, dockModel, floatingWindows, metrics)) {
        const kb::scene::SceneEntity entity = EditorHierarchyRowPicker::EntityAtContentPoint(*hierarchy, x, y, sceneContext);
        if (entity.IsValid()) {
            drag.kind = EditorPointerDragKind::HierarchyEntity;
            drag.entity = entity;
            return;
        }
    }

    if (const std::optional<RECT> assets = EditorPanelContentResolver::Resolve(DockPanelKind::Assets, sourceWindow, mainWindow, dockModel, floatingWindows, metrics)) {
        const std::optional<std::filesystem::path> prefab = EditorProjectPanelHitTester::PrefabAssetAt(*assets, x, y);
        if (prefab.has_value()) {
            drag.kind = EditorPointerDragKind::PrefabAsset;
            drag.assetPath = *prefab;
        }
    }
}

} // namespace kb::editor

#endif
