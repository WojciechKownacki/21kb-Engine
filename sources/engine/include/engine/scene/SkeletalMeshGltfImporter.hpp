#pragma once

#include "engine/scene/SkeletalMeshAsset.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace kb::scene {

struct SkeletalMeshGltfImportResult {
    SkeletonAsset skeleton;
    SkeletalMeshAsset mesh;
};

// Imports one glTF skin and the mesh node bound to it into the canonical
// skeletal asset pair. The caller owns atomic publication of the result.
class SkeletalMeshGltfImporter final {
public:
    SkeletalMeshGltfImporter() = delete;

    [[nodiscard]] static std::optional<SkeletalMeshGltfImportResult> Import(
        const std::filesystem::path& path,
        std::uint64_t skeletonAssetId,
        std::string* error = nullptr);
};

} // namespace kb::scene
