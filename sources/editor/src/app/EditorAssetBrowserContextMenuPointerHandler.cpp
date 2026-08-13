#include "app/EditorAssetBrowserContextMenuPointerHandler.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserContextCommandExecutor.hpp"
#include "app/EditorAssetBrowserNativeCommandMap.hpp"
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] HMENU CreateLightingSubmenu() {
    HMENU lighting = CreatePopupMenu();
    AppendMenuA(lighting, MF_STRING,
        EditorAssetBrowserNativeCommandMap::Id(EditorAssetContextCommand::AddDirectionalLight), "Directional Light");
    AppendMenuA(lighting, MF_STRING,
        EditorAssetBrowserNativeCommandMap::Id(EditorAssetContextCommand::AddPointLight), "Point Light");
    AppendMenuA(lighting, MF_STRING,
        EditorAssetBrowserNativeCommandMap::Id(EditorAssetContextCommand::AddSpotLight), "Spot Light");
    return lighting;
}

void AppendAddSubmenu(HMENU menu) {
    HMENU add = CreatePopupMenu();
    AppendMenuA(add, MF_POPUP, reinterpret_cast<UINT_PTR>(CreateLightingSubmenu()), "Lighting");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(add), "Add");
}

[[nodiscard]] UINT ShowProjectFilesSystemMenu(HWND window, int x, int y, const std::vector<EditorAssetContextMenuItem>& items) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return 0;
    }

    for (const EditorAssetContextMenuItem& item : items) {
        if (item.command == EditorAssetContextCommand::AddLighting) {
            continue;
        } else if (const UINT_PTR id = EditorAssetBrowserNativeCommandMap::Id(item.command); id != 0) {
            AppendMenuA(menu, MF_STRING, id, item.label);
        }
        if (item.separatorAfter) {
            AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
        }
    }

    POINT screenPoint{ x, y };
    ClientToScreen(window, &screenPoint);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x,
        screenPoint.y,
        0,
        window,
        nullptr);
    DestroyMenu(menu);
    return command;
}

[[nodiscard]] bool ExecuteNativeProjectFilesMenu(HWND window, int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    const std::vector<EditorAssetContextMenuItem> items = state.ContextMenuItems(sceneContext.Scene().Assets().Manager());
    state.CloseContextMenu();
    InvalidateRect(window, nullptr, FALSE);
    UpdateWindow(window);
    const EditorAssetContextCommand command = EditorAssetBrowserNativeCommandMap::Command(
        ShowProjectFilesSystemMenu(window, x, y, items));
    if (command == EditorAssetContextCommand::None) {
        return true;
    }
    return EditorAssetBrowserContextCommandExecutor::Execute(command, sceneContext);
}

} // namespace

std::optional<bool> EditorAssetBrowserContextMenuPointerHandler::HandleOpenMenuPointerDown(const EditorAssetBrowserHit& hit, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (!state.IsContextMenuOpen()) {
        return std::nullopt;
    }

    if (hit.kind == EditorAssetBrowserHitKind::ContextMenuCommand) {
        if (hit.command == EditorAssetContextCommand::AddLighting) {
            static_cast<void>(state.SetContextMenuHoveredCommand(EditorAssetContextCommand::AddLighting));
            return true;
        }
        static_cast<void>(EditorAssetBrowserContextCommandExecutor::Execute(hit.command, sceneContext));
        state.CloseContextMenu();
        return true;
    }
    if (hit.kind == EditorAssetBrowserHitKind::ContextMenuBody) {
        return true;
    }

    state.CloseContextMenu();
    return std::nullopt;
}

bool EditorAssetBrowserContextMenuPointerHandler::HandleRightButtonDown(HWND window, const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (state.IsTextEditing()) {
        static_cast<void>(sceneContext.CommitAssetTextEdit());
    }

    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    state.CloseContextMenu();
    const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(content, x, y, state, manager);

    switch (hit.kind) {
    case EditorAssetBrowserHitKind::Asset: {
        const std::optional<kb::assets::AssetId> id = EditorAssetBrowserHitPayloadResolver::AssetIdAt(hit, state, manager);
        if (!id.has_value()) {
            return false;
        }
        static_cast<void>(state.SelectAsset(*id, manager));
        return state.OpenContextMenuForAsset(x, y, *id, manager) && ExecuteNativeProjectFilesMenu(window, x, y, sceneContext);
    }
    case EditorAssetBrowserHitKind::ContentFolder: {
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderAt(hit, state, manager);
        if (!folder.has_value()) {
            return false;
        }
        static_cast<void>(state.SelectContentFolder(*folder, manager));
        return state.OpenContextMenuForFolder(x, y, *folder, manager) && ExecuteNativeProjectFilesMenu(window, x, y, sceneContext);
    }
    case EditorAssetBrowserHitKind::FolderDisclosure:
    case EditorAssetBrowserHitKind::Folder: {
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderDropTargetAt(hit, state, manager);
        if (!folder.has_value()) {
            return false;
        }
        static_cast<void>(state.SelectFolder(*folder, manager));
        return state.OpenContextMenuForFolder(x, y, *folder, manager) && ExecuteNativeProjectFilesMenu(window, x, y, sceneContext);
    }
    case EditorAssetBrowserHitKind::DropTarget:
        state.OpenContextMenuForBackground(x, y);
        return ExecuteNativeProjectFilesMenu(window, x, y, sceneContext);
    case EditorAssetBrowserHitKind::DeleteConfirmBody:
    case EditorAssetBrowserHitKind::DeleteConfirmListBody:
    case EditorAssetBrowserHitKind::DeleteConfirmCheckbox:
    case EditorAssetBrowserHitKind::DeleteConfirmScrollbarThumb:
    case EditorAssetBrowserHitKind::DeleteConfirmScrollbarTrack:
    case EditorAssetBrowserHitKind::DeleteConfirmAccept:
    case EditorAssetBrowserHitKind::DeleteConfirmCancel:
        return true;
    default:
        return false;
    }
}

bool EditorAssetBrowserContextMenuPointerHandler::HandlePointerMove(const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (!state.IsContextMenuOpen()) {
        return false;
    }

    const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(content, x, y, state, sceneContext.Scene().Assets().Manager());
    const EditorAssetContextCommand hovered = hit.kind == EditorAssetBrowserHitKind::ContextMenuCommand ? hit.command : EditorAssetContextCommand::None;
    return state.SetContextMenuHoveredCommand(hovered);
}

} // namespace kb::editor

#endif
