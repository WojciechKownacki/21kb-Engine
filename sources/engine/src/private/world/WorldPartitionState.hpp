#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/world/WorldCellIndex.hpp"
#include "engine/world/WorldPartitionGrid.hpp"
#include "engine/world/WorldPartitionRuntime.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::world {

struct WorldUnitRuntime {
    WorldCellState state = WorldCellState::Unloaded;
    std::uint64_t loadedSceneId = 0U;
};

// A cell's navigation mesh: requested from the asset manager, then added to the scene's navigation.
struct WorldNavMeshRuntime {
    kb::assets::AssetId assetId{};
    bool loading = false;
    bool failed = false;
    // SceneNavigation::AddNavMesh handle while the tiles are in the scene.
    std::uint64_t handle = 0U;
};

// Runtime of one placed world. Everything here is derived: it can be dropped
// and rebuilt from the ContentInstanceComponent and the built cell index.
struct WorldRuntimeInstance {
    scene::SceneEntity owner{};
    std::uint64_t worldAssetId = 0U;
    kb::assets::AssetId indexId{};
    std::string indexVirtualPath;
    bool indexRequested = false;
    bool ready = false;
    std::string error;
    std::string lastFailure;
    std::shared_ptr<const WorldCellIndex> index;
    std::unordered_map<WorldCellCoord, std::vector<std::size_t>, WorldCellCoordHash> unitsByCoord;
    std::vector<std::size_t> spatialUnits;
    std::vector<std::size_t> persistentUnits;
    std::vector<std::string> unitScenePaths;
    std::vector<WorldUnitRuntime> units;
    std::unordered_map<WorldCellCoord, std::size_t, WorldCellCoordHash> hlodByCoord;
    std::map<WorldCellCoord, scene::SceneEntity> visibleHlods;
    std::vector<std::string> navMeshPaths;
    std::vector<WorldNavMeshRuntime> navMeshes;
};

struct WorldPartitionState {
    static constexpr std::size_t MaxEvents = 4096U;

    std::map<std::uint64_t, WorldRuntimeInstance> worlds;
    std::map<std::uint64_t, WorldStreamingSource> sources;
    std::uint64_t nextSourceId = 1U;
    std::map<std::string, bool, std::less<>> layers;
    WorldStreamingBudget budget;
    WorldStreamingStats stats;
    std::deque<WorldStreamingEvent> events;
};

// A ContentInstanceComponent of kind PartitionedWorld found this frame.
struct WorldOwnerRef {
    scene::SceneEntity owner{};
    std::uint64_t worldAssetId = 0U;
};

class WorldStreamingService {
public:
    WorldStreamingService() = delete;

    [[nodiscard]] static WorldPartitionState& State(scene::Scene& scene);
    [[nodiscard]] static const WorldPartitionState* TryState(const scene::Scene& scene) noexcept;
    // Called once per update while the scene plays, with every active placed world.
    static void Synchronize(scene::Scene& scene, std::span<const WorldOwnerRef> owners);
    // Releases every cell and proxy of every world (play stopped, scene torn down).
    static void Shutdown(scene::Scene& scene) noexcept;
};

} // namespace kb::world
