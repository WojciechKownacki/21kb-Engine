#pragma once

#include "engine/assets/IAssetLoader.hpp"

#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace kb::render {

class RenderMeshAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] std::vector<std::string> BakedAssetTypes() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
};

} // namespace kb::render
