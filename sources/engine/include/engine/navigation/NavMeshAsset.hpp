#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "engine/math/DVec3.hpp"
#include "engine/navigation/NavMeshBuild.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::navigation {

// A baked navigation mesh: the settings it was baked with and its tiles. A scene's navigation
// mesh is one asset; a partitioned world has one per cell, holding the tiles whose centre lies in
// the cell, so each cell's tiles stream with it. A scene uses one through a ContentInstance
// component of kind NavigationMesh.
struct NavMeshAsset {
    static constexpr std::string_view Extension = ".21kbnavmesh";
    static constexpr std::string_view AssetType = "NavMesh";
    static constexpr std::uint32_t CurrentVersion = 1U;
    static constexpr std::size_t MaxTiles = 1U << 20U;
    static constexpr std::size_t MaxLayersPerTile = 32U;

    NavMeshBuildSettings settings;
    // Sorted by profile, then tile x, then tile z; one entry per (profile, tile).
    std::vector<NavTile> tiles;

    friend bool operator==(const NavMeshAsset&, const NavMeshAsset&) = default;
};

struct NavMeshAssetReadResult {
    bool succeeded = false;
    NavMeshAsset asset;
    std::string error;
};

// Binary format (little endian): the magic "21KBNAVM", a version, the build settings, then every
// tile with its layers. A layer's three grids are stored as one zstd frame. Reading validates
// everything a tile is later rebuilt from: grid sizes, area codes and that no neighbour connection
// leads out of its grid.
class NavMeshAssetIO {
public:
    NavMeshAssetIO() = delete;

    [[nodiscard]] static std::vector<std::uint8_t> Serialize(const NavMeshAsset& asset);
    [[nodiscard]] static NavMeshAssetReadResult Parse(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static NavMeshAssetReadResult Read(const std::filesystem::path& path);
    [[nodiscard]] static bool Write(const std::filesystem::path& path, const NavMeshAsset& asset, std::string& error);
    // Empty when valid, otherwise the first problem found.
    [[nodiscard]] static std::string Validate(const NavMeshAsset& asset);
};

// The polygons the asset's tiles build into for one agent profile, without obstacles or links:
// three world positions per triangle (debug drawing, inspection).
[[nodiscard]] std::vector<kb::math::DVec3> NavMeshAssetTriangles(std::shared_ptr<const NavMeshAsset> asset, std::uint32_t profile);

// What a debug view draws of one agent profile, in world space: the polygons as triangles with the
// navigation area of each, every polygon edge (not the diagonals inside a polygon) with the edges
// no polygon continues past marked, and the bounds of every tile.
struct NavMeshDebugGeometry {
    struct Edge {
        kb::math::DVec3 from{};
        kb::math::DVec3 to{};
        bool boundary = false;
    };
    struct Box {
        kb::math::DVec3 min{};
        kb::math::DVec3 max{};
    };
    // Three corners per triangle; triangleAreas holds the navigation area id of each.
    std::vector<kb::math::DVec3> triangles;
    std::vector<std::uint8_t> triangleAreas;
    std::vector<Edge> edges;
    std::vector<Box> tiles;
};

[[nodiscard]] NavMeshDebugGeometry NavMeshAssetDebugGeometry(std::shared_ptr<const NavMeshAsset> asset, std::uint32_t profile);

class NavMeshAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
};

} // namespace kb::navigation
