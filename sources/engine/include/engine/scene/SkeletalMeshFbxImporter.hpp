#pragma once

#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

struct SkeletalMeshFbxImportResult {
    SkeletonAsset skeleton;
    SkeletalMeshAsset mesh;
    std::vector<AnimationClip> clips;
};

using SkeletalMeshFbxMaterialResolver = std::uint64_t (*)(
    std::string_view sourceMaterialName,
    void* userData);

struct SkeletalMeshFbxImportOptions {
    bool importMaterialSlots = true;
    SkeletalMeshFbxMaterialResolver materialResolver = nullptr;
    void* materialResolverUserData = nullptr;
    bool combineMeshes = true;
};

// Imports compatible skinned FBX mesh nodes into the canonical Skeleton, SkeletalMesh and
// AnimationClip runtime assets. Coordinates and units are normalized by ufbx
// to the engine's left-handed, Y-up metre convention at the file boundary.
class SkeletalMeshFbxImporter final {
public:
    SkeletalMeshFbxImporter() = delete;

    [[nodiscard]] static std::optional<SkeletalMeshFbxImportResult> Import(
        const std::filesystem::path& path,
        std::uint64_t skeletonAssetId,
        const SkeletalMeshFbxImportOptions& options = {},
        std::string* error = nullptr);
};

} // namespace kb::scene
