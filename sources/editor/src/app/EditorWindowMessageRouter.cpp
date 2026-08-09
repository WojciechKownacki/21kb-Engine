#include "app/EditorWindowMessageRouter.hpp"

#if defined(_WIN32)
#include "app/EditorEditCommandInputHandler.hpp"
#include "app/EditorHierarchySearchInputHandler.hpp"
#include "app/EditorSkeletalMeshTreeSearchInputHandler.hpp"
#include "app/EditorTextInputShortcuts.hpp"
#include "app/EditorAssetBrowserInputHandler.hpp"
#include "app/EditorWindowHitTestHandler.hpp"
#include "app/EditorWindowLifecycleHandler.hpp"
#include "app/EditorPaintDispatcher.hpp"
#include "app/EditorWindowPointerMessageDispatcher.hpp"
#include "app/EditorWindowResizeHandler.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorViewportCameraNavigationInput.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "inspection/InspectorPanelInteraction.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/FloatingWindowBackBufferPainter.hpp"
#include "rendering/MainWindowBackBufferPainter.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "scene/EditorTerrainService.hpp"

#include <optional>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] bool ModifierDown(int virtualKey) noexcept {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

void FinishTerrainStroke(EditorWindowMessageContext& context) {
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    if (!tool.strokeActive) {
        return;
    }
    tool.strokeActive = false;
    tool.heldSculptElapsedSeconds = 0.0F;
    std::string error;
    if (!context.sceneContext.CommitTerrainBrushStroke(&error)) {
        context.sceneContext.Console().Warning(
            "Terrain",
            error.empty() ? "Terrain stroke could not be committed." : error);
    }
}

void CancelAnimationPreviewCameraNavigation(EditorWindowMessageContext& context) noexcept {
    if (!context.sceneContext.AnimationPreviewCamera().IsNavigating()) return;
    context.sceneContext.AnimationPreviewCamera().EndNavigation();
    RestoreEditorViewportNavigationCursor();
    context.sceneViewport.RequestPresent();
}

[[nodiscard]] bool HandleTransformToolShortcut(EditorSceneContext& sceneContext, WPARAM key) noexcept {
    if (ModifierDown(VK_CONTROL) || ModifierDown(VK_MENU) ||
        sceneContext.HasActiveViewportCameraNavigation() ||
        sceneContext.AnimationPreviewCamera().IsNavigating() || sceneContext.Gizmo().IsDragging()) {
        return false;
    }
    // A single letter W/E/R must not retarget the gizmo while the user is
    // typing into any inline text field (e.g. renaming an entity or folder
    // whose name contains 'w', 'e' or 'r') — the letter belongs to the text.
    if (sceneContext.IsAnyInlineTextEditActive()) {
        return false;
    }

    EditorTransformToolMode mode = sceneContext.Gizmo().toolMode;
    switch (key) {
    case 'W':
        mode = EditorTransformToolMode::Translate;
        break;
    case 'E':
        mode = EditorTransformToolMode::Rotate;
        break;
    case 'R':
        mode = EditorTransformToolMode::Scale;
        break;
    default:
        return false;
    }

    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (gizmo.toolMode == mode) {
        return true;
    }
    gizmo.toolMode = mode;
    gizmo.hoveredAxis = -1;
    gizmo.draggedAxis = -1;
    sceneContext.MarkSceneRenderDirty();
    return true;
}

// Viewport "frame selected" (F): recenters the scene camera on the current
// entity selection. Suppressed while typing (so 'f' in a name is text) and
// while the material graph handled it or the camera is being flown.
[[nodiscard]] bool HandleSceneFrameSelectionShortcut(EditorSceneContext& sceneContext, WPARAM key) noexcept {
    if (key != 'F' || ModifierDown(VK_CONTROL) || ModifierDown(VK_MENU) || ModifierDown(VK_SHIFT)) {
        return false;
    }
    if (sceneContext.HasActiveViewportCameraNavigation() || sceneContext.IsAnyInlineTextEditActive()) {
        return false;
    }
    return sceneContext.FrameSelectedEntitiesInViewport();
}

[[nodiscard]] bool HandleMaterialGraphShortcut(HWND messageWindow, EditorWindowMessageContext& context, WPARAM key) {
    HWND mainWindow = context.mainWindow;
    EditorSceneContext& sceneContext = context.sceneContext;
    if (!sceneContext.IsMaterialGraphFocused()) {
        return false;
    }

    if (sceneContext.IsMaterialGraphTexturePickerOpen()) {
        bool handled = true;
        switch (key) {
        case VK_BACK:
            sceneContext.BackspaceMaterialGraphTexturePickerSearch();
            break;
        case VK_RETURN:
            static_cast<void>(sceneContext.SetMaterialGraphTextureSampleAsset(
                sceneContext.MaterialGraphTexturePickerAssetId(),
                sceneContext.MaterialGraphTexturePickerNodeId(),
                sceneContext.MaterialGraphTexturePickerSelectedAssetId()));
            static_cast<void>(sceneContext.CloseMaterialGraphTexturePicker());
            break;
        case VK_ESCAPE:
            static_cast<void>(sceneContext.CloseMaterialGraphTexturePicker());
            break;
        default:
            handled = true;
            break;
        }
        if (handled) {
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != mainWindow) {
                InvalidateRect(mainWindow, nullptr, FALSE);
            }
            return true;
        }
    }

    if (sceneContext.IsMaterialGraphContextMenuOpen()) {
        bool handled = true;
        switch (key) {
        case VK_BACK:
            sceneContext.BackspaceMaterialGraphContextMenuSearch();
            break;
        case VK_UP:
            static_cast<void>(sceneContext.MoveMaterialGraphContextMenuKeyboardSelection(-1));
            break;
        case VK_DOWN:
            static_cast<void>(sceneContext.MoveMaterialGraphContextMenuKeyboardSelection(1));
            break;
        case VK_RETURN:
            static_cast<void>(sceneContext.ActivateMaterialGraphContextMenuKeyboardSelection());
            break;
        case VK_ESCAPE:
            static_cast<void>(sceneContext.CloseMaterialGraphContextMenu());
            static_cast<void>(sceneContext.CancelMaterialGraphPinConnection());
            break;
        default:
            handled = false;
            break;
        }
        if (handled) {
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != mainWindow) {
                InvalidateRect(mainWindow, nullptr, FALSE);
            }
            return true;
        }
    }

    if (key == VK_SPACE && !ModifierDown(VK_CONTROL) && !ModifierDown(VK_MENU) && !ModifierDown(VK_SHIFT)) {
        const std::optional<RECT> materialEditorContent =
            EditorPanelContentResolver::Resolve(DockPanelKind::MaterialEditor, messageWindow, mainWindow, context.dockModel, context.floatingWindows, context.metrics);
        if (!materialEditorContent.has_value()) {
            return false;
        }
        const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
        const int canvasWidth = std::max(0L, layout.graphCanvas.right - layout.graphCanvas.left);
        const int canvasHeight = std::max(0L, layout.graphCanvas.bottom - layout.graphCanvas.top);
        if (canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        const int x = layout.graphCanvas.left + canvasWidth / 2;
        const int y = layout.graphCanvas.top + canvasHeight / 2;
        const float zoom = std::max(0.1F, sceneContext.MaterialGraphZoom());
        const int graphX = static_cast<int>(static_cast<float>(x - layout.graphCanvas.left - sceneContext.MaterialGraphPanX()) / zoom);
        const int graphY = static_cast<int>(static_cast<float>(y - layout.graphCanvas.top - sceneContext.MaterialGraphPanY()) / zoom);
        sceneContext.SetMaterialGraphCanvasViewport(layout.graphCanvas.left, layout.graphCanvas.top, canvasWidth, canvasHeight);
        if (!sceneContext.OpenMaterialGraphContextMenu(sceneContext.MaterialEditor().OpenAssetId(), x, y, graphX, graphY)) {
            return false;
        }
        InvalidateRect(messageWindow, nullptr, FALSE);
        if (messageWindow != mainWindow) {
            InvalidateRect(mainWindow, nullptr, FALSE);
        }
        return true;
    }

    if (sceneContext.IsMaterialGraphConstantInlineEditing()) {
        switch (key) {
        case VK_BACK:
            sceneContext.BackspaceMaterialGraphConstantInlineEdit();
            break;
        case VK_RETURN:
            static_cast<void>(sceneContext.CommitMaterialGraphConstantInlineEdit());
            break;
        case VK_ESCAPE:
            sceneContext.CancelMaterialGraphConstantInlineEdit();
            break;
        default:
            return false;
        }
        InvalidateRect(messageWindow, nullptr, FALSE);
        if (messageWindow != mainWindow) {
            InvalidateRect(mainWindow, nullptr, FALSE);
        }
        return true;
    }

    if (sceneContext.IsMaterialGraphNodeRenameEditing()) {
        switch (EditorTextInputShortcuts::Resolve(key)) {
        case EditorTextInputShortcut::SelectAll:
            sceneContext.SelectAllMaterialGraphNodeRenameEditText();
            break;
        case EditorTextInputShortcut::Copy:
            static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(messageWindow, sceneContext.MaterialEditor().GraphNodeRenameEditBuffer()));
            break;
        case EditorTextInputShortcut::Cut:
            static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(messageWindow, sceneContext.MaterialEditor().GraphNodeRenameEditBuffer()));
            sceneContext.ClearMaterialGraphNodeRenameEditText();
            break;
        case EditorTextInputShortcut::Paste:
            if (const std::optional<std::string> text = EditorTextInputShortcuts::PasteFromClipboard(messageWindow); text.has_value()) {
                sceneContext.InsertMaterialGraphNodeRenameEditText(*text);
            }
            break;
        case EditorTextInputShortcut::None:
            switch (key) {
            case VK_BACK:
                sceneContext.BackspaceMaterialGraphNodeRenameEdit();
                break;
            case VK_RETURN:
                static_cast<void>(sceneContext.CommitMaterialGraphNodeRenameEdit());
                break;
            case VK_ESCAPE:
                sceneContext.CancelMaterialGraphNodeRenameEdit();
                break;
            default:
                return false;
            }
            break;
        }
        InvalidateRect(messageWindow, nullptr, FALSE);
        if (messageWindow != mainWindow) {
            InvalidateRect(mainWindow, nullptr, FALSE);
        }
        return true;
    }

    if (sceneContext.IsMaterialEditorFindFocused()) {
        switch (EditorTextInputShortcuts::Resolve(key)) {
        case EditorTextInputShortcut::Copy:
            static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(messageWindow, sceneContext.MaterialEditor().FindQuery()));
            break;
        case EditorTextInputShortcut::Cut:
            static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(messageWindow, sceneContext.MaterialEditor().FindQuery()));
            sceneContext.ClearMaterialEditorFind();
            break;
        case EditorTextInputShortcut::Paste:
            if (const std::optional<std::string> text = EditorTextInputShortcuts::PasteFromClipboard(messageWindow); text.has_value()) {
                sceneContext.InsertMaterialEditorFindText(*text);
            }
            break;
        case EditorTextInputShortcut::SelectAll:
            break;
        case EditorTextInputShortcut::None:
            switch (key) {
            case VK_BACK:
                sceneContext.BackspaceMaterialEditorFind();
                break;
            case VK_RETURN:
                static_cast<void>(sceneContext.FocusFirstMaterialEditorFindResult());
                break;
            case VK_ESCAPE:
                sceneContext.FocusMaterialEditorFind(false);
                break;
            default:
                return false;
            }
            break;
        }
        InvalidateRect(messageWindow, nullptr, FALSE);
        if (messageWindow != mainWindow) {
            InvalidateRect(mainWindow, nullptr, FALSE);
        }
        return true;
    }

    if (key == VK_F2 && !ModifierDown(VK_CONTROL) && !ModifierDown(VK_MENU) && !ModifierDown(VK_SHIFT)) {
        const kb::assets::AssetId materialId = sceneContext.MaterialEditor().OpenAssetId();
        const std::uint32_t nodeId = sceneContext.SelectedMaterialGraphNodeId();
        if (nodeId != 0U && sceneContext.BeginMaterialGraphNodeRenameEdit(materialId, nodeId)) {
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != mainWindow) {
                InvalidateRect(mainWindow, nullptr, FALSE);
            }
            return true;
        }
    }

    if (ModifierDown(VK_CONTROL) && !ModifierDown(VK_MENU)) {
        const kb::assets::AssetId materialId = sceneContext.MaterialEditor().OpenAssetId();
        bool graphClipboardShortcut = false;
        switch (key) {
        case 'C':
            graphClipboardShortcut = true;
            static_cast<void>(sceneContext.CopySelectedMaterialGraphNodes());
            break;
        case 'F':
            graphClipboardShortcut = true;
            sceneContext.FocusMaterialEditorFind(true);
            break;
        case 'V':
            graphClipboardShortcut = true;
            static_cast<void>(sceneContext.PasteMaterialGraphNodes(materialId, 32, 32));
            break;
        case 'D':
            graphClipboardShortcut = true;
            static_cast<void>(sceneContext.DuplicateSelectedMaterialGraphNodes(materialId, 32, 32));
            break;
        default:
            break;
        }
        if (graphClipboardShortcut) {
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != mainWindow) {
                InvalidateRect(mainWindow, nullptr, FALSE);
            }
            return true;
        }
    }

    if (key == 'F' && !ModifierDown(VK_CONTROL) && !ModifierDown(VK_MENU) && !ModifierDown(VK_SHIFT)) {
        static_cast<void>(sceneContext.FrameSelectedMaterialGraphNodes());
        InvalidateRect(messageWindow, nullptr, FALSE);
        if (messageWindow != mainWindow) {
            InvalidateRect(mainWindow, nullptr, FALSE);
        }
        return true;
    }

    if (key != VK_DELETE) {
        return false;
    }

    if (!sceneContext.SelectedMaterialGraphNodeIds().empty()) {
        static_cast<void>(sceneContext.DeleteSelectedMaterialGraphNode(sceneContext.MaterialEditor().OpenAssetId()));
    } else if (sceneContext.SelectedMaterialGraphCommentId() != 0U) {
        static_cast<void>(sceneContext.DeleteSelectedMaterialGraphComment(sceneContext.MaterialEditor().OpenAssetId()));
    }
    InvalidateRect(messageWindow, nullptr, FALSE);
    if (messageWindow != mainWindow) {
        InvalidateRect(mainWindow, nullptr, FALSE);
    }
    return true;
}

[[nodiscard]] bool HandleMaterialGraphChar(HWND mainWindow, HWND messageWindow, EditorSceneContext& sceneContext, wchar_t character) {
    if (!sceneContext.IsMaterialGraphFocused()) {
        return false;
    }
    if (sceneContext.IsMaterialGraphTexturePickerOpen()) {
        if (character == VK_BACK || character == VK_ESCAPE || character == VK_RETURN) {
            return false;
        }
        sceneContext.AppendMaterialGraphTexturePickerSearchText(character);
        InvalidateRect(messageWindow, nullptr, FALSE);
        if (messageWindow != mainWindow) {
            InvalidateRect(mainWindow, nullptr, FALSE);
        }
        return true;
    }
    if (sceneContext.IsMaterialGraphContextMenuOpen()) {
        if (character == VK_BACK || character == VK_ESCAPE || character == VK_RETURN) {
            return false;
        }
        sceneContext.AppendMaterialGraphContextMenuSearchText(character);
        InvalidateRect(messageWindow, nullptr, FALSE);
        if (messageWindow != mainWindow) {
            InvalidateRect(mainWindow, nullptr, FALSE);
        }
        return true;
    }
    if (!sceneContext.IsMaterialGraphConstantInlineEditing()) {
        if (!sceneContext.IsMaterialGraphNodeRenameEditing()) {
            if (sceneContext.IsMaterialEditorFindFocused()) {
                if (character == VK_BACK || character == VK_ESCAPE || character == VK_RETURN) {
                    return false;
                }
                sceneContext.AppendMaterialEditorFindText(character);
                InvalidateRect(messageWindow, nullptr, FALSE);
                if (messageWindow != mainWindow) {
                    InvalidateRect(mainWindow, nullptr, FALSE);
                }
                return true;
            }
            return false;
        }
        if (character == VK_BACK || character == VK_ESCAPE || character == VK_RETURN) {
            return false;
        }
        sceneContext.AppendMaterialGraphNodeRenameEditText(character);
        InvalidateRect(messageWindow, nullptr, FALSE);
        if (messageWindow != mainWindow) {
            InvalidateRect(mainWindow, nullptr, FALSE);
        }
        return true;
    }
    if (character == VK_BACK || character == VK_ESCAPE || character == VK_RETURN) {
        return false;
    }
    sceneContext.AppendMaterialGraphConstantInlineEditText(character);
    InvalidateRect(messageWindow, nullptr, FALSE);
    if (messageWindow != mainWindow) {
        InvalidateRect(mainWindow, nullptr, FALSE);
    }
    return true;
}

} // namespace

EditorWindowMessageRouter::EditorWindowMessageRouter(EditorWindowMessageContext context) noexcept
    : context_(context) {}

LRESULT EditorWindowMessageRouter::Handle(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        HDC controlDc = reinterpret_cast<HDC>(wparam);
        SetTextColor(controlDc, RGB(196, 205, 214));
        SetBkColor(controlDc, RGB(20, 22, 24));
        static HBRUSH consoleDetailBrush = CreateSolidBrush(RGB(20, 22, 24));
        return reinterpret_cast<LRESULT>(consoleDetailBrush);
    }
    case WM_PAINT:
        EditorPaintDispatcher{
            context_.mainWindow,
            context_.dockModel,
            context_.sceneContext,
            context_.theme,
            context_.metrics,
            context_.renderer,
            context_.renderBackendSettings,
            context_.playMode,
            context_.shellInteraction,
            context_.sceneViewport,
            context_.floatingWindows,
            context_.dockController,
            context_.pointerDrag,
        }.Paint(messageWindow);
        return 0;
    case WM_ENTERSIZEMOVE:
        return EditorWindowResizeHandler::HandleEnterSizeMove(messageWindow, context_.sceneViewport);
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) {
            context_.sceneViewport.SetHostSurfaceSuspended(messageWindow, true);
        } else {
            context_.sceneViewport.SetHostSurfaceSuspended(messageWindow, false);
        }
        return EditorWindowResizeHandler::HandleSize(messageWindow, wparam, lparam, context_.dockModel, context_.floatingWindows, context_.sceneViewport);
    case WM_DPICHANGED:
        context_.sceneViewport.NotifyHostDpiChanged(messageWindow);
        return DefWindowProcW(messageWindow, message, wparam, lparam);
    case WM_ACTIVATEAPP:
        if (wparam == FALSE) {
            // Native scene surfaces are WS_CHILD and therefore cannot escape
            // their editor host. Keeping their last frame avoids flashing the
            // Scene View gray while the editor is merely inactive. Owned
            // WS_POPUP overlays are different: hide them explicitly.
            MainWindowBackBufferPainter::HideAllOverlays();
            FloatingWindowBackBufferPainter::HideAllOverlays();
            return 0;
        }
        if (context_.mainWindow != nullptr) {
            InvalidateRect(context_.mainWindow, nullptr, FALSE);
        }
        for (const HWND floatingWindow : context_.floatingWindows.Queries().Windows()) {
            if (floatingWindow != nullptr) {
                InvalidateRect(floatingWindow, nullptr, FALSE);
            }
        }
        return 0;
    case WM_EXITSIZEMOVE:
        return EditorWindowResizeHandler::HandlePlacementChanged(messageWindow, context_.sceneViewport);
    case WM_CANCELMODE:
        FinishTerrainStroke(context_);
        CancelAnimationPreviewCameraNavigation(context_);
        static_cast<void>(context_.sceneContext.CancelMaterialGraphInteractions());
        EditorSceneViewportCameraController{
            context_.mainWindow,
            context_.dockModel,
            context_.floatingWindows,
            context_.metrics,
            context_.sceneContext,
            context_.sceneViewport,
        }.Cancel(messageWindow);
        static_cast<void>(EditorSceneViewportObjectInteraction::CancelGizmoDrag(context_.sceneContext));
        context_.dockController.CancelDrag();
        context_.pointerDrag.Clear();
        context_.shellInteraction.ClearPressedSave();
        context_.shellInteraction.ClearPressedTransport();
        context_.sceneViewport.RequestPresent();
        return 0;
    case WM_CAPTURECHANGED:
    {
        HWND newCapture = reinterpret_cast<HWND>(lparam);
        context_.dockController.HandleCaptureChanged(newCapture);
        if (newCapture != messageWindow) {
            FinishTerrainStroke(context_);
            CancelAnimationPreviewCameraNavigation(context_);
            static_cast<void>(context_.sceneContext.CancelMaterialGraphInteractions());
            EditorSceneViewportCameraController{
                context_.mainWindow,
                context_.dockModel,
                context_.floatingWindows,
                context_.metrics,
                context_.sceneContext,
                context_.sceneViewport,
            }.Cancel(messageWindow);
            static_cast<void>(EditorSceneViewportObjectInteraction::CancelGizmoDrag(context_.sceneContext));
        }
        const bool captureStayedInEditor = newCapture == context_.mainWindow || context_.floatingWindows.Queries().IsFloatingWindow(newCapture);
        if (!captureStayedInEditor) {
            context_.pointerDrag.Clear();
            context_.shellInteraction.ClearPressedSave();
            context_.shellInteraction.ClearPressedTransport();
            if (context_.mainWindow != nullptr) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(messageWindow, nullptr, FALSE);
            }
        }
        context_.sceneViewport.RequestPresent();
        return 0;
    }
    case WM_CHAR:
        if (HandleMaterialGraphChar(context_.mainWindow, messageWindow, context_.sceneContext, static_cast<wchar_t>(wparam))) {
            return 0;
        }
        if (InspectorPanelInteraction::HandleChar(context_.sceneContext, static_cast<wchar_t>(wparam))) {
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        if (EditorAssetBrowserInputHandler{ context_.mainWindow, context_.sceneContext }.HandleChar(messageWindow, wparam)) {
            return 0;
        }
        if (EditorHierarchySearchInputHandler{ context_.mainWindow, context_.sceneContext }.HandleChar(messageWindow, wparam)) {
            return 0;
        }
        if (EditorSkeletalMeshTreeSearchInputHandler{ context_.mainWindow, context_.sceneContext }.HandleChar(messageWindow, wparam)) {
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (InspectorPanelInteraction::HandleKeyCapture(context_.sceneContext, wparam)) {
            context_.sceneViewport.RequestPresent();
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        if (InspectorPanelInteraction::HandleKeyDown(messageWindow, context_.sceneContext, wparam)) {
            context_.sceneViewport.RequestPresent();
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        if (EditorAssetBrowserInputHandler{ context_.mainWindow, context_.sceneContext }.HandleKeyDown(messageWindow, wparam)) {
            return 0;
        }
        if (HandleMaterialGraphShortcut(messageWindow, context_, wparam)) {
            context_.sceneViewport.RequestPresent();
            return 0;
        }
        if (EditorHierarchySearchInputHandler{ context_.mainWindow, context_.sceneContext }.HandleKeyDown(messageWindow, wparam)) {
            context_.sceneViewport.RequestPresent();
            return 0;
        }
        if (EditorSkeletalMeshTreeSearchInputHandler{ context_.mainWindow, context_.sceneContext }.HandleKeyDown(messageWindow, wparam)) {
            context_.sceneViewport.RequestPresent();
            return 0;
        }
        if (HandleTransformToolShortcut(context_.sceneContext, wparam)) {
            context_.sceneViewport.RequestPresent();
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        if (HandleSceneFrameSelectionShortcut(context_.sceneContext, wparam)) {
            context_.sceneViewport.RequestPresent();
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        if (EditorEditCommandInputHandler{ context_.sceneContext }.HandleKeyDown(wparam)) {
            context_.sceneViewport.RequestPresent();
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_TIMER:
        if (EditorWindowResizeHandler::HandleTimer(messageWindow, wparam, context_.sceneViewport)) {
            return 0;
        }
        if (EditorSceneViewportCameraController{
                context_.mainWindow,
                context_.dockModel,
                context_.floatingWindows,
                context_.metrics,
                context_.sceneContext,
                context_.sceneViewport,
            }.HandleTimer(messageWindow, wparam)) {
            return 0;
        }
        break;
    case WM_NCHITTEST:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_SETCURSOR:
        if (message == WM_NCHITTEST) {
            return EditorWindowHitTestHandler::Handle(messageWindow, lparam, context_.floatingWindows);
        }
        if (context_.sceneContext.Inspector().IsListeningForKey() &&
            (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN)) {
            const int captureButton = message == WM_LBUTTONDOWN ? VK_LBUTTON : (message == WM_RBUTTONDOWN ? VK_RBUTTON : VK_MBUTTON);
            static_cast<void>(InspectorPanelInteraction::HandleKeyCapture(context_.sceneContext, static_cast<WPARAM>(captureButton)));
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        return EditorWindowPointerMessageDispatcher{ context_ }.Dispatch(messageWindow, message, wparam, lparam);
    case WM_CONTEXTMENU:
        return 0;
    case WM_CLOSE:
        return EditorWindowLifecycleHandler{ context_.mainWindow, context_.running, context_.dockModel, context_.floatingWindows, context_.sceneContext }.HandleClose(messageWindow);
    case WM_DESTROY:
        return EditorWindowLifecycleHandler{ context_.mainWindow, context_.running, context_.dockModel, context_.floatingWindows, context_.sceneContext }.HandleDestroy(messageWindow);
    default:
        break;
    }

    return DefWindowProcW(messageWindow, message, wparam, lparam);
}

} // namespace kb::editor

#endif
