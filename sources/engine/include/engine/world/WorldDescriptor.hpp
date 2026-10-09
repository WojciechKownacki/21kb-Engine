#pragma once

#include "engine/navigation/NavMeshBuild.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::world {

// A named group of objects that can be switched on and off at runtime
// ("night", "quest_x_done"). Objects outside every named layer belong to the
// base layer, which is always active.
struct WorldDataLayerDesc {
    std::string name;
    bool initiallyActive = true;
};

struct WorldHlodSettings {
    // Build a merged, simplified proxy mesh per cell from the base layer's meshes.
    bool enabled = true;
    // Proxies are shown for unloaded cells closer than this to a streaming source.
    // Zero disables proxies at runtime even when they were built.
    double range = 1024.0;
    // Fraction of the merged triangles the simplifier aims to keep, in (0, 1].
    double triangleRatio = 0.25;
};

// Navigation mesh baking of a world. When enabled, every world build bakes the static geometry
// of the base layer and the always-loaded objects into navigation tiles and writes one navigation
// mesh per cell, holding the tiles whose centre lies in the cell; the cells' tiles stream with
// them (docs/navigation.md).
struct WorldNavigationSettings {
    bool enabled = false;
    kb::navigation::NavMeshBuildSettings build;
};

// The authored world (map) file: a small text document that names the object
// directory and the partition policy. It never lists objects itself, so two
// people adding objects to one world touch different files only.
struct WorldDescriptor {
    static constexpr std::string_view Schema = "21kb.world/v1";
    static constexpr std::string_view Extension = ".21kbworld";
    static constexpr std::string_view AssetType = "World";
    static constexpr std::size_t MaxDataLayers = 64U;
    static constexpr std::size_t MaxDataLayerNameBytes = 64U;
    static constexpr std::uint32_t MaxRegionCells = 1U << 20U;

    std::string guid;
    std::string name;
    double cellSize = 128.0;
    // Side, in cells, of the square regions the build groups its output by. Each region's cells
    // and proxies share a folder, so one region can ship in a pack chunk of its own.
    std::uint32_t regionCells = 16U;
    // Directory holding one .21kbobject file per placed object, relative to the
    // descriptor's own directory.
    std::string objectsDirectory;
    std::vector<WorldDataLayerDesc> dataLayers;
    WorldHlodSettings hlod;
    WorldNavigationSettings navigation;
    std::vector<std::string> tagDefinitions{ "Player", "Enemy", "Monster", "AI", "NPC", "Collision" };

    [[nodiscard]] const WorldDataLayerDesc* FindDataLayer(std::string_view layer) const noexcept;
};

struct WorldDescriptorReadResult {
    bool succeeded = false;
    WorldDescriptor descriptor;
    std::string error;
};

// Letters, digits, '_', '-' and '.', 1..64 bytes. The empty name is the base layer.
[[nodiscard]] bool IsValidDataLayerName(std::string_view name) noexcept;

class WorldDescriptorIO {
public:
    WorldDescriptorIO() = delete;

    [[nodiscard]] static WorldDescriptorReadResult Parse(std::string_view text);
    [[nodiscard]] static WorldDescriptorReadResult Read(const std::filesystem::path& path);
    // Empty string when the descriptor is not valid.
    [[nodiscard]] static std::string Serialize(const WorldDescriptor& descriptor);
    [[nodiscard]] static bool Write(const std::filesystem::path& path, const WorldDescriptor& descriptor, std::string& error);
    // Empty when valid, otherwise the first problem found.
    [[nodiscard]] static std::string Validate(const WorldDescriptor& descriptor);
};

// Layout of a world on disk, derived from the descriptor path:
//   Forest.21kbworld                 the descriptor
//   <objectsDirectory>/<guid>.21kbobject   one file per placed object
//   Forest.cells/                    build output (cell scenes, HLOD meshes)
//   Forest.cells/Forest.21kbcells    the cell index the runtime streams from
struct WorldPaths {
    [[nodiscard]] static std::filesystem::path ObjectsDirectory(const std::filesystem::path& descriptorPath, const WorldDescriptor& descriptor);
    [[nodiscard]] static std::filesystem::path CellsDirectory(const std::filesystem::path& descriptorPath);
    [[nodiscard]] static std::filesystem::path CellIndexPath(const std::filesystem::path& descriptorPath);
    // Same derivation for registry (virtual) paths such as /Game/Worlds/Forest.21kbworld.
    [[nodiscard]] static std::string CellIndexVirtualPath(std::string_view descriptorVirtualPath);
};

} // namespace kb::world
