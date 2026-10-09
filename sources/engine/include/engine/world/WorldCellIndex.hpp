#pragma once

#include "engine/world/WorldDescriptor.hpp"
#include "engine/world/WorldPartitionGrid.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::world {

// One streamable piece of a built world: the objects of one data layer inside
// one cell (or, for `persistent`, the objects that are loaded whenever the world
// and their layer are active).
struct WorldCellUnit {
    WorldCellCoord coord{};
    bool persistent = false;
    std::string dataLayer;
    // Cell scene path relative to the index file's directory.
    std::string scene;
    std::uint32_t objectCount = 0U;
    std::uint32_t nodeCount = 0U;
    // Resident-memory estimate charged against the streaming memory budget:
    // the serialized scene size plus a fixed cost per created entity.
    std::uint64_t estimatedBytes = 0U;
};

// A merged, simplified proxy mesh drawn in place of an unloaded cell.
struct WorldCellHlod {
    WorldCellCoord coord{};
    // Mesh path relative to the index file's directory.
    std::string mesh;
    // Material asset id per mesh material slot, in slot order.
    std::vector<std::uint64_t> materials;
    std::uint32_t triangleCount = 0U;
    std::uint32_t sourceTriangleCount = 0U;
};

// The navigation tiles of one cell: a navigation mesh asset with the tiles whose centre lies in
// the cell, for every agent profile.
struct WorldCellNavMesh {
    WorldCellCoord coord{};
    // Navigation mesh path relative to the index file's directory.
    std::string mesh;
    std::uint32_t tileCount = 0U;
};

// Built index of a partitioned world. Produced by WorldCellBuilder next to the
// cell scenes; the runtime streams exclusively from it.
struct WorldCellIndex {
    static constexpr std::string_view Schema = "21kb.world-cells/v1";
    static constexpr std::string_view Extension = ".21kbcells";
    static constexpr std::string_view AssetType = "WorldCells";
    // Cost charged per entity a cell creates, on top of its serialized size.
    static constexpr std::uint64_t EstimatedBytesPerNode = 512U;

    std::string worldGuid;
    std::string worldName;
    double cellSize = 128.0;
    // Side of a region in cells: unit and proxy paths are grouped into r_<x>_<z> folders by it.
    std::uint32_t regionCells = 16U;
    double hlodRange = 0.0;
    std::vector<WorldDataLayerDesc> dataLayers;
    std::vector<WorldCellUnit> units;
    std::vector<WorldCellHlod> hlods;
    std::vector<WorldCellNavMesh> navMeshes;
};

struct WorldCellIndexReadResult {
    bool succeeded = false;
    WorldCellIndex index;
    std::string error;
};

class WorldCellIndexIO {
public:
    WorldCellIndexIO() = delete;

    [[nodiscard]] static WorldCellIndexReadResult Parse(std::string_view text);
    [[nodiscard]] static WorldCellIndexReadResult Read(const std::filesystem::path& path);
    [[nodiscard]] static std::string Serialize(const WorldCellIndex& index);
    [[nodiscard]] static bool Write(const std::filesystem::path& path, const WorldCellIndex& index, std::string& error);
    [[nodiscard]] static std::string Validate(const WorldCellIndex& index);
};

// Joins a path relative to the index file's directory onto the index's own
// (virtual or physical) path, always with forward slashes.
[[nodiscard]] std::string ResolveWorldCellPath(std::string_view indexPath, std::string_view relative);

} // namespace kb::world
