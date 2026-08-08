#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SkeletalMeshGltfImporter.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace kb::assets { class AssetManager; }

namespace kb::scene {

// A pure import decision. Publishing the resulting files is deliberately
// separate so a later transaction can commit every dependent asset together.
struct SkeletalMeshGltfImportPlan {
    SkeletalMeshGltfImportResult imported;
    kb::assets::AssetId skeletonAssetId{};
    std::filesystem::path skeletonVirtualPath;
    std::filesystem::path meshVirtualPath;
    bool reusesSkeleton = false;
    bool updatesSkeleton = false;
};

class SkeletalMeshGltfImportPlanner final {
public:
    SkeletalMeshGltfImportPlanner() = delete;

    [[nodiscard]] static std::optional<SkeletalMeshGltfImportPlan> Plan(
        const kb::assets::AssetManager& manager,
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& destinationVirtualFolder,
        const SkeletalMeshGltfImportOptions& options = {},
        std::string* error = nullptr);
};

} // namespace kb::scene
