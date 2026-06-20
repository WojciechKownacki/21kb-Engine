#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "scene/material/EditorEmbeddedMaterialExtractionTypes.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace kb::assets {
class AssetManager;
}

namespace kb::editor {

class EditorEmbeddedMaterialAssetWriter final {
public:
    EditorEmbeddedMaterialAssetWriter() = delete;

    [[nodiscard]] static std::optional<EditorExtractedMaterialSlot> Write(
        const kb::render::RenderMeshEmbeddedMaterial& embedded,
        std::uint32_t slotIndex,
        const kb::assets::AssetMetadata& meshMetadata,
        const std::filesystem::path& outputFolder,
        kb::assets::AssetManager& manager,
        std::vector<std::string>& diagnostics);
};

} // namespace kb::editor
