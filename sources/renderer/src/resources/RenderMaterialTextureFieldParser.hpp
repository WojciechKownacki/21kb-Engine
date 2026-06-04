#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <string_view>

namespace kb::render {

enum class RenderMaterialFieldParseResult {
    Unknown,
    Parsed,
    Invalid,
};

class RenderMaterialTextureFieldParser final {
public:
    [[nodiscard]] static RenderMaterialFieldParseResult Apply(std::string_view keyword, std::string_view rest, RenderMaterialAssetData& asset);
};

} // namespace kb::render
