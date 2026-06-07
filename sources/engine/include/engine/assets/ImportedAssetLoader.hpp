#pragma once

#include "engine/assets/IAssetLoader.hpp"

namespace kb::assets {

class ImportedAssetLoader final : public IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] AssetLoadResult Load(const AssetLoadRequest& request) override;
};

} // namespace kb::assets
