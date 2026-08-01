#pragma once

#include "engine/assets/TerrainAsset.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <optional>

namespace kb::render {

struct RenderTerrainMeshUpdateRegion {
    std::uint32_t minX = 0U;
    std::uint32_t minZ = 0U;
    std::uint32_t maxX = 0U;
    std::uint32_t maxZ = 0U;
};

class RenderTerrainMeshBuilder final {
public:
    RenderTerrainMeshBuilder() = delete;
    [[nodiscard]] static std::optional<RenderMeshAssetData> Build(const kb::assets::TerrainAsset& terrain);
    [[nodiscard]] static bool PrepareDynamicPreview(
        const kb::assets::TerrainAsset& terrain,
        RenderMeshAssetData& mesh) noexcept;
    [[nodiscard]] static bool UpdateDynamicPreview(
        const kb::assets::TerrainAsset& terrain,
        const RenderTerrainMeshUpdateRegion& region,
        RenderMeshAssetData& mesh) noexcept;
};

} // namespace kb::render
