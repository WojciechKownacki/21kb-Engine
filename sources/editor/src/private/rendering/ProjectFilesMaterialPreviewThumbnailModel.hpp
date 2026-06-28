#pragma once

#include "engine/assets/AssetMetadata.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace kb::editor {

struct ProjectFilesMaterialPreviewColor {
    std::uint8_t r = 194U;
    std::uint8_t g = 168U;
    std::uint8_t b = 116U;
};

struct ProjectFilesMaterialPreviewStyle {
    ProjectFilesMaterialPreviewColor baseColor{};
    ProjectFilesMaterialPreviewColor emissiveColor{ 0U, 0U, 0U };
    float roughness = 0.65F;
    float emissiveStrength = 1.0F;
    bool loadedFromAsset = false;
    bool errorFallback = false;
    bool usesPreviewScenePrimitive = false;
    float previewBoundsRadius = 1.0F;
    std::uint32_t previewTriangleCount = 0U;
};

struct ProjectFilesMaterialPreviewImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> bgra;
};

struct ProjectFilesMaterialPreviewCacheEntry {
    std::uint64_t contentHash = 0U;
    std::filesystem::path physicalPath;
    ProjectFilesMaterialPreviewStyle style;
};

class ProjectFilesMaterialPreviewThumbnailModel {
public:
    ProjectFilesMaterialPreviewThumbnailModel() = delete;

    [[nodiscard]] static ProjectFilesMaterialPreviewStyle StyleFromAsset(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] static ProjectFilesMaterialPreviewImage RenderImage(int width, int height, const ProjectFilesMaterialPreviewStyle& style, bool selected);
};

} // namespace kb::editor
