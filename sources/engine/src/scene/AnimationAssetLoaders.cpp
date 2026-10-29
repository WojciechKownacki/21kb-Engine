#include "engine/scene/AnimationAssetLoaders.hpp"

#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/BlendSpace2D.hpp"
#include "engine/scene/BlendSpace2DAssetIO.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace kb::scene {
namespace {
template<class Reader>
std::optional<std::string> ValidateControllerSpaces(const AnimatorController& controller,
    const kb::assets::AssetRegistry& registry, Reader&& reader) {
    for (const auto& layer : controller.layers) for (const auto& state : layer.states) {
        if (state.blendSpaceReference.empty()) continue;
        kb::assets::AssetId id;
        const auto* metadata = kb::assets::TryParseAssetId(state.blendSpaceReference,id) && id.IsValid()
            ? registry.Find(id) : registry.FindByPath(kb::assets::Utf8Path(state.blendSpaceReference));
        if (metadata == nullptr || metadata->type != kBlendSpace2DAssetType)
            return "Animator BlendSpace2D is missing or has an incompatible type: " + state.blendSpaceReference;
        std::string error;
        const auto space = reader(*metadata,error);
        if (!space || space->samples.empty()) return "Animator BlendSpace2D has no usable samples: " + error;
    }
    return std::nullopt;
}
} // namespace

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
            if (!state.blendSpaceReference.empty()) {
                kb::assets::AssetId id;
                const auto* dependency = kb::assets::TryParseAssetId(state.blendSpaceReference,id) && id.IsValid()
                    ? registry.Find(id) : registry.FindByPath(kb::assets::Utf8Path(state.blendSpaceReference));
                if (dependency != nullptr && dependency->type == kBlendSpace2DAssetType &&
                    std::ranges::find(dependencies,dependency->id) == dependencies.end()) dependencies.push_back(dependency->id);
            }
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

std::optional<std::string> AnimatorControllerAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    std::string error;
    const auto controller = AnimationAssetIO::LoadController(metadata.physicalPath,&error);
    if (!controller) return error;
    return ValidateControllerSpaces(*controller,registry,[](const auto& source,std::string& diagnostic) {
        return BlendSpace2DAssetIO::Load(source.physicalPath,diagnostic);
    });
}

std::optional<std::string> AnimatorControllerAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request, const kb::assets::AssetRegistry& registry) const {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes,error)) return error;
    const auto controller = AnimationAssetIO::LoadController(bytes,&error);
    if (!controller) return error;
    return ValidateControllerSpaces(*controller,registry,[&](const auto& source,std::string& diagnostic) -> std::optional<BlendSpace2DAsset> {
        std::vector<std::uint8_t> dependency;
        if (!request.ReadDependencySourceBytes(source,dependency,diagnostic)) return std::nullopt;
        return BlendSpace2DAssetIO::Load(dependency,diagnostic);
    });
}

} // namespace kb::scene
