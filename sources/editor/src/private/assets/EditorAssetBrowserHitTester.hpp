#pragma once

#include "assets/EditorAssetBrowserLayout.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <optional>

namespace kb::assets {

class AssetManager;

} // namespace kb::assets

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserState;

enum class EditorAssetBrowserHitKind {
    None,
    Search,
    Filters,
    Refresh,
    NewFolder,
    Rename,
    DeleteAsset,
    ListMode,
    TileMode,
    Recursive,
    Sort,
    SortByName,
    SortByType,
    SortByPath,
    Slider,
    Breadcrumb,
    FolderDisclosure,
    Folder,
    ContentFolder,
    Asset,
    DropTarget,
    DeleteConfirmBody,
    DeleteConfirmAccept,
    DeleteConfirmCancel,
    ContextMenuBody,
    ContextMenuCommand,
};

struct EditorAssetBrowserHit {
    EditorAssetBrowserHitKind kind = EditorAssetBrowserHitKind::None;
    std::size_t index = 0;
    float value = 0.0F;
    EditorAssetContextCommand command = EditorAssetContextCommand::None;
};

class EditorAssetBrowserHitTester {
public:
    EditorAssetBrowserHitTester() = delete;

    [[nodiscard]] static EditorAssetBrowserHit HitTest(
        const RECT& content,
        int x,
        int y,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager,
        const RECT* overlayBounds = nullptr);

    [[nodiscard]] static std::optional<std::filesystem::path> PrefabAssetAt(
        const RECT& content,
        int x,
        int y,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static std::optional<kb::assets::AssetId> AssetIdAt(
        const RECT& content,
        int x,
        int y,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static std::optional<kb::assets::AssetMetadata> AssetMetadataAt(
        const RECT& content,
        int x,
        int y,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static std::optional<std::filesystem::path> FolderAt(
        const RECT& content,
        int x,
        int y,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static std::optional<std::filesystem::path> FolderDropTargetAt(
        const RECT& content,
        int x,
        int y,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static std::optional<std::filesystem::path> BreadcrumbFolderAt(
        const RECT& content,
        int x,
        int y,
        const EditorAssetBrowserState& state);

    [[nodiscard]] static bool IsDropTarget(const RECT& content, int x, int y) noexcept;
};

#endif

} // namespace kb::editor
