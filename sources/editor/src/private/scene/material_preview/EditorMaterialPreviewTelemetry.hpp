#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::editor {

struct EditorMaterialPreviewTelemetry {
    kb::assets::AssetId materialAssetId{};
    bool materialMetadataFound = false;
    bool materialLoaded = false;
    bool previewSceneReady = false;
    std::uint32_t missingTextureCount = 0;
    std::vector<std::string> missingTextures;
};

class EditorMaterialPreviewTelemetryBuilder {
public:
    EditorMaterialPreviewTelemetryBuilder() = delete;

    [[nodiscard]] static EditorMaterialPreviewTelemetry Build(
        const kb::assets::AssetManager& manager,
        kb::assets::AssetId materialAssetId,
        const kb::render::RenderMaterialAssetData* material,
        bool previewSceneReady);
};

} // namespace kb::editor
