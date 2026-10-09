#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/math/DVec3.hpp"
#include "engine/navigation/NavGeometryCollector.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/navigation/NavMeshBuild.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "engine/scene/SceneNavigationComponents.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/world/WorldCellBuilder.hpp"
#include "engine/world/WorldCellIndex.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "engine/world/WorldObjectFile.hpp"
#include "engine/world/WorldPartitionRuntime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace kb::tests {
namespace {

namespace nav = kb::navigation;
namespace scene = kb::scene;
using kb::math::DVec3;
using kb::math::Vec3;

constexpr float kFrame = 1.0F / 60.0F;

void Check(bool condition, const std::string& message) {
    Require(condition, message.c_str());
}

[[nodiscard]] std::filesystem::path FreshDirectory(std::string_view label) {
    static std::atomic<unsigned> counter{ 0U };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("kb_nav_" + std::string{ label } + "_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1U)));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

[[nodiscard]] nav::NavMeshBuildSettings Settings() {
    nav::NavMeshBuildSettings settings;
    settings.cellSize = 0.2F;
    settings.cellHeight = 0.1F;
    settings.tileCells = 64U;
    return settings;
}

// A static box collider placed at `position` (its centre) with an optional rotation.
[[nodiscard]] scene::ScenePrefabNodeDesc BoxNode(DVec3 position, Vec3 size, kb::math::Quat rotation = {}) {
    scene::ScenePrefabNodeDesc node;
    node.name = "Box";
    node.SetLocalTranslation(position);
    node.transform.localRotation = rotation;
    node.components.collider = scene::ColliderComponent{ .shape = scene::ColliderShape::Box, .boxSize = size };
    return node;
}

// A walkable slab whose top face lies at `top`, spanning [minX, maxX] x [minZ, maxZ].
[[nodiscard]] scene::ScenePrefabNodeDesc Slab(double minX, double maxX, double minZ, double maxZ, double top, double thickness = 0.5) {
    return BoxNode(DVec3{ (minX + maxX) * 0.5, top - thickness * 0.5, (minZ + maxZ) * 0.5 },
        Vec3{ static_cast<float>(maxX - minX), static_cast<float>(thickness), static_cast<float>(maxZ - minZ) });
}

// A single-sided quad, corners in order.
void AddQuad(nav::NavGeometry& geometry, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    nav::NavGeometryChunk chunk;
    chunk.vertices = { a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, d.x, d.y, d.z };
    chunk.indices = { 0U, 1U, 2U, 0U, 2U, 3U };
    geometry.Add(std::move(chunk));
}

[[nodiscard]] nav::NavMeshAsset BakeGeometry(const nav::NavGeometry& geometry, const nav::NavMeshBuildSettings& settings = Settings(),
    kb::ecs::WorkerPool* workers = nullptr) {
    const nav::NavMeshBakeResult baked = nav::NavMeshBuilder::Bake(settings, geometry, workers);
    Check(baked.succeeded, "the navigation bake must succeed: " + baked.error);
    nav::NavMeshAsset asset;
    asset.settings = settings;
    asset.tiles = baked.tiles;
    Check(nav::NavMeshAssetIO::Validate(asset).empty(), "a baked navigation mesh must validate: " + nav::NavMeshAssetIO::Validate(asset));
    return asset;
}

[[nodiscard]] nav::NavMeshAsset BakeNodes(const std::vector<scene::ScenePrefabNodeDesc>& nodes, const nav::NavMeshBuildSettings& settings = Settings()) {
    nav::NavGeometry geometry;
    nav::NavGeometryCollectStats stats;
    nav::CollectNavGeometry(nodes, settings, nullptr, geometry, stats);
    Check(stats.colliders == nodes.size(), "every static box collider must be collected");
    return BakeGeometry(geometry, settings);
}

[[nodiscard]] std::uint64_t AddMesh(scene::Scene& target, const nav::NavMeshAsset& asset) {
    std::string error;
    const std::uint64_t handle = target.Navigation().AddNavMesh(std::make_shared<nav::NavMeshAsset>(asset), &error);
    Check(handle != 0U, "the navigation mesh must be accepted: " + error);
    return handle;
}

[[nodiscard]] scene::SceneEntity AddAgent(scene::Scene& target, DVec3 position, Vec3 destination, float radius = 0.4F, float speed = 3.0F) {
    const scene::SceneObject object = target.Entities().CreateObject(scene::SceneObjectDesc{ .name = "Agent" });
    target.Transforms().SetLocalTranslation(object.Entity(), position);
    target.Components().NavAgents().Set(object.Entity(), scene::NavAgent{
        .radius = radius, .maxSpeed = speed, .acceleration = 12.0F, .stoppingDistance = 0.1F, .destination = destination });
    target.Runtime().SynchronizeTransforms();
    return object.Entity();
}

[[nodiscard]] scene::SceneEntity AddObstacle(scene::Scene& target, Vec3 position, scene::NavObstacle obstacle) {
    const scene::SceneObject object = target.Entities().CreateObject(scene::SceneObjectDesc{
        .name = "Obstacle", .transform = scene::TransformComponent{ .localPosition = position } });
    target.Components().NavObstacles().Set(object.Entity(), obstacle);
    target.Runtime().SynchronizeTransforms();
    return object.Entity();
}

[[nodiscard]] scene::SceneEntity AddLink(scene::Scene& target, Vec3 position, scene::NavLink link) {
    const scene::SceneObject object = target.Entities().CreateObject(scene::SceneObjectDesc{
        .name = "Link", .transform = scene::TransformComponent{ .localPosition = position } });
    target.Components().NavLinks().Set(object.Entity(), link);
    target.Runtime().SynchronizeTransforms();
    return object.Entity();
}

[[nodiscard]] Vec3 Position(scene::Scene& target, scene::SceneEntity entity) {
    return kb::math::ToVec3(target.Transforms().WorldTranslation(entity));
}

[[nodiscard]] float Horizontal(Vec3 a, Vec3 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
}

[[nodiscard]] const scene::NavAgent& Agent(scene::Scene& target, scene::SceneEntity entity) {
    const scene::NavAgent* agent = target.Components().NavAgents().TryGet(entity);
    Require(agent != nullptr, "the navigation agent lost its component");
    return *agent;
}

[[nodiscard]] scene::NavPathResult Path(scene::Scene& target, Vec3 from, Vec3 to, std::uint32_t profile = 0U) {
    return target.Navigation().FindPath(kb::math::ToDVec3(from), kb::math::ToDVec3(to), scene::NavQueryOptions{ .profile = profile });
}

void TestSettingsAndAssetFormat() {
    nav::NavMeshBuildSettings settings = Settings();
    Check(nav::ValidateNavMeshBuildSettings(settings).empty(), "the default navigation settings are valid");
    nav::NavMeshBuildSettings invalid = settings;
    invalid.tileCells = 300U;
    Check(!nav::ValidateNavMeshBuildSettings(invalid).empty(), "tiles wider than a layer can hold are refused");
    invalid = settings;
    invalid.profiles.push_back(invalid.profiles.front());
    Check(!nav::ValidateNavMeshBuildSettings(invalid).empty(), "two agent profiles may not share a name");
    invalid = settings;
    invalid.profiles.front().maxSlopeDegrees = 95.0F;
    Check(!nav::ValidateNavMeshBuildSettings(invalid).empty(), "slopes beyond 89 degrees are refused");

    const nav::NavMeshAsset asset = BakeNodes({ Slab(-6.0, 6.0, -6.0, 6.0, 0.0) });
    Check(!asset.tiles.empty(), "a floor bakes into tiles");
    const std::vector<std::uint8_t> bytes = nav::NavMeshAssetIO::Serialize(asset);
    const nav::NavMeshAssetReadResult read = nav::NavMeshAssetIO::Parse(bytes);
    Check(read.succeeded && read.asset == asset, "a navigation mesh reads back exactly as written: " + read.error);
    for (std::size_t length = 0U; length < bytes.size(); length += std::max<std::size_t>(1U, bytes.size() / 97U)) {
        Check(!nav::NavMeshAssetIO::Parse(std::span{ bytes }.first(length)).succeeded, "a truncated navigation mesh is refused");
    }
    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0U);
    Check(!nav::NavMeshAssetIO::Parse(trailing).succeeded, "trailing bytes are refused");
    std::vector<std::uint8_t> version = bytes;
    version[8] = 99U;
    Check(!nav::NavMeshAssetIO::Parse(version).succeeded, "an unknown version is refused");

    // A connection leading out of the grid would make the polygon builder read outside it.
    nav::NavMeshAsset escaping = asset;
    escaping.tiles.front().layers.front().connections[0] |= 1U;
    Check(!nav::NavMeshAssetIO::Validate(escaping).empty() && nav::NavMeshAssetIO::Serialize(escaping).empty(),
        "a layer connecting a column to one outside its tile is refused");
    nav::NavMeshAsset badArea = asset;
    badArea.tiles.front().layers.front().areas[5] = 200U;
    Check(!nav::NavMeshAssetIO::Validate(badArea).empty(), "a layer with an unknown area is refused");
    nav::NavMeshAsset unsorted = asset;
    if (unsorted.tiles.size() > 1U) {
        std::swap(unsorted.tiles[0], unsorted.tiles[1]);
        Check(!nav::NavMeshAssetIO::Validate(unsorted).empty(), "tiles out of order are refused");
    }

    const std::filesystem::path root = FreshDirectory("asset");
    std::string error;
    Check(nav::NavMeshAssetIO::Write(root / "Level.21kbnavmesh", asset, error), "the navigation mesh must write: " + error);
    Check(!nav::NavMeshAssetIO::Write(root / "Level.bin", asset, error), "navigation meshes keep their extension");
    const nav::NavMeshAssetReadResult fromFile = nav::NavMeshAssetIO::Read(root / "Level.21kbnavmesh");
    Check(fromFile.succeeded && fromFile.asset == asset, "a written navigation mesh reads back");
    std::filesystem::remove_all(root);
}

void TestBakeIsDeterministic() {
    std::vector<scene::ScenePrefabNodeDesc> nodes{ Slab(-20.0, 20.0, -20.0, 20.0, 0.0) };
    for (int pillar = 0; pillar < 6; ++pillar) {
        nodes.push_back(BoxNode(DVec3{ -15.0 + pillar * 6.0, 1.0, (pillar % 2 == 0) ? 4.0 : -4.0 }, Vec3{ 1.0F, 2.0F, 1.0F }));
    }
    nav::NavGeometry geometry;
    nav::NavGeometryCollectStats stats;
    nav::CollectNavGeometry(nodes, Settings(), nullptr, geometry, stats);
    const std::vector<std::uint8_t> serial = nav::NavMeshAssetIO::Serialize(BakeGeometry(geometry, Settings()));
    kb::ecs::WorkerPool workers{ kb::ecs::WorkerPoolConfig{ .workerCount = 4U, .collectDispatchTelemetry = true } };
    const std::vector<std::uint8_t> parallel = nav::NavMeshAssetIO::Serialize(BakeGeometry(geometry, Settings(), &workers));
    const kb::ecs::WorkerPoolDispatchTelemetry telemetry = workers.DispatchTelemetry();
    Check(telemetry.dispatchCount == 1U && telemetry.lastWorkItemCount > 1U, "a bake given the engine's worker pool builds its tiles on it");
    Check(!serial.empty() && serial == parallel, "a bake gives the same bytes on the worker pool as without one");
}

void TestWalkableAreaOfAFloor() {
    // A 20 x 20 m floor: the walkable area is the floor shrunk by the agent radius (0.4 m, two cells)
    // and the ledge cell at its rim.
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes({ Slab(-10.0, 10.0, -10.0, 10.0, 0.0) })));
    const double area = scene.Navigation().NavMeshWalkableArea();
    Check(area >= 18.8 * 18.8 && area <= 19.2 * 19.2, "a floor's walkable area is the floor less the agent radius: " + std::to_string(area));
    const std::optional<DVec3> top = scene.Navigation().NearestPoint(DVec3{ 3.0, 1.5, -2.0 });
    Check(top.has_value() && std::fabs(top->y) < 0.15 && std::fabs(top->x - 3.0) < 0.01 && std::fabs(top->z + 2.0) < 0.01,
        "the nearest point above a floor lies on its top");
    const std::optional<DVec3> edge = scene.Navigation().NearestPoint(DVec3{ 9.95, 0.0, 0.0 });
    Check(edge.has_value() && edge->x < 9.65 && edge->x > 9.35, "the mesh keeps the agent radius from the floor's edge");
    Check(!scene.Navigation().NearestPoint(DVec3{ 40.0, 0.0, 0.0 }).has_value(), "nothing is found far off the mesh");
    const scene::NavPathResult straight = Path(scene, { -8.0F, 0.0F, -8.0F }, { 8.0F, 0.0F, 8.0F });
    Check(straight.status == scene::NavPathStatus::Complete && straight.corners.size() >= 2U &&
        std::fabs(straight.length - std::sqrt(2.0F) * 16.0F) < 0.05F, "an open floor gives a straight path: status " +
        std::to_string(static_cast<int>(straight.status)) + ", " + std::to_string(straight.corners.size()) + " corners, length " +
        std::to_string(straight.length));
    const scene::NavRaycastResult open = scene.Navigation().Raycast(DVec3{ 0.0, 0.0, 0.0 }, DVec3{ 5.0, 0.0, 0.0 });
    const scene::NavRaycastResult wall = scene.Navigation().Raycast(DVec3{ 0.0, 0.0, 0.0 }, DVec3{ 20.0, 0.0, 0.0 });
    Check(open.valid && !open.hit && std::fabs(open.position.x - 5.0) < 0.01, "a ray across open floor reaches its end");
    Check(wall.valid && wall.hit && std::fabs(wall.position.x - 9.6) < 0.25 && wall.normal.x < -0.9F, "a ray stops at the mesh's border");
}

// A floor, a ramp rising `degrees` to a 3 m high platform, and the platform.
[[nodiscard]] scene::NavPathStatus RampPath(float degrees) {
    const float rise = 3.0F;
    const float run = rise / std::tan(degrees * 3.14159265F / 180.0F);
    nav::NavGeometry geometry;
    AddQuad(geometry, { -10.0F, 0.0F, -3.0F }, { -10.0F, 0.0F, 3.0F }, { 0.0F, 0.0F, 3.0F }, { 0.0F, 0.0F, -3.0F });
    AddQuad(geometry, { 0.0F, 0.0F, -3.0F }, { 0.0F, 0.0F, 3.0F }, { run, rise, 3.0F }, { run, rise, -3.0F });
    AddQuad(geometry, { run, rise, -3.0F }, { run, rise, 3.0F }, { run + 10.0F, rise, 3.0F }, { run + 10.0F, rise, -3.0F });
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeGeometry(geometry)));
    const scene::NavPathResult path = Path(scene, { -6.0F, 0.0F, 0.0F }, { run + 6.0F, rise, 0.0F });
    Check(path.status != scene::NavPathStatus::Failed, "both ends of the ramp scene are on the mesh");
    return path.status;
}

// Steps of `rise` metres, 1 m deep, from a floor up to a landing.
[[nodiscard]] scene::NavPathStatus StairPath(double rise) {
    std::vector<scene::ScenePrefabNodeDesc> nodes{ Slab(-10.0, 0.0, -3.0, 3.0, 0.0) };
    constexpr int kSteps = 5;
    for (int step = 0; step < kSteps; ++step) {
        nodes.push_back(Slab(step, step + 1.0, -3.0, 3.0, rise * (step + 1), rise * (step + 1) + 0.5));
    }
    nodes.push_back(Slab(kSteps, kSteps + 10.0, -3.0, 3.0, rise * kSteps, rise * kSteps + 0.5));
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes(nodes)));
    const scene::NavPathResult path = Path(scene, { -6.0F, 0.0F, 0.0F }, { kSteps + 6.0F, static_cast<float>(rise * kSteps), 0.0F });
    Check(path.status != scene::NavPathStatus::Failed, "both ends of the stairs are on the mesh");
    return path.status;
}

void TestSlopesAndSteps() {
    Check(RampPath(30.0F) == scene::NavPathStatus::Complete, "a 30 degree ramp is walkable for a 45 degree agent");
    Check(RampPath(60.0F) == scene::NavPathStatus::Partial, "a 60 degree ramp is not walkable for a 45 degree agent");
    Check(StairPath(0.3) == scene::NavPathStatus::Complete, "steps lower than the agent's climb are walked up");
    Check(StairPath(0.6) == scene::NavPathStatus::Partial, "steps higher than the agent's climb block the way");
}

void TestDynamicObstaclesCarveTiles() {
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes({ Slab(-15.0, 15.0, -3.0, 3.0, 0.0) })));
    const Vec3 from{ -12.0F, 0.0F, 0.0F };
    const Vec3 to{ 12.0F, 0.0F, 0.0F };
    Check(Path(scene, from, to).status == scene::NavPathStatus::Complete && std::fabs(Path(scene, from, to).length - 24.0F) < 0.05F,
        "the open corridor gives a straight path");
    const std::size_t rebuildsBefore = scene.Navigation().NavMeshTileRebuilds();
    const scene::SceneEntity wall = AddObstacle(scene, { 0.0F, 0.5F, 0.0F },
        scene::NavObstacle{ .shape = scene::NavObstacleShape::Box, .size = { 1.0F, 2.0F, 8.0F }, .carve = true });
    Check(Path(scene, from, to).status == scene::NavPathStatus::Partial, "a carving obstacle across the corridor cuts the path");
    Check(scene.Navigation().NavMeshTileRebuilds() > rebuildsBefore, "the tiles under the obstacle are rebuilt");
    // A narrower obstacle leaves a gap at one side: the path goes around it.
    scene.Components().NavObstacles().TryGet(wall)->size = Vec3{ 1.0F, 2.0F, 3.0F };
    scene.Runtime().SynchronizeTransforms();
    const scene::NavPathResult around = Path(scene, from, to);
    Check(around.status == scene::NavPathStatus::Complete && around.length > 24.1F &&
        std::ranges::any_of(around.corners, [](const DVec3& corner) { return std::fabs(corner.z) > 1.8; }),
        "a path bends around a carving obstacle");
    // Rotating the box opens the gap on the other side.
    scene::TransformComponent rotated = scene.Transforms().Get(wall);
    const float half = 15.0F * 3.14159265F / 180.0F;
    rotated.localRotation = kb::math::Quat{ 0.0F, std::sin(half), 0.0F, std::cos(half) };
    scene.Transforms().Set(wall, rotated);
    scene.Runtime().SynchronizeTransforms();
    Check(Path(scene, from, to).status == scene::NavPathStatus::Complete, "a rotated obstacle carves its rotated footprint");

    // An agent walking the corridor goes around the obstacle and arrives.
    const scene::SceneEntity agent = AddAgent(scene, DVec3{ -12.0, 0.0, 0.0 }, to);
    for (int frame = 0; frame < 60 * 15; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        const Vec3 position = Position(scene, agent);
        Check(!(std::fabs(position.x) < 0.5F && std::fabs(position.z) < 1.2F), "an agent walked through a carving obstacle");
    }
    Check(Horizontal(Position(scene, agent), to) <= 0.15F && Agent(scene, agent).pathStatus == scene::NavPathStatus::Complete,
        "the agent walks around the obstacle to its destination");

    // Disabling the obstacle restores the straight corridor.
    scene.Components().NavObstacles().TryGet(wall)->enabled = false;
    Check(std::fabs(Path(scene, from, to).length - 24.0F) < 0.05F, "a disabled obstacle no longer carves");
}

[[nodiscard]] scene::SceneEntity AddCollider(scene::Scene& target, DVec3 center, Vec3 size) {
    const scene::SceneObject object = target.Entities().CreateObject(scene::SceneObjectDesc{ .name = "Geometry" });
    target.Transforms().SetLocalTranslation(object.Entity(), center);
    target.Components().Colliders().Set(object.Entity(), scene::ColliderComponent{ .shape = scene::ColliderShape::Box, .boxSize = size });
    target.Runtime().SynchronizeTransforms();
    return object.Entity();
}

void TestGeometryChangesRebuildTiles() {
    // A corridor closed by a wall; the live scene holds the same colliders the mesh was baked from.
    const std::vector<scene::ScenePrefabNodeDesc> nodes{ Slab(-10.0, 10.0, -3.0, 3.0, 0.0), BoxNode(DVec3{ 0.0, 1.5, 0.0 }, Vec3{ 0.5F, 3.0F, 6.0F }) };
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes(nodes)));
    static_cast<void>(AddCollider(scene, DVec3{ 0.0, -0.25, 0.0 }, Vec3{ 20.0F, 0.5F, 6.0F }));
    const scene::SceneEntity wall = AddCollider(scene, DVec3{ 0.0, 1.5, 0.0 }, Vec3{ 0.5F, 3.0F, 6.0F });
    const Vec3 from{ -8.0F, 0.0F, 0.0F };
    const Vec3 to{ 8.0F, 0.0F, 0.0F };
    Check(Path(scene, from, to).status == scene::NavPathStatus::Partial, "the baked wall closes the corridor");
    // The wall is knocked down while the scene runs: the tiles around it are rasterised again.
    scene.Entities().Destroy(wall);
    const std::size_t rebuilt = scene.Navigation().RebakeTiles(DVec3{ -1.0, 0.0, -4.0 }, DVec3{ 1.0, 0.0, 4.0 });
    Check(rebuilt > 0U, "tiles around the changed geometry are rebuilt");
    const scene::NavPathResult open = Path(scene, from, to);
    Check(open.status == scene::NavPathStatus::Complete && std::fabs(open.length - 16.0F) < 0.05F, "the rebuilt tiles open the corridor");
    // A new wall elsewhere closes it again once its tiles are rebuilt.
    static_cast<void>(AddCollider(scene, DVec3{ 4.0, 1.5, 0.0 }, Vec3{ 0.5F, 3.0F, 6.0F }));
    Check(Path(scene, from, to).status == scene::NavPathStatus::Complete, "geometry the mesh has not been rebuilt for does not change it");
    static_cast<void>(scene.Navigation().RebakeTiles(DVec3{ 3.0, 0.0, -4.0 }, DVec3{ 5.0, 0.0, 4.0 }));
    Check(Path(scene, from, to).status == scene::NavPathStatus::Partial, "a wall built at runtime closes the corridor once its tiles are rebuilt");
    scene.Navigation().RestoreBakedTiles();
    Check(Path(scene, from, to).status == scene::NavPathStatus::Partial && Path(scene, from, { -1.0F, 0.0F, 0.0F }).status == scene::NavPathStatus::Complete,
        "restoring the baked tiles brings the baked wall back");
}

void TestAreasAndCosts() {
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes({ Slab(-10.0, 10.0, -10.0, 10.0, 0.0) })));
    // Area 3 (say, mud) covers a strip across the middle, open only near z = 10.
    static_cast<void>(AddObstacle(scene, { 0.0F, 0.0F, -2.0F },
        scene::NavObstacle{ .shape = scene::NavObstacleShape::Box, .size = { 2.0F, 2.0F, 16.0F }, .area = 3U, .carve = false }));
    const Vec3 from{ -8.0F, 0.0F, 0.0F };
    const Vec3 to{ 8.0F, 0.0F, 0.0F };
    Check(std::fabs(Path(scene, from, to).length - 16.0F) < 0.05F, "an area with the default cost is crossed directly");
    scene.Navigation().SetAreaCost(3U, 50.0F);
    Check(scene.Navigation().AreaCost(3U) == 50.0F, "area costs are kept");
    const scene::NavPathResult detour = Path(scene, from, to);
    Check(detour.status == scene::NavPathStatus::Complete && detour.length > 20.0F, "an expensive area is walked around");
    scene.Navigation().SetAreaCost(3U, 1.0F);
    const scene::NavPathResult excluded = scene.Navigation().FindPath(kb::math::ToDVec3(from), kb::math::ToDVec3(to),
        scene::NavQueryOptions{ .areas = scene::kAllNavAreas & ~scene::NavAreaBit(3U) });
    Check(excluded.status == scene::NavPathStatus::Complete && excluded.length > 20.0F, "an excluded area is never entered");

    // An agent that may not enter the area walks around it too.
    const scene::SceneEntity agent = AddAgent(scene, DVec3{ -8.0, 0.0, 0.0 }, to);
    scene.Components().NavAgents().TryGet(agent)->areaMask = scene::kAllNavAreas & ~scene::NavAreaBit(3U);
    float farthestZ = -100.0F;
    for (int frame = 0; frame < 60 * 15; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        farthestZ = std::max(farthestZ, Position(scene, agent).z);
    }
    Check(farthestZ > 6.0F && Horizontal(Position(scene, agent), to) <= 0.15F, "an agent keeps out of areas outside its mask");
}

void TestMultipleAgentSizes() {
    nav::NavMeshBuildSettings settings = Settings();
    settings.profiles = { nav::NavAgentProfile{ .name = "Small", .radius = 0.3F }, nav::NavAgentProfile{ .name = "Large", .radius = 1.0F } };
    // Two rooms joined by a 1.4 m doorway in a wall at x = 0.
    const std::vector<scene::ScenePrefabNodeDesc> nodes{
        Slab(-10.0, 10.0, -10.0, 10.0, 0.0),
        BoxNode(DVec3{ 0.0, 1.5, -5.35 }, Vec3{ 0.4F, 3.0F, 9.3F }),
        BoxNode(DVec3{ 0.0, 1.5, 5.35 }, Vec3{ 0.4F, 3.0F, 9.3F }),
    };
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes(nodes, settings)));
    Check(scene.Navigation().Profiles().size() == 2U && scene.Navigation().NavMeshTileCount(0U) == scene.Navigation().NavMeshTileCount(1U),
        "each agent profile has its own tiles");
    const Vec3 from{ -6.0F, 0.0F, 0.0F };
    const Vec3 to{ 6.0F, 0.0F, 0.0F };
    Check(Path(scene, from, to, 0U).status == scene::NavPathStatus::Complete, "a small agent passes the doorway");
    Check(Path(scene, from, to, 1U).status == scene::NavPathStatus::Partial, "a large agent does not fit through the doorway");
    const scene::SceneEntity small = AddAgent(scene, DVec3{ -6.0, 0.0, 0.0 }, to, 0.25F);
    const scene::SceneEntity large = AddAgent(scene, DVec3{ -6.0, 0.0, 3.0 }, to, 0.8F);
    for (int frame = 0; frame < 60 * 10; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Horizontal(Position(scene, small), to) <= 0.15F, "a small agent walks through the doorway");
    Check(Agent(scene, large).pathStatus == scene::NavPathStatus::Partial && Position(scene, large).x < -0.5F,
        "an agent wider than the small profile uses the large one and stays in its room");
}

[[nodiscard]] std::vector<scene::ScenePrefabNodeDesc> TwoPlatforms(double secondTop) {
    return { Slab(-10.0, 0.0, -3.0, 3.0, 0.0), Slab(3.0, 13.0, -3.0, 3.0, secondTop) };
}

void TestOffMeshLinks() {
    for (const scene::NavLinkKind kind : { scene::NavLinkKind::Jump, scene::NavLinkKind::Ladder }) {
        const double top = kind == scene::NavLinkKind::Jump ? 1.0 : 3.0;
        scene::Scene scene;
        static_cast<void>(AddMesh(scene, BakeNodes(TwoPlatforms(top))));
        const Vec3 from{ -6.0F, 0.0F, 0.0F };
        const Vec3 to{ 8.0F, static_cast<float>(top), 0.0F };
        Check(Path(scene, from, to).status == scene::NavPathStatus::Partial, "without a link the platforms are apart");
        const scene::SceneEntity link = AddLink(scene, { -0.8F, 0.0F, 0.0F }, scene::NavLink{
            .start = {}, .end = { 4.6F, static_cast<float>(top), 0.0F }, .radius = 0.6F, .kind = kind, .bidirectional = false });
        const scene::NavPathResult linked = Path(scene, from, to);
        Check(linked.status == scene::NavPathStatus::Complete && linked.corners.size() >= 4U, "a link joins the platforms");
        Check(Path(scene, to, from).status == scene::NavPathStatus::Partial, "a one-way link is not taken backwards");

        const scene::SceneEntity agent = AddAgent(scene, DVec3{ -6.0, 0.0, 0.0 }, to);
        bool crossed = false;
        float highest = 0.0F;
        bool climbedInPlace = false;
        for (int frame = 0; frame < 60 * 12; ++frame) {
            static_cast<void>(scene.Runtime().Update(kFrame));
            const Vec3 position = Position(scene, agent);
            if (scene.Navigation().AgentOnLink(agent)) {
                crossed = true;
                highest = std::max(highest, position.y);
                climbedInPlace = climbedInPlace || (position.x < 0.0F && position.y > 1.0F);
            }
        }
        Check(crossed, "the agent crosses the link");
        Check(Horizontal(Position(scene, agent), to) <= 0.15F && std::fabs(Position(scene, agent).y - static_cast<float>(top)) < 0.2F &&
            Agent(scene, agent).pathStatus == scene::NavPathStatus::Complete, "the agent arrives on the far platform");
        if (kind == scene::NavLinkKind::Jump) {
            Check(highest > static_cast<float>(top) + 0.2F, "a jump arcs above both ends");
        } else {
            Check(climbedInPlace, "a ladder is climbed before stepping across");
        }
        scene.Components().NavLinks().TryGet(link)->enabled = false;
        Check(Path(scene, from, to).status == scene::NavPathStatus::Partial, "a disabled link no longer joins the platforms");
    }
}

[[nodiscard]] DVec3 WalkFar(const DVec3& origin) {
    scene::Scene scene;
    scene::NavMesh graph;
    graph.origin = origin;
    scene.Navigation().SetMesh(graph);
    std::vector<scene::ScenePrefabNodeDesc> nodes{ Slab(origin.x - 10.0, origin.x + 10.0, origin.z - 5.0, origin.z + 5.0, 0.0) };
    static_cast<void>(AddMesh(scene, BakeNodes(nodes)));
    const scene::SceneEntity agent = AddAgent(scene, origin + DVec3{ -6.0, 0.0, 0.0 }, Vec3{ 6.0F, 0.0F, 0.0F }, 0.4F, 2.0F);
    for (int frame = 0; frame < 60; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    return scene.Transforms().WorldTranslation(agent) - origin;
}

void TestFarFromTheWorldOrigin() {
    // Ten thousand kilometres out; tiles, queries and agents work in the space of NavMesh::origin.
    const DVec3 far{ 10'000'000.25, 0.0, -10'000'000.5 };
    const DVec3 near = WalkFar({});
    const DVec3 there = WalkFar(far);
    Check(near.x > -5.0 && near.x < -3.9, "an agent walks over a polygon mesh near the origin");
    Check(kb::math::Length(there - near) <= 1.0e-3, "a far polygon mesh moves its agents as precisely as one at the origin");
}

struct CrowdRun {
    std::vector<Vec3> positions;
    std::size_t arrived = 0U;
    double milliseconds = 0.0;
};

[[nodiscard]] nav::NavMeshAsset CrowdArena(double half) {
    std::vector<scene::ScenePrefabNodeDesc> nodes{ Slab(-half, half, -half, half, 0.0) };
    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            nodes.push_back(BoxNode(DVec3{ x * half * 0.3, 1.0, z * half * 0.3 }, Vec3{ 2.0F, 2.0F, 2.0F }));
        }
    }
    nav::NavMeshBuildSettings settings = Settings();
    settings.cellSize = 0.3F;
    settings.tileCells = 96U;
    return BakeNodes(nodes, settings);
}

// `count` agents on a grid each walk to the mirrored point across the arena's centre.
[[nodiscard]] CrowdRun RunCrowd(const std::shared_ptr<const nav::NavMeshAsset>& arena, double half, int count, int steps,
    std::vector<DVec3> focuses = {}) {
    scene::Scene scene;
    std::string error;
    Check(scene.Navigation().AddNavMesh(arena, &error) != 0U, "the crowd arena must load: " + error);
    if (!focuses.empty()) scene.Navigation().SetCrowdFocuses(std::move(focuses));
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    const double spacing = (half * 1.6) / side;
    std::vector<scene::SceneEntity> agents;
    for (int index = 0; index < count; ++index) {
        const double x = -half * 0.8 + spacing * (index % side + 0.5);
        const double z = -half * 0.8 + spacing * (index / side + 0.5);
        const scene::SceneObject object = scene.Entities().CreateObject(scene::SceneObjectDesc{ .name = "Crowd" });
        scene.Transforms().SetLocalTranslation(object.Entity(), DVec3{ x, 0.0, z });
        scene.Components().NavAgents().Set(object.Entity(), scene::NavAgent{
            .radius = 0.4F, .maxSpeed = 3.0F, .acceleration = 10.0F, .stoppingDistance = 0.3F,
            .destination = Vec3{ static_cast<float>(-x), 0.0F, static_cast<float>(-z) } });
        agents.push_back(object.Entity());
    }
    scene.Runtime().SynchronizeTransforms();
    CrowdRun run;
    for (int step = 0; step < steps; ++step) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        if (step >= steps / 2) run.milliseconds += scene.Navigation().CrowdStats().milliseconds;
    }
    run.milliseconds /= static_cast<double>(steps - steps / 2);
    for (const scene::SceneEntity agent : agents) {
        run.positions.push_back(Position(scene, agent));
        const scene::NavAgent& component = Agent(scene, agent);
        run.arrived += component.remainingDistance <= 0.5F ? 1U : 0U;
    }
    return run;
}

void TestCrowdIsDeterministic() {
    constexpr double kHalf = 60.0;
    const auto arena = std::make_shared<const nav::NavMeshAsset>(CrowdArena(kHalf));
    constexpr int kAgents = 1200;
    const CrowdRun first = RunCrowd(arena, kHalf, kAgents, 300);
    const CrowdRun second = RunCrowd(arena, kHalf, kAgents, 300);
    Check(first.positions.size() == static_cast<std::size_t>(kAgents), "every crowd agent is reported");
    Check(std::memcmp(first.positions.data(), second.positions.data(), first.positions.size() * sizeof(Vec3)) == 0,
        "a crowd of 1200 agents replays to the same positions");
    // Every agent started 0.8 m or more from the centre line and has had five seconds to walk toward it.
    std::size_t moved = 0U;
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(kAgents))));
    const double spacing = (kHalf * 1.6) / side;
    for (int index = 0; index < kAgents; ++index) {
        const double x = -kHalf * 0.8 + spacing * (index % side + 0.5);
        const double z = -kHalf * 0.8 + spacing * (index / side + 0.5);
        moved += Horizontal(first.positions[static_cast<std::size_t>(index)], Vec3{ static_cast<float>(x), 0.0F, static_cast<float>(z) }) > 3.0F ? 1U : 0U;
    }
    Check(moved > kAgents * 9U / 10U, "the crowd makes its way: " + std::to_string(moved) + " of 1200 agents moved");
}

struct LevelOfDetailRun {
    std::size_t nearAgents = 0U;
    std::size_t farAgents = 0U;
    std::size_t velocitySamples = 0U;
    Vec3 near{};
    Vec3 far{};
};

// One agent near (-40, -40) and one near (40, 40) walk 2 s; `focuses` are the level-of-detail foci.
[[nodiscard]] LevelOfDetailRun RunLevelOfDetail(const std::shared_ptr<const nav::NavMeshAsset>& arena, std::vector<DVec3> focuses) {
    scene::Scene scene;
    static_cast<void>(scene.Navigation().AddNavMesh(arena));
    scene.Navigation().ConfigureCrowd(scene::NavigationCrowdSettings{ .nearDistance = 20.0F, .farPathOptimizationInterval = 4U });
    scene.Navigation().SetCrowdFocuses(std::move(focuses));
    const scene::SceneEntity near = AddAgent(scene, DVec3{ -40.0, 0.0, -42.0 }, Vec3{ -40.0F, 0.0F, -20.0F });
    const scene::SceneEntity far = AddAgent(scene, DVec3{ 40.0, 0.0, 38.0 }, Vec3{ 40.0F, 0.0F, 20.0F });
    LevelOfDetailRun run;
    for (int frame = 0; frame < 120; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        const scene::NavigationCrowdStats stats = scene.Navigation().CrowdStats();
        run.nearAgents += stats.nearAgents;
        run.farAgents += stats.farAgents;
        run.velocitySamples += stats.velocitySamples;
    }
    run.near = Position(scene, near);
    run.far = Position(scene, far);
    return run;
}

void TestCrowdLevelOfDetail() {
    constexpr double kHalf = 60.0;
    const auto arena = std::make_shared<const nav::NavMeshAsset>(CrowdArena(kHalf));
    const LevelOfDetailRun detailed = RunLevelOfDetail(arena, { DVec3{ -40.0, 0.0, -40.0 }, DVec3{ 40.0, 0.0, 40.0 } });
    const LevelOfDetailRun reduced = RunLevelOfDetail(arena, { DVec3{ -40.0, 0.0, -40.0 } });
    Check(detailed.nearAgents == 240U && detailed.farAgents == 0U && reduced.nearAgents == 120U && reduced.farAgents == 120U,
        "agents within the near distance of a focus are near, the others far: near " + std::to_string(reduced.nearAgents) + ", far " +
            std::to_string(reduced.farAgents));
    Check(reduced.velocitySamples > 0U && reduced.velocitySamples * 3U < detailed.velocitySamples * 2U,
        "far agents do not sample avoidance velocities: " + std::to_string(reduced.velocitySamples) + " of " + std::to_string(detailed.velocitySamples));
    Check(Horizontal(reduced.near, { -40.0F, 0.0F, -42.0F }) > 4.0F && Horizontal(reduced.far, { 40.0F, 0.0F, 38.0F }) > 4.0F,
        "near and far agents both make progress");
    Check(std::fabs(Horizontal(reduced.far, { 40.0F, 0.0F, 20.0F }) - Horizontal(detailed.far, { 40.0F, 0.0F, 20.0F })) < 0.5F,
        "a far agent with nothing to avoid covers the same ground as a near one");
}

void TestNavLinkPersists() {
    const std::filesystem::path root = FreshDirectory("persist");
    {
        scene::Scene scene;
        static_cast<void>(AddLink(scene, { 1.0F, 2.0F, 3.0F }, scene::NavLink{
            .start = { 0.5F, 0.0F, 0.0F }, .end = { 4.0F, -1.0F, 2.0F }, .radius = 0.75F, .kind = scene::NavLinkKind::Ladder, .area = 7U,
            .bidirectional = false, .enabled = true }));
        Check(scene::SceneDocumentService::Save(scene, root / "Links.21kbscene", "Links"), "a scene with a link saves");
    }
    scene::Scene loaded;
    Check(scene::SceneDocumentService::LoadFileIntoScene(loaded, root / "Links.21kbscene"), "a scene with a link loads");
    struct Found {
        scene::Scene* scene = nullptr;
        std::vector<scene::NavLink> links;
    } found{ .scene = &loaded, .links = {} };
    loaded.Transforms().ForEach([](scene::SceneEntity entity, const scene::TransformComponent&, void* context) {
        auto* search = static_cast<Found*>(context);
        if (const scene::NavLink* link = search->scene->Components().NavLinks().TryGet(entity); link != nullptr) search->links.push_back(*link);
    }, &found);
    const std::vector<scene::NavLink>& links = found.links;
    Check(links.size() == 1U && links[0].end.x == 4.0F && links[0].end.y == -1.0F && links[0].radius == 0.75F &&
        links[0].kind == scene::NavLinkKind::Ladder && links[0].area == 7U && !links[0].bidirectional,
        "a link keeps every field through a save and a load");
    std::filesystem::remove_all(root);
}

void TestNavigationScriptApi() {
    // Scripts edit links through the component API and query the mesh through Navigation.*.
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes(TwoPlatforms(1.0))));
    const scene::SceneEntity link = AddLink(scene, { -0.8F, 0.0F, 0.0F }, scene::NavLink{ .end = { 4.6F, 1.0F, 0.0F }, .radius = 0.6F, .enabled = false });
    const auto enabled = kb::script::ScriptSceneComponentApi::SetProperty(scene, link, "NavLink", "enabled",
        kb::script::ScriptValue{ true });
    Check(enabled.succeeded && scene.Components().NavLinks().TryGet(link)->enabled, "a script enables a link");
    kb::script::ScriptRuntimeHost host{ scene };
    const kb::script::ScriptFunctionCallContext call{ .scene = &scene };
    const auto point = [](std::string_view prefix, Vec3 value) {
        return std::vector<kb::script::ScriptFunctionArgument>{
            { .name = std::string{ prefix } + "X", .value = kb::script::ScriptValue{ value.x } },
            { .name = std::string{ prefix } + "Y", .value = kb::script::ScriptValue{ value.y } },
            { .name = std::string{ prefix } + "Z", .value = kb::script::ScriptValue{ value.z } } };
    };
    std::vector<kb::script::ScriptFunctionArgument> arguments = point("start", { -6.0F, 0.0F, 0.0F });
    const std::vector<kb::script::ScriptFunctionArgument> end = point("end", { 8.0F, 1.0F, 0.0F });
    arguments.insert(arguments.end(), end.begin(), end.end());
    const kb::script::ScriptFunctionCallResult path = host.Functions().Call("Navigation.FindPath", arguments, call);
    Check(path.Succeeded() && path.Output("complete").has_value() && path.Output("complete")->AsBool() && path.Output("corners")->AsInt() >= 4 &&
        std::fabs(path.Output("endX")->AsFloat() - 8.0F) < 0.01F, "Navigation.FindPath finds the linked path");
    const kb::script::ScriptFunctionCallResult corner = host.Functions().Call("Navigation.PathCorner",
        std::vector<kb::script::ScriptFunctionArgument>{ { .name = "index", .value = kb::script::ScriptValue{ 0 } } }, call);
    Check(corner.Succeeded() && corner.Output("found")->AsBool() && std::fabs(corner.Output("x")->AsFloat() + 6.0F) < 0.01F,
        "Navigation.PathCorner returns the corners of that path");
    std::vector<kb::script::ScriptFunctionArgument> ray = point("start", { -6.0F, 0.0F, 0.0F });
    const std::vector<kb::script::ScriptFunctionArgument> rayEnd = point("end", { -16.0F, 0.0F, 0.0F });
    ray.insert(ray.end(), rayEnd.begin(), rayEnd.end());
    const kb::script::ScriptFunctionCallResult cast = host.Functions().Call("Navigation.Raycast", ray, call);
    Check(cast.Succeeded() && cast.Output("hit")->AsBool() && cast.Output("x")->AsFloat() < -9.0F && cast.Output("x")->AsFloat() > -10.0F,
        "Navigation.Raycast stops at the platform's edge");
    const kb::script::ScriptFunctionCallResult nearest = host.Functions().Call("Navigation.NearestPoint", std::vector<kb::script::ScriptFunctionArgument>{
        { .name = "x", .value = kb::script::ScriptValue{ 5.0F } }, { .name = "y", .value = kb::script::ScriptValue{ 2.0F } },
        { .name = "z", .value = kb::script::ScriptValue{ 0.0F } } }, call);
    Check(nearest.Succeeded() && nearest.Output("found")->AsBool() && std::fabs(nearest.Output("y")->AsFloat() - 1.0F) < 0.15F,
        "Navigation.NearestPoint projects onto the mesh");
    const kb::script::ScriptFunctionCallResult cost = host.Functions().Call("Navigation.SetAreaCost", std::vector<kb::script::ScriptFunctionArgument>{
        { .name = "area", .value = kb::script::ScriptValue{ 4 } }, { .name = "cost", .value = kb::script::ScriptValue{ 3.5F } } }, call);
    Check(cost.Succeeded() && cost.Output("applied")->AsBool() && scene.Navigation().AreaCost(4U) == 3.5F, "Navigation.SetAreaCost sets an area's cost");
}

void TestPlacedNavigationMesh() {
    const std::filesystem::path root = FreshDirectory("placed");
    std::string error;
    Check(nav::NavMeshAssetIO::Write(root / "Navigation" / "Arena.21kbnavmesh", BakeNodes({ Slab(-10.0, 10.0, -10.0, 10.0, 0.0) }), error),
        "the arena navigation mesh must write: " + error);
    scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Check(manager.Mounts().Mount("Game", root) && manager.DiscoverMountedAssets() > 0U, "the project must mount");
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Navigation/Arena.21kbnavmesh");
    Check(metadata != nullptr && metadata->type == nav::NavMeshAsset::AssetType, "a navigation mesh file is a NavMesh asset");
    const scene::SceneObject holder = scene.Entities().CreateObject(scene::SceneObjectDesc{ .name = "Navigation" });
    scene.Components().ContentInstances().Set(holder.Entity(), scene::ContentInstanceComponent{
        .assetId = metadata->id.value, .kind = scene::ContentInstanceKind::NavigationMesh });
    const scene::SceneEntity agent = AddAgent(scene, DVec3{ -6.0, 0.0, 0.0 }, Vec3{ 6.0F, 0.0F, 0.0F });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 20 };
    while (!scene.Navigation().HasNavMesh() && std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        std::this_thread::yield();
    }
    Check(scene.Navigation().HasNavMesh(), "a ContentInstance of kind NavigationMesh places its tiles while the scene plays");
    for (int frame = 0; frame < 60 * 6; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Horizontal(Position(scene, agent), { 6.0F, 0.0F, 0.0F }) <= 0.15F, "agents walk on the placed navigation mesh");
    scene.Components().ContentInstances().Remove(holder.Entity());
    static_cast<void>(scene.Runtime().Update(kFrame));
    Check(!scene.Navigation().HasNavMesh(), "removing the ContentInstance removes its tiles");
    std::filesystem::remove_all(root);
}

[[nodiscard]] kb::world::WorldObjectFile FloorObject(const std::string& key, double minX, double maxX, double minZ, double maxZ) {
    kb::world::WorldObjectFile object;
    object.header.guid = kb::world::MakeDeterministicWorldObjectGuid(key);
    object.header.name = key;
    object.header.position = { (minX + maxX) * 0.5, -0.25, (minZ + maxZ) * 0.5 };
    scene::ScenePrefabNodeDesc root = Slab(minX, maxX, minZ, maxZ, 0.0);
    root.stableId = kb::world::WorldObjectStableId(object.header.guid, 0U);
    root.name = key;
    static_cast<void>(object.prefab.AddNode(std::move(root)));
    object.header.nodeCount = 1U;
    return object;
}

void TestTilesStreamWithCells() {
    // Four 32 m cells in a row, each with its own floor slab; the strip stays inside the first row of
    // tiles' cells (tile centres at z = 6.4 and 19.2).
    const std::filesystem::path root = FreshDirectory("stream");
    const std::filesystem::path descriptorPath = root / "Worlds" / "Strip.21kbworld";
    kb::world::WorldDescriptor descriptor;
    descriptor.guid = "strip-world";
    descriptor.name = "Strip";
    descriptor.cellSize = 32.0;
    descriptor.objectsDirectory = "Strip.objects";
    descriptor.hlod.enabled = false;
    descriptor.navigation.enabled = true;
    descriptor.navigation.build = Settings();
    std::string error;
    Check(kb::world::WorldDescriptorIO::Write(descriptorPath, descriptor, error), "the world must write: " + error);
    const kb::world::WorldDescriptorReadResult reread = kb::world::WorldDescriptorIO::Read(descriptorPath);
    Check(reread.succeeded && reread.descriptor.navigation.enabled && reread.descriptor.navigation.build == descriptor.navigation.build,
        "a world's navigation settings read back exactly: " + reread.error);
    const std::filesystem::path objects = kb::world::WorldPaths::ObjectsDirectory(descriptorPath, descriptor);
    for (int cell = 0; cell < 4; ++cell) {
        const kb::world::WorldObjectFile floor = FloorObject("floor" + std::to_string(cell), cell * 32.0, cell * 32.0 + 32.0, 2.0, 22.0);
        const std::vector<std::uint8_t> bytes = kb::world::WorldObjectFileIO::Serialize(floor, error);
        Check(!bytes.empty() && kb::world::WorldObjectFileIO::WriteBytes(objects / (floor.header.guid + ".21kbobject"), bytes, error),
            "a floor object must write: " + error);
    }
    const kb::world::WorldBuildResult built = kb::world::WorldCellBuilder::Build(descriptorPath, nullptr);
    Check(built.succeeded, "the world builds with navigation: " + built.error);
    const kb::world::WorldCellIndexReadResult index = kb::world::WorldCellIndexIO::Read(kb::world::WorldPaths::CellIndexPath(descriptorPath));
    Check(index.succeeded && index.index.navMeshes.size() == 4U && built.report.navMeshCount == 4U && built.report.navTileCount > 0U,
        "every cell with walkable ground gets a navigation mesh: " + std::to_string(index.index.navMeshes.size()));
    const kb::world::WorldBuildResult again = kb::world::WorldCellBuilder::Build(descriptorPath, nullptr);
    const kb::world::WorldCellIndexReadResult rebuilt = kb::world::WorldCellIndexIO::Read(kb::world::WorldPaths::CellIndexPath(descriptorPath));
    Check(again.succeeded && rebuilt.succeeded && rebuilt.index.navMeshes.size() == 4U, "a rebuild writes the navigation meshes again");

    scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Check(manager.Mounts().Mount("Game", root) && manager.DiscoverMountedAssets() > 0U, "the world project must mount");
    const kb::assets::AssetMetadata* world = manager.Registry().FindByPath("/Game/Worlds/Strip.21kbworld");
    const kb::assets::AssetMetadata* cellIndex = manager.Registry().FindByPath("/Game/Worlds/Strip.cells/Strip.21kbcells");
    const kb::assets::AssetMetadata* firstNav = manager.Registry().FindByPath("/Game/Worlds/Strip.cells/" + index.index.navMeshes.front().mesh);
    Check(world != nullptr && cellIndex != nullptr && firstNav != nullptr &&
        std::ranges::find(cellIndex->dependencies, firstNav->id) != cellIndex->dependencies.end(),
        "the cell index depends on the cells' navigation meshes, so packaging carries them");
    const scene::SceneObject owner = scene.Entities().CreateObject(scene::SceneObjectDesc{ .name = "World" });
    scene.Components().ContentInstances().Set(owner.Entity(), scene::ContentInstanceComponent{
        .assetId = world->id.value, .kind = scene::ContentInstanceKind::PartitionedWorld });
    kb::world::WorldPartitionRuntime runtime{ scene };
    const std::uint64_t source = runtime.AddSource({ .position = { 16.0, 0.0, 16.0 }, .loadRadius = 40.0, .unloadRadius = 60.0, .priority = 0 });
    const auto settle = [&](auto&& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 30 };
        while (!done() && std::chrono::steady_clock::now() < deadline) {
            static_cast<void>(scene.Runtime().Update(kFrame));
            std::this_thread::yield();
        }
        return done();
    };
    Check(settle([&] { return runtime.IsNavMeshLoaded(owner.Entity(), { 0, 0 }) && runtime.IsNavMeshLoaded(owner.Entity(), { 1, 0 }); }),
        "the navigation tiles of cells near the source stream in");
    Check(!runtime.IsNavMeshLoaded(owner.Entity(), { 3, 0 }) && runtime.Stats().loadedNavMeshes == 2U, "far cells keep their tiles out");
    Check(Path(scene, { 4.0F, 0.0F, 16.0F }, { 60.0F, 0.0F, 16.0F }).status == scene::NavPathStatus::Complete,
        "a path crosses the border between two streamed cells");
    Check(Path(scene, { 4.0F, 0.0F, 16.0F }, { 120.0F, 0.0F, 16.0F }).status == scene::NavPathStatus::Failed,
        "nothing is known about unstreamed cells");
    const scene::SceneEntity agent = AddAgent(scene, DVec3{ 4.0, 0.0, 16.0 }, Vec3{ 120.0F, 0.0F, 16.0F }, 0.4F, 8.0F);

    Check(runtime.UpdateSource(source, { .position = { 112.0, 0.0, 16.0 }, .loadRadius = 40.0, .unloadRadius = 60.0, .priority = 0 }), "move the source");
    Check(settle([&] {
        return runtime.IsNavMeshLoaded(owner.Entity(), { 3, 0 }) && runtime.IsNavMeshLoaded(owner.Entity(), { 2, 0 }) &&
            !runtime.IsNavMeshLoaded(owner.Entity(), { 0, 0 });
    }), "tiles follow the source: new cells stream in, cells left behind stream out");
    Check(Path(scene, { 70.0F, 0.0F, 16.0F }, { 120.0F, 0.0F, 16.0F }).status == scene::NavPathStatus::Complete,
        "paths use the newly streamed cells");
    static_cast<void>(agent);
    std::vector<kb::world::WorldStreamingEvent> events = runtime.DrainEvents();
    Check(std::ranges::any_of(events, [](const kb::world::WorldStreamingEvent& event) { return event.kind == kb::world::WorldStreamingEventKind::NavMeshLoaded; }) &&
        std::ranges::any_of(events, [](const kb::world::WorldStreamingEvent& event) { return event.kind == kb::world::WorldStreamingEventKind::NavMeshUnloaded; }),
        "loading and unloading tiles is reported as streaming events");
    scene.Runtime().SetPlaying(false);
    static_cast<void>(scene.Runtime().Update(kFrame));
    Check(!scene.Navigation().HasNavMesh(), "stopping play removes the world's tiles");
    std::filesystem::remove_all(root);
}

} // namespace

void RunNavigationMeshTests() {
    TestSettingsAndAssetFormat();
    TestBakeIsDeterministic();
    TestWalkableAreaOfAFloor();
    TestSlopesAndSteps();
    TestDynamicObstaclesCarveTiles();
    TestGeometryChangesRebuildTiles();
    TestAreasAndCosts();
    TestMultipleAgentSizes();
    TestOffMeshLinks();
    TestFarFromTheWorldOrigin();
    TestCrowdIsDeterministic();
    TestCrowdLevelOfDetail();
    TestNavLinkPersists();
    TestNavigationScriptApi();
    TestPlacedNavigationMesh();
    TestTilesStreamWithCells();
}

// Crowd cost on this machine, for the performance gate: 1000 agents must step well inside a frame.
void RunNavigationCrowdBenchmark() {
    constexpr double kHalf = 100.0;
    const auto arena = std::make_shared<const nav::NavMeshAsset>(CrowdArena(kHalf));
    const auto bakeStart = std::chrono::steady_clock::now();
    static_cast<void>(CrowdArena(kHalf));
    const double bakeMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bakeStart).count();
    const CrowdRun thousand = RunCrowd(arena, kHalf, 1000, 240);
    const CrowdRun fiveThousand = RunCrowd(arena, kHalf, 5000, 240);
    const CrowdRun fiveThousandLod = RunCrowd(arena, kHalf, 5000, 240, { DVec3{ -50.0, 0.0, -50.0 } });
    std::cout << "navigation: bake of a 200 x 200 m arena with 25 pillars " << bakeMilliseconds << " ms ("
              << arena->tiles.size() << " tiles)\n";
    std::cout << "navigation: crowd step, 1000 agents " << thousand.milliseconds << " ms, 5000 agents " << fiveThousand.milliseconds
              << " ms, 5000 agents with level of detail around one focus " << fiveThousandLod.milliseconds << " ms\n";
    Check(thousand.milliseconds < 8.0 * kSanitizerTimeScale, "1000 crowd agents must step within 8 ms: " + std::to_string(thousand.milliseconds));
    Check(fiveThousandLod.milliseconds < fiveThousand.milliseconds, "level of detail must make a large crowd cheaper");
}

} // namespace kb::tests
