#pragma once
#include "engine/assets/IAssetLoader.hpp"
#include "engine/scene/VectorShape2DAsset.hpp"
namespace kb::scene {
class VectorShape2DAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override {return "VectorShape2D";}
    [[nodiscard]] std::type_index PayloadType() const noexcept override {return typeid(VectorShape2DAsset);}
    [[nodiscard]] std::vector<std::string> Extensions() const override {return {".kbvector2d"};}
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(const kb::assets::AssetMetadata& metadata,const kb::assets::AssetRegistry& registry) const override;
    [[nodiscard]] std::optional<std::string> ValidateDependencies(const kb::assets::AssetMetadata& metadata,const kb::assets::AssetRegistry& registry) const override;
    [[nodiscard]] std::optional<std::string> ValidateRuntimeDependencies(const kb::assets::AssetLoadRequest& request,const kb::assets::AssetRegistry& registry) const override;
};
}
