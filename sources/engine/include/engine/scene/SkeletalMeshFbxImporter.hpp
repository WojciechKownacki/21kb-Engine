#pragma once

#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"
#include "engine/scene/SkeletalSourceSummary.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

struct SkeletalMeshFbxImportResult {
    SkeletonAsset skeleton;
    // No LODs when the source is an armature with animations and no mesh.
    SkeletalMeshAsset mesh;
    std::vector<AnimationClip> clips;
    // The source's name of each clip (a Blender action, an FBX take), in clip order.
    std::vector<std::string> clipNames;
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

// The memory ufbx may use, for its temporary and for its result allocations each,
// to load an FBX file of `fileBytes`. A file declares array lengths and counts it
// need not back up, and ufbx allocates for them before reading the data, so every
// load of an FBX source is given this ceiling.
[[nodiscard]] std::size_t FbxLoadMemoryLimit(std::uintmax_t fileBytes) noexcept;

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

    // Loads the scene without geometry: skin deformers, bone nodes and animation stacks.
    [[nodiscard]] static SkeletalSourceSummary Probe(const std::filesystem::path& path);
};

} // namespace kb::scene
