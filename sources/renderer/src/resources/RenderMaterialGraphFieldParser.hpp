#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <vector>

namespace kb::render {

enum class RenderMaterialGraphFieldParseResult : std::uint8_t {
    Unknown,
    Parsed,
    Failed,
};

class RenderMaterialGraphFieldParser final {
public:
    RenderMaterialGraphFieldParser() = delete;

    [[nodiscard]] static RenderMaterialGraphFieldParseResult Apply(
        std::string_view keyword,
        std::string_view rest,
        std::size_t line,
        RenderMaterialAssetData& asset,
        std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics);
};

} // namespace kb::render
