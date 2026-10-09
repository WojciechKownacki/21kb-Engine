#pragma once

#include "engine/assets/IAssetLoader.hpp"

namespace kb::world {

// "World": the authored .21kbworld descriptor. Its one runtime dependency is the
// built cell index next to it, which is how a scene that places the world pulls
// every cell into a packaged build's dependency closure.
class WorldDescriptorAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(
        const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const override;
};

// "WorldCells": the built cell index. Depends on every cell scene, navigation mesh,
// HLOD mesh and HLOD material it names.
class WorldCellIndexAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(
        const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const override;
    [[nodiscard]] std::optional<std::string> ValidateDependencies(
        const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const override;
    [[nodiscard]] std::optional<std::string> ValidateRuntimeDependencies(
        const kb::assets::AssetLoadRequest& request, const kb::assets::AssetRegistry& registry) const override;
};

} // namespace kb::world
