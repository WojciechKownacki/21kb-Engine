#pragma once

#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kb::scene {

struct SkeletalMeshFbxImportResult {
    SkeletonAsset skeleton;
    SkeletalMeshAsset mesh;
    std::vector<AnimationClip> clips;
};

struct SkeletalMeshFbxImportOptions {
    // FBX material translation is intentionally outside the skeletal importer.
    // Source slots are retained as unassigned runtime slots until material assets
    // are authored or a dedicated FBX material pipeline is available.
    bool importMaterialSlots = true;
};

// Imports one skinned FBX mesh into the canonical Skeleton, SkeletalMesh and
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
