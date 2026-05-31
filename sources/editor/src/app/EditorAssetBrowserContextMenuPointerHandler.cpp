#include "app/EditorAssetBrowserContextMenuPointerHandler.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserContextCommandExecutor.hpp"
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {

std::optional<bool> EditorAssetBrowserContextMenuPointerHandler::HandleOpenMenuPointerDown(const EditorAssetBrowserHit& hit, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (!state.IsContextMenuOpen()) {
        return std::nullopt;
    }

    if (hit.kind == EditorAssetBrowserHitKind::ContextMenuCommand) {
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

bool EditorAssetBrowserContextMenuPointerHandler::HandleRightButtonDown(const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
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
        return state.OpenContextMenuForAsset(x, y, *id, manager);
    }
    case EditorAssetBrowserHitKind::ContentFolder: {
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderAt(hit, state, manager);
        if (!folder.has_value()) {
            return false;
        }
        static_cast<void>(state.SelectContentFolder(*folder, manager));
        return state.OpenContextMenuForFolder(x, y, *folder, manager);
    }
    case EditorAssetBrowserHitKind::FolderDisclosure:
    case EditorAssetBrowserHitKind::Folder: {
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderDropTargetAt(hit, state, manager);
        if (!folder.has_value()) {
            return false;
        }
        static_cast<void>(state.SelectFolder(*folder, manager));
        return state.OpenContextMenuForFolder(x, y, *folder, manager);
    }
    case EditorAssetBrowserHitKind::DropTarget:
        state.OpenContextMenuForBackground(x, y);
        return true;
    case EditorAssetBrowserHitKind::DeleteConfirmBody:
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
