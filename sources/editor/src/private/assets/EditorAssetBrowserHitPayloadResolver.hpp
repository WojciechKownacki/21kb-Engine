#pragma once

#include "assets/EditorAssetBrowserHitTester.hpp"

#include <optional>

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserHitPayloadResolver {
public:
    EditorAssetBrowserHitPayloadResolver() = delete;

    [[nodiscard]] static std::optional<std::filesystem::path> PrefabAssetAt(
        const EditorAssetBrowserHit& hit,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static std::optional<kb::assets::AssetId> AssetIdAt(
        const EditorAssetBrowserHit& hit,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static std::optional<kb::assets::AssetMetadata> AssetMetadataAt(
        const EditorAssetBrowserHit& hit,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static std::optional<std::filesystem::path> FolderAt(
        const EditorAssetBrowserHit& hit,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static std::optional<std::filesystem::path> FolderDropTargetAt(
        const EditorAssetBrowserHit& hit,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);
};

#endif

} // namespace kb::editor
