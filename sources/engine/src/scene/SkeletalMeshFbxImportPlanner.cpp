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
    const auto candidate = SkeletalMeshFbxImporter::Import(sourcePath, kPlanningSkeletonId, options, &importError);
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
        if (manager.Registry().FindByPath(candidatePath) != nullptr) return Fail<SkeletalMeshFbxImportPlan>(error,
            "Skeletal FBX import destination Skeleton path is already occupied by an incompatible asset.");
        selectedPath = candidatePath;
        selected = kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(selectedPath) + ":" + kSkeletonAssetType);
    }
    const auto imported = SkeletalMeshFbxImporter::Import(sourcePath, selected.value, options, &importError);
    if (!imported) return Fail<SkeletalMeshFbxImportPlan>(error, std::move(importError));
    return SkeletalMeshFbxImportPlan{
        .imported = std::move(*imported), .skeletonAssetId = selected,
        .skeletonVirtualPath = std::move(selectedPath),
        .meshVirtualPath = std::filesystem::path{ normalizedFolder } /
            (sourcePath.stem().string() + kSkeletalMeshAssetExtension),
        .reusesSkeleton = reuse,
    };
}

} // namespace kb::scene
