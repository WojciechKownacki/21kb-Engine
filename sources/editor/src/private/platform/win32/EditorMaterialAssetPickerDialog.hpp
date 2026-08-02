#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetId.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/material/EditorTextureAssetMetadataResolver.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>

namespace kb::editor {

class EditorSceneContext;
class EditorSceneBgfxViewport;

enum class EditorTextureAssetPickerFilter : std::uint8_t {
    Texture2D,
    TextureCube,
    TextureVolume,
    Texture2DArray,
};

[[nodiscard]] inline bool EditorTextureAssetMatchesFilter(
    const kb::assets::AssetMetadata& metadata,
    EditorTextureAssetPickerFilter filter) {
    const EditorTextureAssetMetadataResolution resolved = EditorResolveTextureAssetMetadata(metadata);
    if (!resolved.Resolved()) {
        return false;
    }
    const kb::render::RenderTextureDimension dimension = resolved.asset->dimension;
    switch (filter) {
    case EditorTextureAssetPickerFilter::TextureCube:
        return dimension == kb::render::RenderTextureDimension::TextureCube;
    case EditorTextureAssetPickerFilter::TextureVolume:
        return dimension == kb::render::RenderTextureDimension::Texture3D;
    case EditorTextureAssetPickerFilter::Texture2DArray:
        return dimension == kb::render::RenderTextureDimension::Texture2DArray;
    case EditorTextureAssetPickerFilter::Texture2D:
    default:
        return dimension == kb::render::RenderTextureDimension::Texture2D;
    }
}

class EditorMaterialAssetPickerDialog {
public:
    EditorMaterialAssetPickerDialog() = delete;

#if defined(_WIN32)
    struct Result {
        bool accepted = false;
        kb::assets::AssetId assetId{};
    };

    [[nodiscard]] static Result Show(
        HWND owner,
        const EditorTheme& theme,
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport,
        kb::assets::AssetId currentMaterial,
        bool allowClear = true);
#endif
};

class EditorTextureAssetPickerDialog {
public:
    EditorTextureAssetPickerDialog() = delete;

#if defined(_WIN32)
    struct Result {
        bool accepted = false;
        kb::assets::AssetId assetId{};
    };

    [[nodiscard]] static Result Show(
        HWND owner,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId currentTexture,
        EditorTextureAssetPickerFilter filter = EditorTextureAssetPickerFilter::Texture2D);

    [[nodiscard]] static bool MatchesFilter(
        const kb::assets::AssetMetadata& metadata,
        EditorTextureAssetPickerFilter filter) {
        return EditorTextureAssetMatchesFilter(metadata, filter);
    }
#endif
};

} // namespace kb::editor
