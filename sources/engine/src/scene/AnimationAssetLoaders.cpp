#include "engine/scene/AnimationAssetLoaders.hpp"

#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace kb::scene {

std::string_view AnimationClipAssetLoader::Type() const noexcept { return kAnimationClipAssetType; }
std::type_index AnimationClipAssetLoader::PayloadType() const noexcept { return typeid(AnimationClip); }
std::vector<std::string> AnimationClipAssetLoader::Extensions() const { return { kAnimationClipAssetExtension }; }
kb::assets::AssetLoadResult AnimationClipAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string error;
    if (request.SourceExtension() != kAnimationClipAssetExtension) {
        return kb::assets::AssetLoadResult{ {}, "Animation clip has an unexpected file extension." };
    }
    std::vector<std::uint8_t> sourceBytes;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ {}, std::move(error) };
    }
    auto value = AnimationAssetIO::LoadClip(sourceBytes, &error);
    return value ? kb::assets::AssetLoadResult{ std::make_shared<AnimationClip>(std::move(*value)), {} }
                 : kb::assets::AssetLoadResult{ {}, std::move(error) };
}

std::vector<kb::assets::AssetId> AnimationClipAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    static_cast<void>(registry);
    const auto clip = AnimationAssetIO::LoadClip(metadata.physicalPath);
    if (!clip || clip->targetSkeletonAssetId == 0U) return {};
    return { kb::assets::AssetId{ clip->targetSkeletonAssetId } };
}

std::optional<std::string> AnimationClipAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    std::string error;
    const auto clip = AnimationAssetIO::LoadClip(metadata.physicalPath, &error);
    if (!clip) return error;
    if (clip->targetSkeletonAssetId == 0U) return std::nullopt;

    const kb::assets::AssetMetadata* skeletonMetadata = registry.Find(
        kb::assets::AssetId{ clip->targetSkeletonAssetId });
    if (skeletonMetadata == nullptr) return std::nullopt;
    if (skeletonMetadata->type != kSkeletonAssetType) {
        return "references an asset that is not a Skeleton.";
    }
    const auto skeleton = SkeletonAssetIO::Load(skeletonMetadata->physicalPath, &error);
    if (!skeleton) return "references a Skeleton that cannot be loaded: " + error;
    if (SkeletonCompatibilitySignature(*skeleton) != clip->targetSkeletonCompatibilitySignature) {
        return "references a Skeleton with an incompatible compatibility signature.";
    }
    return std::nullopt;
}

std::optional<std::string> AnimationClipAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request,
    const kb::assets::AssetRegistry& registry) const {
    if (!request.IsPackaged()) {
        return ValidateDependencies(request.metadata, registry);
    }

    std::string error;
    std::vector<std::uint8_t> clipBytes;
    if (!request.ReadSourceBytes(clipBytes, error)) {
        return error;
    }
    const auto clip = AnimationAssetIO::LoadClip(clipBytes, &error);
    if (!clip) return error;
    if (clip->targetSkeletonAssetId == 0U) return std::nullopt;

    const kb::assets::AssetMetadata* skeletonMetadata = registry.Find(
        kb::assets::AssetId{ clip->targetSkeletonAssetId });
    if (skeletonMetadata == nullptr) return std::nullopt;
    if (skeletonMetadata->type != kSkeletonAssetType) {
        return "references an asset that is not a Skeleton.";
    }
    std::vector<std::uint8_t> skeletonBytes;
    if (!request.ReadDependencySourceBytes(*skeletonMetadata, skeletonBytes, error)) {
        return "references a Skeleton that cannot be loaded: " + error;
    }
    const auto skeleton = SkeletonAssetIO::Load(skeletonBytes, &error);
    if (!skeleton) return "references a Skeleton that cannot be loaded: " + error;
    if (SkeletonCompatibilitySignature(*skeleton) != clip->targetSkeletonCompatibilitySignature) {
        return "references a Skeleton with an incompatible compatibility signature.";
    }
    return std::nullopt;
}

std::string_view AnimatorControllerAssetLoader::Type() const noexcept { return kAnimatorControllerAssetType; }
std::type_index AnimatorControllerAssetLoader::PayloadType() const noexcept { return typeid(AnimatorController); }
std::vector<std::string> AnimatorControllerAssetLoader::Extensions() const { return { kAnimatorControllerAssetExtension }; }
kb::assets::AssetLoadResult AnimatorControllerAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string error;
    if (request.SourceExtension() != kAnimatorControllerAssetExtension) {
        return kb::assets::AssetLoadResult{ {}, "Animator controller has an unexpected file extension." };
    }
    std::vector<std::uint8_t> sourceBytes;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ {}, std::move(error) };
    }
    auto value = AnimationAssetIO::LoadController(sourceBytes, &error);
    return value ? kb::assets::AssetLoadResult{ std::make_shared<AnimatorController>(std::move(*value)), {} }
                 : kb::assets::AssetLoadResult{ {}, std::move(error) };
}

std::vector<kb::assets::AssetId> AnimatorControllerAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    const auto controller = AnimationAssetIO::LoadController(metadata.physicalPath);
    if (!controller) return {};
    std::vector<kb::assets::AssetId> dependencies;
    for (const AnimatorControllerLayer& layer : controller->layers) {
        for (const AnimatorControllerState& state : layer.states) {
            std::vector<std::string_view> references;
            if (!state.clipReference.empty()) references.push_back(state.clipReference);
            for (const AnimatorControllerState::BlendChild& child : state.blendChildren) {
                references.push_back(child.clipReference);
            }
            for (const std::string_view reference : references) {
                kb::assets::AssetId id{};
                const kb::assets::AssetMetadata* dependency = nullptr;
                if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) dependency = registry.Find(id);
                else dependency = registry.FindByPath(std::filesystem::path{ reference });
                if (dependency != nullptr && dependency->type == kAnimationClipAssetType &&
                    std::find(dependencies.begin(), dependencies.end(), dependency->id) == dependencies.end()) {
                    dependencies.push_back(dependency->id);
                }
            }
        }
    }
    return dependencies;
}

} // namespace kb::scene
