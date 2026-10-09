#include "world/WorldPartitionState.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <tuple>

namespace kb::world {
namespace {

using Clock = std::chrono::steady_clock;

struct Source {
    WorldPoint position{};
    double loadRadius = 0.0;
    double unloadRadius = 0.0;
    std::int32_t priority = 0;
};

[[nodiscard]] bool IsUsableSource(const WorldStreamingSource& source) noexcept {
    return std::isfinite(source.position.x) && std::isfinite(source.position.y) && std::isfinite(source.position.z) &&
        std::isfinite(source.loadRadius) && std::isfinite(source.unloadRadius) && source.loadRadius >= 0.0 &&
        source.unloadRadius >= source.loadRadius;
}

// Stream focus entities are read through the scene's transform storage; the
// world position is widened to double before any partition math.
[[nodiscard]] std::vector<Source> CollectSources(scene::Scene& scene, const WorldPartitionState& state) {
    std::vector<Source> sources;
    kb::ecs::Query<scene::StreamFocusComponent, scene::TransformComponent> query =
        scene.Runtime().EcsWorld().CreateQuery<scene::StreamFocusComponent, scene::TransformComponent>();
    kb::ecs::UnsafeHotReadQuery<scene::StreamFocusComponent, scene::TransformComponent> hot;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (query.IsValid() && hot.Rebuild(query, settings)) {
        hot.ForEachRange(settings.maxBatchSize, [&sources, &scene](const auto& batch) {
            const scene::StreamFocusComponent* focuses = batch.template Components<0>();
            const scene::TransformComponent* transforms = batch.template Components<1>();
            for (std::size_t index = 0U; index < batch.Count(); ++index) {
                const scene::StreamFocusComponent& focus = focuses[index];
                if (!batch.EntityAt(index).IsValid() || !focus.enabled || !scene::IsStreamFocusValid(focus) ||
                    !scene::ContainsStreamLoadMask(focus.loadMask, scene::StreamLoadMask::WorldFragment)) {
                    continue;
                }
                const kb::math::DVec3 position = scene.Transforms().WorldTranslation(batch.EntityAt(index), transforms[index]);
                sources.push_back({
                    .position = { position.x, position.y, position.z },
                    .loadRadius = static_cast<double>(focus.innerRadius),
                    .unloadRadius = static_cast<double>(focus.outerRadius),
                    .priority = focus.priority,
                });
            }
        });
    }
    for (const auto& [id, source] : state.sources) {
        static_cast<void>(id);
        sources.push_back({ .position = source.position, .loadRadius = source.loadRadius, .unloadRadius = source.unloadRadius, .priority = source.priority });
    }
    // Query order follows ECS storage; sort so decisions never depend on it.
    std::ranges::sort(sources, [](const Source& left, const Source& right) {
        return std::tie(left.priority, left.position.x, left.position.z, left.position.y, left.loadRadius, left.unloadRadius) <
            std::tie(right.priority, right.position.x, right.position.z, right.position.y, right.loadRadius, right.unloadRadius);
    });
    return sources;
}

void Record(WorldPartitionState& state, WorldStreamingEventKind kind, const WorldRuntimeInstance& world, WorldCellCoord coord, bool persistent, std::string_view layer) {
    if (state.events.size() >= WorldPartitionState::MaxEvents) {
        state.events.pop_front();
    }
    state.events.push_back({ .kind = kind, .world = world.owner, .coord = coord, .persistent = persistent, .dataLayer = std::string{ layer } });
}

void Record(WorldPartitionState& state, WorldStreamingEventKind kind, const WorldRuntimeInstance& world, const WorldCellUnit& unit) {
    Record(state, kind, world, unit.persistent ? WorldCellCoord{} : unit.coord, unit.persistent, unit.dataLayer);
}

[[nodiscard]] bool LayerActive(const WorldPartitionState& state, std::string_view layer) {
    if (layer.empty()) {
        return true;
    }
    const auto found = state.layers.find(layer);
    return found != state.layers.end() && found->second;
}

void HideHlod(scene::Scene& scene, WorldPartitionState& state, WorldRuntimeInstance& world, std::map<WorldCellCoord, scene::SceneEntity>::iterator visible) {
    if (scene.Entities().IsAlive(visible->second)) {
        scene.Entities().Destroy(visible->second);
    }
    Record(state, WorldStreamingEventKind::HlodHidden, world, visible->first, false, {});
    world.visibleHlods.erase(visible);
}

void ReleaseNavMesh(scene::Scene& scene, WorldPartitionState& state, WorldRuntimeInstance& world, std::size_t index) noexcept {
    WorldNavMeshRuntime& runtime = world.navMeshes[index];
    if (runtime.handle != 0U) {
        static_cast<void>(scene::SceneNavigation{ scene }.RemoveNavMesh(runtime.handle));
        if (world.index) {
            Record(state, WorldStreamingEventKind::NavMeshUnloaded, world, world.index->navMeshes[index].coord, false, {});
        }
    }
    runtime = {};
}

void Release(scene::Scene& scene, WorldPartitionState& state, WorldRuntimeInstance& world) noexcept {
    for (std::size_t index = 0U; index < world.navMeshes.size(); ++index) {
        try {
            ReleaseNavMesh(scene, state, world, index);
        } catch (...) {
            world.navMeshes[index] = {};
        }
    }
    for (std::size_t unit = 0U; unit < world.units.size(); ++unit) {
        WorldUnitRuntime& runtime = world.units[unit];
        if (runtime.loadedSceneId != 0U && (runtime.state == WorldCellState::Loading || runtime.state == WorldCellState::Loaded)) {
            try {
                static_cast<void>(scene.LoadedContent().UnloadAsync(runtime.loadedSceneId));
            } catch (...) {
            }
        }
        runtime = {};
    }
    while (!world.visibleHlods.empty()) {
        try {
            HideHlod(scene, state, world, world.visibleHlods.begin());
        } catch (...) {
            world.visibleHlods.erase(world.visibleHlods.begin());
        }
    }
}

void BuildLookup(WorldRuntimeInstance& world) {
    const WorldCellIndex& index = *world.index;
    world.units.assign(index.units.size(), WorldUnitRuntime{});
    world.unitScenePaths.clear();
    world.unitScenePaths.reserve(index.units.size());
    for (std::size_t unit = 0U; unit < index.units.size(); ++unit) {
        world.unitScenePaths.push_back(ResolveWorldCellPath(world.indexVirtualPath, index.units[unit].scene));
        if (index.units[unit].persistent) {
            world.persistentUnits.push_back(unit);
        } else {
            world.spatialUnits.push_back(unit);
            world.unitsByCoord[index.units[unit].coord].push_back(unit);
        }
    }
    for (std::size_t hlod = 0U; hlod < index.hlods.size(); ++hlod) {
        world.hlodByCoord.emplace(index.hlods[hlod].coord, hlod);
    }
    world.navMeshPaths.clear();
    for (const WorldCellNavMesh& navMesh : index.navMeshes) {
        world.navMeshPaths.push_back(ResolveWorldCellPath(world.indexVirtualPath, navMesh.mesh));
    }
    world.navMeshes.assign(index.navMeshes.size(), WorldNavMeshRuntime{});
}

// Resolves the world asset to its built cell index and requests the index on the
// asset worker. Returns true once the index is available.
[[nodiscard]] bool Resolve(scene::Scene& scene, WorldPartitionState& state, WorldRuntimeInstance& world) {
    if (world.ready) {
        return true;
    }
    if (!world.error.empty()) {
        return false;
    }
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    if (!world.indexRequested) {
        const kb::assets::AssetMetadata* descriptor = manager.Registry().Find(kb::assets::AssetId{ world.worldAssetId });
        if (descriptor == nullptr || descriptor->type != WorldDescriptor::AssetType) {
            world.error = "asset " + std::to_string(world.worldAssetId) + " is not a registered world";
            return false;
        }
        world.indexVirtualPath = WorldPaths::CellIndexVirtualPath(descriptor->virtualPath.generic_string());
        const kb::assets::AssetMetadata* index = manager.Registry().FindByPath(world.indexVirtualPath);
        if (index == nullptr || index->type != WorldCellIndex::AssetType) {
            world.error = "world " + descriptor->virtualPath.generic_string() + " has not been built: " + world.indexVirtualPath + " is missing";
            return false;
        }
        world.indexId = index->id;
        if (!manager.RequestLoadAsync(world.indexId)) {
            world.error = "world cell index could not be requested: " + manager.LastError();
            return false;
        }
        world.indexRequested = true;
    }
    // Scene streaming pumps the asset worker only once it has jobs of its own.
    manager.PumpAsyncLoads();
    const kb::assets::AsyncAssetLoadStatus status = manager.AsyncLoadStatus(world.indexId);
    if (status == kb::assets::AsyncAssetLoadStatus::Failed) {
        world.error = "world cell index failed to load: " + manager.AsyncLoadError(world.indexId);
        return false;
    }
    const kb::assets::AssetHandle<WorldCellIndex> handle = manager.AcquireLoaded<WorldCellIndex>(world.indexId);
    if (!handle.IsLoaded()) {
        if (status != kb::assets::AsyncAssetLoadStatus::Pending) {
            world.error = "world cell index is not available after loading";
        }
        return false;
    }
    world.index = handle.Shared();
    BuildLookup(world);
    for (const WorldDataLayerDesc& layer : world.index->dataLayers) {
        state.layers.try_emplace(layer.name, layer.initiallyActive);
    }
    world.ready = true;
    return true;
}

void ObserveTransitions(scene::Scene& scene, WorldPartitionState& state, WorldRuntimeInstance& world) {
    for (std::size_t unit = 0U; unit < world.units.size(); ++unit) {
        WorldUnitRuntime& runtime = world.units[unit];
        if (runtime.state != WorldCellState::Loading && runtime.state != WorldCellState::Unloading) {
            continue;
        }
        const scene::SceneLoadStatus status = scene.LoadedContent().Status(runtime.loadedSceneId);
        const WorldCellUnit& desc = world.index->units[unit];
        if (runtime.state == WorldCellState::Loading) {
            if (status == scene::SceneLoadStatus::Ready) {
                runtime.state = WorldCellState::Loaded;
                Record(state, WorldStreamingEventKind::Loaded, world, desc);
            } else if (status == scene::SceneLoadStatus::Failed) {
                world.lastFailure = world.unitScenePaths[unit] + ": " + scene.LoadedContent().Error(runtime.loadedSceneId);
                runtime = { .state = WorldCellState::Failed, .loadedSceneId = 0U };
                Record(state, WorldStreamingEventKind::LoadFailed, world, desc);
            } else if (status == scene::SceneLoadStatus::Cancelled || status == scene::SceneLoadStatus::Unknown) {
                runtime = {};
                Record(state, WorldStreamingEventKind::Unloaded, world, desc);
            }
        } else if (status == scene::SceneLoadStatus::Unknown || status == scene::SceneLoadStatus::Cancelled ||
                   status == scene::SceneLoadStatus::Failed) {
            runtime = {};
            Record(state, WorldStreamingEventKind::Unloaded, world, desc);
        }
    }
}

struct Demand {
    bool retain = false;
    bool wanted = false;
    std::int32_t priority = std::numeric_limits<std::int32_t>::min();
    double distanceSquared = std::numeric_limits<double>::infinity();
};

void Consider(Demand& demand, std::int32_t priority, double distanceSquared) {
    if (!demand.wanted || priority > demand.priority || (priority == demand.priority && distanceSquared < demand.distanceSquared)) {
        demand.priority = priority;
        demand.distanceSquared = distanceSquared;
    }
    demand.wanted = true;
}

// Visits every spatial unit within `radius` of the source: a grid walk when the
// disc spans fewer cells than the world has units, otherwise a scan of the units.
template <typename Visit>
void ForEachUnitNear(const WorldRuntimeInstance& world, const WorldPartitionGrid& grid, const Source& source, double radius, Visit&& visit) {
    const double radiusSquared = radius * radius;
    if (grid.BoundingCellCount(radius) <= world.spatialUnits.size()) {
        grid.ForEachCellInRadius(source.position.x, source.position.z, radius, [&](WorldCellCoord coord) {
            const auto found = world.unitsByCoord.find(coord);
            if (found == world.unitsByCoord.end()) {
                return;
            }
            const double distanceSquared = grid.DistanceSquared(coord, source.position.x, source.position.z);
            for (const std::size_t unit : found->second) {
                visit(unit, distanceSquared);
            }
        });
        return;
    }
    for (const std::size_t unit : world.spatialUnits) {
        const double distanceSquared = grid.DistanceSquared(world.index->units[unit].coord, source.position.x, source.position.z);
        if (distanceSquared <= radiusSquared) {
            visit(unit, distanceSquared);
        }
    }
}

void RequestUnload(scene::Scene& scene, WorldPartitionState& state, WorldRuntimeInstance& world, std::size_t unit) {
    WorldUnitRuntime& runtime = world.units[unit];
    if (scene.LoadedContent().UnloadAsync(runtime.loadedSceneId)) {
        runtime.state = WorldCellState::Unloading;
    } else {
        runtime = {};
    }
    Record(state, WorldStreamingEventKind::UnloadRequested, world, world.index->units[unit]);
}

[[nodiscard]] bool Resident(WorldCellState state) noexcept {
    return state == WorldCellState::Loading || state == WorldCellState::Loaded || state == WorldCellState::Unloading;
}

void StreamCells(scene::Scene& scene, WorldPartitionState& state, WorldRuntimeInstance& world, const std::vector<Source>& sources,
    Clock::time_point start, std::size_t& requests, std::size_t& pending, std::uint64_t& resident) {
    const WorldCellIndex& index = *world.index;
    const WorldPartitionGrid grid{ index.cellSize };
    std::vector<Demand> demand(index.units.size());
    for (const std::size_t unit : world.persistentUnits) {
        if (LayerActive(state, index.units[unit].dataLayer)) {
            demand[unit].retain = true;
            Consider(demand[unit], std::numeric_limits<std::int32_t>::max(), 0.0);
        }
    }
    for (const Source& source : sources) {
        const double loadSquared = source.loadRadius * source.loadRadius;
        ForEachUnitNear(world, grid, source, source.unloadRadius, [&](std::size_t unit, double distanceSquared) {
            if (!LayerActive(state, index.units[unit].dataLayer)) {
                return;
            }
            demand[unit].retain = true;
            if (distanceSquared <= loadSquared) {
                Consider(demand[unit], source.priority, distanceSquared);
            }
        });
    }

    // Leaving the unload radius, or a deactivated layer, releases the cell.
    for (std::size_t unit = 0U; unit < world.units.size(); ++unit) {
        const WorldCellState current = world.units[unit].state;
        if ((current == WorldCellState::Loading || current == WorldCellState::Loaded) && !demand[unit].retain) {
            RequestUnload(scene, state, world, unit);
        }
    }

    // A lowered budget evicts the least important spatial cells first.
    std::uint64_t committed = 0U;
    std::vector<std::size_t> evictable;
    for (std::size_t unit = 0U; unit < world.units.size(); ++unit) {
        const WorldCellState current = world.units[unit].state;
        if (current == WorldCellState::Loading || current == WorldCellState::Loaded) {
            committed += index.units[unit].estimatedBytes;
            if (!index.units[unit].persistent) {
                evictable.push_back(unit);
            }
        }
    }
    if (committed > state.budget.maxResidentBytes) {
        std::ranges::sort(evictable, [&](std::size_t left, std::size_t right) {
            const Demand& a = demand[left];
            const Demand& b = demand[right];
            if (a.wanted != b.wanted) return !a.wanted;
            if (a.priority != b.priority) return a.priority < b.priority;
            if (a.distanceSquared != b.distanceSquared) return a.distanceSquared > b.distanceSquared;
            return left > right;
        });
        for (const std::size_t unit : evictable) {
            if (committed <= state.budget.maxResidentBytes) {
                break;
            }
            committed -= index.units[unit].estimatedBytes;
            demand[unit].wanted = false;
            RequestUnload(scene, state, world, unit);
        }
    }

    for (std::size_t unit = 0U; unit < world.units.size(); ++unit) {
        if (Resident(world.units[unit].state)) {
            resident += index.units[unit].estimatedBytes;
        }
        if (world.units[unit].state == WorldCellState::Loading) {
            ++pending;
        }
    }

    std::vector<std::size_t> candidates;
    for (std::size_t unit = 0U; unit < world.units.size(); ++unit) {
        if (demand[unit].wanted && world.units[unit].state == WorldCellState::Unloaded) {
            candidates.push_back(unit);
        }
    }
    std::ranges::sort(candidates, [&](std::size_t left, std::size_t right) {
        const Demand& a = demand[left];
        const Demand& b = demand[right];
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.distanceSquared != b.distanceSquared) return a.distanceSquared < b.distanceSquared;
        const WorldCellUnit& l = index.units[left];
        const WorldCellUnit& r = index.units[right];
        return std::tie(l.coord.z, l.coord.x, l.dataLayer) < std::tie(r.coord.z, r.coord.x, r.dataLayer);
    });
    for (const std::size_t unit : candidates) {
        const WorldCellUnit& desc = index.units[unit];
        if (requests >= state.budget.maxLoadRequestsPerFrame || pending >= state.budget.maxPendingLoads ||
            std::chrono::duration<double, std::milli>(Clock::now() - start).count() >= state.budget.maxMillisecondsPerFrame) {
            break;
        }
        // Persistent units are part of the world itself: charged, never refused.
        if (!desc.persistent && resident + desc.estimatedBytes > state.budget.maxResidentBytes) {
            continue;
        }
        const std::uint64_t id = scene.LoadedContent().LoadAsync(world.unitScenePaths[unit]);
        if (id == 0U) {
            // The scene-wide streaming queue is full; try again next frame.
            break;
        }
        world.units[unit] = { .state = WorldCellState::Loading, .loadedSceneId = id };
        ++requests;
        ++pending;
        resident += desc.estimatedBytes;
        Record(state, WorldStreamingEventKind::LoadRequested, world, desc);
    }
}

[[nodiscard]] bool BaseCellLoaded(const WorldRuntimeInstance& world, WorldCellCoord coord) {
    const auto found = world.unitsByCoord.find(coord);
    if (found == world.unitsByCoord.end()) {
        return false;
    }
    for (const std::size_t unit : found->second) {
        if (world.index->units[unit].dataLayer.empty()) {
            return world.units[unit].state == WorldCellState::Loaded;
        }
    }
    return false;
}

// A cell's navigation tiles follow the same radii as its objects: requested inside a source's
// load radius, released outside every unload radius.
void StreamNavMeshes(scene::Scene& scene, WorldPartitionState& state, WorldRuntimeInstance& world, const std::vector<Source>& sources) {
    const WorldCellIndex& index = *world.index;
    if (index.navMeshes.empty()) {
        return;
    }
    const WorldPartitionGrid grid{ index.cellSize };
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    bool pumped = false;
    for (std::size_t item = 0U; item < index.navMeshes.size(); ++item) {
        const WorldCellNavMesh& desc = index.navMeshes[item];
        bool wanted = false;
        bool retained = false;
        for (const Source& source : sources) {
            const double distanceSquared = grid.DistanceSquared(desc.coord, source.position.x, source.position.z);
            wanted = wanted || distanceSquared <= source.loadRadius * source.loadRadius;
            retained = retained || distanceSquared <= source.unloadRadius * source.unloadRadius;
        }
        WorldNavMeshRuntime& runtime = world.navMeshes[item];
        if ((runtime.handle != 0U || runtime.loading) && !retained) {
            ReleaseNavMesh(scene, state, world, item);
            continue;
        }
        if (runtime.failed || runtime.handle != 0U) {
            continue;
        }
        if (!runtime.loading) {
            if (!wanted) continue;
            const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(world.navMeshPaths[item]);
            if (metadata == nullptr || !manager.LoadAsync<kb::navigation::NavMeshAsset>(metadata->id)) {
                runtime.failed = true;
                world.lastFailure = world.navMeshPaths[item] + ": the navigation mesh could not be requested";
                continue;
            }
            runtime.assetId = metadata->id;
            runtime.loading = true;
        }
        if (!pumped) {
            manager.PumpAsyncLoads();
            pumped = true;
        }
        const kb::assets::AssetHandle<kb::navigation::NavMeshAsset> handle = manager.AcquireLoaded<kb::navigation::NavMeshAsset>(runtime.assetId);
        if (!handle.IsLoaded()) {
            if (manager.AsyncLoadStatus(runtime.assetId) == kb::assets::AsyncAssetLoadStatus::Failed) {
                runtime.loading = false;
                runtime.failed = true;
                world.lastFailure = world.navMeshPaths[item] + ": " + manager.AsyncLoadError(runtime.assetId);
            }
            continue;
        }
        std::string error;
        runtime.loading = false;
        runtime.handle = scene::SceneNavigation{ scene }.AddNavMesh(handle.Shared(), &error);
        if (runtime.handle == 0U) {
            runtime.failed = true;
            world.lastFailure = world.navMeshPaths[item] + ": " + error;
            continue;
        }
        Record(state, WorldStreamingEventKind::NavMeshLoaded, world, desc.coord, false, {});
    }
}

void StreamHlods(scene::Scene& scene, WorldPartitionState& state, WorldRuntimeInstance& world, const std::vector<Source>& sources) {
    const WorldCellIndex& index = *world.index;
    if (index.hlods.empty() || index.hlodRange <= 0.0) {
        while (!world.visibleHlods.empty()) {
            HideHlod(scene, state, world, world.visibleHlods.begin());
        }
        return;
    }
    const WorldPartitionGrid grid{ index.cellSize };
    // Proxies appear inside the HLOD range and stay until one cell beyond it.
    const double showRange = index.hlodRange;
    const double keepRange = index.hlodRange + index.cellSize;
    const auto nearest = [&](WorldCellCoord coord) {
        double best = std::numeric_limits<double>::infinity();
        for (const Source& source : sources) {
            best = std::min(best, grid.DistanceSquared(coord, source.position.x, source.position.z));
        }
        return best;
    };
    for (auto visible = world.visibleHlods.begin(); visible != world.visibleHlods.end();) {
        const auto current = visible++;
        if (BaseCellLoaded(world, current->first) || nearest(current->first) > keepRange * keepRange) {
            HideHlod(scene, state, world, current);
        }
    }
    std::vector<WorldCellCoord> show;
    for (const Source& source : sources) {
        const auto consider = [&](WorldCellCoord coord) {
            if (world.hlodByCoord.contains(coord) && !world.visibleHlods.contains(coord) && !BaseCellLoaded(world, coord)) {
                show.push_back(coord);
            }
        };
        if (grid.BoundingCellCount(showRange) <= index.hlods.size()) {
            grid.ForEachCellInRadius(source.position.x, source.position.z, showRange, consider);
        } else {
            for (const WorldCellHlod& hlod : index.hlods) {
                if (grid.DistanceSquared(hlod.coord, source.position.x, source.position.z) <= showRange * showRange) {
                    consider(hlod.coord);
                }
            }
        }
    }
    std::ranges::sort(show);
    show.erase(std::unique(show.begin(), show.end()), show.end());
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    for (const WorldCellCoord coord : show) {
        const WorldCellHlod& hlod = index.hlods[world.hlodByCoord.at(coord)];
        const kb::assets::AssetMetadata* mesh = manager.Registry().FindByPath(ResolveWorldCellPath(world.indexVirtualPath, hlod.mesh));
        if (mesh == nullptr) {
            // A host without mesh loaders cannot draw proxies.
            continue;
        }
        scene::SceneObjectDesc desc;
        desc.name = "HLOD " + std::to_string(coord.x) + "," + std::to_string(coord.z);
        const scene::SceneObject object = scene.Entities().CreateObject(desc);
        if (!object.IsValid()) {
            continue;
        }
        // The proxy mesh is relative to the cell corner, which is placed in double precision.
        scene.Transforms().SetLocalTranslation(object.Entity(), kb::math::DVec3{ grid.MinX(coord), 0.0, grid.MinZ(coord) });
        scene::MeshRendererComponent renderer{};
        renderer.meshAssetId = mesh->id.value;
        renderer.castsShadow = false;
        const std::size_t slots = std::min<std::size_t>(hlod.materials.size(), renderer.materialSlotAssetIds.size());
        for (std::size_t slot = 0U; slot < slots; ++slot) {
            renderer.materialSlotAssetIds[slot] = hlod.materials[slot];
        }
        renderer.materialSlotOverrideCount = static_cast<std::uint32_t>(slots);
        scene.Components().MeshRenderers().Set(object.Entity(), renderer);
        world.visibleHlods.emplace(coord, object.Entity());
        Record(state, WorldStreamingEventKind::HlodShown, world, coord, false, {});
    }
}

} // namespace

WorldPartitionState& WorldStreamingService::State(scene::Scene& scene) {
    auto& state = scene::SceneAccess::State(scene);
    if (!state.worldPartition) {
        state.worldPartition = std::make_unique<WorldPartitionState>();
    }
    return *state.worldPartition;
}

const WorldPartitionState* WorldStreamingService::TryState(const scene::Scene& scene) noexcept {
    return scene::SceneAccess::State(scene).worldPartition.get();
}

void WorldStreamingService::Synchronize(scene::Scene& scene, std::span<const WorldOwnerRef> owners) {
    if (owners.empty() && TryState(scene) == nullptr) {
        return;
    }
    WorldPartitionState& state = State(scene);
    const Clock::time_point start = Clock::now();
    for (auto world = state.worlds.begin(); world != state.worlds.end();) {
        const auto owner = std::ranges::find_if(owners, [&](const WorldOwnerRef& ref) { return ref.owner.Id() == world->first; });
        if (owner == owners.end() || owner->worldAssetId != world->second.worldAssetId || !scene.Entities().IsAlive(world->second.owner)) {
            Release(scene, state, world->second);
            world = state.worlds.erase(world);
        } else {
            ++world;
        }
    }
    for (const WorldOwnerRef& owner : owners) {
        if (!state.worlds.contains(owner.owner.Id())) {
            WorldRuntimeInstance instance;
            instance.owner = owner.owner;
            instance.worldAssetId = owner.worldAssetId;
            state.worlds.emplace(owner.owner.Id(), std::move(instance));
        }
    }
    const std::vector<Source> sources = CollectSources(scene, state);
    std::size_t requests = 0U;
    std::uint64_t resident = 0U;
    WorldStreamingStats stats{};
    for (auto& [id, world] : state.worlds) {
        static_cast<void>(id);
        ++stats.worlds;
        if (!Resolve(scene, state, world)) {
            continue;
        }
        ObserveTransitions(scene, state, world);
        std::size_t pending = 0U;
        StreamCells(scene, state, world, sources, start, requests, pending, resident);
        StreamHlods(scene, state, world, sources);
        StreamNavMeshes(scene, state, world, sources);
        for (const WorldNavMeshRuntime& navMesh : world.navMeshes) {
            stats.loadedNavMeshes += navMesh.handle != 0U ? 1U : 0U;
        }
        for (const WorldUnitRuntime& unit : world.units) {
            stats.loadedUnits += unit.state == WorldCellState::Loaded ? 1U : 0U;
            stats.loadingUnits += unit.state == WorldCellState::Loading ? 1U : 0U;
            stats.unloadingUnits += unit.state == WorldCellState::Unloading ? 1U : 0U;
            stats.failedUnits += unit.state == WorldCellState::Failed ? 1U : 0U;
        }
        stats.visibleHlods += world.visibleHlods.size();
    }
    stats.requestsThisFrame = requests;
    stats.residentBytes = resident;
    stats.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    state.stats = stats;
}

void WorldStreamingService::Shutdown(scene::Scene& scene) noexcept {
    auto& sceneState = scene::SceneAccess::State(scene);
    if (!sceneState.worldPartition) {
        return;
    }
    WorldPartitionState& state = *sceneState.worldPartition;
    for (auto& [id, world] : state.worlds) {
        static_cast<void>(id);
        Release(scene, state, world);
    }
    state.worlds.clear();
    state.stats = {};
}

WorldPartitionRuntime::WorldPartitionRuntime(scene::Scene& scene) noexcept
    : scene_(scene) {}

std::uint64_t WorldPartitionRuntime::AddSource(const WorldStreamingSource& source) {
    if (!IsUsableSource(source)) {
        throw std::invalid_argument("World streaming sources need a finite position and 0 <= loadRadius <= unloadRadius");
    }
    WorldPartitionState& state = WorldStreamingService::State(scene_);
    const std::uint64_t id = state.nextSourceId++;
    state.sources.emplace(id, source);
    return id;
}

bool WorldPartitionRuntime::UpdateSource(std::uint64_t id, const WorldStreamingSource& source) {
    if (!IsUsableSource(source)) {
        throw std::invalid_argument("World streaming sources need a finite position and 0 <= loadRadius <= unloadRadius");
    }
    WorldPartitionState& state = WorldStreamingService::State(scene_);
    const auto found = state.sources.find(id);
    if (found == state.sources.end()) {
        return false;
    }
    found->second = source;
    return true;
}

bool WorldPartitionRuntime::RemoveSource(std::uint64_t id) {
    return WorldStreamingService::State(scene_).sources.erase(id) != 0U;
}

void WorldPartitionRuntime::SetDataLayerActive(std::string_view layer, bool active) {
    if (layer.empty()) {
        return;
    }
    WorldStreamingService::State(scene_).layers.insert_or_assign(std::string{ layer }, active);
}

bool WorldPartitionRuntime::IsDataLayerActive(std::string_view layer) const {
    const WorldPartitionState* state = WorldStreamingService::TryState(scene_);
    return layer.empty() || (state != nullptr && LayerActive(*state, layer));
}

void WorldPartitionRuntime::ConfigureBudget(const WorldStreamingBudget& budget) {
    if (budget.maxLoadRequestsPerFrame == 0U || budget.maxPendingLoads == 0U || budget.maxResidentBytes == 0U ||
        !std::isfinite(budget.maxMillisecondsPerFrame) || budget.maxMillisecondsPerFrame <= 0.0) {
        throw std::invalid_argument("World streaming budgets must be positive");
    }
    WorldStreamingService::State(scene_).budget = budget;
}

WorldStreamingBudget WorldPartitionRuntime::Budget() const {
    const WorldPartitionState* state = WorldStreamingService::TryState(scene_);
    return state != nullptr ? state->budget : WorldStreamingBudget{};
}

std::vector<WorldInstanceInfo> WorldPartitionRuntime::Worlds() const {
    std::vector<WorldInstanceInfo> worlds;
    const WorldPartitionState* state = WorldStreamingService::TryState(scene_);
    if (state == nullptr) {
        return worlds;
    }
    for (const auto& [id, world] : state->worlds) {
        static_cast<void>(id);
        worlds.push_back({
            .owner = world.owner,
            .worldAssetId = world.worldAssetId,
            .ready = world.ready,
            .error = world.error,
            .lastFailure = world.lastFailure,
            .cellSize = world.index ? world.index->cellSize : 0.0,
            .unitCount = world.index ? world.index->units.size() : 0U,
            .hlodCount = world.index ? world.index->hlods.size() : 0U,
        });
    }
    return worlds;
}

namespace {

[[nodiscard]] const WorldRuntimeInstance* FindWorld(const scene::Scene& scene, scene::SceneEntity owner) {
    const WorldPartitionState* state = WorldStreamingService::TryState(scene);
    if (state == nullptr) {
        return nullptr;
    }
    const auto found = state->worlds.find(owner.Id());
    return found == state->worlds.end() || !found->second.ready ? nullptr : &found->second;
}

} // namespace

WorldCellState WorldPartitionRuntime::CellState(scene::SceneEntity world, WorldCellCoord coord, std::string_view dataLayer) const {
    const WorldRuntimeInstance* instance = FindWorld(scene_, world);
    if (instance == nullptr) {
        return WorldCellState::Unloaded;
    }
    const auto found = instance->unitsByCoord.find(coord);
    if (found == instance->unitsByCoord.end()) {
        return WorldCellState::Unloaded;
    }
    for (const std::size_t unit : found->second) {
        if (instance->index->units[unit].dataLayer == dataLayer) {
            return instance->units[unit].state;
        }
    }
    return WorldCellState::Unloaded;
}

WorldCellState WorldPartitionRuntime::PersistentState(scene::SceneEntity world, std::string_view dataLayer) const {
    const WorldRuntimeInstance* instance = FindWorld(scene_, world);
    if (instance == nullptr) {
        return WorldCellState::Unloaded;
    }
    for (const std::size_t unit : instance->persistentUnits) {
        if (instance->index->units[unit].dataLayer == dataLayer) {
            return instance->units[unit].state;
        }
    }
    return WorldCellState::Unloaded;
}

bool WorldPartitionRuntime::IsHlodVisible(scene::SceneEntity world, WorldCellCoord coord) const {
    const WorldRuntimeInstance* instance = FindWorld(scene_, world);
    return instance != nullptr && instance->visibleHlods.contains(coord);
}

bool WorldPartitionRuntime::IsNavMeshLoaded(scene::SceneEntity world, WorldCellCoord coord) const {
    const WorldRuntimeInstance* instance = FindWorld(scene_, world);
    if (instance == nullptr) {
        return false;
    }
    for (std::size_t item = 0U; item < instance->navMeshes.size(); ++item) {
        if (instance->index->navMeshes[item].coord == coord) {
            return instance->navMeshes[item].handle != 0U;
        }
    }
    return false;
}

std::vector<WorldCellCoord> WorldPartitionRuntime::LoadedCells(scene::SceneEntity world) const {
    std::vector<WorldCellCoord> cells;
    const WorldRuntimeInstance* instance = FindWorld(scene_, world);
    if (instance == nullptr) {
        return cells;
    }
    for (const std::size_t unit : instance->spatialUnits) {
        if (instance->units[unit].state == WorldCellState::Loaded) {
            cells.push_back(instance->index->units[unit].coord);
        }
    }
    std::ranges::sort(cells);
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
    return cells;
}

WorldStreamingStats WorldPartitionRuntime::Stats() const {
    const WorldPartitionState* state = WorldStreamingService::TryState(scene_);
    return state != nullptr ? state->stats : WorldStreamingStats{};
}

std::vector<WorldStreamingEvent> WorldPartitionRuntime::DrainEvents() {
    WorldPartitionState& state = WorldStreamingService::State(scene_);
    std::vector<WorldStreamingEvent> events{ std::make_move_iterator(state.events.begin()), std::make_move_iterator(state.events.end()) };
    state.events.clear();
    return events;
}

} // namespace kb::world
