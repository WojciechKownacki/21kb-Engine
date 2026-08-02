#include "engine/scene/AnimationAssetLoaders.hpp"

#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/assets/AssetRegistry.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace kb::scene {

std::string_view AnimationClipAssetLoader::Type() const noexcept { return kAnimationClipAssetType; }
std::type_index AnimationClipAssetLoader::PayloadType() const noexcept { return typeid(AnimationClip); }
std::vector<std::string> AnimationClipAssetLoader::Extensions() const { return { kAnimationClipAssetExtension }; }
kb::assets::AssetLoadResult AnimationClipAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string error;
    auto value = AnimationAssetIO::LoadClip(request.resolvedPath, &error);
    return value ? kb::assets::AssetLoadResult{ std::make_shared<AnimationClip>(std::move(*value)), {} }
                 : kb::assets::AssetLoadResult{ {}, std::move(error) };
}

std::string_view AnimatorControllerAssetLoader::Type() const noexcept { return kAnimatorControllerAssetType; }
std::type_index AnimatorControllerAssetLoader::PayloadType() const noexcept { return typeid(AnimatorController); }
std::vector<std::string> AnimatorControllerAssetLoader::Extensions() const { return { kAnimatorControllerAssetExtension }; }
kb::assets::AssetLoadResult AnimatorControllerAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string error;
    auto value = AnimationAssetIO::LoadController(request.resolvedPath, &error);
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
