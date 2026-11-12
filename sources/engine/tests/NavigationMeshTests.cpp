#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/math/DVec3.hpp"
#include "engine/navigation/NavGeometryCollector.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/navigation/NavMeshBuild.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocument.hpp"
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
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/SceneAssetComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetPhysicsComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetRenderComponentCodec.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
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
    // One worker, several, and the pool a bake starts without one, all give the same bytes.
    kb::ecs::WorkerPool one{ kb::ecs::WorkerPoolConfig{ .workerCount = 1U, .collectDispatchTelemetry = true } };
    kb::ecs::WorkerPool four{ kb::ecs::WorkerPoolConfig{ .workerCount = 4U, .collectDispatchTelemetry = true } };
    const std::vector<std::uint8_t> single = nav::NavMeshAssetIO::Serialize(BakeGeometry(geometry, Settings(), &one));
    const std::vector<std::uint8_t> parallel = nav::NavMeshAssetIO::Serialize(BakeGeometry(geometry, Settings(), &four));
    const std::vector<std::uint8_t> owned = nav::NavMeshAssetIO::Serialize(BakeGeometry(geometry, Settings()));
    Check(one.DispatchTelemetry().dispatchCount == 1U && four.DispatchTelemetry().dispatchCount == 1U &&
        four.DispatchTelemetry().lastWorkItemCount > 1U && four.DispatchTelemetry().lastActiveWorkerCount > 1U,
        "a bake given the engine's worker pool builds its tiles on it");
    Check(!single.empty() && single == parallel && single == owned, "a bake gives the same bytes on one worker as on several");
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

void TestInterruptedLinkCrossing() {
    for (const scene::NavLinkKind kind : { scene::NavLinkKind::Jump, scene::NavLinkKind::Ladder }) {
        for (const bool unloadLanding : { false, true }) {
            scene::Scene scene;
            const double top = kind == scene::NavLinkKind::Jump ? 1.0 : 3.0;
            auto departure = BakeNodes(TwoPlatforms(top));
            auto landing = departure;
            std::erase_if(departure.tiles, [](const auto& tile) { return tile.coord.x >= 0; });
            std::erase_if(landing.tiles, [](const auto& tile) { return tile.coord.x < 0; });
            static_cast<void>(AddMesh(scene, departure));
            const auto landingHandle = AddMesh(scene, landing);
            const Vec3 goal{ 8.0F, static_cast<float>(top), 0.0F };
            const auto link = AddLink(scene, { -0.8F, 0.0F, 0.0F }, scene::NavLink{
                .end = { 4.6F, static_cast<float>(top), 0.0F }, .radius = 0.6F, .kind = kind });
            const auto agent = AddAgent(scene, { -6.0, 0.0, 0.0 }, goal);
            bool crossing = false;
            for (int frame = 0; frame < 720; ++frame) {
                static_cast<void>(scene.Runtime().Update(kFrame));
                const auto position = Position(scene, agent);
                if (scene.Navigation().AgentOnLink(agent) &&
                    (kind == scene::NavLinkKind::Jump ? position.x > 0.5F : position.y > 0.75F)) {
                    crossing = true;
                    break;
                }
            }
            Check(crossing, "the agent must enter the link before it is interrupted");
            if (unloadLanding) Check(scene.Navigation().RemoveNavMesh(landingHandle), "the landing region must unload");
            else scene.Components().NavLinks().TryGet(link)->enabled = false;
            for (int frame = 0; frame < 240; ++frame) {
                static_cast<void>(scene.Runtime().Update(kFrame));
                const Vec3 position = Position(scene, agent);
                Check(std::isfinite(position.x) && std::isfinite(position.y) && position.x < 0.0F && position.y < 0.5F &&
                    !scene.Navigation().AgentOnLink(agent), "an interrupted crossing must return to its supported departure side");
            }
            Check(Agent(scene, agent).pathStatus != scene::NavPathStatus::Complete,
                "a removed link or landing region must invalidate the crossing path");
            if (unloadLanding) static_cast<void>(AddMesh(scene, landing));
            else scene.Components().NavLinks().TryGet(link)->enabled = true;
            for (int frame = 0; frame < 720; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
            Check(Horizontal(Position(scene, agent), goal) <= 0.15F,
                "the agent must replan and arrive when the link and landing region return");
        }
    }
}

[[nodiscard]] DVec3 WalkFar(const DVec3& origin) {
    scene::Scene scene;
    scene.Navigation().SetOrigin(origin);
    std::vector<scene::ScenePrefabNodeDesc> nodes{ Slab(origin.x - 10.0, origin.x + 10.0, origin.z - 5.0, origin.z + 5.0, 0.0) };
    static_cast<void>(AddMesh(scene, BakeNodes(nodes)));
    const scene::SceneEntity agent = AddAgent(scene, origin + DVec3{ -6.0, 0.0, 0.0 }, Vec3{ 6.0F, 0.0F, 0.0F }, 0.4F, 2.0F);
    for (int frame = 0; frame < 60; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    return scene.Transforms().WorldTranslation(agent) - origin;
}

void TestFarFromTheWorldOrigin() {
    // Ten thousand kilometres out; tiles, queries and agents work in the space of the navigation origin.
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
    // The slowest measured step.
    double worst = 0.0;
    // Average stage times of the measured steps, and the stages of the slowest one.
    scene::NavigationCrowdStats stages{};
    scene::NavigationCrowdStats worstStages{};
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
    std::vector<DVec3> focuses = {}, const scene::NavigationCrowdSettings& settings = {}) {
    scene::Scene scene;
    std::string error;
    Check(scene.Navigation().AddNavMesh(arena, &error) != 0U, "the crowd arena must load: " + error);
    scene.Navigation().ConfigureCrowd(settings);
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
        if (step >= steps / 2) {
            const scene::NavigationCrowdStats stats = scene.Navigation().CrowdStats();
            run.milliseconds += stats.milliseconds;
            if (stats.milliseconds > run.worst) run.worstStages = stats;
            run.worst = std::max(run.worst, stats.milliseconds);
            run.stages.syncMilliseconds += stats.syncMilliseconds;
            run.stages.pathMilliseconds += stats.pathMilliseconds;
            run.stages.neighbourMilliseconds += stats.neighbourMilliseconds;
            run.stages.cornerMilliseconds += stats.cornerMilliseconds;
            run.stages.steeringMilliseconds += stats.steeringMilliseconds;
            run.stages.avoidanceMilliseconds += stats.avoidanceMilliseconds;
            run.stages.moveMilliseconds += stats.moveMilliseconds;
        }
    }
    const double measured = static_cast<double>(steps - steps / 2);
    run.milliseconds /= measured;
    for (double* stage : { &run.stages.syncMilliseconds, &run.stages.pathMilliseconds, &run.stages.neighbourMilliseconds, &run.stages.cornerMilliseconds,
             &run.stages.steeringMilliseconds, &run.stages.avoidanceMilliseconds, &run.stages.moveMilliseconds }) {
        *stage /= measured;
    }
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
    // Near, mid and far agents around one focus, stepped on one, two and eight threads.
    const std::vector<DVec3> focuses{ DVec3{ -30.0, 0.0, -30.0 } };
    const auto run = [&](std::uint32_t threads) {
        return RunCrowd(arena, kHalf, kAgents, 300, focuses,
            scene::NavigationCrowdSettings{ .nearDistance = 30.0F, .midDistance = 60.0F, .workerThreads = threads });
    };
    const CrowdRun first = run(1U);
    Check(first.positions.size() == static_cast<std::size_t>(kAgents), "every crowd agent is reported");
    for (const std::uint32_t threads : { 1U, 2U, 8U }) {
        const CrowdRun again = run(threads);
        Check(std::memcmp(first.positions.data(), again.positions.data(), first.positions.size() * sizeof(Vec3)) == 0,
            "a crowd of 1200 agents replays to the same positions on " + std::to_string(threads) + " threads");
    }
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
    scene.Navigation().ConfigureCrowd(scene::NavigationCrowdSettings{ .nearDistance = 20.0F, .midDistance = 20.0F, .farPathOptimizationInterval = 4U });
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

// An agent walking away from the focus turns mid and then far a tenth beyond each edge, and near
// again as soon as it walks back inside the near distance.
void TestCrowdLevelOfDetailTransitions() {
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes({ Slab(-5.0, 45.0, -3.0, 3.0, 0.0) })));
    scene.Navigation().ConfigureCrowd(scene::NavigationCrowdSettings{ .nearDistance = 10.0F, .midDistance = 20.0F });
    scene.Navigation().SetCrowdFocuses({ DVec3{} });
    const scene::SceneEntity agent = AddAgent(scene, DVec3{}, Vec3{ 40.0F, 0.0F, 0.0F });
    float firstMid = -1.0F;
    float firstFar = -1.0F;
    bool ordered = true;
    for (int frame = 0; frame < 900 && firstFar < 0.0F; ++frame) {
        const float before = Position(scene, agent).x;
        static_cast<void>(scene.Runtime().Update(kFrame));
        const scene::NavigationCrowdStats stats = scene.Navigation().CrowdStats();
        if (stats.midAgents == 1U && firstMid < 0.0F) firstMid = before;
        if (stats.farAgents == 1U && firstFar < 0.0F) firstFar = before;
        ordered = ordered && (firstMid >= 0.0F || stats.nearAgents == 1U) && (firstFar >= 0.0F || stats.farAgents == 0U);
    }
    Check(ordered, "an agent walking away is near, then mid, then far");
    Check(firstMid > 11.0F && firstMid < 11.2F, "an agent turns mid a tenth beyond the near distance: " + std::to_string(firstMid));
    Check(firstFar > 22.0F && firstFar < 22.2F, "an agent turns far a tenth beyond the mid distance: " + std::to_string(firstFar));
    scene::NavAgent back = Agent(scene, agent);
    back.destination = Vec3{};
    scene.Components().NavAgents().Set(agent, back);
    float firstNear = -1.0F;
    for (int frame = 0; frame < 900 && firstNear < 0.0F; ++frame) {
        const float before = Position(scene, agent).x;
        static_cast<void>(scene.Runtime().Update(kFrame));
        if (scene.Navigation().CrowdStats().nearAgents == 1U) firstNear = before;
    }
    Check(firstNear <= 10.0F && firstNear > 9.8F, "an agent walking back is near at the near distance: " + std::to_string(firstNear));
}

// With a budget of three destinations per step, the agents near the focus are planned first even
// though the far ones were created first, and every agent is planned in the end.
void TestCrowdPathBudgetPriority() {
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes({ Slab(-30.0, 30.0, -5.0, 5.0, 0.0) })));
    scene.Navigation().ConfigureCrowd(scene::NavigationCrowdSettings{ .nearDistance = 10.0F, .midDistance = 10.0F, .maxPathRequestsPerStep = 3U });
    scene.Navigation().SetCrowdFocuses({ DVec3{ 20.0, 0.0, 0.0 } });
    std::vector<scene::SceneEntity> far;
    std::vector<scene::SceneEntity> near;
    for (int index = 0; index < 6; ++index) {
        const float x = -25.0F + 1.5F * static_cast<float>(index);
        far.push_back(AddAgent(scene, DVec3{ x, 0.0, -2.0 }, Vec3{ x, 0.0F, 2.0F }));
    }
    for (int index = 0; index < 6; ++index) {
        const float x = 16.0F + 1.5F * static_cast<float>(index);
        near.push_back(AddAgent(scene, DVec3{ x, 0.0, -2.0 }, Vec3{ x, 0.0F, 2.0F }));
    }
    const auto planned = [&](const std::vector<scene::SceneEntity>& group) {
        return static_cast<std::size_t>(std::ranges::count_if(group, [&](scene::SceneEntity entity) {
            return Agent(scene, entity).pathStatus != scene::NavPathStatus::Pending;
        }));
    };
    std::string history;
    bool withinBudget = true;
    for (int frame = 0; frame < 4; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        withinBudget = withinBudget && scene.Navigation().CrowdStats().pathRequests <= 3U;
        history += std::to_string(planned(near)) + "/" + std::to_string(planned(far)) + " ";
    }
    Check(withinBudget, "no step hands the crowd more destinations than its budget");
    Check(history == "3/0 6/0 6/3 6/6 ", "near agents are planned first, then the far ones: " + history);
}

// A level on two storeys: ground [-half, half]^2; a deck 3 m up over x in [half/3, half]; stairs up
// to the deck in the far corner (z near -0.8 half); a 3 m wide bridge at deck height reaching back
// over the ground along z in [10, 13]; and a ladder link from the ground up onto the bridge's end.
struct HordeLevel {
    std::shared_ptr<const nav::NavMeshAsset> asset;
    double half = 0.0;
    double deckX = 0.0;
    Vec3 ladderStart{};
    Vec3 ladderEnd{};
};

[[nodiscard]] HordeLevel MakeHordeLevel(double half) {
    HordeLevel level;
    level.half = half;
    level.deckX = half / 3.0;
    std::vector<scene::ScenePrefabNodeDesc> nodes{ Slab(-half, half, -half, half, 0.0), Slab(level.deckX, half, -half, half, 3.0) };
    const double stairsZ = -half * 0.8;
    for (int step = 0; step < 12; ++step) {
        const double top = 0.25 * (step + 1);
        nodes.push_back(Slab(level.deckX - 6.0 + 0.5 * step, level.deckX - 5.5 + 0.5 * step, stairsZ - 4.0, stairsZ + 4.0, top, top));
    }
    nodes.push_back(Slab(-half + 2.0, level.deckX, 10.0, 13.0, 3.0));
    nav::NavMeshBuildSettings settings = Settings();
    settings.cellSize = 0.3F;
    settings.tileCells = 96U;
    level.asset = std::make_shared<const nav::NavMeshAsset>(BakeNodes(nodes, settings));
    level.ladderStart = Vec3{ static_cast<float>(-half + 4.0), 0.0F, 15.5F };
    level.ladderEnd = Vec3{ static_cast<float>(-half + 4.0), 3.0F, 12.0F };
    return level;
}

struct HordeRun {
    std::vector<Vec3> positions;
    std::vector<double> steps;
    double crowdMilliseconds = 0.0;
    // Horde agents waiting for guidance over the steps after the first half second, up to half way.
    std::size_t laterWaiting = 0U;
    bool crossedLink = false;
    std::size_t maxFields = 0U;
    std::size_t maxWaiting = 0U;
    std::size_t updated = 0U;
    // Field growth time of the slowest step.
    double slowestFieldMilliseconds = 0.0;
    [[nodiscard]] double Percentile(double fraction) const {
        std::vector<double> sorted = steps;
        std::ranges::sort(sorted);
        return sorted.empty() ? 0.0 : sorted[std::min(sorted.size() - 1U, static_cast<std::size_t>(fraction * static_cast<double>(sorted.size())))];
    }
    [[nodiscard]] double Average() const {
        double sum = 0.0;
        for (const double step : steps) sum += step;
        return steps.empty() ? 0.0 : sum / static_cast<double>(steps.size());
    }
};

// `count` agents on the ground chase `targets` targets circling on the deck and on the ground
// (agent i chases target i % targets), as a horde or as individual crowd agents. Measures the
// navigation step of the second half of `steps`.
[[nodiscard]] HordeRun RunHorde(const HordeLevel& level, int count, int targets, int steps, bool horde, std::uint32_t threads = 0U,
    std::optional<Vec3> fixedGoal = std::nullopt, double targetSpin = 0.25, int crowdAgents = 0, std::uint32_t fieldBudget = 4096U) {
    const bool moving = !fixedGoal.has_value();
    scene::Scene scene;
    std::string error;
    Check(scene.Navigation().AddNavMesh(level.asset, &error) != 0U, "the horde level must load: " + error);
    scene.Navigation().ConfigureCrowd(scene::NavigationCrowdSettings{ .workerThreads = threads, .hordeFieldNodesPerStep = fieldBudget });
    static_cast<void>(AddLink(scene, level.ladderStart, scene::NavLink{
        .start = {}, .end = level.ladderEnd - level.ladderStart, .radius = 0.6F, .kind = scene::NavLinkKind::Walk, .bidirectional = true }));
    const double half = level.half;
    const auto target = [&](int index, int step) {
        if (fixedGoal) return *fixedGoal;
        // Even targets circle on the deck, odd ones on the ground; 8 m circles at 2 m/s.
        const double angle = (moving ? step * kFrame * targetSpin : 0.0) + index * 0.7;
        const bool deck = index % 2 == 0;
        const double spanZ = half * 1.4;
        const double cz = -half * 0.7 + spanZ * ((index * 0.618) - std::floor(index * 0.618));
        const double cx = deck ? (level.deckX + half) * 0.5 : -half * 0.5;
        return Vec3{ static_cast<float>(cx + 8.0 * std::cos(angle)), deck ? 3.0F : 0.0F, static_cast<float>(cz + 8.0 * std::sin(angle)) };
    };
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    const double minX = -half * 0.95;
    const double spanX = (level.deckX - 8.0) - minX;
    const double spanZ = half * 1.9;
    std::vector<scene::SceneEntity> agents;
    std::vector<scene::SceneEntity> crowd;
    agents.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const double x = minX + spanX * (index % side + 0.5) / side;
        const double z = -half * 0.95 + spanZ * (index / side + 0.5) / side;
        const scene::SceneObject object = scene.Entities().CreateObject(scene::SceneObjectDesc{ .name = "Horde" });
        scene.Transforms().SetLocalTranslation(object.Entity(), DVec3{ x, 0.0, z });
        scene.Components().NavAgents().Set(object.Entity(), scene::NavAgent{
            .radius = 0.4F, .maxSpeed = 3.5F, .acceleration = 10.0F, .stoppingDistance = 1.0F, .destination = target(index % targets, 0), .horde = horde });
        agents.push_back(object.Entity());
    }
    // Individual crowd agents among them, after the same targets.
    for (int index = 0; index < crowdAgents; ++index) {
        const double x = minX + spanX * ((index * 37) % 101 + 0.5) / 101.0;
        const double z = -half * 0.95 + spanZ * ((index * 53) % 103 + 0.5) / 103.0;
        const scene::SceneObject object = scene.Entities().CreateObject(scene::SceneObjectDesc{ .name = "Crowd" });
        scene.Transforms().SetLocalTranslation(object.Entity(), DVec3{ x, 0.0, z });
        scene.Components().NavAgents().Set(object.Entity(), scene::NavAgent{
            .radius = 0.4F, .maxSpeed = 3.5F, .acceleration = 10.0F, .stoppingDistance = 1.0F, .destination = target(index % targets, 0) });
        crowd.push_back(object.Entity());
    }
    scene.Runtime().SynchronizeTransforms();
    HordeRun run;
    double slowest = 0.0;
    std::vector<Vec3> goals(static_cast<std::size_t>(targets));
    for (int step = 0; step < steps; ++step) {
        if (moving) {
            for (int index = 0; index < targets; ++index) goals[static_cast<std::size_t>(index)] = target(index, step);
            for (int index = 0; index < count; ++index) {
                scene.Components().NavAgents().TryGet(agents[static_cast<std::size_t>(index)])->destination = goals[static_cast<std::size_t>(index % targets)];
            }
            for (int index = 0; index < crowdAgents; ++index) {
                scene.Components().NavAgents().TryGet(crowd[static_cast<std::size_t>(index)])->destination = goals[static_cast<std::size_t>(index % targets)];
            }
        }
        static_cast<void>(scene.Runtime().Update(kFrame));
        const scene::NavigationCrowdStats stats = scene.Navigation().CrowdStats();
        if (step >= steps / 2) run.crowdMilliseconds += stats.milliseconds;
        if (step >= 30 && step < steps / 2) run.laterWaiting += stats.hordeWaiting;
        run.crossedLink = run.crossedLink || (horde ? stats.hordeOnLinks : stats.onLinks) > 0U;
        run.maxFields = std::max(run.maxFields, stats.hordeFields);
        if (step >= steps / 2) {
            const double milliseconds = horde ? stats.hordeMilliseconds : stats.milliseconds;
            if (run.steps.empty() || milliseconds > slowest) {
                slowest = milliseconds;
                run.slowestFieldMilliseconds = stats.hordeFieldMilliseconds;
            }
            run.steps.push_back(milliseconds);
            run.maxWaiting = std::max(run.maxWaiting, stats.hordeWaiting);
            run.updated += stats.hordeUpdated;
        }
    }
    for (const scene::SceneEntity agent : agents) run.positions.push_back(Position(scene, agent));
    return run;
}

// Horde agents on the ground reach a target on the deck over the stairs, the bridge and the
// ladder link, sharing one field; agents under the bridge are not pushed by those on it.
void TestHordeCrossesLevels() {
    const HordeLevel level = MakeHordeLevel(30.0);
    const Vec3 goal{ static_cast<float>(level.deckX + 4.0), 3.0F, 11.5F };
    const HordeRun run = RunHorde(level, 300, 1, 60 * 40, true, 0U, goal);
    std::size_t arrived = 0U;
    for (const Vec3 position : run.positions) {
        arrived += position.y > 2.5F && Horizontal(position, goal) < 12.0F ? 1U : 0U;
    }
    Check(arrived >= 270U, "a horde climbs onto the deck and gathers at its target: " + std::to_string(arrived) + " of 300");
    Check(run.maxFields <= 24U, "one target gives a few shared fields, not one per agent: " + std::to_string(run.maxFields));
    Check(run.crossedLink, "horde agents take the ladder link when it is the short way");
}

// The horde replays to the same positions on one, two and eight threads, with moving targets.
void TestHordeIsDeterministic() {
    const HordeLevel level = MakeHordeLevel(30.0);
    const HordeRun first = RunHorde(level, 1500, 4, 400, true, 1U);
    for (const std::uint32_t threads : { 2U, 8U }) {
        const HordeRun again = RunHorde(level, 1500, 4, 400, true, threads);
        Check(std::memcmp(first.positions.data(), again.positions.data(), first.positions.size() * sizeof(Vec3)) == 0,
            "a horde of 1500 agents replays to the same positions on " + std::to_string(threads) + " threads");
    }
}

// A target moving within its polygon reuses its field; one moving into other polygons builds new
// fields while the agents keep following the old one, so none stands waiting.
void TestHordeFieldFollowsItsTarget() {
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes({ Slab(-20.0, 20.0, -20.0, 20.0, 0.0), BoxNode(DVec3{ 0.0, 1.0, 0.0 }, Vec3{ 2.0F, 2.0F, 12.0F }) })));
    std::vector<scene::SceneEntity> agents;
    for (int index = 0; index < 50; ++index) {
        agents.push_back(AddAgent(scene, DVec3{ -15.0 + (index % 10) * 1.2, 0.0, -6.0 + (index / 10) * 1.2 }, Vec3{ 12.0F, 0.0F, 0.0F }));
        scene.Components().NavAgents().TryGet(agents.back())->horde = true;
    }
    for (int frame = 0; frame < 30; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    const std::size_t fields = scene.Navigation().CrowdStats().hordeFields;
    Check(fields <= 12U && scene.Navigation().CrowdStats().hordeWaiting == 0U,
        "50 agents with one destination share a few fields and all follow them: " + std::to_string(fields));
    for (const scene::SceneEntity agent : agents) scene.Components().NavAgents().TryGet(agent)->destination = Vec3{ 12.05F, 0.0F, 0.02F };
    static_cast<void>(scene.Runtime().Update(kFrame));
    Check(scene.Navigation().CrowdStats().hordeFields == fields, "a destination moved within its polygon keeps its field");
    std::size_t waiting = 0U;
    std::size_t waitedSteps = 0U;
    for (int frame = 0; frame < 900; ++frame) {
        const Vec3 destination{ 12.0F, 0.0F, -15.0F + 0.03F * static_cast<float>(frame) };
        for (const scene::SceneEntity agent : agents) scene.Components().NavAgents().TryGet(agent)->destination = destination;
        static_cast<void>(scene.Runtime().Update(kFrame));
        waiting = std::max(waiting, scene.Navigation().CrowdStats().hordeWaiting);
        waitedSteps += scene.Navigation().CrowdStats().hordeWaiting;
    }
    // Agents keep to the old fields while new ones grow; only one that has just stepped into a tile whose field is
    // not there yet stands for a step.
    Check(waiting <= 4U && waitedSteps <= 90U, "agents follow the old field while the new one grows: at most " + std::to_string(waiting) +
        " waited at once, " + std::to_string(waitedSteps) + " agent steps in all");
    std::size_t near = 0U;
    for (const scene::SceneEntity agent : agents) near += Horizontal(Position(scene, agent), { 12.0F, 0.0F, 11.97F }) < 6.0F ? 1U : 0U;
    Check(near >= 45U, "the horde goes around the wall and keeps up with its moving target: " + std::to_string(near) + " of 50");
}

// Far from their target and with a tiny field budget, horde agents still set off along the route over the
// tiles instead of waiting for a field that would take long to reach them.
void TestHordeFarAgentsFollowTheRoute() {
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes({ Slab(-150.0, 150.0, -40.0, 40.0, 0.0) })));
    scene.Navigation().ConfigureCrowd(scene::NavigationCrowdSettings{ .hordeFieldNodesPerStep = 48U });
    std::vector<scene::SceneEntity> agents;
    std::vector<float> startX;
    for (int index = 0; index < 120; ++index) {
        const double x = -140.0 + (index % 12) * 2.5;
        agents.push_back(AddAgent(scene, DVec3{ x, 0.0, -14.0 + (index / 12) * 2.5 }, Vec3{ 120.0F, 0.0F, 0.0F }));
        startX.push_back(static_cast<float>(x));
        scene.Components().NavAgents().TryGet(agents.back())->horde = true;
    }
    std::size_t waitingLater = 0U;
    for (int frame = 0; frame < 150; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        if (frame >= 30) waitingLater = std::max(waitingLater, scene.Navigation().CrowdStats().hordeWaiting);
    }
    Check(waitingLater == 0U, "far horde agents are guided along the route within half a second: " + std::to_string(waitingLater) + " waited");
    std::size_t advanced = 0U;
    float total = 0.0F;
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        const float travelled = Position(scene, agents[index]).x - startX[index];
        advanced += travelled > 2.0F ? 1U : 0U;
        total += travelled;
    }
    Check(advanced >= 110U && total / 120.0F >= 4.0F, "far horde agents walk toward their target: " + std::to_string(advanced) + " of 120, average " +
        std::to_string(total / 120.0F) + " m");
}

// A horde keeps its distance from a crowd agent and from an obstacle it steers around, like from each other.
void TestHordeKeepsOffOtherBodies() {
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes({ Slab(-30.0, 30.0, -30.0, 30.0, 0.0) })));
    const scene::SceneEntity guard = AddAgent(scene, DVec3{ 10.0, 0.0, 0.0 }, Vec3{ 10.0F, 0.0F, 0.0F });
    static_cast<void>(AddObstacle(scene, { 10.0F, 0.0F, 12.0F }, scene::NavObstacle{
        .shape = scene::NavObstacleShape::Cylinder, .radius = 1.0F, .height = 2.0F, .carve = false }));
    std::vector<scene::SceneEntity> toGuard;
    std::vector<scene::SceneEntity> pastObstacle;
    for (int index = 0; index < 4; ++index) {
        toGuard.push_back(AddAgent(scene, DVec3{ -10.0 + index * 1.0, 0.0, -6.0 }, Vec3{ 10.0F, 0.0F, 0.0F }));
        scene.Components().NavAgents().TryGet(toGuard.back())->horde = true;
        // These walk straight at the obstacle's centre on their way to a point behind it.
        pastObstacle.push_back(AddAgent(scene, DVec3{ 10.0 + (index - 1.5) * 0.15, 0.0, 2.0 + index * 1.0 }, Vec3{ 10.0F, 0.0F, 24.0F }));
        scene.Components().NavAgents().TryGet(pastObstacle.back())->horde = true;
    }
    float closestToObstacle = 1.0e9F;
    for (int frame = 0; frame < 60 * 20; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        for (const scene::SceneEntity agent : pastObstacle) closestToObstacle = std::min(closestToObstacle, Horizontal(Position(scene, agent), { 10.0F, 0.0F, 12.0F }));
    }
    float closestToGuard = 1.0e9F;
    for (const scene::SceneEntity agent : toGuard) closestToGuard = std::min(closestToGuard, Horizontal(Position(scene, agent), Position(scene, guard)));
    Check(closestToGuard >= 0.6F, "horde agents keep apart from a crowd agent: " + std::to_string(closestToGuard) + " m");
    Check(closestToObstacle >= 0.8F, "horde agents steer around a non-carving obstacle: " + std::to_string(closestToObstacle) + " m");
}

void TestNavComponentsPersist() {
    const std::filesystem::path root = FreshDirectory("persist");
    {
        scene::Scene scene;
        static_cast<void>(AddLink(scene, { 1.0F, 2.0F, 3.0F }, scene::NavLink{
            .start = { 0.5F, 0.0F, 0.0F }, .end = { 4.0F, -1.0F, 2.0F }, .radius = 0.75F, .kind = scene::NavLinkKind::Ladder, .area = 7U,
            .bidirectional = false, .enabled = true }));
        const scene::SceneEntity agent = AddAgent(scene, DVec3{ 2.0, 0.0, 1.0 }, Vec3{ 5.0F, 0.0F, -3.0F }, 0.6F, 4.5F);
        scene.Components().NavAgents().TryGet(agent)->areaMask = scene::kAllNavAreas & ~scene::NavAreaBit(5U);
        scene.Components().NavAgents().TryGet(agent)->horde = true;
        static_cast<void>(AddObstacle(scene, { -2.0F, 0.0F, 4.0F }, scene::NavObstacle{
            .shape = scene::NavObstacleShape::Cylinder, .radius = 1.25F, .height = 3.0F, .area = 6U, .carve = false }));
        Check(scene::SceneDocumentService::Save(scene, root / "Navigation.21kbscene", "Navigation"), "a scene with navigation components saves");
    }
    scene::Scene loaded;
    Check(scene::SceneDocumentService::LoadFileIntoScene(loaded, root / "Navigation.21kbscene"), "a scene with navigation components loads");
    struct Found {
        scene::Scene* scene = nullptr;
        std::vector<scene::NavLink> links;
        std::vector<scene::NavAgent> agents;
        std::vector<scene::NavObstacle> obstacles;
    } found{ .scene = &loaded, .links = {}, .agents = {}, .obstacles = {} };
    loaded.Transforms().ForEach([](scene::SceneEntity entity, const scene::TransformComponent&, void* context) {
        auto* search = static_cast<Found*>(context);
        if (const scene::NavLink* link = search->scene->Components().NavLinks().TryGet(entity); link != nullptr) search->links.push_back(*link);
        if (const scene::NavAgent* agent = search->scene->Components().NavAgents().TryGet(entity); agent != nullptr) search->agents.push_back(*agent);
        if (const scene::NavObstacle* obstacle = search->scene->Components().NavObstacles().TryGet(entity); obstacle != nullptr) {
            search->obstacles.push_back(*obstacle);
        }
    }, &found);
    const std::vector<scene::NavLink>& links = found.links;
    Check(links.size() == 1U && links[0].end.x == 4.0F && links[0].end.y == -1.0F && links[0].radius == 0.75F &&
        links[0].kind == scene::NavLinkKind::Ladder && links[0].area == 7U && !links[0].bidirectional,
        "a link keeps every field through a save and a load");
    Check(found.agents.size() == 1U && found.agents[0].radius == 0.6F && found.agents[0].maxSpeed == 4.5F && found.agents[0].destination.z == -3.0F &&
        found.agents[0].areaMask == (scene::kAllNavAreas & ~scene::NavAreaBit(5U)) && found.agents[0].horde,
        "an agent keeps its fields through a save and a load");
    Check(found.obstacles.size() == 1U && found.obstacles[0].shape == scene::NavObstacleShape::Cylinder && found.obstacles[0].radius == 1.25F &&
        found.obstacles[0].height == 3.0F && found.obstacles[0].area == 6U && !found.obstacles[0].carve,
        "an obstacle keeps its fields through a save and a load");
    std::filesystem::remove_all(root);
}

// A Navigation Bounds node: a box `size` wide around `position`.
[[nodiscard]] scene::ScenePrefabNodeDesc BoundsNode(DVec3 position, Vec3 size, scene::NavBounds bounds = {}) {
    scene::ScenePrefabNodeDesc node;
    node.name = "Navigation Bounds";
    node.SetLocalTranslation(position);
    bounds.size = size;
    node.components.navBounds = bounds;
    return node;
}

[[nodiscard]] double WalkableArea(const nav::NavMeshAsset& asset) {
    if (asset.tiles.empty()) return 0.0;
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, asset));
    return scene.Navigation().NavMeshWalkableArea();
}

[[nodiscard]] nav::NavMeshAsset BakeWithBounds(const std::vector<scene::ScenePrefabNodeDesc>& nodes) {
    nav::NavGeometry geometry;
    nav::NavGeometryCollectStats stats;
    nav::CollectNavGeometry(nodes, Settings(), nullptr, geometry, stats);
    return BakeGeometry(geometry, Settings());
}

void TestBoundsLimitTheBake() {
    const scene::ScenePrefabNodeDesc floor = Slab(-20.0, 20.0, -20.0, 20.0, 0.0);
    const double whole = WalkableArea(BakeWithBounds({ floor }));
    Check(whole > 38.0 * 38.0, "without Navigation Bounds the whole floor is baked: " + std::to_string(whole));

    const nav::NavMeshAsset boxed = BakeWithBounds({ floor, BoundsNode(DVec3{ 2.0, 0.0, 3.0 }, Vec3{ 10.0F, 4.0F, 10.0F }) });
    const double inside = WalkableArea(boxed);
    Check(inside > 90.0 && inside <= 101.0, "one Navigation Bounds keeps the floor inside its 10 x 10 m box only: " + std::to_string(inside));
    scene::Scene single;
    static_cast<void>(AddMesh(single, boxed));
    const std::optional<DVec3> centre = single.Navigation().NearestPoint(DVec3{ 2.0, 0.5, 3.0 });
    Check(centre.has_value() && std::fabs(centre->x - 2.0) < 0.01, "the box's centre is walkable");
    const std::optional<DVec3> outside = single.Navigation().NearestPoint(DVec3{ -12.0, 0.0, -12.0 });
    Check(!outside.has_value() || outside->x > -3.2, "floor outside the box is not baked");
    Check(boxed.tiles.size() < BakeWithBounds({ floor }).tiles.size(), "tiles no Navigation Bounds touches are not baked");

    const double both = WalkableArea(BakeWithBounds({ floor, BoundsNode(DVec3{ -12.0, 0.0, 0.0 }, Vec3{ 8.0F, 4.0F, 8.0F }),
        BoundsNode(DVec3{ 12.0, 0.0, 0.0 }, Vec3{ 8.0F, 4.0F, 8.0F }) }));
    Check(both > 2.0 * 56.0 && both <= 2.0 * 65.0, "two Navigation Bounds bake the union of their boxes: " + std::to_string(both));

    const double above = WalkableArea(BakeWithBounds({ floor, BoundsNode(DVec3{ 0.0, 10.0, 0.0 }, Vec3{ 10.0F, 4.0F, 10.0F }) }));
    Check(above == 0.0, "a box above the floor bakes nothing: " + std::to_string(above));
}

void TestBoundsBakeSettings() {
    scene::NavBounds bounds{};
    const nav::NavMeshBuildSettings defaults = nav::NavBoundsBuildSettings(bounds);
    Check(defaults.profiles.size() == 1U && defaults.profiles[0] == nav::NavAgentProfile{} && defaults.cellSize == nav::NavMeshBuildSettings{}.cellSize &&
        defaults.tileCells == nav::NavMeshBuildSettings{}.tileCells, "a default Navigation Bounds bakes with the default settings");
    Check(scene::NavAgent{}.radius == nav::NavAgentProfile{}.radius, "a default agent is as wide as the default baked profile");
    bounds.agentRadius = 1.0F;
    bounds.agentHeight = 1.2F;
    bounds.maxStepHeight = 0.3F;
    bounds.maxSlopeDegrees = 30.0F;
    bounds.cellSize = 0.2F;
    bounds.tileSize = 9.6F;
    const nav::NavMeshBuildSettings own = nav::NavBoundsBuildSettings(bounds);
    Check(own.profiles[0].radius == 1.0F && own.profiles[0].height == 1.2F && own.profiles[0].maxClimb == 0.3F &&
        own.profiles[0].maxSlopeDegrees == 30.0F && own.cellSize == 0.2F && own.tileCells == 48U, "a Navigation Bounds' settings become the bake's");

    // A wider agent keeps further from the walls, so less of the floor is walkable.
    const std::filesystem::path root = FreshDirectory("bounds");
    const auto bake = [&root](std::vector<scene::NavBounds> boxes) {
        scene::Scene scene;
        static_cast<void>(AddCollider(scene, DVec3{ 0.0, -0.25, 0.0 }, Vec3{ 20.0F, 0.5F, 20.0F }));
        for (const scene::NavBounds& box : boxes) {
            const scene::SceneObject holder = scene.Entities().CreateObject(scene::SceneObjectDesc{ .name = "Navigation Bounds" });
            scene.Components().NavBounds().Set(holder.Entity(), box);
        }
        Check(scene::SceneDocumentService::Save(scene, root / "Bounds.21kbscene", "Bounds"), "a scene with Navigation Bounds saves");
        return nav::BakeSceneNavMesh(root / "Bounds.21kbscene", Settings(), nullptr);
    };
    scene::NavBounds narrow{ .size = { 40.0F, 10.0F, 40.0F } };
    scene::NavBounds wide = narrow;
    wide.agentRadius = 1.2F;
    const nav::NavSceneBakeResult narrowBake = bake({ narrow });
    const nav::NavSceneBakeResult wideBake = bake({ wide });
    Check(narrowBake.succeeded && wideBake.succeeded, "scenes with Navigation Bounds bake: " + narrowBake.error + wideBake.error);
    Check(narrowBake.asset.settings == nav::NavBoundsBuildSettings(narrow) && wideBake.asset.settings.profiles[0].radius == 1.2F,
        "a scene with Navigation Bounds bakes with their settings, not the ones passed in");
    const double narrowArea = WalkableArea(narrowBake.asset);
    const double wideArea = WalkableArea(wideBake.asset);
    Check(wideArea < narrowArea - 20.0, "a bigger agent radius leaves a smaller walkable area: " + std::to_string(wideArea) + " vs " + std::to_string(narrowArea));
    const nav::NavSceneBakeResult mixed = bake({ narrow, wide });
    Check(!mixed.succeeded && mixed.error.find("different bake settings") != std::string::npos,
        "Navigation Bounds that disagree on the settings are refused: " + mixed.error);
    std::filesystem::remove_all(root);
}

void TestBoundsPersist() {
    const std::filesystem::path root = FreshDirectory("bounds_persist");
    const scene::NavBounds saved{ .center = { 1.0F, 2.0F, 3.0F }, .size = { 4.0F, 5.0F, 6.0F }, .agentRadius = 0.7F, .agentHeight = 1.6F,
        .maxStepHeight = 0.35F, .maxSlopeDegrees = 38.0F, .cellSize = 0.3F, .tileSize = 12.0F };
    {
        scene::Scene scene;
        const scene::SceneObject holder = scene.Entities().CreateObject(scene::SceneObjectDesc{ .name = "Navigation Bounds" });
        scene.Components().NavBounds().Set(holder.Entity(), saved);
        Check(scene::SceneDocumentService::Save(scene, root / "Bounds.21kbscene", "Bounds"), "a scene with Navigation Bounds saves");
    }
    scene::Scene loaded;
    Check(scene::SceneDocumentService::LoadFileIntoScene(loaded, root / "Bounds.21kbscene"), "a scene with Navigation Bounds loads");
    struct Found {
        scene::Scene* scene = nullptr;
        std::vector<scene::NavBounds> bounds;
    } found{ .scene = &loaded, .bounds = {} };
    loaded.Transforms().ForEach([](scene::SceneEntity entity, const scene::TransformComponent&, void* context) {
        auto* search = static_cast<Found*>(context);
        if (const scene::NavBounds* bounds = search->scene->Components().NavBounds().TryGet(entity); bounds != nullptr) search->bounds.push_back(*bounds);
    }, &found);
    Check(found.bounds.size() == 1U && std::memcmp(&found.bounds[0], &saved, sizeof(saved)) == 0, "a Navigation Bounds keeps every field through a save and a load");
    std::filesystem::remove_all(root);

    // Files from before Navigation Bounds still read; a Navigation Bounds in a file that claims to be older is refused.
    scene::ScenePrefabNodeComponents plain;
    plain.navLink = scene::NavLink{};
    std::vector<std::uint8_t> plainBytes;
    scene::SceneAssetComponentCodec::Write(plainBytes, plain);
    scene::SceneAssetBinaryIO::ByteReader plainReader{ plainBytes };
    scene::ScenePrefabNodeComponents plainRead;
    Check(scene::SceneAssetComponentCodec::Read(plainReader, scene::SceneDocument::NavBoundsFileVersion - 1U, plainRead) && plainRead.navLink.has_value() &&
        !plainRead.navBounds.has_value(), "components written without Navigation Bounds read as an older file");
    scene::ScenePrefabNodeComponents withBounds;
    withBounds.navBounds = saved;
    std::vector<std::uint8_t> boundsBytes;
    scene::SceneAssetComponentCodec::Write(boundsBytes, withBounds);
    scene::SceneAssetBinaryIO::ByteReader oldReader{ boundsBytes };
    scene::ScenePrefabNodeComponents oldRead;
    Check(!scene::SceneAssetComponentCodec::Read(oldReader, scene::SceneDocument::NavBoundsFileVersion - 1U, oldRead),
        "a file older than Navigation Bounds cannot hold one");
}

[[nodiscard]] nav::NavGeometry CollectNodes(const std::vector<scene::ScenePrefabNodeDesc>& nodes) {
    nav::NavGeometry geometry;
    nav::NavGeometryCollectStats stats;
    nav::CollectNavGeometry(nodes, Settings(), nullptr, geometry, stats);
    return geometry;
}

// Moves `nodes` from one geometry to the next: only the changed tiles are rebuilt, and they give the full bake.
void CheckEditRebuildsItsTiles(const std::vector<scene::ScenePrefabNodeDesc>& before, const std::vector<scene::ScenePrefabNodeDesc>& after,
    const std::string& edit, bool fewerThanAll = true) {
    const nav::NavGeometry oldGeometry = CollectNodes(before);
    const nav::NavGeometry newGeometry = CollectNodes(after);
    const nav::NavMeshBakeResult baked = nav::NavMeshBuilder::Bake(Settings(), oldGeometry);
    const nav::NavMeshBakeResult full = nav::NavMeshBuilder::Bake(Settings(), newGeometry);
    const std::optional<std::vector<nav::NavTileCoord>> changed = nav::ChangedNavTiles(Settings(), oldGeometry, newGeometry);
    Check(baked.succeeded && full.succeeded && changed.has_value() && !changed->empty(), edit + ": the edit names the tiles it changes");
    Check(!fewerThanAll || changed->size() < full.tiles.size(), edit + ": " + std::to_string(changed->size()) + " of " + std::to_string(full.tiles.size()) +
        " tiles are rebuilt, not all of them");
    const nav::NavMeshBakeResult rebuilt = nav::NavMeshBuilder::RebuildTiles(Settings(), newGeometry, baked.tiles, *changed);
    Check(rebuilt.succeeded && rebuilt.tiles == full.tiles, edit + ": rebuilding the changed tiles gives the full bake of the edited scene");
    Check(baked.tiles != full.tiles, edit + ": the edit changes the navigation mesh");
}

void TestEditsRebuildOnlyTheirTiles() {
    const scene::ScenePrefabNodeDesc floor = Slab(-40.0, 40.0, -40.0, 40.0, 0.0);
    const scene::ScenePrefabNodeDesc pillar = BoxNode(DVec3{ 5.0, 1.0, 5.0 }, Vec3{ 2.0F, 2.0F, 2.0F });
    const scene::ScenePrefabNodeDesc moved = BoxNode(DVec3{ -20.0, 1.0, -20.0 }, Vec3{ 2.0F, 2.0F, 2.0F });
    const nav::NavGeometry still = CollectNodes({ floor, pillar });
    const std::optional<std::vector<nav::NavTileCoord>> none = nav::ChangedNavTiles(Settings(), still, CollectNodes({ floor, pillar }));
    Check(none.has_value() && none->empty(), "geometry that did not change rebuilds no tile");
    CheckEditRebuildsItsTiles({ floor, pillar }, { floor, moved }, "a moved pillar");
    CheckEditRebuildsItsTiles({ floor, pillar }, { floor }, "a removed pillar");
    CheckEditRebuildsItsTiles({ floor }, { floor, pillar }, "an added pillar");
    const scene::ScenePrefabNodeDesc bounds = BoundsNode(DVec3{ 0.0, 0.0, 0.0 }, Vec3{ 30.0F, 4.0F, 30.0F });
    const scene::ScenePrefabNodeDesc grown = BoundsNode(DVec3{ 10.0, 0.0, 0.0 }, Vec3{ 30.0F, 4.0F, 30.0F });
    // A box moving across the floor changes every tile it covered or covers now, but not the other box's.
    const scene::ScenePrefabNodeDesc far = BoundsNode(DVec3{ -30.0, 0.0, -30.0 }, Vec3{ 10.0F, 4.0F, 10.0F });
    CheckEditRebuildsItsTiles({ floor, pillar, bounds, far }, { floor, pillar, grown, far }, "a moved Navigation Bounds", false);
    Check(!nav::ChangedNavTiles(Settings(), CollectNodes({ floor }), CollectNodes({ floor, bounds })).has_value(),
        "the first Navigation Bounds changes every tile, so the whole scene is baked again");
}

void TestBackgroundBakeMatchesTheSynchronousOne() {
    const std::filesystem::path root = FreshDirectory("background");
    scene::Scene source;
    static_cast<void>(AddCollider(source, DVec3{ 0.0, -0.25, 0.0 }, Vec3{ 60.0F, 0.5F, 60.0F }));
    static_cast<void>(AddCollider(source, DVec3{ 3.0, 1.0, -2.0 }, Vec3{ 2.0F, 2.0F, 2.0F }));
    const std::filesystem::path path = root / "Background.21kbscene";
    Check(scene::SceneDocumentService::Save(source, path, "Background"), "the scene saves");
    const nav::NavSceneBakeResult synchronous = nav::BakeSceneNavMesh(path, Settings(), nullptr);
    Check(synchronous.succeeded && !synchronous.asset.tiles.empty(), "the saved scene bakes: " + synchronous.error);
    // The editor collects the open scene on its thread and bakes it on a worker.
    const scene::SceneDocument captured = scene::SceneDocumentService::Capture(source, "Background");
    const nav::NavScenePreparation prepared = nav::PrepareSceneNavBake(captured.worldPrefab.Nodes(), Settings(), nullptr);
    Check(prepared.succeeded, "the open scene's geometry is collected: " + prepared.error);
    nav::NavBakeProgress progress;
    std::thread worker;
    nav::NavSceneBakeResult background;
    worker = std::thread([&prepared, &progress, &background]() { background = nav::BakePreparedSceneNavMesh(prepared, &progress); });
    worker.join();
    Check(background.succeeded && background.asset.settings == synchronous.asset.settings && background.asset.tiles == synchronous.asset.tiles,
        "a bake on a worker gives the same navigation mesh as the synchronous bake of the saved scene");
    Check(progress.total.load() > 0U && progress.built.load() == progress.total.load(), "the bake reports every tile it built");
    std::filesystem::remove_all(root);
}

void TestDebugGeometry() {
    const nav::NavMeshAsset floor = BakeNodes({ Slab(-10.0, 10.0, -10.0, 10.0, 0.0) });
    const auto shared = std::make_shared<const nav::NavMeshAsset>(floor);
    const nav::NavMeshDebugGeometry drawn = nav::NavMeshAssetDebugGeometry(shared, 0U);
    const std::vector<DVec3> triangles = nav::NavMeshAssetTriangles(shared, 0U);
    Check(drawn.triangles == triangles && drawn.triangleAreas.size() * 3U == triangles.size(), "the debug view fills the same triangles the mesh has");
    Check(std::ranges::all_of(drawn.triangleAreas, [](std::uint8_t area) { return area == scene::kDefaultNavArea; }), "an unpainted floor is all ground");
    Check(drawn.tiles.size() == floor.tiles.size(), "every tile's bounds are drawn");
    std::size_t boundary = 0U;
    for (const nav::NavMeshDebugGeometry::Edge& edge : drawn.edges) {
        if (edge.boundary) ++boundary;
    }
    Check(boundary >= 4U && drawn.edges.size() < triangles.size(), "polygon edges are drawn without the diagonals inside polygons: " +
        std::to_string(drawn.edges.size()) + " edges for " + std::to_string(triangles.size() / 3U) + " triangles");
    for (const nav::NavMeshDebugGeometry::Edge& edge : drawn.edges) {
        if (!edge.boundary) continue;
        const bool onRim = std::fabs(std::fabs(edge.from.x) - 9.6) < 0.3 || std::fabs(std::fabs(edge.from.z) - 9.6) < 0.3;
        Check(onRim, "an outer edge lies on the rim of the walkable floor");
    }
}

void TestObjectsThatDoNotAffectNavigation() {
    const scene::ScenePrefabNodeDesc floor = Slab(-40.0, 40.0, -40.0, 40.0, 0.0);
    const scene::ScenePrefabNodeDesc pillar = BoxNode(DVec3{ 5.0, 1.0, 5.0 }, Vec3{ 2.0F, 2.0F, 2.0F });
    scene::ScenePrefabNodeDesc ignored = pillar;
    ignored.components.collider->affectsNavigation = false;
    const nav::NavMeshBakeResult bare = nav::NavMeshBuilder::Bake(Settings(), CollectNodes({ floor }));
    const nav::NavMeshBakeResult withIgnored = nav::NavMeshBuilder::Bake(Settings(), CollectNodes({ floor, ignored }));
    const nav::NavMeshBakeResult withPillar = nav::NavMeshBuilder::Bake(Settings(), CollectNodes({ floor, pillar }));
    Check(bare.succeeded && withIgnored.succeeded && withPillar.succeeded && withIgnored.tiles == bare.tiles && withPillar.tiles != bare.tiles,
        "a collider that does not affect navigation adds nothing to the bake");
    // Switching the flag is an edit like any other: only the pillar's tiles are rebuilt, to the full bake.
    CheckEditRebuildsItsTiles({ floor, pillar }, { floor, ignored }, "a pillar that stops affecting navigation");
    CheckEditRebuildsItsTiles({ floor, ignored }, { floor, pillar }, "a pillar that starts affecting navigation");

    // A mesh renderer is left out before its mesh is even looked for.
    scene::ScenePrefabNodeDesc mesh;
    mesh.name = "Mesh";
    mesh.components.meshRenderer = scene::MeshRendererComponent{ .meshAssetId = 7U };
    nav::NavGeometry geometry;
    nav::NavGeometryCollectStats stats;
    nav::CollectNavGeometry(std::vector<scene::ScenePrefabNodeDesc>{ mesh }, Settings(), nullptr, geometry, stats);
    Check(stats.unresolved == 1U, "a mesh that affects navigation is looked for");
    mesh.components.meshRenderer->affectsNavigation = false;
    nav::NavGeometryCollectStats skipped;
    nav::CollectNavGeometry(std::vector<scene::ScenePrefabNodeDesc>{ mesh }, Settings(), nullptr, geometry, skipped);
    Check(skipped.unresolved == 0U && skipped.meshes == 0U, "a mesh that does not affect navigation is not collected");

    // The flag survives a save and a load; files from before it load with it on.
    const std::filesystem::path root = FreshDirectory("affects_navigation");
    {
        scene::Scene saved;
        const scene::SceneObject holder = saved.Entities().CreateObject(scene::SceneObjectDesc{ .name = "Grass" });
        saved.Components().Colliders().Set(holder.Entity(), scene::ColliderComponent{ .affectsNavigation = false });
        saved.Components().MeshRenderers().Set(holder.Entity(), scene::MeshRendererComponent{ .affectsNavigation = false });
        Check(scene::SceneDocumentService::Save(saved, root / "Grass.21kbscene", "Grass"), "a scene with objects that do not affect navigation saves");
    }
    scene::Scene loaded;
    Check(scene::SceneDocumentService::LoadFileIntoScene(loaded, root / "Grass.21kbscene"), "that scene loads");
    struct Found {
        scene::Scene* scene = nullptr;
        int off = 0;
    } found{ .scene = &loaded };
    loaded.Transforms().ForEach([](scene::SceneEntity entity, const scene::TransformComponent&, void* context) {
        auto* search = static_cast<Found*>(context);
        const scene::ColliderComponent* collider = search->scene->Components().Colliders().TryGet(entity);
        const scene::MeshRendererComponent* renderer = search->scene->Components().MeshRenderers().TryGet(entity);
        search->off += (collider != nullptr && !collider->affectsNavigation ? 1 : 0) + (renderer != nullptr && !renderer->affectsNavigation ? 1 : 0);
    }, &found);
    Check(found.off == 2, "the collider and the mesh renderer keep affecting navigation off through a save and a load");
    std::filesystem::remove_all(root);
    std::vector<std::uint8_t> colliderBytes;
    scene::SceneAssetPhysicsComponentCodec::WriteCollider(colliderBytes, scene::ColliderComponent{ .affectsNavigation = false });
    colliderBytes.pop_back();
    scene::SceneAssetBinaryIO::ByteReader colliderReader{ colliderBytes };
    scene::ColliderComponent oldCollider;
    Check(scene::SceneAssetPhysicsComponentCodec::ReadCollider(colliderReader, scene::SceneDocument::AffectsNavigationFileVersion - 1U, oldCollider) &&
        oldCollider.affectsNavigation, "a collider from an older file affects navigation");
    std::vector<std::uint8_t> rendererBytes;
    scene::SceneAssetRenderComponentCodec::WriteMeshRenderer(rendererBytes, scene::MeshRendererComponent{ .affectsNavigation = false });
    rendererBytes.pop_back();
    scene::SceneAssetBinaryIO::ByteReader rendererReader{ rendererBytes };
    scene::MeshRendererComponent oldRenderer;
    Check(scene::SceneAssetRenderComponentCodec::ReadMeshRenderer(rendererReader, scene::SceneDocument::AffectsNavigationFileVersion - 1U, oldRenderer) &&
        oldRenderer.affectsNavigation, "a mesh renderer from an older file affects navigation");

    // Scripts read and write the flag like any other field.
    scene::Scene scripted;
    const scene::SceneObject object = scripted.Entities().CreateObject(scene::SceneObjectDesc{ .name = "Scripted" });
    scripted.Components().Colliders().Set(object.Entity(), scene::ColliderComponent{});
    const auto set = kb::script::ScriptSceneComponentApi::SetProperty(scripted, object.Entity(), "Collider", "affectsNavigation", kb::script::ScriptValue{ false });
    Check(set.succeeded && !scripted.Components().Colliders().TryGet(object.Entity())->affectsNavigation, "a script turns a collider's affects navigation off");
}

void TestNamedAreas() {
    const std::vector<scene::NavAreaDefinition> defaults = scene::DefaultNavAreaDefinitions();
    Check(defaults.size() == 5U && defaults[0].name == "Walkable" && defaults[4].name == "Not walkable" && defaults[4].cost == 0.0F &&
        scene::AreNavAreaDefinitionsValid(defaults), "the default areas are Walkable, Road, Grass, Water and Not walkable");
    Check(scene::FindNavArea(defaults, "water") == std::optional<scene::NavAreaId>{ 3U } && !scene::FindNavArea(defaults, "Lava").has_value(),
        "an area name resolves to its id, whatever its letter case");
    std::vector<scene::NavAreaDefinition> repeated = defaults;
    repeated.push_back(scene::NavAreaDefinition{ .name = "ROAD" });
    Check(!scene::AreNavAreaDefinitionsValid(repeated), "two areas cannot share a name");

    // The list round-trips through the project's settings file; a file without it keeps the defaults.
    const std::filesystem::path root = FreshDirectory("named_areas");
    kb::project::ProjectSettings settings;
    Check(settings.navigationAreas == defaults, "a new project starts with the default areas");
    settings.navigationAreas = { scene::NavAreaDefinition{ .name = "Floor", .colour = 0x123456U, .cost = 1.0F },
        scene::NavAreaDefinition{ .name = "Mud", .colour = 0xABCDEFU, .cost = 4.5F }, scene::NavAreaDefinition{ .name = "Pit", .colour = 0U, .cost = 0.0F } };
    settings.navigationNearDistance = 25.0F;
    settings.navigationMidDistance = 70.0F;
    settings.navigationFarPathInterval = 8U;
    std::string error;
    Check(kb::project::ProjectSettingsStore::Save(root / "ProjectSettings.ini", settings, error), "project settings save: " + error);
    const kb::project::ProjectSettingsLoadResult reloaded = kb::project::ProjectSettingsStore::Load(root / "ProjectSettings.ini");
    Check(reloaded.Succeeded() && reloaded.settings.navigationAreas == settings.navigationAreas, "named areas keep their names, colours and costs");
    const scene::NavigationCrowdSettings crowd = reloaded.settings.NavigationCrowd();
    Check(crowd.nearDistance == 25.0F && crowd.midDistance == 70.0F && crowd.farPathOptimizationInterval == 8U,
        "the crowd's level of detail round-trips through the project's settings");
    const kb::project::ProjectSettingsLoadResult missing = kb::project::ProjectSettingsStore::Load(root / "Missing.ini");
    Check(missing.settings.navigationAreas == defaults, "settings without areas use the default areas");
    Check(missing.settings.NavigationCrowd().nearDistance == scene::NavigationCrowdSettings{}.nearDistance &&
            missing.settings.NavigationCrowd().midDistance == scene::NavigationCrowdSettings{}.midDistance,
        "settings without a crowd level of detail use the engine's defaults");
    std::filesystem::remove_all(root);

    // A scene takes the project's costs: a strip of Not walkable is never entered, one of Water is walked around.
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes({ Slab(-10.0, 10.0, -10.0, 10.0, 0.0) })));
    const scene::SceneEntity strip = AddObstacle(scene, { 0.0F, 0.0F, -2.0F },
        scene::NavObstacle{ .shape = scene::NavObstacleShape::Box, .size = { 2.0F, 2.0F, 16.0F }, .area = 3U, .carve = false });
    const Vec3 from{ -8.0F, 0.0F, 0.0F };
    const Vec3 to{ 8.0F, 0.0F, 0.0F };
    Check(std::fabs(Path(scene, from, to).length - 16.0F) < 0.05F, "without the project's areas every area costs 1");
    scene.Navigation().SetAreaDefinitions(defaults);
    Check(scene.Navigation().FindArea("Water") == std::optional<scene::NavAreaId>{ 3U } && scene.Navigation().AreaCost(3U) == 10.0F,
        "the scene knows the project's areas and their costs");
    const scene::NavPathResult aroundWater = Path(scene, from, to);
    Check(aroundWater.status == scene::NavPathStatus::Complete && aroundWater.length > 20.0F, "a costly area is walked around");
    scene.Components().NavObstacles().TryGet(strip)->area = 2U;
    scene.Components().NavObstacles().TryGet(strip)->size.z = 30.0F;
    scene.Components().NavObstacles().MarkModified(strip);
    static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Path(scene, from, to).status == scene::NavPathStatus::Complete, "a cheap area across the whole floor is crossed");
    scene.Components().NavObstacles().TryGet(strip)->area = 4U;
    scene.Components().NavObstacles().MarkModified(strip);
    static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Path(scene, from, to).status != scene::NavPathStatus::Complete, "a Not walkable area across the whole floor cannot be crossed");

    // Scripts name areas instead of numbering them.
    kb::script::ScriptRuntimeHost host{ scene };
    const kb::script::ScriptFunctionCallContext call{ .scene = &scene };
    const kb::script::ScriptFunctionCallResult found = host.Functions().Call("Navigation.FindArea",
        std::vector<kb::script::ScriptFunctionArgument>{ { .name = "name", .value = kb::script::ScriptValue{ std::string{ "grass" } } } }, call);
    Check(found.Succeeded() && found.Output("found")->AsBool() && found.Output("area")->AsInt() == 2, "Navigation.FindArea resolves an area's name");
    const kb::script::ScriptFunctionCallResult unknown = host.Functions().Call("Navigation.FindArea",
        std::vector<kb::script::ScriptFunctionArgument>{ { .name = "name", .value = kb::script::ScriptValue{ std::string{ "Lava" } } } }, call);
    Check(unknown.Succeeded() && !unknown.Output("found")->AsBool() && unknown.Output("area")->AsInt() == -1, "an unknown area name finds nothing");
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

// An agent whose navigation mesh is removed while it walks stops where it is, with a finite position and a failed
// path; when a mesh comes back it plans again from where it stands and arrives.
void TestAgentLosesItsNavigationMesh() {
    scene::Scene scene;
    const std::uint64_t handle = AddMesh(scene, BakeNodes({ Slab(-10.0, 10.0, -10.0, 10.0, 0.0) }));
    const Vec3 goal{ 8.0F, 0.0F, 0.0F };
    const scene::SceneEntity agent = AddAgent(scene, DVec3{ -8.0, 0.0, 0.0 }, goal, 0.4F, 2.0F);
    for (int frame = 0; frame < 60 * 3; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    const Vec3 walking = Position(scene, agent);
    Check(walking.x > -7.0F && walking.x < 6.0F && Agent(scene, agent).pathStatus == scene::NavPathStatus::Complete,
        "the agent is on its way across the arena");
    Check(scene.Navigation().RemoveNavMesh(handle), "the navigation mesh is removed while the agent walks");
    for (int frame = 0; frame < 60; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    const Vec3 stopped = Position(scene, agent);
    for (int frame = 0; frame < 60 * 2; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    const Vec3 later = Position(scene, agent);
    Check(std::isfinite(later.x) && std::isfinite(later.y) && std::isfinite(later.z), "an agent without a mesh keeps a finite position");
    Check(Horizontal(stopped, later) < 0.05F, "an agent without a mesh does not keep walking");
    Check(Agent(scene, agent).pathStatus != scene::NavPathStatus::Complete, "an agent without a mesh no longer reports a complete path");
    static_cast<void>(AddMesh(scene, BakeNodes({ Slab(-10.0, 10.0, -10.0, 10.0, 0.0) })));
    for (int frame = 0; frame < 60 * 10; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Horizontal(Position(scene, agent), goal) <= 0.15F && Agent(scene, agent).pathStatus == scene::NavPathStatus::Complete,
        "an agent plans again when a mesh returns and arrives");
}

// Two profiles keep their own caches: a doorway that only the large agent's profile cannot pass is closed by an
// obstacle that is wider than the doorway for the large profile only, and changing an agent's radius moves it to
// the other profile at once.
void TestAgentProfilesStayIndependent() {
    nav::NavMeshBuildSettings settings = Settings();
    settings.profiles = { nav::NavAgentProfile{ .name = "Small", .radius = 0.3F }, nav::NavAgentProfile{ .name = "Large", .radius = 1.0F } };
    const std::vector<scene::ScenePrefabNodeDesc> nodes{
        Slab(-10.0, 10.0, -10.0, 10.0, 0.0),
        BoxNode(DVec3{ 0.0, 1.5, -5.35 }, Vec3{ 0.4F, 3.0F, 9.3F }),
        BoxNode(DVec3{ 0.0, 1.5, 5.35 }, Vec3{ 0.4F, 3.0F, 9.3F }),
    };
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, BakeNodes(nodes, settings)));
    const Vec3 to{ 6.0F, 0.0F, 0.0F };
    const Vec3 nearDoor{ -2.0F, 0.0F, 0.0F };
    const scene::SceneEntity walker = AddAgent(scene, DVec3{ -6.0, 0.0, 0.0 }, nearDoor, 0.25F);
    for (int frame = 0; frame < 60 * 6; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Horizontal(Position(scene, walker), nearDoor) <= 0.15F, "an agent of the small radius walks to the doorway");
    // The agent grows: its profile changes with its radius and the doorway closes for it.
    scene::NavAgent changed = Agent(scene, walker);
    changed.radius = 0.8F;
    changed.destination = to;
    scene.Components().NavAgents().Set(walker, changed);
    for (int frame = 0; frame < 60 * 6; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Agent(scene, walker).pathStatus == scene::NavPathStatus::Partial && Position(scene, walker).x < -0.4F,
        "after its radius grew the agent uses the large profile and no longer fits through the doorway");
    // It shrinks again: the small profile's path is complete again and it walks through.
    changed = Agent(scene, walker);
    changed.radius = 0.25F;
    scene.Components().NavAgents().Set(walker, changed);
    for (int frame = 0; frame < 60 * 10; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Horizontal(Position(scene, walker), to) <= 0.15F && Agent(scene, walker).pathStatus == scene::NavPathStatus::Complete,
        "after its radius shrank the agent uses the small profile again and walks through the doorway");
}

void TestOnlyChangedProfileReplans() {
    auto settings = Settings();
    settings.profiles = { nav::NavAgentProfile{ .name = "Small", .radius = 0.3F },
        nav::NavAgentProfile{ .name = "Large", .radius = 1.0F } };
    auto initial = BakeNodes({ Slab(-10.0, 20.0, -6.0, 6.0, 0.0) }, settings);
    auto extension = initial;
    std::erase_if(initial.tiles, [](const auto& tile) { return tile.coord.x >= 0; });
    std::erase_if(extension.tiles, [](const auto& tile) { return tile.coord.x < 0 || tile.profile != 0U; });
    scene::Scene scene;
    static_cast<void>(AddMesh(scene, initial));
    const auto small = AddAgent(scene, { -6.0, 0.0, -2.0 }, { 15.0F, 0.0F, -2.0F }, 0.25F);
    const auto large = AddAgent(scene, { -6.0, 0.0, 2.0 }, { 15.0F, 0.0F, 2.0F }, 0.8F);
    for (int frame = 0; frame < 60; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Agent(scene, small).pathStatus == scene::NavPathStatus::Failed &&
        Agent(scene, large).pathStatus == scene::NavPathStatus::Failed, "both profiles must initially wait for their destination tiles");
    static_cast<void>(AddMesh(scene, extension));
    static_cast<void>(scene.Runtime().Update(kFrame));
    Check(scene.Navigation().CrowdStats().pathRequests == 1U,
        "streaming tiles for one profile must not invalidate another profile's failed path cache");
    for (int frame = 0; frame < 600; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Position(scene, small).x > 14.0F && Position(scene, large).x < -5.5F,
        "only the profile with a loaded route may advance to its destination");
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
        const kb::world::WorldObjectFile floor = FloorObject("floor" + std::to_string(cell),
            cell * 32.0 + (cell == 2 ? 2.0 : 0.0), cell * 32.0 + 32.0 - (cell == 1 ? 2.0 : 0.0), 2.0, 22.0);
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
    // A second agent stands in a streamed cell and is sent to a cell that is not streamed yet.
    const scene::SceneEntity traveller = AddAgent(scene, DVec3{ 50.0, 0.0, 16.0 }, Vec3{ 120.0F, 0.0F, 16.0F }, 0.4F, 8.0F);
    const auto borderLink = AddLink(scene, { 61.2F, 0.0F, 16.0F }, scene::NavLink{
        .end = { 5.6F, 0.0F, 0.0F }, .radius = 0.6F, .kind = scene::NavLinkKind::Jump, .enabled = false });
    const auto run = [&](int frames) {
        for (int frame = 0; frame < frames; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    };
    const auto finite = [](Vec3 position) { return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z); };
    run(120);
    Check(finite(Position(scene, traveller)) && Position(scene, traveller).x <= 64.5F && Position(scene, traveller).x >= 49.0F &&
              Agent(scene, traveller).pathStatus == scene::NavPathStatus::Failed,
        "an agent sent to a cell that is not streamed waits on the ground it knows instead of walking into the void");
    Check(finite(Position(scene, agent)) && Horizontal(Position(scene, agent), { 4.0F, 0.0F, 16.0F }) < 0.5F,
        "an agent with no known way to its destination stays where it is");

    Check(runtime.UpdateSource(source, { .position = { 96.0, 0.0, 16.0 }, .loadRadius = 40.0, .unloadRadius = 60.0, .priority = 0 }), "move the source");
    Check(settle([&] {
        return runtime.IsNavMeshLoaded(owner.Entity(), { 3, 0 }) && runtime.IsNavMeshLoaded(owner.Entity(), { 2, 0 }) &&
            !runtime.IsNavMeshLoaded(owner.Entity(), { 0, 0 });
    }), "tiles follow the source: new cells stream in, cells left behind stream out");
    Check(Path(scene, { 70.0F, 0.0F, 16.0F }, { 120.0F, 0.0F, 16.0F }).status == scene::NavPathStatus::Complete,
        "paths use the newly streamed cells");
    run(240);
    Check(Position(scene, traveller).x < 62.0F && Agent(scene, traveller).pathStatus == scene::NavPathStatus::Partial,
        "a traveller must wait at a disconnected cell border while its dynamic link is disabled");
    scene.Components().NavLinks().TryGet(borderLink)->enabled = true;
    run(900);
    Check(Horizontal(Position(scene, traveller), { 120.0F, 0.0F, 16.0F }) <= 0.5F && Agent(scene, traveller).pathStatus == scene::NavPathStatus::Complete,
        "the waiting agent replans when its destination streams in and walks there across the cell border");
    Check(finite(Position(scene, agent)) && Horizontal(Position(scene, agent), { 4.0F, 0.0F, 16.0F }) < 0.5F,
        "an agent whose own tile streamed out stays on its place instead of leaving the loaded world");
    // The cells around the traveller stream out again: it stays where it stands, finite and inside the strip.
    Check(runtime.UpdateSource(source, { .position = { 16.0, 0.0, 16.0 }, .loadRadius = 40.0, .unloadRadius = 60.0, .priority = 0 }), "move the source back");
    Check(settle([&] { return !runtime.IsNavMeshLoaded(owner.Entity(), { 3, 0 }) && runtime.IsNavMeshLoaded(owner.Entity(), { 0, 0 }); }),
        "cells behind the source stream out and the first cells stream in again");
    run(300);
    const Vec3 stranded = Position(scene, traveller);
    Check(finite(stranded) && stranded.x >= 0.0F && stranded.x <= 128.5F && std::fabs(stranded.z - 16.0F) < 6.0F,
        "an agent on a tile that streamed out stays inside the strip with a finite position");
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
    TestInterruptedLinkCrossing();
    TestFarFromTheWorldOrigin();
    TestCrowdIsDeterministic();
    TestCrowdLevelOfDetail();
    TestCrowdLevelOfDetailTransitions();
    TestCrowdPathBudgetPriority();
    TestHordeCrossesLevels();
    TestHordeIsDeterministic();
    TestHordeFieldFollowsItsTarget();
    TestHordeFarAgentsFollowTheRoute();
    TestHordeKeepsOffOtherBodies();
    TestNavComponentsPersist();
    TestBoundsLimitTheBake();
    TestBoundsBakeSettings();
    TestBoundsPersist();
    TestEditsRebuildOnlyTheirTiles();
    TestBackgroundBakeMatchesTheSynchronousOne();
    TestDebugGeometry();
    TestObjectsThatDoNotAffectNavigation();
    TestNamedAreas();
    TestNavigationScriptApi();
    TestPlacedNavigationMesh();
    TestTilesStreamWithCells();
    TestAgentLosesItsNavigationMesh();
    TestAgentProfilesStayIndependent();
    TestOnlyChangedProfileReplans();
}

// Crowd cost on this machine, for the performance gate: 1000 agents must step well inside a frame.
void RunNavigationCrowdBenchmark() {
    // A local geometry edit: four 12.8 m tiles, with a floor and one wall.
    scene::Scene rebakeScene;
    const std::vector<scene::ScenePrefabNodeDesc> geometry{
        Slab(-10.0, 10.0, -3.0, 3.0, 0.0), BoxNode({ 0.0, 1.5, 0.0 }, { 0.5F, 3.0F, 6.0F }) };
    static_cast<void>(AddMesh(rebakeScene, BakeNodes(geometry)));
    static_cast<void>(AddCollider(rebakeScene, { 0.0, -0.25, 0.0 }, { 20.0F, 0.5F, 6.0F }));
    static_cast<void>(AddCollider(rebakeScene, { 0.0, 1.5, 0.0 }, { 0.5F, 3.0F, 6.0F }));
    std::vector<double> rebakeTimes;
    std::size_t rebuiltTiles = 0U;
    for (int sample = 0; sample < 10; ++sample) {
        const auto begin = std::chrono::steady_clock::now();
        rebuiltTiles = rebakeScene.Navigation().RebakeTiles({ -1.0, 0.0, -4.0 }, { 1.0, 0.0, 4.0 });
        const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        if (sample > 0) rebakeTimes.push_back(elapsed);
    }
    std::ranges::sort(rebakeTimes);
    Check(rebuiltTiles == 4U && rebakeTimes[4] < 8.0 * kSanitizerTimeScale,
        "a four-tile local rebake must fit the 8 ms median budget: " + std::to_string(rebakeTimes[4]));
    std::cout << "navigation: local rebake of " << rebuiltTiles << " tiles, median " << rebakeTimes[4] << " ms\n";
    constexpr double kHalf = 100.0;
    const auto arena = std::make_shared<const nav::NavMeshAsset>(CrowdArena(kHalf));
    const auto bakeStart = std::chrono::steady_clock::now();
    static_cast<void>(CrowdArena(kHalf));
    const double bakeMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bakeStart).count();
    const CrowdRun thousand = RunCrowd(arena, kHalf, 1000, 240);
    // The two 5000-agent runs alternate and their fastest runs are compared: a run on workers that
    // other load preempts swings by more than the level of detail saves.
    std::vector<double> fullRuns;
    std::vector<double> lodRuns;
    for (int round = 0; round < 3; ++round) {
        fullRuns.push_back(RunCrowd(arena, kHalf, 5000, 240).milliseconds);
        lodRuns.push_back(RunCrowd(arena, kHalf, 5000, 240, { DVec3{ -50.0, 0.0, -50.0 } }).milliseconds);
    }
    std::ranges::sort(fullRuns);
    std::ranges::sort(lodRuns);
    const CrowdRun fiveThousand{ .milliseconds = fullRuns[0] };
    const CrowdRun fiveThousandLod{ .milliseconds = lodRuns[0] };
    std::cout << "navigation: bake of a 200 x 200 m arena with 25 pillars " << bakeMilliseconds << " ms ("
              << arena->tiles.size() << " tiles)\n";
    std::cout << "navigation: crowd step, 1000 agents " << thousand.milliseconds << " ms, 5000 agents " << fiveThousand.milliseconds
              << " ms, 5000 agents with level of detail around one focus " << fiveThousandLod.milliseconds << " ms\n";
    Check(thousand.milliseconds < 8.0 * kSanitizerTimeScale, "1000 crowd agents must step within 8 ms: " + std::to_string(thousand.milliseconds));

    const CrowdRun tenThousand = RunCrowd(arena, kHalf, 10000, 240);
    const CrowdRun tenThousandOneThread = RunCrowd(arena, kHalf, 10000, 240, {}, scene::NavigationCrowdSettings{ .workerThreads = 1U });
    // 600 measured steps: the slowest one shows the spikes a frame would see.
    const CrowdRun twentyThousand = RunCrowd(arena, kHalf, 20000, 1200);
    std::cout << "navigation: crowd step, 10000 agents " << tenThousand.milliseconds << " ms (slowest " << tenThousand.worst
              << " ms; one thread " << tenThousandOneThread.milliseconds << " ms), 20000 agents " << twentyThousand.milliseconds
              << " ms (slowest " << twentyThousand.worst << " ms over 600 steps)\n";
    const CrowdRun fiveThousandStages = RunCrowd(arena, kHalf, 5000, 240);
    const CrowdRun fiveThousandLodStages = RunCrowd(arena, kHalf, 5000, 240, { DVec3{ -50.0, 0.0, -50.0 } });
    for (const auto& [label, stages] : { std::pair{ "5000 agents", fiveThousandStages.stages }, std::pair{ "5000 agents with level of detail", fiveThousandLodStages.stages },
             std::pair{ "10000 agents", tenThousand.stages }, std::pair{ "10000 agents on one thread", tenThousandOneThread.stages },
             std::pair{ "the slowest of 20000 agents", twentyThousand.worstStages } }) {
        std::cout << "navigation: stages of a step, " << label << ": sync " << stages.syncMilliseconds << " ms, paths " << stages.pathMilliseconds
                  << " ms, neighbours " << stages.neighbourMilliseconds << " ms, corners " << stages.cornerMilliseconds << " ms, steering "
                  << stages.steeringMilliseconds << " ms, avoidance " << stages.avoidanceMilliseconds << " ms, move " << stages.moveMilliseconds << " ms\n";
    }

    // Path queries across the arena (around the pillars) and over a few metres, the first in a fresh scene and repeated.
    const auto pathMilliseconds = [&](DVec3 from, DVec3 to, int repeats, std::string_view label) {
        scene::Scene paths;
        static_cast<void>(paths.Navigation().AddNavMesh(arena));
        const auto cold = std::chrono::steady_clock::now();
        Check(paths.Navigation().FindPath(from, to).status == scene::NavPathStatus::Complete, "a benchmark path must be complete");
        const double first = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cold).count();
        const auto warm = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < repeats; ++repeat) static_cast<void>(paths.Navigation().FindPath(from, to));
        const double each = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - warm).count() / repeats;
        std::cout << "navigation: FindPath " << label << ", first " << first << " ms, then " << each << " ms each\n";
    };
    pathMilliseconds(DVec3{ -90.0, 0.0, -90.0 }, DVec3{ 90.0, 0.0, 90.0 }, 200, "across 250 m");
    pathMilliseconds(DVec3{ -5.0, 0.0, -5.0 }, DVec3{ 5.0, 0.0, 5.0 }, 2000, "over 14 m");
    Check(fiveThousandLod.milliseconds < fiveThousand.milliseconds, "level of detail must make a large crowd cheaper");
    Check(tenThousand.milliseconds < 24.0 * kSanitizerTimeScale, "10000 crowd agents must step within 24 ms: " + std::to_string(tenThousand.milliseconds));
}

// Horde cost: 10 000 and 50 000 agents chasing 1, 10 and 100 moving targets on the two-storey level
// (stairs, bridge, ladder link) over 2000 steps, and individual crowd agents in the same scenario.
void RunNavigationHordeBenchmark() {
    const HordeLevel level = MakeHordeLevel(120.0);
    const auto report = [](std::string_view label, const HordeRun& run) {
        std::cout << "navigation: " << label << ": average " << run.Average() << " ms, p50 " << run.Percentile(0.5) << " ms, p95 "
                  << run.Percentile(0.95) << " ms, p99 " << run.Percentile(0.99) << " ms, max " << run.Percentile(1.0) << " ms, fields "
                  << run.maxFields << ", most waiting " << run.maxWaiting << ", fields in the slowest step " << run.slowestFieldMilliseconds << " ms\n";
    };
    double tenThousandOne = 0.0;
    double tenThousandHundred = 0.0;
    double fiftyThousandHundred = 0.0;
    for (const int count : { 10000, 50000 }) {
        for (const int targets : { 1, 10, 100 }) {
            const HordeRun run = RunHorde(level, count, targets, 2000, true);
            report("horde step, " + std::to_string(count) + " agents, " + std::to_string(targets) + " moving targets", run);
            if (count == 10000 && targets == 1) tenThousandOne = run.Percentile(0.5);
            if (count == 10000 && targets == 100) tenThousandHundred = run.Percentile(0.5);
            if (count == 50000 && targets == 100) fiftyThousandHundred = run.Percentile(0.5);
        }
    }
    const HordeRun oneThread = RunHorde(level, 10000, 1, 600, true, 1U);
    report("horde step, 10000 agents, 1 moving target, one thread", oneThread);
    for (const int targets : { 1, 100 }) {
        const HordeRun run = RunHorde(level, 10000, targets, 2000, false);
        report("crowd step, 10000 individual agents, " + std::to_string(targets) + " moving targets", run);
    }
    // Targets that cross polygons and tiles every step (about 13 m per second per step), 10 and 100 of them.
    for (const int targets : { 10, 100 }) {
        const HordeRun run = RunHorde(level, 10000, targets, 1000, true, 0U, std::nullopt, 25.0);
        report("horde step, 10000 agents, " + std::to_string(targets) + " targets crossing polygons every step", run);
    }
    // A mixed scene: a horde and individual crowd agents after the same targets.
    {
        const HordeRun run = RunHorde(level, 10000, 10, 1000, true, 0U, std::nullopt, 0.25, 500);
        report("horde step, 10000 horde agents among 500 crowd agents", run);
        std::cout << "navigation: crowd step of those 500 agents, average " << run.crowdMilliseconds / static_cast<double>(run.steps.size()) << " ms\n";
    }
    // A 2 km map with agents spread over it: far agents follow the route, none waits for a field.
    {
        const HordeLevel huge = MakeHordeLevel(1000.0);
        const HordeRun run = RunHorde(huge, 10000, 10, 600, true, 0U, std::nullopt, 0.25, 0, 4096U);
        report("horde step, 10000 agents spread over a 2 km map, 10 moving targets", run);
        // Agent steps spent waiting for guidance after the first half second, of 10000 agents over 270 steps: a few edge crossings at most.
        Check(run.laterWaiting < 2700U, "horde agents on a 2 km map do not wait for fields after the first half second: " + std::to_string(run.laterWaiting) + " agent steps");
        Check(run.Percentile(0.5) < 6.0 * kSanitizerTimeScale, "10000 horde agents on a 2 km map must step within 6 ms: " + std::to_string(run.Percentile(0.5)));
    }
    Check(tenThousandOne < 3.0 * kSanitizerTimeScale, "10000 horde agents after one target must step within 3 ms: " + std::to_string(tenThousandOne));
    Check(tenThousandHundred < 6.0 * kSanitizerTimeScale, "10000 horde agents after 100 targets must step within 6 ms: " + std::to_string(tenThousandHundred));
    Check(fiftyThousandHundred < 30.0 * kSanitizerTimeScale, "50000 horde agents must step within 30 ms: " + std::to_string(fiftyThousandHundred));
}

} // namespace kb::tests
