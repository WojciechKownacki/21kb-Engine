#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/math/DVec3.hpp"
#include "engine/navigation/NavGeometryCollector.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/navigation/NavMeshBuild.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "engine/scene/SceneNavigationComponents.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kb::tests {
namespace {

namespace nav = kb::navigation;
using kb::math::DVec3;
using kb::math::Vec3;

constexpr float kFrame = 1.0F / 60.0F;

void Check(bool condition, const std::string& message) {
    Require(condition, message.c_str());
}

// A walkable slab whose top face lies at y = 0, spanning [minX, maxX] x [minZ, maxZ] around `origin`.
[[nodiscard]] kb::scene::ScenePrefabNodeDesc Floor(const DVec3& origin, double minX, double maxX, double minZ, double maxZ) {
    kb::scene::ScenePrefabNodeDesc node;
    node.name = "Floor";
    node.SetLocalTranslation(origin + DVec3{ (minX + maxX) * 0.5, -0.25, (minZ + maxZ) * 0.5 });
    node.components.collider = kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Box,
        .boxSize = Vec3{ static_cast<float>(maxX - minX), 0.5F, static_cast<float>(maxZ - minZ) } };
    return node;
}

// Bakes the floors (agent radius 0.4 m, 0.2 m cells) and adds the mesh to the scene.
void AddFloors(kb::scene::Scene& scene, const std::vector<kb::scene::ScenePrefabNodeDesc>& floors) {
    nav::NavMeshBuildSettings settings;
    settings.cellSize = 0.2F;
    settings.cellHeight = 0.1F;
    settings.tileCells = 64U;
    nav::NavGeometry geometry;
    nav::NavGeometryCollectStats stats;
    nav::CollectNavGeometry(floors, settings, nullptr, geometry, stats);
    const nav::NavMeshBakeResult baked = nav::NavMeshBuilder::Bake(settings, geometry);
    Check(baked.succeeded, "the floors must bake: " + baked.error);
    auto asset = std::make_shared<nav::NavMeshAsset>();
    asset->settings = settings;
    asset->tiles = baked.tiles;
    std::string error;
    Check(scene.Navigation().AddNavMesh(std::move(asset), &error) != 0U, "the baked floors must be accepted: " + error);
}

// One floor from x = -2 to `length` + 2 and `width` wide around z = 0.
void AddCorridor(kb::scene::Scene& scene, double length, double width = 6.0) {
    AddFloors(scene, { Floor({}, -2.0, length + 2.0, -width * 0.5, width * 0.5) });
}

[[nodiscard]] kb::scene::SceneEntity AddAgent(kb::scene::Scene& scene, Vec3 position, Vec3 destination, float maxSpeed = 2.0F) {
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Agent", .transform = kb::scene::TransformComponent{ .localPosition = position } });
    scene.Components().NavAgents().Set(object.Entity(), kb::scene::NavAgent{
        .radius = 0.4F, .maxSpeed = maxSpeed, .acceleration = 8.0F, .stoppingDistance = 0.1F, .destination = destination });
    return object.Entity();
}

[[nodiscard]] kb::scene::SceneEntity AddObstacle(kb::scene::Scene& scene, Vec3 position, kb::scene::NavObstacle obstacle) {
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Obstacle", .transform = kb::scene::TransformComponent{ .localPosition = position } });
    scene.Components().NavObstacles().Set(object.Entity(), obstacle);
    return object.Entity();
}

[[nodiscard]] Vec3 Position(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    return scene.Transforms().Get(entity).worldPosition;
}

[[nodiscard]] const kb::scene::NavAgent& Agent(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    const kb::scene::NavAgent* agent = scene.Components().NavAgents().TryGet(entity);
    Require(agent != nullptr, "The navigation agent lost its component");
    return *agent;
}

[[nodiscard]] float Horizontal(Vec3 a, Vec3 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
}

void TestAgentMovesOverTheMeshAndArrives() {
    kb::scene::Scene scene;
    AddCorridor(scene, 10.0);
    const kb::scene::SceneEntity agent = AddAgent(scene, { 0.0F, 0.0F, 0.0F }, { 10.0F, 0.0F, 0.0F });
    float fastest = 0.0F;
    for (int frame = 0; frame < 60; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        const Vec3 velocity = Agent(scene, agent).velocity;
        fastest = std::max(fastest, std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z));
    }
    const Vec3 afterOneSecond = Position(scene, agent);
    Check(afterOneSecond.x > 1.4F && afterOneSecond.x < 2.0F && std::fabs(afterOneSecond.z) < 0.05F,
        "An agent must accelerate toward its destination over the mesh at up to its maximum speed: x " + std::to_string(afterOneSecond.x) +
            ", z " + std::to_string(afterOneSecond.z));
    Check(fastest <= 2.0F + 1.0e-4F, "An agent must never exceed its maximum speed");
    Check(Agent(scene, agent).pathStatus == kb::scene::NavPathStatus::Complete && Agent(scene, agent).remainingDistance > 7.0F,
        "A moving agent must report a complete path and the distance left");

    for (int frame = 0; frame < 60 * 8; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    const Vec3 arrived = Position(scene, agent);
    Check(Horizontal(arrived, { 10.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F && arrived.x <= 10.0F + 1.0e-4F,
        "An agent must stop within its stopping distance without overshooting the destination");
    Check(Agent(scene, agent).velocity.x == 0.0F && Agent(scene, agent).remainingDistance <= 0.1F + 1.0e-4F,
        "An arrived agent must stand still");

    // A new destination makes it re-plan.
    scene.Components().NavAgents().TryGet(agent)->destination = Vec3{ 4.0F, 0.0F, 0.0F };
    for (int frame = 0; frame < 60 * 6; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Horizontal(Position(scene, agent), { 4.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F, "A changed destination must be reached");

    scene.Runtime().SetPlaying(false);
    scene.Components().NavAgents().TryGet(agent)->destination = Vec3{ 0.0F, 0.0F, 0.0F };
    const Vec3 paused = Position(scene, agent);
    for (int frame = 0; frame < 30; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Horizontal(Position(scene, agent), paused) == 0.0F, "Agents must not move while the scene is not playing");
}

void TestAgentFollowsCorners() {
    // An L of two floors: along x from the start, then along z up to the destination.
    kb::scene::Scene scene;
    AddFloors(scene, { Floor({}, -1.0, 6.0, -1.0, 1.0), Floor({}, 4.0, 6.0, 1.0, 6.0) });
    const kb::scene::SceneEntity agent = AddAgent(scene, { 0.0F, 0.0F, 0.0F }, { 5.0F, 0.0F, 5.0F });
    static_cast<void>(scene.Runtime().Update(kFrame));
    const std::vector<Vec3> corners = scene.Navigation().AgentPath(agent);
    Check(corners.size() >= 2U && std::ranges::any_of(corners, [](Vec3 corner) { return corner.x > 3.9F && corner.z < 1.0F; }),
        "An agent's path must turn at the inside corner of the L");
    bool cutCorner = false;
    for (int frame = 0; frame < 60 * 10; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        const Vec3 position = Position(scene, agent);
        if (position.z > 1.05F && position.x < 3.95F) cutCorner = true;
    }
    Check(!cutCorner, "An agent must stay on the mesh through the corner");
    Check(Horizontal(Position(scene, agent), { 5.0F, 0.0F, 5.0F }) <= 0.1F + 1.0e-4F, "The agent must reach the end of the path");
}

void TestCarvingObstacleBlocksThePath() {
    {
        // A wide floor; a carving wall across the straight route leaves a way around it at z > 3.
        kb::scene::Scene scene;
        AddFloors(scene, { Floor({}, -2.0, 12.0, -3.0, 8.0) });
        const kb::scene::SceneEntity agent = AddAgent(scene, { 0.0F, 0.0F, 0.0F }, { 10.0F, 0.0F, 0.0F }, 3.0F);
        const kb::scene::SceneEntity wall = AddObstacle(scene, { 5.0F, 0.0F, 0.0F },
            kb::scene::NavObstacle{ .shape = kb::scene::NavObstacleShape::Box, .size = { 2.0F, 2.0F, 6.0F }, .carve = true });
        float farthestZ = 0.0F;
        for (int frame = 0; frame < 60 * 10; ++frame) {
            static_cast<void>(scene.Runtime().Update(kFrame));
            const Vec3 position = Position(scene, agent);
            farthestZ = std::max(farthestZ, position.z);
            Check(std::fabs(position.x - 5.0F) > 1.0F || std::fabs(position.z) > 3.0F, "An agent walked through a carving obstacle");
        }
        Check(farthestZ > 3.0F && Horizontal(Position(scene, agent), { 10.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F &&
                Agent(scene, agent).pathStatus == kb::scene::NavPathStatus::Complete,
            "The agent must go around the carving obstacle and arrive");

        // Removing the obstacle opens the straight route again.
        scene.Components().NavObstacles().TryGet(wall)->enabled = false;
        scene.Components().NavAgents().TryGet(agent)->destination = Vec3{ 0.0F, 0.0F, 0.0F };
        static_cast<void>(scene.Runtime().Update(kFrame));
        static_cast<void>(scene.Runtime().Update(kFrame));
        const std::vector<Vec3> open = scene.Navigation().AgentPath(agent);
        Check(!open.empty() && std::ranges::none_of(open, [](Vec3 corner) { return corner.z > 1.0F; }),
            "A disabled obstacle must no longer block the mesh");
    }
    {
        // Without a way around, the agent gets as close as it can and reports a partial path.
        kb::scene::Scene scene;
        AddCorridor(scene, 10.0, 4.0);
        const kb::scene::SceneEntity agent = AddAgent(scene, { 0.0F, 0.0F, 0.0F }, { 10.0F, 0.0F, 0.0F });
        static_cast<void>(AddObstacle(scene, { 5.0F, 0.0F, 0.0F },
            kb::scene::NavObstacle{ .shape = kb::scene::NavObstacleShape::Box, .size = { 1.0F, 2.0F, 6.0F }, .carve = true }));
        for (int frame = 0; frame < 60 * 8; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
        // The hole reaches the obstacle's face plus the agent radius: x = 4.5 - 0.4.
        Check(Agent(scene, agent).pathStatus == kb::scene::NavPathStatus::Partial && Position(scene, agent).x < 4.2F &&
                Position(scene, agent).x > 3.7F,
            "A blocked destination must give a partial path to the closest reachable point: x " + std::to_string(Position(scene, agent).x));
    }
}

void TestNonCarvingObstacleIsSteeredAround() {
    kb::scene::Scene scene;
    AddCorridor(scene, 10.0);
    const kb::scene::SceneEntity agent = AddAgent(scene, { 0.0F, 0.0F, 0.0F }, { 10.0F, 0.0F, 0.0F });
    static_cast<void>(AddObstacle(scene, { 5.0F, 0.0F, 0.0F },
        kb::scene::NavObstacle{ .shape = kb::scene::NavObstacleShape::Cylinder, .radius = 0.5F, .height = 2.0F, .carve = false }));
    float closest = 100.0F;
    for (int frame = 0; frame < 60 * 12; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        closest = std::min(closest, Horizontal(Position(scene, agent), { 5.0F, 0.0F, 0.0F }));
    }
    Check(closest >= 0.9F - 0.05F, "An agent must steer around an obstacle that does not carve the mesh: closest " + std::to_string(closest));
    Check(Horizontal(Position(scene, agent), { 10.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F, "The agent must still arrive");
}

struct CrossingResult {
    Vec3 first{};
    Vec3 second{};
    float closest = 100.0F;
};

// Two agents walk the same line toward each other's start.
[[nodiscard]] CrossingResult RunCrossing(bool firstCreatedFirst) {
    kb::scene::Scene scene;
    AddFloors(scene, { Floor({}, -8.0, 8.0, -3.0, 3.0) });
    kb::scene::SceneEntity first{};
    kb::scene::SceneEntity second{};
    if (firstCreatedFirst) {
        first = AddAgent(scene, { -6.0F, 0.0F, 0.0F }, { 6.0F, 0.0F, 0.0F });
        second = AddAgent(scene, { 6.0F, 0.0F, 0.0F }, { -6.0F, 0.0F, 0.0F });
    } else {
        second = AddAgent(scene, { 6.0F, 0.0F, 0.0F }, { -6.0F, 0.0F, 0.0F });
        first = AddAgent(scene, { -6.0F, 0.0F, 0.0F }, { 6.0F, 0.0F, 0.0F });
    }
    CrossingResult result;
    for (int frame = 0; frame < 60 * 14; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        result.closest = std::min(result.closest, Horizontal(Position(scene, first), Position(scene, second)));
    }
    result.first = Position(scene, first);
    result.second = Position(scene, second);
    return result;
}

void TestAgentsAvoidEachOtherDeterministically() {
    const CrossingResult crossing = RunCrossing(true);
    Check(crossing.closest >= 0.8F - 0.05F, "Agents walking toward each other must not run into each other: closest " + std::to_string(crossing.closest));
    Check(Horizontal(crossing.first, { 6.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F && Horizontal(crossing.second, { -6.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F,
        "Both agents must reach their destinations after passing each other");
    const CrossingResult replay = RunCrossing(true);
    const CrossingResult reordered = RunCrossing(false);
    Check(std::memcmp(&crossing.first, &replay.first, sizeof(Vec3)) == 0 && std::memcmp(&crossing.second, &replay.second, sizeof(Vec3)) == 0 &&
            crossing.closest == replay.closest,
        "The same navigation input must replay to the same positions");
    // Created the other way round the agents get other entity ids, so the crowd serves them in another
    // order (its path and corridor queues favour the lower slot); they still pass and arrive.
    Check(reordered.closest >= 0.8F - 0.05F && Horizontal(reordered.first, { 6.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F &&
            Horizontal(reordered.second, { -6.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F,
        "Agents created in the other order must pass each other and arrive as well");
}

void TestWithoutMeshAgentsFail() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity agent = AddAgent(scene, { 1.0F, 0.0F, 0.0F }, { 8.0F, 0.0F, 0.0F });
    for (int frame = 0; frame < 30; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Agent(scene, agent).pathStatus == kb::scene::NavPathStatus::Failed && Horizontal(Position(scene, agent), { 1.0F, 0.0F, 0.0F }) == 0.0F,
        "Without a navigation mesh an agent must stay where it is and report a failed path");
    Check(!scene.Navigation().NearestPoint(DVec3{ 1.0, 0.0, 0.0 }).has_value(), "Without a navigation mesh there is no nearest point");
    // A mesh arriving later (a streamed cell, a placed asset) sets the agent going.
    AddCorridor(scene, 10.0);
    const std::optional<DVec3> nearest = scene.Navigation().NearestPoint(DVec3{ 2.2, 0.5, 0.5 });
    Check(nearest.has_value() && std::fabs(nearest->x - 2.2) < 0.01 && std::fabs(nearest->z - 0.5) < 0.01, "The nearest point lies on the mesh");
    for (int frame = 0; frame < 60 * 6; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Check(Agent(scene, agent).pathStatus == kb::scene::NavPathStatus::Complete && Horizontal(Position(scene, agent), { 8.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F,
        "An agent must plan and walk once a navigation mesh is added");
}

// Where an agent walking one second over a mesh baked around `origin` ends up, relative to that origin; the
// navigation origin is set there too. The agent is a root, or the child of a parent standing at the origin.
[[nodiscard]] DVec3 WalkOneSecond(const DVec3& origin, bool parented) {
    kb::scene::Scene scene;
    scene.Navigation().SetOrigin(origin);
    AddFloors(scene, { Floor(origin, -2.0, 12.0, -3.0, 3.0) });
    const kb::scene::SceneEntity agent = AddAgent(scene, {}, { 10.0F, 0.0F, 0.0F });
    if (parented) {
        const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Group" });
        scene.Transforms().SetLocalTranslation(parent.Entity(), origin);
        Require(scene.Hierarchy().SetParent(agent, parent.Entity()), "The far agent could not be parented");
    } else {
        scene.Transforms().SetLocalTranslation(agent, origin);
    }
    scene.Runtime().SynchronizeTransforms();
    for (int frame = 0; frame < 60; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    return scene.Transforms().WorldTranslation(agent) - origin;
}

void TestAgentsWalkFarFromTheWorldOrigin() {
    // Ten thousand kilometres out a float holds whole metres only, so an agent stepping centimetres per frame could
    // not move at all; with the navigation origin there it walks exactly as it does at the world origin.
    const DVec3 farOrigin{ 10'000'000.0, 0.0, 10'000'000.0 };
    Check(kb::scene::Scene{}.Navigation().Origin() == DVec3{}, "The navigation origin is the world origin by default");
    for (const bool parented : { false, true }) {
        const DVec3 nearWalk = WalkOneSecond({}, parented);
        const DVec3 farWalk = WalkOneSecond(farOrigin, parented);
        Check(nearWalk.x > 1.4 && nearWalk.x < 2.0, "An agent must walk over the mesh near the world origin");
        Check(kb::math::Length(farWalk - nearWalk) <= 1.0e-3, "An agent on a far mesh must walk as it does near the world origin, to the millimetre");
    }
}

} // namespace

void RunNavigationRuntimeTests() {
    TestAgentMovesOverTheMeshAndArrives();
    TestAgentFollowsCorners();
    TestCarvingObstacleBlocksThePath();
    TestNonCarvingObstacleIsSteeredAround();
    TestAgentsAvoidEachOtherDeterministically();
    TestWithoutMeshAgentsFail();
    TestAgentsWalkFarFromTheWorldOrigin();
}

} // namespace kb::tests
