#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "scene/assets/ScenePrefabGuidAssetIndex.hpp"

namespace kb::scene {

class SceneAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry) const override;
    [[nodiscard]] std::optional<std::string> ValidateDependencies(
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry) const override;
    [[nodiscard]] std::optional<std::string> ValidateRuntimeDependencies(
        const kb::assets::AssetLoadRequest& request,
        const kb::assets::AssetRegistry& registry) const override;

private:
    // Cached across one dependency-discovery pass only (keyed on the registry
    // generation), which is what keeps discovery linear in the number of prefab
    // assets instead of rebuilding the guid index for every scene. Discovery
    // stops the async loader worker, so this is only ever touched from one
    // thread; ValidateDependencies runs off that path and uses its own instance.
    mutable ScenePrefabGuidAssetIndex nestedPrefabGuidIndex_;
};

} // namespace kb::scene
