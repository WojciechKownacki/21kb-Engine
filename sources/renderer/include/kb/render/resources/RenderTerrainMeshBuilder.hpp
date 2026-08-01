#pragma once

#include "engine/assets/TerrainAsset.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <optional>

namespace kb::render {

class RenderTerrainMeshBuilder final {
public:
    RenderTerrainMeshBuilder() = delete;
    [[nodiscard]] static std::optional<RenderMeshAssetData> Build(const kb::assets::TerrainAsset& terrain);
};

} // namespace kb::render
