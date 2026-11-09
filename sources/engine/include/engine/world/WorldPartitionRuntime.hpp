#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/world/WorldPartitionGrid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::world {

// A point cells stream around. Entities with an enabled StreamFocusComponent
// whose load mask includes WorldFragment are sources automatically (their
// inner/outer radius); hosts add others, such as a free camera, here.
struct WorldStreamingSource {
    WorldPoint position{};
    // Cells closer than `loadRadius` are loaded; loaded cells stay until they are
    // farther than `unloadRadius` (>= loadRadius), so a source moving back and
    // forth across a cell border does not thrash.
    double loadRadius = 256.0;
    double unloadRadius = 320.0;
    std::int32_t priority = 0;
};

struct WorldStreamingBudget {
    // New cell load requests issued per frame.
    std::size_t maxLoadRequestsPerFrame = 4U;
    // Cells loading at the same time (requested and not yet ready).
    std::size_t maxPendingLoads = 4U;
    // Upper bound for the summed estimated bytes of resident cells (loading,
    // loaded or still unloading). Persistent units are charged but never evicted.
    std::uint64_t maxResidentBytes = 512ULL * 1024ULL * 1024ULL;
    // Wall-clock budget for issuing requests in one frame. Entity creation itself
    // is bounded by SceneStreamingSettings (SceneLoadedContent::ConfigureStreaming).
    double maxMillisecondsPerFrame = 1.0;
};

enum class WorldCellState : std::uint8_t {
    Unloaded,
    Loading,
    Loaded,
    Unloading,
    Failed,
};

enum class WorldStreamingEventKind : std::uint8_t {
    LoadRequested,
    Loaded,
    UnloadRequested,
    Unloaded,
    LoadFailed,
    HlodShown,
    HlodHidden,
    // A cell's navigation tiles joined or left the scene's navigation.
    NavMeshLoaded,
    NavMeshUnloaded,
};

struct WorldStreamingEvent {
    WorldStreamingEventKind kind = WorldStreamingEventKind::LoadRequested;
    scene::SceneEntity world{};
    WorldCellCoord coord{};
    bool persistent = false;
    std::string dataLayer;
};

struct WorldStreamingStats {
    std::size_t worlds = 0U;
    std::size_t loadedUnits = 0U;
    std::size_t loadingUnits = 0U;
    std::size_t unloadingUnits = 0U;
    std::size_t failedUnits = 0U;
    std::size_t visibleHlods = 0U;
    std::size_t loadedNavMeshes = 0U;
    std::size_t requestsThisFrame = 0U;
    std::uint64_t residentBytes = 0U;
    double milliseconds = 0.0;
};

// What a streamed world costs, by kind. The streaming budget charges every cell with one number (its serialized size and a fixed cost
// per entity); this is what the cells actually hold, so the estimate can be judged.
enum class WorldMemoryKind : std::uint8_t {
    AssetsCpu,  // the assets the cells made resident, in CPU memory
    Instances,  // the entities and components of the scene
    Scripts,    // the script runtime's heap
    Physics,    // the physics world
    Gpu,        // graphics resources (textures, meshes, buffers)
};
inline constexpr std::size_t kWorldMemoryKindCount = 5U;

enum class WorldMemorySource : std::uint8_t {
    Unavailable, // nothing reads this kind in this host
    Estimated,   // derived from something measurable (a file size, a count), not the memory itself
    Measured,    // read from the subsystem that holds the memory
};

// A host's reading of the total memory of one kind, in bytes, now; empty when it cannot be read. The report counts growth since the
// first world was placed. Shared-process readings can include unrelated growth and require a control for attribution.
using WorldMemoryProbe = std::function<std::optional<std::uint64_t>()>;

struct WorldMemoryCategory {
    WorldMemoryKind kind = WorldMemoryKind::AssetsCpu;
    WorldMemorySource source = WorldMemorySource::Unavailable;
    std::uint64_t bytes = 0U;         // growth since the baseline
    std::uint64_t baselineBytes = 0U; // the reading before any cell loaded
    std::string note;
    // CPU asset coverage, independent of any file-size estimate in bytes.
    std::uint64_t knownPayloadBytes = 0U;
    std::size_t unmeasuredPayloads = 0U;
    std::size_t baselineUnmeasuredPayloads = 0U;
};

struct WorldMemoryReport {
    std::uint64_t budgetBytes = 0U;
    // What the streaming budget charges for the cells resident now.
    std::uint64_t estimatedResidentBytes = 0U;
    std::array<WorldMemoryCategory, kWorldMemoryKindCount> categories{};
    // Only subsystem measurements contribute here; estimates remain separate.
    std::uint64_t measuredBytes = 0U;
    std::uint64_t estimatedBytes = 0U;
    bool hasEstimatedKinds = false;
    bool hasUnavailableKinds = false;
    // measuredBytes / estimatedResidentBytes for the cells resident now (0 when none), and the lowest, highest and number of such
    // ratios sampled when this report is requested with no cell load/unload pending. With estimated or unavailable kinds,
    // this is only a partial comparison, not the total estimation error.
    double errorRatio = 0.0;
    double minimumErrorRatio = 0.0;
    double maximumErrorRatio = 0.0;
    std::uint32_t samples = 0U;
    std::size_t residentUnits = 0U;
};

struct WorldInstanceInfo {
    scene::SceneEntity owner{};
    std::uint64_t worldAssetId = 0U;
    bool ready = false;
    // Why the world cannot stream (unknown asset, not built, unreadable index).
    std::string error;
    // The most recent cell that failed to load, with its loader diagnostic.
    std::string lastFailure;
    double cellSize = 0.0;
    std::size_t unitCount = 0U;
    std::size_t hlodCount = 0U;
};

// Streams partitioned worlds. A world is placed in a scene through a
// ContentInstanceComponent whose kind is PartitionedWorld and whose asset is the
// world's .21kbworld file; while the scene plays, its built cells are loaded and
// unloaded around the streaming sources through SceneLoadedContent's
// asynchronous, budgeted loader.
class WorldPartitionRuntime {
public:
    explicit WorldPartitionRuntime(scene::Scene& scene) noexcept;

    // Sources added here persist until removed; ids are never reused.
    [[nodiscard]] std::uint64_t AddSource(const WorldStreamingSource& source);
    [[nodiscard]] bool UpdateSource(std::uint64_t id, const WorldStreamingSource& source);
    [[nodiscard]] bool RemoveSource(std::uint64_t id);

    // Layer names are shared by every world in the scene. A layer never set here
    // takes its descriptor's initial state when a world that declares it opens.
    void SetDataLayerActive(std::string_view layer, bool active);
    [[nodiscard]] bool IsDataLayerActive(std::string_view layer) const;

    // Throws std::invalid_argument for a zero count or a non-positive time budget.
    void ConfigureBudget(const WorldStreamingBudget& budget);
    [[nodiscard]] WorldStreamingBudget Budget() const;

    // Readings the report cannot take itself: the script heap, the physics world, graphics memory. Instances (the scene's ECS storage) and
    // assets (the file sizes of the loaded assets) are read by the engine unless a probe replaces them.
    void SetMemoryProbe(WorldMemoryKind kind, WorldMemoryProbe probe);
    // Reads every kind now and compares the growth with what the budget charged.
    [[nodiscard]] WorldMemoryReport MemoryReport();

    [[nodiscard]] std::vector<WorldInstanceInfo> Worlds() const;
    [[nodiscard]] WorldCellState CellState(scene::SceneEntity world, WorldCellCoord coord, std::string_view dataLayer = {}) const;
    [[nodiscard]] WorldCellState PersistentState(scene::SceneEntity world, std::string_view dataLayer = {}) const;
    [[nodiscard]] bool IsHlodVisible(scene::SceneEntity world, WorldCellCoord coord) const;
    // True while the navigation tiles of the cell are part of the scene's navigation. A cell's
    // tiles load while a source's load radius reaches the cell and stay until its unload radius
    // no longer does, like the cell's objects.
    [[nodiscard]] bool IsNavMeshLoaded(scene::SceneEntity world, WorldCellCoord coord) const;
    // Coordinates of loaded spatial units of `world` (any layer), sorted.
    [[nodiscard]] std::vector<WorldCellCoord> LoadedCells(scene::SceneEntity world) const;
    [[nodiscard]] WorldStreamingStats Stats() const;
    // Returns and clears the decisions and transitions recorded since the last
    // drain (bounded; the oldest are dropped first).
    [[nodiscard]] std::vector<WorldStreamingEvent> DrainEvents();

private:
    scene::Scene& scene_;
};

} // namespace kb::world
