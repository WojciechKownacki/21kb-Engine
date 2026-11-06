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

// Authoritative terrain source. Heights are stored in world units; holes are
// per quad, so removing one cell never changes neighbouring vertex ownership.
struct TerrainAsset {
    static constexpr std::uint32_t CurrentVersion = 3U;
    static constexpr std::uint32_t MinimumResolution = 17U;
    static constexpr std::uint32_t MaximumResolution = 2049U;
    static constexpr std::uint32_t MaximumMaterialLayers = 4U;
    static constexpr std::uint32_t DefaultLayerWeightResolution = 512U;
    static constexpr std::uint32_t MinimumLayerWeightResolution = 512U;
    static constexpr std::uint32_t MaximumLayerWeightResolution = 2048U;
    static constexpr std::uint32_t MaximumCollisionStep = 8U;
    static constexpr float DefaultLodPixelError = 2.0F;

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
    // Version 3 settings (older files read as these defaults). The chunk level of detail keeps the screen
    // error of a chunk under this many pixels; the collision height field samples every collisionStep-th
    // height (1 = every sample), a power of two.
    float lodPixelError = DefaultLodPixelError;
    std::uint32_t collisionStep = 1U;
};

// A rectangle of terrain samples, both corners included (a hole edit names the cells it changed by their corner samples).
struct TerrainSampleRegion {
    std::uint32_t minX = 0U;
    std::uint32_t minZ = 0U;
    std::uint32_t maxX = 0U;
    std::uint32_t maxZ = 0U;
};

[[nodiscard]] bool IsTerrainResolutionValid(std::uint32_t value) noexcept;
[[nodiscard]] bool IsTerrainAssetValid(const TerrainAsset& terrain, std::string* error = nullptr) noexcept;
[[nodiscard]] TerrainAsset MakeFlatTerrainAsset(
    std::uint32_t resolution = 129U,
    float worldSizeX = 128.0F,
    float worldSizeZ = 128.0F);

} // namespace kb::assets
