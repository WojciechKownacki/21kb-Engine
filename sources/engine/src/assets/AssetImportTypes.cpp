#include "engine/assets/AssetImportTypes.hpp"

#include <algorithm>

namespace kb::assets {

std::string AssetImportRecoveryAdvice(const AssetImportItemResult& item) {
    if (item.Succeeded()) return {};
    if (!item.conflicts.empty())
        return "Keep the edited assets or choose Overwrite in Reimport (CLI: --on-conflict keep-local or overwrite).";
    if (item.status == AssetImportItemStatus::Missing)
        return "Restore the source file at the reported path, or select its new location, then retry Import.";
    if (item.status == AssetImportItemStatus::Unsupported)
        return "Export the source in a format listed by Import, then import that file (models: glTF, GLB, OBJ or FBX).";
    return "Correct the reported source error and restore any missing companion files beside the source, then retry Import. For an existing asset, use Reimport to preserve its references.";
}

std::size_t AssetImportResult::ImportedCount() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(items, [](const AssetImportItemResult& item) {
        return item.Succeeded();
    }));
}

std::size_t AssetImportResult::CreatedCount() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(items, [](const AssetImportItemResult& item) {
        return item.status == AssetImportItemStatus::Created;
    }));
}

std::size_t AssetImportResult::ReusedCount() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(items, [](const AssetImportItemResult& item) {
        return item.status == AssetImportItemStatus::Reused;
    }));
}

std::size_t AssetImportResult::MissingCount() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(items, [](const AssetImportItemResult& item) {
        return item.status == AssetImportItemStatus::Missing;
    }));
}

std::size_t AssetImportResult::UnsupportedCount() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(items, [](const AssetImportItemResult& item) {
        return item.status == AssetImportItemStatus::Unsupported;
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

std::string_view ToString(AssetImportItemStatus status) noexcept {
    switch (status) {
    case AssetImportItemStatus::Created:
        return "created";
    case AssetImportItemStatus::Reused:
        return "reused";
    case AssetImportItemStatus::Missing:
        return "missing";
    case AssetImportItemStatus::Unsupported:
        return "unsupported";
    case AssetImportItemStatus::Failed:
        return "failed";
    case AssetImportItemStatus::Updated:
        return "updated";
    case AssetImportItemStatus::None:
    default:
        return "none";
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
