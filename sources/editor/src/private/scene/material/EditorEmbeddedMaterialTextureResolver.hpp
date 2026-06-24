#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::assets {
class AssetManager;
}

namespace kb::editor {

class EditorEmbeddedMaterialTextureResolver final {
public:
    EditorEmbeddedMaterialTextureResolver() = delete;

    static void Resolve(
        kb::render::RenderMaterialAssetData& material,
        const kb::render::RenderMeshEmbeddedMaterial& embedded,
        const kb::assets::AssetMetadata& meshMetadata,
        const kb::assets::AssetManager& manager,
        std::vector<std::string>& diagnostics);
};

} // namespace kb::editor
