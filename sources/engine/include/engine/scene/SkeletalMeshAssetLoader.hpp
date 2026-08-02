#pragma once

#include "engine/assets/IAssetLoader.hpp"

namespace kb::scene {

class SkeletalMeshAssetLoader final : public kb::assets::IAssetLoader {
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
};

} // namespace kb::scene
