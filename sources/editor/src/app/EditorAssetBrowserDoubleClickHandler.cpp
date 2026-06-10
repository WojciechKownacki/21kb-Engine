#include "app/EditorAssetBrowserDoubleClickHandler.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserHitTester.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

[[nodiscard]] bool IsSceneDocumentAsset(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "Scene" && Lower(metadata.virtualPath.extension().string()) == ".21kbscene";
}

[[nodiscard]] std::filesystem::path ResolveAssetPath(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetManager& manager) {
    if (const std::optional<std::filesystem::path> mounted = manager.Mounts().Resolve(metadata.virtualPath)) {
        return *mounted;
    }
    return metadata.physicalPath;
}

} // namespace

EditorAssetBrowserDoubleClickResult EditorAssetBrowserDoubleClickHandler::HandleDoubleClick(
    HWND owner,
    const RECT& content,
    int x,
    int y,
    EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(content, x, y, state, manager);

    switch (hit.kind) {
    case EditorAssetBrowserHitKind::ContentFolder:
    case EditorAssetBrowserHitKind::Folder: {
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderAt(hit, state, manager);
        return folder.has_value() && state.SelectFolder(*folder, manager)
            ? EditorAssetBrowserDoubleClickResult::BrowserNavigation
            : EditorAssetBrowserDoubleClickResult::None;
    }
    case EditorAssetBrowserHitKind::Asset: {
        const std::optional<kb::assets::AssetMetadata> metadata = EditorAssetBrowserHitPayloadResolver::AssetMetadataAt(hit, state, manager);
        if (!metadata.has_value()) {
            return EditorAssetBrowserDoubleClickResult::None;
        }
        if (metadata->type == "LuaScript") {
            return sceneContext.OpenLuaScript(metadata->id)
                ? EditorAssetBrowserDoubleClickResult::ScriptEditorOpened
                : EditorAssetBrowserDoubleClickResult::None;
        }
        if (!IsSceneDocumentAsset(*metadata)) {
            return EditorAssetBrowserDoubleClickResult::None;
        }
        const std::optional<EditorDirtySceneResolution> resolution =
            EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(owner, sceneContext, L"opening another scene");
        if (!resolution.has_value()) {
            return EditorAssetBrowserDoubleClickResult::None;
        }
        return sceneContext.OpenScene(ResolveAssetPath(*metadata, manager), *resolution)
            ? EditorAssetBrowserDoubleClickResult::SceneOpened
            : EditorAssetBrowserDoubleClickResult::None;
    }
    default:
        return EditorAssetBrowserDoubleClickResult::None;
    }
}

} // namespace kb::editor

#endif
