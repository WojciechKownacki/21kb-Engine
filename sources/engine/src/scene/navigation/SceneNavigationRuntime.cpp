#include "engine/scene/SceneNavigation.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/navigation/NavGeometryCollector.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityCounter.hpp"
#include "scene/navigation/SceneNavigationState.hpp"
#include "scene/systems/NavigationSceneSystem.hpp"
#include "scene/transform/SceneTransformHierarchySystem.hpp"
#include "scene/transform/SceneTransformPrecision.hpp"
#include "scene/transform/SceneTransformResiduals.hpp"
#include "navigation/NavMeshCrowd.hpp"
#include "navigation/NavMeshRuntime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

namespace nav = kb::navigation;
using kb::math::Quat;
using kb::math::Vec3;

constexpr float kEpsilon = 1.0e-5F;

struct ObstacleVolume {
    std::uint64_t entity = 0U;
    NavObstacle obstacle{};
    Vec3 center{};
    Quat rotation{};
    float halfX = 0.5F;
    float halfZ = 0.5F;
    float radius = 0.5F;
    float halfHeight = 0.5F;
};

struct AgentState {
    SceneEntity entity{};
    NavAgent agent{};
    Vec3 position{};
    Quat rotation{};
    bool character = false;
};

[[nodiscard]] float HorizontalLength(Vec3 value) noexcept {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

// Obstacles are placed in the navigation space (world space minus `origin`).
[[nodiscard]] std::vector<ObstacleVolume> CollectObstacles(Scene& scene, SceneState& state, const kb::math::DVec3& origin) {
    std::vector<std::pair<SceneEntity, NavObstacle>> authored;
    state.world.CreateQuery<NavObstacle>().ForEach(
        [](SceneEntity entity, const NavObstacle& obstacle, void* context) {
            static_cast<std::vector<std::pair<SceneEntity, NavObstacle>>*>(context)->emplace_back(entity, obstacle);
        }, &authored);
    std::vector<ObstacleVolume> volumes;
    volumes.reserve(authored.size());
    for (const auto& [entity, obstacle] : authored) {
        if (!obstacle.enabled) continue;
        const TransformComponent* transform = scene.Transforms().TryGet(entity);
        if (transform == nullptr) continue;
        const Vec3 scale{ std::fabs(transform->worldScale.x), std::fabs(transform->worldScale.y), std::fabs(transform->worldScale.z) };
        ObstacleVolume volume{
            .entity = entity.Id(),
            .obstacle = obstacle,
            .center = kb::math::RelativeTo(scene.Transforms().WorldTranslation(entity, *transform), origin) +
                kb::math::Rotate(transform->worldRotation, Vec3{ obstacle.center.x * transform->worldScale.x,
                    obstacle.center.y * transform->worldScale.y, obstacle.center.z * transform->worldScale.z }),
            .rotation = transform->worldRotation,
        };
        if (obstacle.shape == NavObstacleShape::Cylinder) {
            volume.radius = std::max(obstacle.radius, 0.0F) * std::max(scale.x, scale.z);
            volume.halfHeight = std::max(obstacle.height, 0.0F) * 0.5F * scale.y;
        } else {
            volume.halfX = std::max(obstacle.size.x, 0.0F) * 0.5F * scale.x;
            volume.halfZ = std::max(obstacle.size.z, 0.0F) * 0.5F * scale.z;
            volume.halfHeight = std::max(obstacle.size.y, 0.0F) * 0.5F * scale.y;
        }
        volumes.push_back(volume);
    }
    std::ranges::sort(volumes, {}, &ObstacleVolume::entity);
    return volumes;
}

void WriteAgentTransform(Scene& scene, SceneState& state, const AgentState& agent, const kb::math::DVec3& origin) {
    TransformComponent* transform = scene.Transforms().TryGet(agent.entity);
    if (transform == nullptr) return;
    TransformComponent updated = *transform;
    // The agent's world position, in double precision.
    kb::math::DVec3 local = origin + agent.position;
    Quat localRotation = agent.rotation;
    const SceneEntity parent = scene.Hierarchy().Parent(agent.entity);
    if (parent.IsValid()) {
        if (const TransformComponent* parentTransform = scene.Transforms().TryGet(parent); parentTransform != nullptr) {
            const Quat inverse = kb::math::Inverse(parentTransform->worldRotation);
            const Vec3 scale = parentTransform->worldScale;
            const kb::math::DVec3 parentWorld = scene.Transforms().WorldTranslation(parent, *parentTransform);
            if (origin == kb::math::DVec3{} && parentWorld == kb::math::ToDVec3(parentTransform->worldPosition)) {
                // Everything is in float world space: the float result agents always had.
                const Vec3 relative = kb::math::Rotate(inverse, agent.position - parentTransform->worldPosition);
                local = kb::math::ToDVec3(Vec3{ std::fabs(scale.x) > kEpsilon ? relative.x / scale.x : relative.x,
                    std::fabs(scale.y) > kEpsilon ? relative.y / scale.y : relative.y,
                    std::fabs(scale.z) > kEpsilon ? relative.z / scale.z : relative.z });
            } else {
                const kb::math::DVec3 relative = kb::math::RotateDouble(inverse, local - parentWorld);
                local = kb::math::DVec3{ std::fabs(scale.x) > kEpsilon ? relative.x / scale.x : relative.x,
                    std::fabs(scale.y) > kEpsilon ? relative.y / scale.y : relative.y,
                    std::fabs(scale.z) > kEpsilon ? relative.z / scale.z : relative.z };
            }
            localRotation = inverse * agent.rotation;
        }
    }
    Vec3 residual{};
    SplitTranslation(local, updated.localPosition, residual);
    updated.localRotation = localRotation;
    scene.Transforms().Set(agent.entity, updated);
    SceneTransformPrecision::StoreLocalResidual(state, agent.entity, residual);
}

[[nodiscard]] nav::NavMeshRuntime& Polygons(SceneNavigationState& navigation) {
    if (!navigation.polygons) {
        navigation.polygons = std::make_shared<nav::NavMeshRuntime>();
        navigation.polygons->SetOrigin(navigation.origin);
    }
    return *navigation.polygons;
}

[[nodiscard]] bool HasPolygons(const SceneNavigationState& navigation) noexcept {
    return navigation.polygons && !navigation.polygons->Empty();
}

// Rotation about the vertical axis of a box obstacle's frame.
[[nodiscard]] float YawOf(Quat rotation) noexcept {
    const Vec3 axis = kb::math::Rotate(rotation, Vec3{ 1.0F, 0.0F, 0.0F });
    return std::atan2(-axis.z, axis.x);
}

// Places the scene's NavObstacles and NavLinks into the polygon meshes' space; the tiles they
// changed are rebuilt.
void SynchronizeOverlays(Scene& scene, SceneState& state) {
    SceneNavigationState& navigation = state.navigation;
    if (!HasPolygons(navigation)) return;
    nav::NavMeshRuntime& runtime = *navigation.polygons;
    runtime.SetOrigin(navigation.origin);
    std::vector<nav::NavRuntimeObstacle> obstacles;
    for (const ObstacleVolume& volume : CollectObstacles(scene, state, navigation.origin)) {
        obstacles.push_back(nav::NavRuntimeObstacle{
            .id = volume.entity,
            .shape = volume.obstacle.shape,
            .center = volume.center,
            .yaw = volume.obstacle.shape == NavObstacleShape::Box ? YawOf(volume.rotation) : 0.0F,
            .halfExtents = { volume.halfX, volume.halfHeight, volume.halfZ },
            .radius = volume.radius,
            .height = volume.halfHeight * 2.0F,
            .carve = volume.obstacle.carve,
            .area = volume.obstacle.area,
        });
    }
    runtime.SetObstacles(std::move(obstacles));

    std::vector<std::pair<SceneEntity, NavLink>> authored;
    state.world.CreateQuery<NavLink>().ForEach(
        [](SceneEntity entity, const NavLink& link, void* context) {
            static_cast<std::vector<std::pair<SceneEntity, NavLink>>*>(context)->emplace_back(entity, link);
        }, &authored);
    std::vector<nav::NavRuntimeLink> links;
    links.reserve(authored.size());
    for (const auto& [entity, link] : authored) {
        const TransformComponent* transform = scene.Transforms().TryGet(entity);
        if (!link.enabled || transform == nullptr || !IsValidNavArea(link.area) || !(link.radius > 0.0F)) continue;
        const kb::math::DVec3 world = scene.Transforms().WorldTranslation(entity, *transform);
        const auto place = [&](Vec3 offset) {
            const Vec3 scaled{ offset.x * transform->worldScale.x, offset.y * transform->worldScale.y, offset.z * transform->worldScale.z };
            return kb::math::RelativeTo(world + kb::math::ToDVec3(kb::math::Rotate(transform->worldRotation, scaled)), navigation.origin);
        };
        links.push_back(nav::NavRuntimeLink{
            .id = entity.Id(),
            .start = place(link.start),
            .end = place(link.end),
            .radius = link.radius,
            .kind = link.kind,
            .area = link.area,
            .bidirectional = link.bidirectional,
        });
    }
    runtime.SetLinks(std::move(links));
}

// Where agents update fully: the focuses set from code, else every enabled Stream Focus.
[[nodiscard]] std::vector<Vec3> CrowdFocuses(Scene& scene, SceneState& state) {
    const SceneNavigationState& navigation = state.navigation;
    std::vector<Vec3> focuses;
    if (navigation.crowdFocusesSet) {
        for (const kb::math::DVec3& focus : navigation.crowdFocuses) focuses.push_back(kb::math::RelativeTo(focus, navigation.origin));
        return focuses;
    }
    std::vector<SceneEntity> entities;
    state.world.CreateQuery<StreamFocusComponent>().ForEach(
        [](SceneEntity entity, const StreamFocusComponent& focus, void* context) {
            if (focus.enabled) static_cast<std::vector<SceneEntity>*>(context)->push_back(entity);
        }, &entities);
    std::ranges::sort(entities, {}, &SceneEntity::Id);
    for (const SceneEntity entity : entities) {
        if (const TransformComponent* transform = scene.Transforms().TryGet(entity); transform != nullptr) {
            focuses.push_back(kb::math::RelativeTo(scene.Transforms().WorldTranslation(entity, *transform), navigation.origin));
        }
    }
    return focuses;
}

// Moves the agents over the polygon meshes as a crowd.
void StepPolygonNavigation(Scene& scene, SceneState& state, std::span<const std::pair<SceneEntity, NavAgent>> authored, float deltaSeconds,
    std::uint32_t steps) {
    SceneNavigationState& navigation = state.navigation;
    nav::NavMeshRuntime& runtime = *navigation.polygons;
    SynchronizeOverlays(scene, state);
    if (!navigation.crowd) navigation.crowd = std::make_shared<nav::NavMeshCrowd>();

    std::vector<AgentState> agents;
    std::vector<nav::NavMeshCrowdAgentInput> inputs;
    agents.reserve(authored.size());
    inputs.reserve(authored.size());
    for (const auto& [entity, agent] : authored) {
        const TransformComponent* transform = scene.Transforms().TryGet(entity);
        if (transform == nullptr) continue;
        const Vec3 position = kb::math::RelativeTo(scene.Transforms().WorldTranslation(entity, *transform), navigation.origin);
        agents.push_back(AgentState{
            .entity = entity,
            .agent = agent,
            .position = position,
            .rotation = transform->worldRotation,
            .character = scene.Components().CharacterControllers().Has(entity),
        });
        inputs.push_back(nav::NavMeshCrowdAgentInput{ .id = entity.Id(), .agent = agent, .position = position });
    }
    // Obstacles that neither carve nor repaint an area are circles the crowd steers around.
    std::vector<nav::NavMeshCrowdObstacle> circles;
    for (const nav::NavRuntimeObstacle& obstacle : runtime.Obstacles()) {
        if (obstacle.carve || obstacle.area != kDefaultNavArea) continue;
        const float radius = obstacle.shape == NavObstacleShape::Cylinder
            ? obstacle.radius
            : std::sqrt(obstacle.halfExtents.x * obstacle.halfExtents.x + obstacle.halfExtents.z * obstacle.halfExtents.z);
        const float height = obstacle.shape == NavObstacleShape::Cylinder ? obstacle.height : obstacle.halfExtents.y * 2.0F;
        circles.push_back(nav::NavMeshCrowdObstacle{ .id = obstacle.id, .center = obstacle.center, .radius = radius, .height = height });
    }
    const std::vector<Vec3> focuses = CrowdFocuses(scene, state);
    std::vector<nav::NavMeshCrowdAgentOutput> outputs;
    for (std::uint32_t step = 0U; step < steps; ++step) {
        navigation.crowd->Step(runtime, inputs, circles, focuses, navigation.crowdSettings, deltaSeconds, outputs);
        for (std::size_t index = 0U; index < inputs.size(); ++index) inputs[index].position = outputs[index].position;
    }
    const float elapsed = deltaSeconds * static_cast<float>(steps);
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        AgentState& agent = agents[index];
        const nav::NavMeshCrowdAgentOutput& output = outputs[index];
        if (NavAgent* component = state.componentStorage.Navigation().TryGetNavAgent(agent.entity); component != nullptr) {
            component->velocity = agent.agent.enabled ? output.velocity : Vec3{};
            component->remainingDistance = output.remainingDistance;
            component->pathStatus = output.status;
            state.componentStorage.Navigation().MarkNavAgentModified(agent.entity);
        }
        if (!agent.agent.enabled || (output.status == NavPathStatus::Failed && !output.onLink && HorizontalLength(output.velocity) <= 0.0F)) {
            continue;
        }
        if (agent.character && !output.onLink && PhysicsBackend::CharacterMove(scene, agent.entity, output.velocity)) continue;
        agent.position = output.position;
        const float speed = HorizontalLength(output.velocity);
        if (!agent.character && speed > kEpsilon) {
            const Vec3 forward = Vec3{ output.velocity.x, 0.0F, output.velocity.z } * (1.0F / speed);
            const Quat facing = kb::math::LookRotation(forward, Vec3{ 0.0F, 1.0F, 0.0F });
            agent.rotation = kb::math::RotateTowards(agent.rotation, facing,
                kb::math::ToRadians(kb::math::Degrees{ std::max(agent.agent.angularSpeedDegrees, 0.0F) * elapsed }));
        }
        WriteAgentTransform(scene, state, agent, navigation.origin);
    }
}

// Adds the navigation meshes ContentInstances of kind NavigationMesh place while the scene plays,
// and removes them when their owner goes away, changes asset or the scene stops.
void SynchronizePlacedNavMeshes(Scene& scene, SceneState& state, bool playing) {
    SceneNavigationState& navigation = state.navigation;
    std::map<std::uint64_t, std::uint64_t> wanted;
    if (playing) {
        state.world.CreateQuery<ContentInstanceComponent>().ForEach(
            [](SceneEntity entity, const ContentInstanceComponent& content, void* context) {
                if (content.active && content.kind == ContentInstanceKind::NavigationMesh && content.assetId != 0U) {
                    static_cast<std::map<std::uint64_t, std::uint64_t>*>(context)->emplace(entity.Id(), content.assetId);
                }
            }, &wanted);
    }
    SceneNavigation api{ scene };
    for (auto record = navigation.placedMeshes.begin(); record != navigation.placedMeshes.end();) {
        const auto found = wanted.find(record->first);
        if (found != wanted.end() && found->second == record->second.assetId) {
            ++record;
            continue;
        }
        if (record->second.handle != 0U) static_cast<void>(api.RemoveNavMesh(record->second.handle));
        record = navigation.placedMeshes.erase(record);
    }
    if (wanted.empty()) return;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    for (const auto& [owner, assetId] : wanted) {
        PlacedNavMeshRecord& record = navigation.placedMeshes[owner];
        record.assetId = assetId;
        if (record.handle != 0U || record.failed) continue;
        const kb::assets::AssetId id{ assetId };
        if (!record.requested) {
            if (!manager.LoadAsync<nav::NavMeshAsset>(id)) {
                record.failed = true;
                continue;
            }
            record.requested = true;
        }
        manager.PumpAsyncLoads();
        const kb::assets::AssetHandle<nav::NavMeshAsset> handle = manager.AcquireLoaded<nav::NavMeshAsset>(id);
        if (!handle.IsLoaded()) {
            record.failed = manager.AsyncLoadStatus(id) == kb::assets::AsyncAssetLoadStatus::Failed;
            continue;
        }
        record.handle = api.AddNavMesh(handle.Shared());
        record.failed = record.handle == 0U;
    }
}

} // namespace

void StepSceneNavigation(Scene& scene, float deltaSeconds, std::uint32_t steps) {
    SceneState& state = SceneAccess::State(scene);
    SceneNavigationState& navigation = state.navigation;
    std::vector<std::pair<SceneEntity, NavAgent>> authored;
    state.world.CreateQuery<NavAgent>().ForEach(
        [](SceneEntity entity, const NavAgent& agent, void* context) {
            static_cast<std::vector<std::pair<SceneEntity, NavAgent>>*>(context)->emplace_back(entity, agent);
        }, &authored);
    std::ranges::sort(authored, {}, [](const auto& entry) { return entry.first.Id(); });
    if (authored.empty() || !(deltaSeconds > 0.0F) || steps == 0U) return;
    if (HasPolygons(navigation)) {
        StepPolygonNavigation(scene, state, authored, deltaSeconds, steps);
        return;
    }
    // Without polygon meshes agents stay where they are.
    if (navigation.crowd) navigation.crowd->Clear();
    for (const auto& [entity, agent] : authored) {
        if (NavAgent* component = state.componentStorage.Navigation().TryGetNavAgent(entity); component != nullptr) {
            component->velocity = Vec3{};
            if (agent.enabled) component->pathStatus = NavPathStatus::Failed;
            state.componentStorage.Navigation().MarkNavAgentModified(entity);
        }
    }
}

void NavigationSceneSystem::OnUpdate(SceneSystemContext& context) {
    Scene& scene = context.GetScene();
    SceneState& state = SceneAccess::State(scene);
    SceneNavigationState& navigation = state.navigation;
    SynchronizePlacedNavMeshes(scene, state, state.isPlaying && state.mode != SceneMode::PrefabPrivate);
    if (!state.isPlaying || SceneEntityCounter::CountWithComponent(state.world, state.components.NavAgentComponentId()) == 0U) {
        navigation.stepAccumulator = 0.0F;
        if (SceneEntityCounter::CountWithComponent(state.world, state.components.NavAgentComponentId()) == 0U) {
            if (navigation.crowd) navigation.crowd->Clear();
        }
        return;
    }
    // Agents step at the scene's fixed rate, so the same play time gives the same motion whatever the frame
    // rate was.
    const SceneRuntimeFixedStepSettings fixed = scene.Runtime().FixedStepSettings();
    const float step = fixed.fixedDeltaSeconds > 0.0F ? fixed.fixedDeltaSeconds : kSceneRuntimeDefaultFixedDeltaSeconds;
    const std::size_t maxSteps = fixed.maxFixedStepsPerFrame > 0U ? fixed.maxFixedStepsPerFrame : kSceneRuntimeDefaultMaxFixedStepsPerFrame;
    navigation.stepAccumulator += std::clamp(context.DeltaSeconds(), 0.0F, std::max(fixed.maxFrameDeltaSeconds, step));
    std::uint32_t steps = 0U;
    while (navigation.stepAccumulator >= step && steps < maxSteps) {
        navigation.stepAccumulator -= step;
        ++steps;
    }
    if (steps == maxSteps) navigation.stepAccumulator = std::min(navigation.stepAccumulator, step);
    if (steps != 0U) StepSceneNavigation(scene, step, steps);
}

SceneNavigation::SceneNavigation(Scene& scene) noexcept : scene_(scene) {}

void SceneNavigation::SetOrigin(const kb::math::DVec3& origin) {
    SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    navigation.origin = origin;
    if (navigation.polygons) navigation.polygons->SetOrigin(origin);
}

kb::math::DVec3 SceneNavigation::Origin() const noexcept {
    return SceneAccess::State(scene_).navigation.origin;
}

std::vector<kb::math::Vec3> SceneNavigation::AgentPath(SceneEntity agent) const {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return HasPolygons(navigation) && navigation.crowd ? navigation.crowd->Corners(agent.Id()) : std::vector<kb::math::Vec3>{};
}

std::uint64_t SceneNavigation::AddNavMesh(std::shared_ptr<const kb::navigation::NavMeshAsset> asset, std::string* error) {
    std::string message;
    const std::uint64_t handle = Polygons(SceneAccess::State(scene_).navigation).AddTileSet(std::move(asset), message);
    if (error != nullptr) *error = std::move(message);
    return handle;
}

bool SceneNavigation::RemoveNavMesh(std::uint64_t handle) {
    SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.polygons && navigation.polygons->RemoveTileSet(handle);
}

bool SceneNavigation::HasNavMesh() const noexcept {
    return HasPolygons(SceneAccess::State(scene_).navigation);
}

std::vector<kb::navigation::NavAgentProfile> SceneNavigation::Profiles() const {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    if (!HasPolygons(navigation) || navigation.polygons->Layout() == nullptr) return {};
    return navigation.polygons->Layout()->profiles;
}

std::size_t SceneNavigation::NavMeshTileCount(std::uint32_t profile) const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.polygons ? navigation.polygons->ActiveTileCount(profile) : 0U;
}

bool SceneNavigation::HasNavMeshTile(std::uint32_t profile, kb::navigation::NavTileCoord coord) const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.polygons && navigation.polygons->HasTile(profile, coord);
}

std::size_t SceneNavigation::NavMeshTileRebuilds() const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.polygons ? navigation.polygons->TileRebuilds() : 0U;
}

std::size_t SceneNavigation::RebakeTiles(const kb::math::DVec3& min, const kb::math::DVec3& max, kb::navigation::INavGeometrySource* geometry) {
    SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    if (!HasPolygons(navigation) || navigation.polygons->Layout() == nullptr) return 0U;
    const nav::NavMeshBuildSettings settings = *navigation.polygons->Layout();
    // The scene's objects as they stand now, read the way a bake reads a saved scene.
    const SceneDocument document = SceneDocumentService::Capture(scene_, "navigation");
    nav::AssetNavGeometrySource assets{ scene_.Assets().Manager() };
    nav::NavGeometry collected;
    nav::NavGeometryCollectStats stats;
    nav::CollectNavGeometry(document.worldPrefab.Nodes(), settings, geometry != nullptr ? geometry : &assets, collected, stats);
    const double tileSize = settings.TileWorldSize();
    const auto tile = [tileSize](double value) { return static_cast<std::int64_t>(std::floor(value / tileSize)); };
    const std::int64_t firstX = tile(std::min(min.x, max.x));
    const std::int64_t lastX = tile(std::max(min.x, max.x));
    const std::int64_t firstZ = tile(std::min(min.z, max.z));
    const std::int64_t lastZ = tile(std::max(min.z, max.z));
    if (lastX - firstX > 1024 || lastZ - firstZ > 1024) {
        throw std::invalid_argument("RebakeTiles covers more than 1024 tiles along an axis");
    }
    struct TileJob {
        std::uint32_t profile = 0U;
        nav::NavTileCoord coord{};
        nav::NavTile tile{};
        bool built = false;
    };
    std::vector<TileJob> jobs;
    for (std::uint32_t profile = 0U; profile < settings.profiles.size(); ++profile) {
        for (std::int64_t z = firstZ; z <= lastZ; ++z) {
            for (std::int64_t x = firstX; x <= lastX; ++x) jobs.push_back(TileJob{ .profile = profile, .coord = nav::NavTileCoord{ x, z } });
        }
    }
    // The tiles are rasterised on the scene's worker pool, then used in order.
    SceneState& state = SceneAccess::State(scene_);
    EnsureSceneTransformWorkerPool(state);
    state.transformWorkerPool->ParallelForChunks(jobs.size(), 1U, [&jobs, &settings, &collected](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count; ++index) {
            TileJob& job = jobs[index];
            std::string error;
            job.built = nav::NavMeshBuilder::BuildTile(settings, job.profile, job.coord, collected, job.tile, error);
        }
    });
    std::size_t rebuilt = 0U;
    for (TileJob& job : jobs) {
        if (job.built && navigation.polygons->UseRebuiltTile(std::move(job.tile))) ++rebuilt;
    }
    return rebuilt;
}

void SceneNavigation::RestoreBakedTiles() {
    SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    if (navigation.polygons) static_cast<void>(navigation.polygons->DropRebuiltTiles());
}

NavPathResult SceneNavigation::FindPath(const kb::math::DVec3& start, const kb::math::DVec3& end, const NavQueryOptions& options) {
    SceneState& state = SceneAccess::State(scene_);
    SceneNavigationState& navigation = state.navigation;
    NavPathResult result;
    if (!HasPolygons(navigation) || options.profile >= navigation.polygons->ProfileCount()) return result;
    SynchronizeOverlays(scene_, state);
    nav::NavAreaFilter filter;
    navigation.polygons->ConfigureFilter(filter, options.areas);
    const kb::math::DVec3& origin = navigation.origin;
    const nav::NavStraightPath path = navigation.polygons->FindPath(options.profile, kb::math::RelativeTo(start, origin),
        kb::math::RelativeTo(end, origin), options.searchExtents, filter);
    result.status = path.status;
    for (std::size_t corner = 0U; corner < path.corners.size(); ++corner) {
        result.corners.push_back(origin + path.corners[corner]);
        if (corner != 0U) result.length += kb::math::Distance(path.corners[corner - 1U], path.corners[corner]);
    }
    return result;
}

NavRaycastResult SceneNavigation::Raycast(const kb::math::DVec3& start, const kb::math::DVec3& end, const NavQueryOptions& options) {
    SceneState& state = SceneAccess::State(scene_);
    SceneNavigationState& navigation = state.navigation;
    NavRaycastResult result;
    if (!HasPolygons(navigation) || options.profile >= navigation.polygons->ProfileCount()) return result;
    SynchronizeOverlays(scene_, state);
    nav::NavAreaFilter filter;
    navigation.polygons->ConfigureFilter(filter, options.areas);
    const kb::math::DVec3& origin = navigation.origin;
    const nav::NavRaycast ray = navigation.polygons->Raycast(options.profile, kb::math::RelativeTo(start, origin), kb::math::RelativeTo(end, origin),
        options.searchExtents, filter);
    result.valid = ray.valid;
    result.hit = ray.hit;
    result.fraction = ray.fraction;
    result.position = origin + ray.position;
    result.normal = ray.normal;
    return result;
}

std::optional<kb::math::DVec3> SceneNavigation::NearestPoint(const kb::math::DVec3& position, const NavQueryOptions& options) {
    SceneState& state = SceneAccess::State(scene_);
    SceneNavigationState& navigation = state.navigation;
    if (!HasPolygons(navigation) || options.profile >= navigation.polygons->ProfileCount()) return std::nullopt;
    SynchronizeOverlays(scene_, state);
    nav::NavAreaFilter filter;
    navigation.polygons->ConfigureFilter(filter, options.areas);
    const auto nearest = navigation.polygons->Nearest(options.profile, kb::math::RelativeTo(position, navigation.origin), options.searchExtents, filter);
    if (!nearest) return std::nullopt;
    return navigation.origin + nearest->second;
}

std::vector<kb::math::DVec3> SceneNavigation::NavMeshTriangles(std::uint32_t profile) const {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    std::vector<kb::math::DVec3> triangles;
    if (!HasPolygons(navigation)) return triangles;
    for (const Vec3& corner : navigation.polygons->DebugTriangles(profile)) triangles.push_back(navigation.origin + corner);
    return triangles;
}

double SceneNavigation::NavMeshWalkableArea(std::uint32_t profile) const {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return HasPolygons(navigation) ? navigation.polygons->WalkableArea(profile) : 0.0;
}

void SceneNavigation::SetAreaCost(NavAreaId area, float cost) noexcept {
    Polygons(SceneAccess::State(scene_).navigation).SetAreaCost(area, cost);
}

float SceneNavigation::AreaCost(NavAreaId area) const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    if (!IsValidNavArea(area)) return 0.0F;
    return navigation.polygons ? navigation.polygons->AreaCost(area) : 1.0F;
}

void SceneNavigation::ConfigureCrowd(const NavigationCrowdSettings& settings) {
    if (!std::isfinite(settings.nearDistance) || settings.nearDistance < 0.0F || settings.farPathOptimizationInterval == 0U ||
        settings.maxPathRequestsPerStep == 0U || !std::isfinite(settings.neighbourRangeRadii) || settings.neighbourRangeRadii <= 0.0F ||
        !std::isfinite(settings.separationWeight) || settings.separationWeight < 0.0F) {
        throw std::invalid_argument("Crowd settings need a non-negative near distance, a positive optimisation interval, request budget and range");
    }
    SceneAccess::State(scene_).navigation.crowdSettings = settings;
}

NavigationCrowdSettings SceneNavigation::CrowdSettings() const noexcept {
    return SceneAccess::State(scene_).navigation.crowdSettings;
}

NavigationCrowdStats SceneNavigation::CrowdStats() const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.crowd ? navigation.crowd->Stats() : NavigationCrowdStats{};
}

void SceneNavigation::SetCrowdFocuses(std::vector<kb::math::DVec3> focuses) {
    SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    navigation.crowdFocuses = std::move(focuses);
    navigation.crowdFocusesSet = true;
}

bool SceneNavigation::AgentOnLink(SceneEntity agent) const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.crowd && navigation.crowd->OnLink(agent.Id());
}

} // namespace kb::scene
