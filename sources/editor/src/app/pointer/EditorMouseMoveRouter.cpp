#include "app/pointer/EditorMouseMoveRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/EditorWindowToolbarPointerHandler.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/cursor/EditorInternalSplitterCursorController.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/plugins/EditorPluginsPointerController.hpp"
#include "app/project_settings/EditorProjectSettingsPointerController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

#include <algorithm>
#include <optional>

namespace kb::editor {
namespace {

constexpr int kHierarchyContextMenuWidth = 128;
constexpr int kHierarchyLightingSubmenuWidth = 190;
constexpr int kHierarchyLightingSubmenuGap = 4;
constexpr int kHierarchyContextMenuRowHeight = 24;
constexpr int kHierarchyContextMenuPadding = 4;
constexpr int kHierarchyContextMenuItemCount = 1;
constexpr int kHierarchyLightingSubmenuRows = 4;

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] RECT HierarchyContextMenuRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    const int width = kHierarchyContextMenuWidth;
    const int height = kHierarchyContextMenuPadding * 2 + kHierarchyContextMenuItemCount * kHierarchyContextMenuRowHeight;
    int menuX = std::clamp(sceneContext.HierarchyContextMenuX(), static_cast<int>(content.left), std::max(static_cast<int>(content.left), static_cast<int>(content.right) - width));
    const int menuY = std::clamp(sceneContext.HierarchyContextMenuY(), static_cast<int>(content.top), std::max(static_cast<int>(content.top), static_cast<int>(content.bottom) - height));
    return RECT{ menuX, menuY, menuX + width, menuY + height };
}

[[nodiscard]] EditorHierarchyContextCommand HierarchyContextMenuCommandAt(const RECT& menu, int x, int y) noexcept {
    if (!PointInRect(menu, x, y)) {
        return EditorHierarchyContextCommand::None;
    }
    const int localY = y - menu.top - kHierarchyContextMenuPadding;
    if (localY < 0) {
        return EditorHierarchyContextCommand::None;
    }
    const int row = localY / kHierarchyContextMenuRowHeight;
    if (row < 0 || row >= kHierarchyContextMenuItemCount) {
        return EditorHierarchyContextCommand::None;
    }
    switch (row) {
    case 0:
        return EditorHierarchyContextCommand::AddLighting;
    default:
        return EditorHierarchyContextCommand::None;
    }
}

[[nodiscard]] bool ShowsHierarchyLightingSubmenu(EditorHierarchyContextCommand command) noexcept {
    return command == EditorHierarchyContextCommand::AddLighting
        || command == EditorHierarchyContextCommand::AddDirectionalLight
        || command == EditorHierarchyContextCommand::AddPointLight
        || command == EditorHierarchyContextCommand::AddSpotLight;
}

[[nodiscard]] RECT HierarchyLightingSubmenuRect(const RECT& content, const RECT& menu) noexcept {
    RECT submenu{
        menu.right + kHierarchyLightingSubmenuGap,
        menu.top + kHierarchyContextMenuPadding,
        menu.right + kHierarchyLightingSubmenuGap + kHierarchyLightingSubmenuWidth,
        menu.top + kHierarchyContextMenuPadding * 2 + kHierarchyLightingSubmenuRows * kHierarchyContextMenuRowHeight,
    };
    if (submenu.right > content.right) {
        const int width = submenu.right - submenu.left;
        const int height = submenu.bottom - submenu.top;
        const int left = std::clamp(static_cast<int>(menu.left), static_cast<int>(content.left), std::max(static_cast<int>(content.left), static_cast<int>(content.right) - width));
        int top = menu.bottom + kHierarchyLightingSubmenuGap;
        if (top + height > content.bottom) {
            top = menu.top - height - kHierarchyLightingSubmenuGap;
        }
        top = std::clamp(top, static_cast<int>(content.top), std::max(static_cast<int>(content.top), static_cast<int>(content.bottom) - height));
        submenu = RECT{ left, top, left + width, top + height };
    } else if (submenu.bottom > content.bottom) {
        OffsetRect(&submenu, 0, content.bottom - submenu.bottom);
    }
    return submenu;
}

[[nodiscard]] EditorHierarchyContextCommand HierarchyLightingSubmenuCommandAt(const RECT& submenu, int x, int y) noexcept {
    if (!PointInRect(submenu, x, y)) {
        return EditorHierarchyContextCommand::None;
    }
    const int localY = y - submenu.top - kHierarchyContextMenuPadding;
    if (localY < 0) {
        return EditorHierarchyContextCommand::AddLighting;
    }
    const int row = localY / kHierarchyContextMenuRowHeight;
    switch (row) {
    case 1:
        return EditorHierarchyContextCommand::AddDirectionalLight;
    case 2:
        return EditorHierarchyContextCommand::AddPointLight;
    case 3:
        return EditorHierarchyContextCommand::AddSpotLight;
    default:
        return EditorHierarchyContextCommand::AddLighting;
    }
}

[[nodiscard]] bool InHierarchyLightingBridge(const RECT& menu, const RECT& submenu, int x, int y) noexcept {
    const RECT addRow{
        menu.left + kHierarchyContextMenuPadding,
        menu.top + kHierarchyContextMenuPadding,
        menu.right - kHierarchyContextMenuPadding,
        menu.top + kHierarchyContextMenuPadding + kHierarchyContextMenuRowHeight,
    };
    RECT bridge{};
    if (submenu.left >= addRow.right) {
        bridge = RECT{ addRow.right, addRow.top, submenu.left, addRow.bottom };
    } else if (submenu.top >= addRow.bottom) {
        bridge = RECT{ addRow.left, addRow.bottom, addRow.right, submenu.top };
    } else if (submenu.bottom <= addRow.top) {
        bridge = RECT{ addRow.left, submenu.bottom, addRow.right, addRow.top };
    } else {
        return false;
    }
    return PointInRect(bridge, x, y);
}

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] int HierarchyMaxScroll(const RECT& hierarchyContent, const EditorSceneContext& sceneContext) {
    const RECT list = HierarchyToolbarLayout::Resolve(hierarchyContent).listContent;
    const int viewportHeight = RectHeight(list);
    const int contentHeight = static_cast<int>(sceneContext.HierarchyRowCount()) * kHierarchyRowHeight;
    return std::max(0, contentHeight - viewportHeight);
}

} // namespace

EditorMouseMoveRouter::EditorMouseMoveRouter(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorShellInteractionState& shellInteraction,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport)
    , shellInteraction_(shellInteraction)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

void EditorMouseMoveRouter::Handle(HWND messageWindow, int x, int y, bool leftButtonDown) {
    if (sceneContext_.IsHierarchyContextMenuOpen()) {
        const std::optional<RECT> hierarchyContent = EditorPanelContentResolver::Resolve(DockPanelKind::Hierarchy, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        EditorHierarchyContextCommand hovered = EditorHierarchyContextCommand::None;
        if (hierarchyContent.has_value()) {
            const RECT menu = HierarchyContextMenuRect(*hierarchyContent, sceneContext_);
            if (ShowsHierarchyLightingSubmenu(sceneContext_.HierarchyContextMenuHoveredCommand())) {
                const RECT submenu = HierarchyLightingSubmenuRect(*hierarchyContent, menu);
                hovered = InHierarchyLightingBridge(menu, submenu, x, y)
                    ? EditorHierarchyContextCommand::AddLighting
                    : HierarchyLightingSubmenuCommandAt(submenu, x, y);
            }
            if (hovered == EditorHierarchyContextCommand::None) {
                hovered = HierarchyContextMenuCommandAt(menu, x, y);
            }
        }
        if (sceneContext_.SetHierarchyContextMenuHoveredCommand(hovered)) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        }
        if (hovered != EditorHierarchyContextCommand::None) {
            return;
        }
    }

    EditorSceneViewportCameraController sceneCamera(mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, sceneViewport_);
    if (sceneCamera.HandlePointerMove(messageWindow, x, y)) {
        return;
    }

    const bool wasGizmoDragging = sceneContext_.Gizmo().IsDragging();
    if (EditorSceneViewportObjectInteraction::UpdateGizmoDragOrHover(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, leftButtonDown)) {
        sceneViewport_.RequestPresent();
        if (!wasGizmoDragging) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        }
        return;
    }

    const EditorInternalSplitterCursorController splitterCursor(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_);
    splitterCursor.UpdateCursor(x, y);
    static_cast<void>(EditorWindowToolbarPointerHandler::HandleMouseMove(mainWindow_, messageWindow, x, y, dockModel_, shellInteraction_, metrics_));
    EditorInspectorPointerController inspectorPointer(sceneContext_);

    if (sceneContext_.IsHierarchyScrollbarDragging()) {
        if (!leftButtonDown) {
            sceneContext_.EndHierarchyScrollbarDrag();
        } else if (const std::optional<RECT> hierarchyContent = EditorPanelContentResolver::Resolve(DockPanelKind::Hierarchy, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
                   hierarchyContent.has_value()) {
            const RECT list = HierarchyToolbarLayout::Resolve(*hierarchyContent).listContent;
            sceneContext_.DragHierarchyScrollbar(y, std::max(1, RectHeight(list) - 24), HierarchyMaxScroll(*hierarchyContent, sceneContext_));
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    const bool draggingMeshPreview = sceneContext_.Inspector().IsDraggingMeshPreview();
    if (leftButtonDown && inspectorPointer.HandlePointerDrag(x, y)) {
        if (!draggingMeshPreview) {
            sceneViewport_.RequestPresent();
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (leftButtonDown && pointerDrag_.Potential() && EditorPointerDragInteraction::Move(messageWindow, mainWindow_, x, y, pointerDrag_)) {
        sceneViewport_.RequestPresent();
        return;
    }

    if (leftButtonDown && dockController_.HandlePointerMove(messageWindow, x, y, true)) {
        sceneViewport_.RequestPresent();
        return;
    }

    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (consolePointer.UpdateHoverOrClear(consoleContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (consolePointer.HandlePointerMove(consoleContent, y, leftButtonDown)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerMove(messageWindow, mainWindow_, x, y, leftButtonDown, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }
    const std::optional<RECT> inspectorContent = EditorPanelContentResolver::Resolve(DockPanelKind::Inspector, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (inspectorPointer.UpdateHoverOrClear(inspectorContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (inspectorPointer.Contains(inspectorContent, x, y)) {
        return;
    }

    const std::optional<RECT> projectSettingsContent = EditorPanelContentResolver::Resolve(DockPanelKind::ProjectSettings, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorProjectSettingsPointerController projectSettingsPointer(sceneContext_);
    if (projectSettingsPointer.UpdateHoverOrClear(projectSettingsContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (projectSettingsPointer.Contains(projectSettingsContent, x, y)) {
        return;
    }

    const std::optional<RECT> pluginsContent = EditorPanelContentResolver::Resolve(DockPanelKind::Plugins, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorPluginsPointerController pluginsPointer(sceneContext_);
    if (pluginsPointer.HandlePointerMove(pluginsContent, x, y, leftButtonDown)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        if (sceneContext_.Plugins().IsScrollbarDragging()) {
            return;
        }
    }
    if (pluginsPointer.Contains(pluginsContent, x, y)) {
        return;
    }
    if (dockController_.HandlePointerMove(messageWindow, x, y, leftButtonDown)) {
        sceneViewport_.RequestPresent();
    }
    splitterCursor.UpdateCursor(x, y);
}

} // namespace kb::editor

#endif
