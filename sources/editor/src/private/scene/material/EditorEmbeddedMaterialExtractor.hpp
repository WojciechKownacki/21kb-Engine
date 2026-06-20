#pragma once

#include "engine/assets/AssetId.hpp"
#include "scene/material/EditorEmbeddedMaterialExtractionTypes.hpp"

#include <filesystem>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorAssetBrowserState;
class EditorConsoleState;

class EditorEmbeddedMaterialExtractor final {
public:
    EditorEmbeddedMaterialExtractor(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept;

    [[nodiscard]] EditorEmbeddedMaterialExtractionResult Extract(kb::assets::AssetId meshAssetId);

private:
    [[nodiscard]] std::filesystem::path OutputFolderFor(const std::filesystem::path& meshVirtualPath) const;

    kb::scene::Scene& scene_;
    EditorAssetBrowserState& browser_;
    EditorConsoleState& console_;
};

} // namespace kb::editor
