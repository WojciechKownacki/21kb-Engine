#include "app/EditorWindowPointerHandler.hpp"

#if defined(_WIN32)
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorPointerDragSourceResolver.hpp"
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/EditorToolbarRenderer.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorHierarchyContentResolver.hpp"

#include <windowsx.h>

#include <optional>

namespace kb::editor {
namespace {

bool CommitPendingNewAssetFolder(EditorSceneContext& sceneContext) {
    if (sceneContext.AssetBrowser().TextEditMode() != EditorAssetTextEditMode::NewFolder) {
        return false;
    }
    static_cast<void>(sceneContext.CommitAssetTextEdit());
    return true;
}

bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] RECT ToRect(const DockRect& rect) noexcept {
    return RECT{
        .left = rect.x,
        .top = rect.y,
        .right = rect.x + rect.width,
        .bottom = rect.y + rect.height,
    };
}

} // namespace

EditorWindowPointerHandler::EditorWindowPointerHandler(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorHierarchySelectionController& hierarchySelection,
    EditorSceneContext& sceneContext,
    EditorRenderBackendSettings& renderBackendSettings,
    EditorPlayModeState& playMode,
    EditorShellInteractionState& shellInteraction,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , hierarchySelection_(hierarchySelection)
    , sceneContext_(sceneContext)
    , renderBackendSettings_(renderBackendSettings)
    , playMode_(playMode)
    , shellInteraction_(shellInteraction)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

LRESULT EditorWindowPointerHandler::HandleLeftButtonDown(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    const bool committedNewFolder = CommitPendingNewAssetFolder(sceneContext_);
    if (committedNewFolder) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (messageWindow == mainWindow_) {
        RECT client{};
        if (GetClientRect(mainWindow_, &client) != 0) {
            const DockLayout layout = dockModel_.Queries().BuildLayout(
                client.right - client.left,
                client.bottom - client.top,
                metrics_.menuHeight,
                metrics_.toolbarHeight,
                metrics_.tabStripHeight,
                metrics_.tabMinWidth,
                metrics_.tabWidth,
                metrics_.splitterSize,
                metrics_.panelPadding);
            const EditorMenuRects menu = EditorToolbarRenderer::ResolveMenu(ToRect(layout.menu), shellInteraction_.OpenMenu());
            if (const EditorMenuCommand hitMenu = EditorToolbarRenderer::HitTestMenu(menu, x, y); hitMenu != EditorMenuCommand::None) {
                static_cast<void>(shellInteraction_.SetHoveredMenu(hitMenu));
                static_cast<void>(shellInteraction_.SetOpenMenu(hitMenu));
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return 0;
            }
            if (shellInteraction_.OpenMenu() != EditorMenuCommand::None) {
                if (EditorToolbarRenderer::HitTestMenuRow(menu, x, y).has_value()) {
                    shellInteraction_.CloseMenu();
                    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                    return 0;
                }
                shellInteraction_.CloseMenu();
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            }
            const EditorToolbarRects toolbar = EditorToolbarRenderer::ResolveToolbar(ToRect(layout.toolbar));
            const EditorTransportCommand transport = EditorToolbarRenderer::HitTestTransport(toolbar, x, y);
            const bool transportEnabled =
                (transport == EditorTransportCommand::Play && playMode_.Mode() == EditorPlayMode::Stopped) ||
                (transport == EditorTransportCommand::Pause && (playMode_.IsPlaying() || playMode_.IsPaused())) ||
                (transport == EditorTransportCommand::Stop && (playMode_.IsPlaying() || playMode_.IsPaused()));
            if (transportEnabled) {
                static_cast<void>(shellInteraction_.SetPressedTransport(transport));
            }
            if (transport == EditorTransportCommand::Play) {
                if (playMode_.Mode() != EditorPlayMode::Stopped) {
                    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                    return 0;
                }
                playMode_.Play();
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return 0;
            }
            if (transport == EditorTransportCommand::Stop) {
                if (!transportEnabled) {
                    return 0;
                }
                playMode_.Stop();
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return 0;
            }
            if (transport == EditorTransportCommand::Pause) {
                if (!transportEnabled) {
                    return 0;
                }
                if (playMode_.IsPaused()) {
                    playMode_.Resume();
                } else {
                    playMode_.Pause();
                }
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return 0;
            }
        }
    }
    const std::optional<RECT> assetContent = EditorPanelContentResolver::Resolve(DockPanelKind::Assets, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    const std::optional<RECT> hierarchyContent = EditorHierarchyContentResolver::Resolve(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    const std::optional<EditorResolvedPanelContent> scenePanelContent =
        EditorPanelContentResolver::ResolvePanel(DockPanelKind::Scene, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    const std::optional<EditorResolvedPanelContent> gamePanelContent =
        EditorPanelContentResolver::ResolvePanel(DockPanelKind::Game, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    std::optional<EditorResolvedPanelContent> sceneContent{};
    if (scenePanelContent.has_value() && PointInRect(scenePanelContent->content, x, y)) {
        sceneContent = scenePanelContent;
    } else if (gamePanelContent.has_value() && PointInRect(gamePanelContent->content, x, y)) {
        sceneContent = gamePanelContent;
    }
    const bool inAssetPanel = assetContent.has_value() && PointInRect(*assetContent, x, y);
    const bool inHierarchyPanel = hierarchyContent.has_value() && PointInRect(*hierarchyContent, x, y);

    if (sceneContent.has_value()) {
        const SceneViewportToolbarRects sceneToolbar = SceneViewportToolbarRenderer::Resolve(sceneContent->content);
        if (PointInRect(sceneToolbar.profileButton, x, y)) {
            sceneContext_.ViewportPreview(sceneContent->panelId).CycleProfile();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return 0;
        }
        if (PointInRect(sceneToolbar.fitButton, x, y)) {
            sceneContext_.ViewportPreview(sceneContent->panelId).CycleFitMode();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return 0;
        }
        const DockPanel* viewportPanel = dockModel_.Queries().FindPanel(sceneContent->panelId);
        if (viewportPanel != nullptr && viewportPanel->kind == DockPanelKind::Scene && PointInRect(sceneToolbar.cameraButton, x, y)) {
            sceneContext_.ViewportPreview(sceneContent->panelId).CycleCameraMode();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return 0;
        }
    }

    EditorPointerDragSourceResolver::Resolve(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
    EditorPointerDragInteraction::CaptureIfActive(messageWindow, pointerDrag_);

    if (hierarchySelection_.HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.ClearHierarchySelection();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (!inAssetPanel) {
        sceneContext_.AssetBrowser().FocusSelection(false);
    }
    if (!inHierarchyPanel) {
        sceneContext_.ClearHierarchySelection();
    }
    dockController_.HandlePointerDown(messageWindow, x, y);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleRightButtonDown(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    const bool committedNewFolder = CommitPendingNewAssetFolder(sceneContext_);
    if (committedNewFolder) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }

    pointerDrag_.Clear();
    if (EditorAssetBrowserPointerHandler::HandleRightButtonDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    return 0;
}

LRESULT EditorWindowPointerHandler::HandleLeftButtonDoubleClick(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);

    if (EditorAssetBrowserPointerHandler::HandleDoubleClick(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.ClearHierarchySelection();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    return HandleLeftButtonDown(messageWindow, lparam);
}

LRESULT EditorWindowPointerHandler::HandleMouseMove(HWND messageWindow, LPARAM lparam) {
    if (messageWindow == mainWindow_) {
        RECT client{};
        if (GetClientRect(mainWindow_, &client) != 0) {
            const DockLayout layout = dockModel_.Queries().BuildLayout(
                client.right - client.left,
                client.bottom - client.top,
                metrics_.menuHeight,
                metrics_.toolbarHeight,
                metrics_.tabStripHeight,
                metrics_.tabMinWidth,
                metrics_.tabWidth,
                metrics_.splitterSize,
                metrics_.panelPadding);
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            const EditorMenuRects menu = EditorToolbarRenderer::ResolveMenu(ToRect(layout.menu), shellInteraction_.OpenMenu());
            bool changed = false;
            changed = shellInteraction_.SetHoveredMenu(EditorToolbarRenderer::HitTestMenu(menu, x, y)) || changed;
            changed = shellInteraction_.SetHoveredMenuRow(EditorToolbarRenderer::HitTestMenuRow(menu, x, y)) || changed;
            changed = shellInteraction_.SetHoveredTransport(EditorToolbarRenderer::HitTestTransport(EditorToolbarRenderer::ResolveToolbar(ToRect(layout.toolbar)), x, y)) || changed;
            if (changed) {
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            }
        }
    } else {
        shellInteraction_.ClearMenuHover();
        shellInteraction_.ClearTransportHover();
    }
    if (EditorAssetBrowserPointerHandler::HandlePointerMove(messageWindow, mainWindow_, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }
    if (EditorPointerDragInteraction::Move(messageWindow, mainWindow_, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), pointerDrag_)) {
        return 0;
    }
    dockController_.HandlePointerMove(messageWindow, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleLeftButtonUp(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);

    if (EditorAssetBrowserPointerHandler::HandlePointerUp(sceneContext_)) {
        shellInteraction_.ClearPressedTransport();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }
    shellInteraction_.ClearPressedTransport();

    const bool handledDrop = EditorPointerDragInteraction::Complete(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
    if (handledDrop) {
        return 0;
    }

    dockController_.HandlePointerUp(messageWindow);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleSetCursor(HWND messageWindow, WPARAM wparam, LPARAM lparam) {
    if (LOWORD(lparam) != HTCLIENT) {
        return DefWindowProcW(messageWindow, WM_SETCURSOR, wparam, lparam);
    }

    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(messageWindow, &point);
    if (EditorPointerDragInteraction::UpdateCursor(pointerDrag_)) {
        return TRUE;
    }
    dockController_.UpdateHoverCursor(messageWindow, point.x, point.y);
    return TRUE;
}

} // namespace kb::editor

#endif
