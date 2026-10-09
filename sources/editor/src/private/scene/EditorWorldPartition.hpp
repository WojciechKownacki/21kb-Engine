#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/world/WorldCellBuilder.hpp"
#include "engine/world/WorldEditSession.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::assets {
class AssetManager;
}

namespace kb::editor {

struct EditorWorldGridLine {
    std::array<float, 3U> from{};
    std::array<float, 3U> to{};
    std::array<float, 3U> color{};
};

// Editor side of a partitioned world: the open world's edit session, the cell
// grid overlay and the World menu commands (region load/unload, build, scene
// conversion, layer assignment).
class EditorWorldPartition {
public:
    // Loaded cells are drawn green, cells that hold unloaded objects grey.
    static constexpr std::array<float, 3U> LoadedCellColor{ 0.30F, 0.85F, 0.45F };
    static constexpr std::array<float, 3U> UnloadedCellColor{ 0.55F, 0.58F, 0.62F };
    // Cells within this many cells of the camera are loaded by "Load Cells Near Camera".
    static constexpr std::int64_t NearCameraRadiusCells = 2;
    // Upper bound for the cells the overlay outlines, nearest first.
    static constexpr std::size_t MaxGridCells = 512U;

    [[nodiscard]] bool Open(kb::scene::Scene& scene, const std::filesystem::path& descriptorPath, std::string& error);
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] kb::world::WorldEditSession& Session() noexcept;
    [[nodiscard]] const kb::world::WorldEditSession& Session() const noexcept;

    [[nodiscard]] bool GridVisible() const noexcept;
    void SetGridVisible(bool visible) noexcept;
    // Outlines of loaded and occupied cells around (x, z), at height `y`.
    [[nodiscard]] std::vector<EditorWorldGridLine> GridLines(double x, double y, double z) const;
    // The viewport's per-frame copy of GridLines: recomputed after InvalidateGrid, when the
    // camera enters another cell, or a quarter of a second after the last computation, so
    // a large world does not walk all of its objects every frame.
    [[nodiscard]] const std::vector<EditorWorldGridLine>& CachedGridLines(double x, double y, double z) const;
    void InvalidateGrid() const noexcept;

    [[nodiscard]] std::size_t LoadAround(double x, double z, std::int64_t radiusCells, std::string& error);
    // Builds the open world's cells and HLOD proxies; `assets` resolves the meshes.
    [[nodiscard]] kb::world::WorldBuildResult Build(kb::assets::AssetManager& assets);
    // Moves `root` to the next data layer the world declares (base layer last).
    [[nodiscard]] bool CycleDataLayer(kb::scene::SceneEntity root, std::string& layer, std::string& error);

    // Keeps object identity across a scene reload that recreates every root in
    // order (play mode restore).
    void PrepareSceneReload();
    void CompleteSceneReload();

private:
    kb::world::WorldEditSession session_;
    std::vector<std::string> reloadOrder_;
    bool gridVisible_ = true;
    mutable std::vector<EditorWorldGridLine> cachedLines_;
    mutable std::optional<kb::world::WorldCellCoord> cachedCell_;
    mutable std::chrono::steady_clock::time_point cachedAt_{};
    mutable bool cacheValid_ = false;
};

} // namespace kb::editor
