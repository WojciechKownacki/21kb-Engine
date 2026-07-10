#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <optional>
#include <string>
#include <utility>

namespace kb::editor {

// Reads structural texture/import metadata from the asset payload. Names and paths
// are deliberately not classification inputs.
struct EditorTextureAssetMetadataResolution {
    std::optional<kb::render::RenderTextureAssetData> asset;
    std::string diagnostic;

    [[nodiscard]] bool Resolved() const noexcept { return asset.has_value(); }
};

[[nodiscard]] inline EditorTextureAssetMetadataResolution EditorResolveTextureAssetMetadata(
    const kb::assets::AssetMetadata& metadata) {
    if (metadata.type != "RenderTexture" && metadata.type != "Texture" && metadata.importCategory != "Texture") {
        return { .diagnostic = "Asset is not registered as a texture." };
    }
    if (metadata.physicalPath.empty()) {
        return { .diagnostic = "Texture has no physical asset/import-settings path; metadata is Unknown." };
    }
    std::optional<kb::render::RenderTextureAssetData> asset =
        kb::render::RenderTextureAssetLoader::LoadTexture(metadata.physicalPath);
    if (!asset.has_value()) {
        return { .diagnostic = "Texture payload/import settings could not be read; metadata is Unknown." };
    }
    return { .asset = std::move(asset) };
}

} // namespace kb::editor
