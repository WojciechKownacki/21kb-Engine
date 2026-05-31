#include "app/EditorAssetBrowserDoubleClickHandler.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserHitTester.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/EditorSceneContext.hpp"

#include <filesystem>
#include <optional>

namespace kb::editor {

bool EditorAssetBrowserDoubleClickHandler::HandleDoubleClick(const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(content, x, y, state, manager);

    switch (hit.kind) {
    case EditorAssetBrowserHitKind::ContentFolder:
    case EditorAssetBrowserHitKind::Folder: {
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderAt(hit, state, manager);
        return folder.has_value() ? state.SelectFolder(*folder, manager) : false;
    }
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
