#include "engine/scene/SkeletalMeshFbxImportPlanner.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

constexpr std::uint64_t kPlanningSkeletonId = 1U;

template <typename T>
[[nodiscard]] std::optional<T> Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return std::nullopt;
}

} // namespace

std::optional<SkeletalMeshFbxImportPlan> SkeletalMeshFbxImportPlanner::Plan(
    const kb::assets::AssetManager& manager, const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationVirtualFolder,
    const SkeletalMeshFbxImportOptions& options, std::string* error) {
    if (error != nullptr) error->clear();
    const std::string normalizedFolder = kb::assets::NormalizeAssetPath(destinationVirtualFolder);
    if (normalizedFolder.empty() || normalizedFolder.front() != '/') return Fail<SkeletalMeshFbxImportPlan>(error,
        "Skeletal FBX import destination must be a mounted virtual folder.");
    std::string importError;
    auto candidate = SkeletalMeshFbxImporter::Import(sourcePath, kPlanningSkeletonId, options, &importError);
    if (!candidate) return Fail<SkeletalMeshFbxImportPlan>(error, std::move(importError));
    const std::uint64_t signature = SkeletonCompatibilitySignature(candidate->skeleton);
    if (signature == 0U) return Fail<SkeletalMeshFbxImportPlan>(error,
        "Skeletal FBX import produced a skeleton without a compatibility signature.");

    std::vector<kb::assets::AssetMetadata> skeletons = manager.Registry().ByType(kSkeletonAssetType);
    std::sort(skeletons.begin(), skeletons.end(), [](const auto& left, const auto& right) {
        return left.id.value < right.id.value;
    });
    kb::assets::AssetId selected{};
    std::filesystem::path selectedPath;
    bool reuse = false;
    bool update = false;
    for (const kb::assets::AssetMetadata& metadata : skeletons) {
        std::string loadError;
        const auto existing = SkeletonAssetIO::Load(metadata.physicalPath, &loadError);
        if (existing && SkeletonCompatibilitySignature(*existing) == signature) {
            selected = metadata.id;
            selectedPath = metadata.virtualPath;
            reuse = true;
            break;
        }
    }
    if (!reuse) {
        const std::filesystem::path candidatePath = std::filesystem::path{ normalizedFolder } /
            (sourcePath.stem().string() + kSkeletonAssetExtension);
        if (const kb::assets::AssetMetadata* occupied = manager.Registry().FindByPath(candidatePath);
            occupied != nullptr) {
            if (occupied->type != kSkeletonAssetType) return Fail<SkeletalMeshFbxImportPlan>(error,
                "Skeletal FBX import destination path is occupied by a non-Skeleton asset.");
            selectedPath = occupied->virtualPath;
            selected = occupied->id;
            update = true;
        } else {
            selectedPath = candidatePath;
            selected = kb::assets::MakeAssetId(
                kb::assets::NormalizeAssetPath(selectedPath) + ":" + kSkeletonAssetType);
        }
    }
    candidate->mesh.skeletonAssetId = selected.value;
    for (AnimationClip& clip : candidate->clips) clip.targetSkeletonAssetId = selected.value;
    return SkeletalMeshFbxImportPlan{
        .imported = std::move(*candidate), .skeletonAssetId = selected,
        .skeletonVirtualPath = std::move(selectedPath),
        .meshVirtualPath = std::filesystem::path{ normalizedFolder } /
            (sourcePath.stem().string() + kSkeletalMeshAssetExtension),
        .reusesSkeleton = reuse,
        .updatesSkeleton = update,
    };
}

} // namespace kb::scene
