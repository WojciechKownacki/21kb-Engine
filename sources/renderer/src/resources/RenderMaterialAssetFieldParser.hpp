#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <string_view>

namespace kb::render {

class RenderMaterialAssetFieldParser final {
public:
    [[nodiscard]] static bool Apply(std::string_view keyword, std::string_view rest, RenderMaterialAssetData& asset);
};

} // namespace kb::render
