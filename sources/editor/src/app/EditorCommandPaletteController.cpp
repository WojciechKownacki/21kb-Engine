#include "app/EditorCommandPaletteController.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserDoubleClickHandler.hpp"
#include "app/EditorWindowToolbarPointerHandler.hpp"
#include "assets/EditorAssetOpenPolicy.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "localization/EditorLocalization.hpp"
#include "rendering/EditorMenuTable.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>
#include <string>
#include <vector>

#include <windowsx.h>

namespace kb::editor {
namespace {

void Repaint(const EditorWindowMessageContext& context) {
    if (context.mainWindow != nullptr) {
        InvalidateRect(context.mainWindow, nullptr, FALSE);
    }
}

} // namespace

bool EditorCommandPaletteController::IsOpenShortcut(WPARAM key) noexcept {
    return EditorCommandPaletteModel::IsOpenShortcut(static_cast<int>(key));
}

void EditorCommandPaletteController::Open(
    HWND mainWindow, EditorDockModel& dockModel, EditorSceneContext& sceneContext, EditorShellInteractionState& shellInteraction) {
    shellInteraction.CloseMenu();
    EditorWindowToolbarPointerHandler::RefreshSuppliedMenus(shellInteraction, sceneContext, dockModel);
    EditorCommandPaletteModel& palette = shellInteraction.CommandPalette();
    palette.ClearEntries();
    EditorMenuTable::ListCommands(palette, EditorMenuTable::AllRows(shellInteraction));
    // The assets the editor can open, from the index the asset browser lists.
    for (const kb::assets::AssetMetadata& metadata : sceneContext.Scene().Assets().Manager().Registry().All()) {
        if (!EditorAssetOpenPolicy::CanOpen(metadata)) {
            continue;
        }
        const std::string path = metadata.virtualPath.generic_string();
        const std::string filename = metadata.name.empty() ? metadata.virtualPath.filename().string() : std::string{};
        palette.AddAsset(metadata.name.empty() ? std::string_view{ filename } : std::string_view{ metadata.name }, path, metadata.id.value);
    }
    palette.Open(std::string_view{ &EditorCommandPaletteModel::kCommandPrefix, 1U });
    if (mainWindow != nullptr) {
        InvalidateRect(mainWindow, nullptr, FALSE);
    }
}

void EditorCommandPaletteController::Close(EditorWindowMessageContext& context) {
    context.shellInteraction.CommandPalette().Close();
    Repaint(context);
}

bool EditorCommandPaletteController::HandleKeyDown(EditorWindowMessageContext& context, WPARAM key) {
    EditorCommandPaletteModel& palette = context.shellInteraction.CommandPalette();
    if (IsOpenShortcut(key)) {
        if (palette.IsOpen()) {
            Close(context);
        } else if (!context.sceneContext.IsAnyInlineTextEditActive()) {
            Open(context.mainWindow, context.dockModel, context.sceneContext, context.shellInteraction);
        }
        return true;
    }
    if (!palette.IsOpen()) {
        return false;
    }
    using Key = EditorCommandPaletteModel::Key;
    Key paletteKey{};
    switch (key) {
    case VK_ESCAPE:
        paletteKey = Key::Escape;
        break;
    case VK_RETURN:
        paletteKey = Key::Enter;
        break;
    case VK_UP:
        paletteKey = Key::Up;
        break;
    case VK_DOWN:
        paletteKey = Key::Down;
        break;
    case VK_PRIOR:
        paletteKey = Key::PageUp;
        break;
    case VK_NEXT:
        paletteKey = Key::PageDown;
        break;
    default:
        return false;
    }
    switch (palette.HandleKey(paletteKey)) {
    case EditorCommandPaletteModel::KeyResult::Close:
        Close(context);
        return true;
    case EditorCommandPaletteModel::KeyResult::Choose:
        ChooseSelected(context);
        return true;
    case EditorCommandPaletteModel::KeyResult::Moved:
        break;
    }
    Repaint(context);
    return true;
}

bool EditorCommandPaletteController::HandlePointer(
    EditorWindowMessageContext& context, HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) {
    EditorCommandPaletteModel& palette = context.shellInteraction.CommandPalette();
    if (!palette.IsOpen()) {
        return false;
    }
    const bool buttonDown = message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN;
    if (message == WM_MOUSEWHEEL) {
        palette.MoveSelection(GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1, false);
        Repaint(context);
        return true;
    }
    if (messageWindow != context.mainWindow) {
        // A press in another editor window is a press outside the palette.
        if (buttonDown) {
            Close(context);
        }
        return buttonDown;
    }
    RECT client{};
    GetClientRect(context.mainWindow, &client);
    const EditorCommandPaletteLayout layout = EditorCommandPaletteLayout::Resolve(
        static_cast<int>(client.right - client.left), static_cast<int>(client.bottom - client.top), palette.VisibleCount());
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    const int row = layout.RowAt(x, y);
    switch (message) {
    case WM_MOUSEMOVE:
        if (row >= 0 && palette.SelectVisible(row)) {
            Repaint(context);
        }
        return true;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (row >= 0) {
            static_cast<void>(palette.SelectVisible(row));
            ChooseSelected(context);
        } else if (!layout.panel.Contains(x, y)) {
            Close(context);
        }
        return true;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        Close(context);
        return true;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        return true;
    default:
        return false;
    }
}

void EditorCommandPaletteController::ChooseSelected(EditorWindowMessageContext& context) {
    EditorCommandPaletteModel& palette = context.shellInteraction.CommandPalette();
    const std::optional<EditorCommandPaletteModel::Row> chosen = palette.Chosen();
    if (!chosen.has_value()) {
        return;
    }
    const bool command = palette.CurrentMode() == EditorCommandPaletteModel::Mode::Commands;
    const std::uint64_t value = chosen->value;
    const std::string label{ chosen->label };
    // The palette goes away before what it chose runs, so a dialog the choice opens has nothing in front of it.
    palette.Close();
    Repaint(context);
    if (context.mainWindow != nullptr) {
        UpdateWindow(context.mainWindow);
    }
    if (command) {
        const std::vector<EditorMenuRowView> rows = EditorMenuTable::AllRows(context.shellInteraction);
        if (const EditorMenuRowView* row = EditorMenuTable::CommandRow(rows, { .label = label, .value = value }); row != nullptr) {
            EditorWindowToolbarPointerHandler::RunMenuRow(*row, context);
        }
        return;
    }
    const kb::assets::AssetMetadata* found = context.sceneContext.Scene().Assets().Manager().Registry().Find(kb::assets::AssetId{ value });
    if (found == nullptr) {
        return;
    }
    // A copy: opening an asset may change the index it came from.
    const kb::assets::AssetMetadata metadata = *found;
    const EditorAssetBrowserDoubleClickResult result =
        EditorAssetBrowserDoubleClickHandler::OpenAsset(context.mainWindow, metadata, context.sceneContext);
    EditorAssetBrowserDoubleClickHandler::ShowOpenedAsset(result, context.dockModel, context.sceneContext);
    context.sceneViewport.RequestPresent();
    Repaint(context);
}

} // namespace kb::editor

#endif
