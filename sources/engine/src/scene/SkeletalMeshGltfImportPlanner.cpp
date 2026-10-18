#include "engine/scene/SkeletalMeshGltfImportPlanner.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "scene/SkeletalImportSkeletonPlan.hpp"
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

std::optional<SkeletalMeshGltfImportPlan> SkeletalMeshGltfImportPlanner::Plan(
    const kb::assets::AssetManager& manager,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationVirtualFolder,
    const SkeletalMeshGltfImportOptions& options,
    std::string* error) {
    if (error != nullptr) error->clear();
    const std::string normalizedFolder = kb::assets::NormalizeAssetPath(destinationVirtualFolder);
    if (normalizedFolder.empty() || normalizedFolder.front() != '/') {
        return Fail<SkeletalMeshGltfImportPlan>(error,
            "Skeletal glTF import destination must be a mounted virtual folder.");
    }
    std::string importError;
    const auto candidate = SkeletalMeshGltfImporter::Import(
        sourcePath, kPlanningSkeletonId, options, &importError);
    if (!candidate) return Fail<SkeletalMeshGltfImportPlan>(error, std::move(importError));
    const std::string stem = sourcePath.stem().string();
    auto choice = skeletal_import::ChooseSkeleton(
        manager, candidate->skeleton, candidate->mesh.lods.empty(), normalizedFolder, stem, error);
    if (!choice) return std::nullopt;
    auto imported = SkeletalMeshGltfImporter::Import(sourcePath, choice->id.value, options, &importError);
    if (!imported) return Fail<SkeletalMeshGltfImportPlan>(error, std::move(importError));
    if (choice->sameRig) {
        skeletal_import::MoveClipsToRig(imported->clips, imported->skeleton, *choice->sameRig, choice->id.value);
    }
    return SkeletalMeshGltfImportPlan{
        .imported = std::move(*imported),
        .skeletonAssetId = choice->id,
        .skeletonVirtualPath = std::move(choice->virtualPath),
        .meshVirtualPath = std::filesystem::path{ normalizedFolder } / (stem + kSkeletalMeshAssetExtension),
        .reusesSkeleton = choice->reuse,
        .updatesSkeleton = choice->update,
    };
}

} // namespace kb::scene
