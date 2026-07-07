#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetId.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace kb::editor {

class EditorSceneContext;

enum class EditorTextureAssetPickerFilter : std::uint8_t {
    Texture2D,
    TextureCube,
    TextureVolume,
    Texture2DArray,
};

namespace detail {

[[nodiscard]] inline std::string TexturePickerLowerAscii(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

[[nodiscard]] inline bool TexturePickerTextContainsAny(std::string_view text, std::initializer_list<std::string_view> needles) {
    const std::string lower = TexturePickerLowerAscii(std::string{ text });
    for (std::string_view needle : needles) {
        if (lower.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool TexturePickerMetadataHasAnyToken(
    const kb::assets::AssetMetadata& metadata,
    std::initializer_list<std::string_view> tokens) {
    return TexturePickerTextContainsAny(metadata.type, tokens) ||
        TexturePickerTextContainsAny(metadata.importCategory, tokens) ||
        TexturePickerTextContainsAny(metadata.name, tokens) ||
        TexturePickerTextContainsAny(metadata.virtualPath.string(), tokens) ||
        TexturePickerTextContainsAny(metadata.physicalPath.string(), tokens);
}

[[nodiscard]] inline bool TexturePickerIsGenericTextureAsset(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "RenderTexture" || metadata.type == "Texture" || metadata.importCategory == "Texture";
}

} // namespace detail

[[nodiscard]] inline bool EditorTextureAssetMatchesFilter(
    const kb::assets::AssetMetadata& metadata,
    EditorTextureAssetPickerFilter filter) {
    if (!detail::TexturePickerIsGenericTextureAsset(metadata) &&
        !detail::TexturePickerMetadataHasAnyToken(metadata, { "texture", "rendertexture" })) {
        return false;
    }
    const bool cube = detail::TexturePickerMetadataHasAnyToken(metadata, { "texturecube", "cubemap", "cube" });
    const bool volume = detail::TexturePickerMetadataHasAnyToken(metadata, { "texturevolume", "texture3d", "volume", "3d" });
    const bool array = detail::TexturePickerMetadataHasAnyToken(metadata, { "texture2darray", "texturearray", "array" });
    switch (filter) {
    case EditorTextureAssetPickerFilter::TextureCube:
        return cube;
    case EditorTextureAssetPickerFilter::TextureVolume:
        return volume;
    case EditorTextureAssetPickerFilter::Texture2DArray:
        return array;
    case EditorTextureAssetPickerFilter::Texture2D:
    default:
        return detail::TexturePickerIsGenericTextureAsset(metadata) && !cube && !volume && !array;
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
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId currentMaterial);
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
