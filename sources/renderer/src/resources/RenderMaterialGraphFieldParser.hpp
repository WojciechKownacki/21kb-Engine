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

// Shared post-parse graph lifecycle for embedded materials, standalone graphs and material
// functions. Warnings keep the document loadable; errors remain fatal at the codec boundary.
void FinalizeRenderMaterialGraphDocument(
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics,
    std::size_t graphShadingModelLine = 0U,
    std::string_view graphShadingModelSourceText = {},
    std::size_t graphLastGoodArtifactLine = 0U);

} // namespace kb::render
