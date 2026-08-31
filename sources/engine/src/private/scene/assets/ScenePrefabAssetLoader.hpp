#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "scene/assets/ScenePrefabGuidAssetIndex.hpp"

namespace kb::scene {

class Scene;

class ScenePrefabAssetLoader final : public kb::assets::IAssetLoader {
public:
    explicit ScenePrefabAssetLoader(Scene& scene) noexcept;

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
    Scene& scene_;
    // Only consulted for a prefab that nests another prefab; see SceneAssetLoader
    // for why it is cached against the registry generation.
    mutable ScenePrefabGuidAssetIndex nestedPrefabGuidIndex_;
};

} // namespace kb::scene
