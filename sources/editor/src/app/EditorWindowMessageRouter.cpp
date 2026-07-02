#include "app/EditorWindowMessageRouter.hpp"

#if defined(_WIN32)
#include "app/EditorEditCommandInputHandler.hpp"
#include "app/EditorHierarchySearchInputHandler.hpp"
#include "app/EditorTextInputShortcuts.hpp"
#include "app/EditorAssetBrowserInputHandler.hpp"
#include "app/EditorWindowHitTestHandler.hpp"
#include "app/EditorWindowLifecycleHandler.hpp"
#include "app/EditorPaintDispatcher.hpp"
#include "app/EditorWindowPointerMessageDispatcher.hpp"
#include "app/EditorWindowResizeHandler.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "inspection/InspectorPanelInteraction.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool ModifierDown(int virtualKey) noexcept {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] bool HandleTransformToolShortcut(EditorSceneContext& sceneContext, WPARAM key) noexcept {
    if (ModifierDown(VK_CONTROL) || ModifierDown(VK_MENU) || sceneContext.HasActiveViewportCameraNavigation() || sceneContext.Gizmo().IsDragging()) {
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

[[nodiscard]] bool HandleMaterialGraphShortcut(HWND mainWindow, HWND messageWindow, EditorSceneContext& sceneContext, WPARAM key) {
    if (!sceneContext.IsMaterialGraphFocused()) {
        return false;
    }

    if (sceneContext.IsMaterialGraphContextMenuOpen()) {
        bool handled = true;
        switch (key) {
        case VK_BACK:
            sceneContext.BackspaceMaterialGraphContextMenuSearch();
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
        return EditorWindowResizeHandler::HandleSize(messageWindow, wparam, lparam, context_.dockModel, context_.floatingWindows, context_.sceneViewport);
    case WM_EXITSIZEMOVE:
        return EditorWindowResizeHandler::HandlePlacementChanged(messageWindow, context_.sceneViewport);
    case WM_CANCELMODE:
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
        if (HandleMaterialGraphShortcut(context_.mainWindow, messageWindow, context_.sceneContext, wparam)) {
            context_.sceneViewport.RequestPresent();
            return 0;
        }
        if (EditorHierarchySearchInputHandler{ context_.mainWindow, context_.sceneContext }.HandleKeyDown(messageWindow, wparam)) {
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
