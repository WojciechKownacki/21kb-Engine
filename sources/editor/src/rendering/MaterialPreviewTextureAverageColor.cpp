#include "rendering/MaterialPreviewTextureAverageColor.hpp"

#if defined(_WIN32)
#include "rendering/EditorTexturePreviewService.hpp"

namespace kb::editor {

std::optional<std::array<float, 3U>> MaterialPreviewTextureAverageColor(
    const kb::assets::AssetManager& assets,
    kb::assets::AssetId textureId) {
    if (!textureId.IsValid()) {
        return std::nullopt;
    }
    const kb::assets::AssetMetadata* metadata = assets.Registry().Find(textureId);
    if (metadata == nullptr || !EditorTexturePreviewService::IsTextureAsset(*metadata)) {
        return std::nullopt;
    }
    const EditorTexturePreviewImage* image = EditorTexturePreviewService::PreviewFor(*metadata);
    if (image == nullptr || image->bgra.empty()) {
        return std::nullopt;
    }

    std::uint64_t blue = 0U;
    std::uint64_t green = 0U;
    std::uint64_t red = 0U;
    for (const std::uint32_t pixel : image->bgra) {
        blue += pixel & 0xFFU;
        green += (pixel >> 8U) & 0xFFU;
        red += (pixel >> 16U) & 0xFFU;
    }
    const auto count = static_cast<float>(image->bgra.size());
    return std::array<float, 3U>{
        static_cast<float>(red) / count / 255.0F,
        static_cast<float>(green) / count / 255.0F,
        static_cast<float>(blue) / count / 255.0F,
    };
}

} // namespace kb::editor

#endif
