#pragma once

#include "engine/world/WorldCellIndex.hpp"
#include "engine/world/WorldPartitionGrid.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kb::world {

// One mesh placed in a cell, as the HLOD baker sees it.
struct WorldHlodMeshInstance {
    std::uint64_t meshAssetId = 0U;
    // Material asset id per mesh slot after the renderer's override rules
    // (per-slot override, then whole-renderer material); 0 keeps the mesh's own.
    std::array<std::uint64_t, 8U> slotMaterials{};
    std::uint32_t slotMaterialCount = 0U;
    std::uint64_t rendererMaterial = 0U;
    // Column-major 3x4 affine transform from mesh space to the cell's local space
    // (origin at the cell's minimum corner, at height 0).
    std::array<double, 12U> transform{ 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0 };
};

struct WorldHlodRequest {
    WorldCellCoord coord{};
    double cellSize = 0.0;
    double triangleRatio = 0.25;
    std::vector<WorldHlodMeshInstance> instances;
};

struct WorldHlodResult {
    bool succeeded = false;
    // A Wavefront OBJ document with one `usemtl` group per material slot.
    std::string objText;
    std::vector<std::uint64_t> materials;
    std::uint32_t triangleCount = 0U;
    std::uint32_t sourceTriangleCount = 0U;
    std::string error;
};

// Implemented by the renderer, which owns mesh geometry and the simplifier.
class IWorldHlodBaker {
public:
    virtual ~IWorldHlodBaker() = default;
    // Returns succeeded=false with an empty error when the cell has nothing to draw.
    [[nodiscard]] virtual WorldHlodResult Bake(const WorldHlodRequest& request) = 0;
};

struct WorldBuildReport {
    std::size_t objectCount = 0U;
    std::size_t unitCount = 0U;
    std::size_t hlodCount = 0U;
    std::uint64_t writtenBytes = 0U;
    std::vector<std::string> warnings;
};

struct WorldBuildResult {
    bool succeeded = false;
    WorldBuildReport report;
    std::string error;
};

// Turns a world's object files into the streamable build: one cell scene per
// (cell, data layer), one persistent scene per layer for always-loaded objects,
// an HLOD mesh per cell when a baker is given, and the cell index. The output
// directory is replaced as a whole, so stale cells never survive a rebuild.
class WorldCellBuilder {
public:
    WorldCellBuilder() = delete;

    [[nodiscard]] static WorldBuildResult Build(const std::filesystem::path& descriptorPath, IWorldHlodBaker* hlodBaker);
    // Builds every .21kbworld below `root`; stops at the first failure.
    [[nodiscard]] static WorldBuildResult BuildAll(const std::filesystem::path& root, IWorldHlodBaker* hlodBaker, std::size_t& builtWorlds);
};

} // namespace kb::world
