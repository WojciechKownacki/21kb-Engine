#pragma once

#include "engine/scene/SkeletalMeshGltfImportPlanner.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kb::assets { class AssetManager; struct AssetId; }

namespace kb::scene {

struct SkeletalMeshImportArtifact {
    std::filesystem::path virtualPath;
    std::string expectedAssetType;
    std::function<bool(const std::filesystem::path&)> write;
};

struct SkeletalMeshGltfPublishResult {
    kb::assets::AssetId skeletonAssetId{};
    kb::assets::AssetId meshAssetId{};
    std::vector<kb::assets::AssetId> animationClipAssetIds;
    std::vector<kb::assets::AssetId> auxiliaryAssetIds;
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

    [[nodiscard]] static std::optional<SkeletalMeshGltfPublishResult> PublishWithArtifacts(
        kb::assets::AssetManager& manager,
        const SkeletalMeshGltfImportPlan& plan,
        std::span<const SkeletalMeshImportArtifact> artifacts,
        std::string* error = nullptr);
};

} // namespace kb::scene
