// .21kbnavmesh navigation meshes (a scene's, or a world cell's), read the way NavMeshAssetLoader
// reads them, then rebuilt into polygons the way the scene's navigation does when it adds them.
// Compressed grids rarely survive mutation, so the input is also taken as the raw grids of one
// layer: whatever the validator accepts must rebuild safely.
#include "FuzzSupport.hpp"

#include "engine/navigation/NavMeshAsset.hpp"

#include <algorithm>
#include <memory>

namespace {

void Rebuild(std::shared_ptr<const kb::navigation::NavMeshAsset> asset) {
    for (std::uint32_t profile = 0U; profile < asset->settings.profiles.size(); ++profile) {
        static_cast<void>(kb::navigation::NavMeshAssetTriangles(asset, profile));
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    kb::navigation::NavMeshAssetReadResult read = kb::navigation::NavMeshAssetIO::Parse(kb::fuzz::Bytes(data, size));
    if (read.succeeded && read.asset.tiles.size() <= 64U) {
        Rebuild(std::make_shared<const kb::navigation::NavMeshAsset>(std::move(read.asset)));
    }

    constexpr std::uint32_t kCells = 16U;
    constexpr std::size_t kGrid = static_cast<std::size_t>(kCells) * kCells;
    if (size < kGrid * 3U + 6U) {
        return 0;
    }
    kb::navigation::NavMeshAsset raw;
    raw.settings.tileCells = kCells;
    kb::navigation::NavTileLayer layer;
    layer.heights.assign(data, data + kGrid);
    layer.areas.assign(data + kGrid, data + kGrid * 2U);
    layer.connections.assign(data + kGrid * 2U, data + kGrid * 3U);
    const std::uint8_t* tail = data + kGrid * 3U;
    layer.minX = static_cast<std::uint8_t>(std::min<std::uint32_t>(tail[0] % kCells, tail[1] % kCells));
    layer.maxX = static_cast<std::uint8_t>(std::max<std::uint32_t>(tail[0] % kCells, tail[1] % kCells));
    layer.minZ = static_cast<std::uint8_t>(std::min<std::uint32_t>(tail[2] % kCells, tail[3] % kCells));
    layer.maxZ = static_cast<std::uint8_t>(std::max<std::uint32_t>(tail[2] % kCells, tail[3] % kCells));
    layer.heightMin = tail[4];
    layer.heightMax = static_cast<std::uint16_t>(tail[4] + tail[5]);
    layer.minY = static_cast<float>(tail[4]) * raw.settings.cellHeight;
    layer.maxY = static_cast<float>(layer.heightMax) * raw.settings.cellHeight;
    raw.tiles.push_back(kb::navigation::NavTile{ .profile = 0U, .coord = { static_cast<std::int64_t>(tail[0]) - 128, 0 }, .layers = { std::move(layer) } });
    if (kb::navigation::NavMeshAssetIO::Validate(raw).empty()) {
        Rebuild(std::make_shared<const kb::navigation::NavMeshAsset>(std::move(raw)));
    }
    return 0;
}
