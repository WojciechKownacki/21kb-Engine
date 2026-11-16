#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {

inline constexpr std::string_view kTerrainAssetType = "RenderMesh";
inline constexpr std::string_view kTerrainAssetExtension = ".kbterrain";

struct TerrainMaterialLayer {
    std::uint64_t materialAssetId = 0U;
};

// One kind of plant scattered over the terrain. The painted density map decides where it grows; the
// rest of the fields shape each instance. Placement is deterministic (see TerrainFoliage.hpp).
struct TerrainFoliageLayer {
    std::uint64_t meshAssetId = 0U;
    std::uint64_t materialAssetId = 0U;
    // Instances per square metre where the layer is painted at full density.
    float density = 1.0F;
    float minScale = 0.8F;
    float maxScale = 1.2F;
    // Random turn around the up axis, from 0 to this many degrees.
    float rotationRangeDegrees = 360.0F;
    // 0 keeps instances upright, 1 tilts them to the slope.
    float alignToNormal = 0.0F;
    // Distance where the plant starts to shrink away, and where it is no longer drawn.
    float fadeStartDistance = 60.0F;
    float cullDistance = 80.0F;
    std::uint32_t seed = 1U;
    // Whether the plants cast shadows (they always receive them).
    bool castsShadow = true;
};

// Authoritative terrain source. Heights are stored in world units; holes are
// per quad, so removing one cell never changes neighbouring vertex ownership.
struct TerrainAsset {
    static constexpr std::uint32_t CurrentVersion = 3U;
    static constexpr std::uint32_t MinimumResolution = 17U;
    static constexpr std::uint32_t MaximumResolution = 2049U;
    static constexpr std::uint32_t MaximumMaterialLayers = 4U;
    static constexpr std::uint32_t DefaultLayerWeightResolution = 512U;
    static constexpr std::uint32_t MaximumFoliageLayers = 8U;
    static constexpr std::uint32_t DefaultFoliageMapResolution = 512U;

    std::uint32_t width = 129U;
    std::uint32_t height = 129U;
    std::uint32_t chunkQuads = 32U;
    std::uint32_t lodCount = 4U;
    float worldSizeX = 128.0F;
    float worldSizeZ = 128.0F;
    std::vector<float> heights;
    std::vector<std::uint8_t> holes;
    std::vector<TerrainMaterialLayer> materialLayers;
    std::uint32_t layerWeightWidth = 0U;
    std::uint32_t layerWeightHeight = 0U;
    // One linear RGBA8 splatmap. Active channels always sum to 255 per texel;
    // unused channels are zero. Keeping the normalized representation in the
    // asset makes painting deterministic and uploads directly to the GPU.
    std::vector<std::uint8_t> layerWeights;
    std::vector<TerrainFoliageLayer> foliageLayers;
    std::uint32_t foliageMapWidth = 0U;
    std::uint32_t foliageMapHeight = 0U;
    // One 8-bit density plane per foliage layer, stored layer after layer; 255 is full density.
    // The map spans the terrain edge to edge, like the layer weights.
    std::vector<std::uint8_t> foliageDensity;
};

[[nodiscard]] bool IsTerrainResolutionValid(std::uint32_t value) noexcept;
[[nodiscard]] bool IsTerrainAssetValid(const TerrainAsset& terrain, std::string* error = nullptr) noexcept;
[[nodiscard]] TerrainAsset MakeFlatTerrainAsset(
    std::uint32_t resolution = 129U,
    float worldSizeX = 128.0F,
    float worldSizeZ = 128.0F);

} // namespace kb::assets
