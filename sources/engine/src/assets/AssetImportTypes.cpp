#include "engine/assets/AssetImportTypes.hpp"

#include <algorithm>

namespace kb::assets {

std::size_t AssetImportResult::ImportedCount() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(items, [](const AssetImportItemResult& item) {
        return item.Succeeded();
    }));
}

std::size_t AssetImportResult::FailedCount() const noexcept {
    return items.size() - ImportedCount();
}

bool AssetImportResult::Succeeded() const noexcept {
    return !items.empty() && FailedCount() == 0U;
}

std::string_view ToString(AssetImportCategory category) noexcept {
    switch (category) {
    case AssetImportCategory::Model:
        return "Mesh";
    case AssetImportCategory::Texture:
        return "Texture";
    case AssetImportCategory::Audio:
        return "Audio";
    case AssetImportCategory::Video:
        return "Video";
    case AssetImportCategory::Animation:
        return "Animation";
    case AssetImportCategory::Material:
        return "Material";
    case AssetImportCategory::Shader:
        return "Shader";
    case AssetImportCategory::Font:
        return "Font";
    case AssetImportCategory::Script:
        return "Script";
    case AssetImportCategory::Scene:
        return "Scene";
    case AssetImportCategory::Data:
        return "Data";
    case AssetImportCategory::InputAction:
        return "InputAction";
    case AssetImportCategory::InputMappingContext:
        return "InputMappingContext";
    case AssetImportCategory::Unknown:
    default:
        return "Unknown";
    }
}

std::string_view RuntimeAssetType(AssetImportCategory category) noexcept {
    switch (category) {
    case AssetImportCategory::Model:
        return "RenderMesh";
    case AssetImportCategory::InputAction:
        return "InputAction";
    case AssetImportCategory::InputMappingContext:
        return "InputMappingContext";
    case AssetImportCategory::Unknown:
    case AssetImportCategory::Texture:
    case AssetImportCategory::Audio:
    case AssetImportCategory::Video:
    case AssetImportCategory::Animation:
    case AssetImportCategory::Material:
    case AssetImportCategory::Shader:
    case AssetImportCategory::Font:
    case AssetImportCategory::Script:
    case AssetImportCategory::Scene:
    case AssetImportCategory::Data:
    default:
        return "ImportedAsset";
    }
}

} // namespace kb::assets
