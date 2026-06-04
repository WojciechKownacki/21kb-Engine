#include "app/EditorPointerDragSourceResolver.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserHitTester.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "scene/EditorHierarchyRowPicker.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

[[nodiscard]] bool IsPrefabLike(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "ScenePrefab" || Lower(metadata.virtualPath.extension().string()) == ".kbprefab";
}

[[nodiscard]] std::filesystem::path ResolveAssetPath(const kb::assets::AssetMetadata& metadata, const kb::assets::AssetManager& manager) {
    if (const std::optional<std::filesystem::path> mounted = manager.Mounts().Resolve(metadata.virtualPath)) {
        return *mounted;
    }
    return metadata.physicalPath;
}

[[nodiscard]] bool IsDraggableFolder(const std::filesystem::path& virtualPath) {
    return kb::assets::NormalizeAssetPath(virtualPath) != "/Game";
}

} // namespace

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
    drag.startX = x;
    drag.startY = y;
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
        const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
        const std::optional<kb::assets::AssetMetadata> metadata = EditorAssetBrowserHitTester::AssetMetadataAt(
            *assets,
            x,
            y,
            sceneContext.AssetBrowser(),
            manager);
        if (metadata.has_value()) {
            drag.kind = EditorPointerDragKind::PrefabAsset;
            drag.assetId = metadata->id;
            drag.assetPath = ResolveAssetPath(*metadata, manager);
            drag.assetLabel = metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
            drag.assetInstantiatesPrefab = IsPrefabLike(*metadata);
            drag.assetAddsBehaviour = kb::script::ScriptBehaviourAsset::IsBehaviourAsset(*metadata);
            return;
        }

        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitTester::FolderAt(
            *assets,
            x,
            y,
            sceneContext.AssetBrowser(),
            manager);
        if (folder.has_value() && IsDraggableFolder(*folder)) {
            drag.kind = EditorPointerDragKind::AssetFolder;
            drag.assetFolderPath = *folder;
            drag.assetLabel = folder->filename().string();
        }
    }
}

} // namespace kb::editor

#endif
