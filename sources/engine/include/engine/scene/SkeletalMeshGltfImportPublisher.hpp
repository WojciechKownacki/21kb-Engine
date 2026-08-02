#pragma once

#include "engine/scene/SkeletalMeshGltfImportPlanner.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kb::assets { class AssetManager; struct AssetId; }

namespace kb::scene {

struct SkeletalMeshGltfPublishResult {
    kb::assets::AssetId skeletonAssetId{};
    kb::assets::AssetId meshAssetId{};
    std::vector<kb::assets::AssetId> animationClipAssetIds;
    bool createdSkeleton = false;
};

class SkeletalMeshGltfImportPublisher final {
public:
    SkeletalMeshGltfImportPublisher() = delete;

    // Publishes the entire plan as one recoverable filesystem transaction.
    // A write/rename failure restores every pre-existing asset byte-for-byte.
    [[nodiscard]] static std::optional<SkeletalMeshGltfPublishResult> Publish(
        kb::assets::AssetManager& manager,
        const SkeletalMeshGltfImportPlan& plan,
        std::string* error = nullptr);
};

} // namespace kb::scene
