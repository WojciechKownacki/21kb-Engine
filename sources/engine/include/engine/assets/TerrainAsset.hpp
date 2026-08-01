#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {

inline constexpr std::string_view kTerrainAssetType = "RenderMesh";
inline constexpr std::string_view kTerrainAssetExtension = ".kbterrain";

// Authoritative terrain source. Heights are stored in world units; holes are
// per quad, so removing one cell never changes neighbouring vertex ownership.
struct TerrainAsset {
    static constexpr std::uint32_t CurrentVersion = 1U;
    static constexpr std::uint32_t MinimumResolution = 17U;
    static constexpr std::uint32_t MaximumResolution = 2049U;

    std::uint32_t width = 129U;
    std::uint32_t height = 129U;
    std::uint32_t chunkQuads = 32U;
    std::uint32_t lodCount = 4U;
    float worldSizeX = 128.0F;
    float worldSizeZ = 128.0F;
    std::vector<float> heights;
    std::vector<std::uint8_t> holes;
};

[[nodiscard]] bool IsTerrainResolutionValid(std::uint32_t value) noexcept;
[[nodiscard]] bool IsTerrainAssetValid(const TerrainAsset& terrain, std::string* error = nullptr) noexcept;
[[nodiscard]] TerrainAsset MakeFlatTerrainAsset(
    std::uint32_t resolution = 129U,
    float worldSizeX = 128.0F,
    float worldSizeZ = 128.0F);

} // namespace kb::assets
