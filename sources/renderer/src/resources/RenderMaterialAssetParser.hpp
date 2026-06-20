#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

namespace kb::render {

class RenderMaterialAssetParser final {
public:
    [[nodiscard]] static std::optional<RenderMaterialAssetData> Load(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderMaterialAssetData> Parse(std::istream& input);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadWithDiagnostics(const std::filesystem::path& path);
    [[nodiscard]] static RenderMaterialAssetParseResult ParseWithDiagnostics(std::istream& input);
};

} // namespace kb::render
