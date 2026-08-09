#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SkeletalMeshFbxImporter.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace kb::assets { class AssetManager; }

namespace kb::scene {

struct SkeletalMeshFbxImportPlan {
    SkeletalMeshFbxImportResult imported;
    kb::assets::AssetId skeletonAssetId{};
    std::filesystem::path skeletonVirtualPath;
    std::filesystem::path meshVirtualPath;
    bool reusesSkeleton = false;
    bool updatesSkeleton = false;
};

class SkeletalMeshFbxImportPlanner final {
public:
    SkeletalMeshFbxImportPlanner() = delete;

    [[nodiscard]] static std::optional<SkeletalMeshFbxImportPlan> Plan(
        const kb::assets::AssetManager& manager,
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& destinationVirtualFolder,
        const SkeletalMeshFbxImportOptions& options = {},
        std::string* error = nullptr);
};

} // namespace kb::scene
