#include "engine/scene/SkeletalMeshFbxImportPlanner.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "scene/SkeletalImportSkeletonPlan.hpp"

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
    const std::string stem = sourcePath.stem().string();
    auto choice = skeletal_import::ChooseSkeleton(
        manager, candidate->skeleton, candidate->mesh.lods.empty(), normalizedFolder, stem, error);
    if (!choice) return std::nullopt;
    candidate->mesh.skeletonAssetId = choice->id.value;
    for (AnimationClip& clip : candidate->clips) clip.targetSkeletonAssetId = choice->id.value;
    if (choice->sameRig) {
        skeletal_import::MoveClipsToRig(candidate->clips, candidate->skeleton, *choice->sameRig, choice->id.value);
    }
    return SkeletalMeshFbxImportPlan{
        .imported = std::move(*candidate), .skeletonAssetId = choice->id,
        .skeletonVirtualPath = std::move(choice->virtualPath),
        .meshVirtualPath = std::filesystem::path{ normalizedFolder } / (stem + kSkeletalMeshAssetExtension),
        .reusesSkeleton = choice->reuse,
        .updatesSkeleton = choice->update,
    };
}

} // namespace kb::scene
