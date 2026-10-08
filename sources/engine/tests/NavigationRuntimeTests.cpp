#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/scene/Navigation.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "engine/scene/SceneNavigationComponents.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

namespace kb::tests {
namespace {

using kb::math::Vec3;

constexpr float kFrame = 1.0F / 60.0F;

[[nodiscard]] kb::scene::NavMesh Graph(std::initializer_list<Vec3> positions, std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> edges) {
    kb::scene::NavMesh mesh;
    for (const Vec3& position : positions) mesh.nodes.push_back(kb::scene::NavMeshNode{ .position = position });
    for (const auto& [from, to] : edges) {
        mesh.nodes[from].neighbours.push_back(to);
        mesh.nodes[to].neighbours.push_back(from);
    }
    for (kb::scene::NavMeshNode& node : mesh.nodes) std::ranges::sort(node.neighbours);
    return mesh;
}

// Nodes every metre along x from `first` to `last` at z = 0, each linked to the next.
[[nodiscard]] kb::scene::NavMesh Line(int first, int last) {
    kb::scene::NavMesh mesh;
    for (int x = first; x <= last; ++x) {
        kb::scene::NavMeshNode node{ .position = Vec3{ static_cast<float>(x), 0.0F, 0.0F } };
        if (x > first) node.neighbours.push_back(static_cast<std::uint32_t>(x - first - 1));
        if (x < last) node.neighbours.push_back(static_cast<std::uint32_t>(x - first + 1));
        mesh.nodes.push_back(std::move(node));
    }
    return mesh;
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

void TestAgentMovesAlongGraphAndArrives() {
    kb::scene::Scene scene;
    scene.Navigation().SetMesh(Line(0, 10));
    const kb::scene::SceneEntity agent = AddAgent(scene, { 0.0F, 0.0F, 0.0F }, { 10.0F, 0.0F, 0.0F });
    float fastest = 0.0F;
    for (int frame = 0; frame < 60; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        const Vec3 velocity = Agent(scene, agent).velocity;
        fastest = std::max(fastest, std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z));
    }
    const Vec3 afterOneSecond = Position(scene, agent);
    Require(afterOneSecond.x > 1.5F && afterOneSecond.x < 2.0F && std::fabs(afterOneSecond.z) < 1.0e-4F,
        "An agent must accelerate toward its destination along the graph at up to its maximum speed");
    Require(fastest <= 2.0F + 1.0e-4F, "An agent must never exceed its maximum speed");
    Require(Agent(scene, agent).pathStatus == kb::scene::NavPathStatus::Complete && Agent(scene, agent).remainingDistance > 7.0F,
        "A moving agent must report a complete path and the distance left");

    for (int frame = 0; frame < 60 * 8; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    const Vec3 arrived = Position(scene, agent);
    Require(Horizontal(arrived, { 10.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F && arrived.x <= 10.0F + 1.0e-4F,
        "An agent must stop within its stopping distance without overshooting the destination");
    Require(Agent(scene, agent).velocity.x == 0.0F && Agent(scene, agent).remainingDistance <= 0.1F + 1.0e-4F,
        "An arrived agent must stand still");

    // A new destination makes it re-plan.
    scene.Components().NavAgents().TryGet(agent)->destination = Vec3{ 4.0F, 0.0F, 0.0F };
    for (int frame = 0; frame < 60 * 6; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Require(Horizontal(Position(scene, agent), { 4.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F, "A changed destination must be reached");

    scene.Runtime().SetPlaying(false);
    scene.Components().NavAgents().TryGet(agent)->destination = Vec3{ 0.0F, 0.0F, 0.0F };
    const Vec3 paused = Position(scene, agent);
    for (int frame = 0; frame < 30; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Require(Horizontal(Position(scene, agent), paused) == 0.0F, "Agents must not move while the scene is not playing");
}

void TestAgentFollowsGraphCorners() {
    kb::scene::Scene scene;
    scene.Navigation().SetMesh(Graph({ { 0.0F, 0.0F, 0.0F }, { 5.0F, 0.0F, 0.0F }, { 5.0F, 0.0F, 5.0F } }, { { 0U, 1U }, { 1U, 2U } }));
    const kb::scene::SceneEntity agent = AddAgent(scene, { 0.0F, 0.0F, 0.0F }, { 5.0F, 0.0F, 5.0F });
    bool cutCorner = false;
    float closestToCorner = 100.0F;
    for (int frame = 0; frame < 60 * 10; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        const Vec3 position = Position(scene, agent);
        closestToCorner = std::min(closestToCorner, Horizontal(position, { 5.0F, 0.0F, 0.0F }));
        if (position.z > 0.5F && position.x < 4.5F) cutCorner = true;
    }
    Require(!cutCorner && closestToCorner < 0.3F, "An agent must follow the graph's edges through its corners");
    Require(Horizontal(Position(scene, agent), { 5.0F, 0.0F, 5.0F }) <= 0.1F + 1.0e-4F, "The agent must reach the end of the path");
}

void TestCarvingObstacleBlocksEdges() {
    // Two routes from 0 to 2: straight through node 1, or the longer detour over node 3.
    const kb::scene::NavMesh mesh = Graph({ { 0.0F, 0.0F, 0.0F }, { 5.0F, 0.0F, 0.0F }, { 10.0F, 0.0F, 0.0F }, { 5.0F, 0.0F, 6.0F } },
        { { 0U, 1U }, { 1U, 2U }, { 0U, 3U }, { 3U, 2U } });
    {
        kb::scene::Scene scene;
        scene.Navigation().SetMesh(mesh);
        const kb::scene::SceneEntity agent = AddAgent(scene, { 0.0F, 0.0F, 0.0F }, { 10.0F, 0.0F, 0.0F }, 3.0F);
        const kb::scene::SceneEntity wall = AddObstacle(scene, { 5.0F, 0.0F, 0.0F },
            kb::scene::NavObstacle{ .shape = kb::scene::NavObstacleShape::Box, .size = { 2.0F, 2.0F, 2.0F }, .carve = true });
        static_cast<void>(scene.Runtime().Update(kFrame));
        const std::vector<Vec3> path = scene.Navigation().AgentPath(agent);
        Require(std::ranges::any_of(path, [](Vec3 corner) { return corner.z == 6.0F; }) &&
                std::ranges::none_of(path, [](Vec3 corner) { return corner.x == 5.0F && corner.z == 0.0F; }),
            "A carving obstacle must block the graph node and edges it stands on");
        float farthestZ = 0.0F;
        for (int frame = 0; frame < 60 * 10; ++frame) {
            static_cast<void>(scene.Runtime().Update(kFrame));
            farthestZ = std::max(farthestZ, Position(scene, agent).z);
            Require(std::fabs(Position(scene, agent).x - 5.0F) > 1.0F || Position(scene, agent).z > 1.0F,
                "An agent walked through a carving obstacle");
        }
        Require(farthestZ > 5.0F && Horizontal(Position(scene, agent), { 10.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F &&
                Agent(scene, agent).pathStatus == kb::scene::NavPathStatus::Complete,
            "The agent must take the detour around the blocked edge and arrive");

        // Removing the obstacle opens the short route again.
        scene.Components().NavObstacles().TryGet(wall)->enabled = false;
        scene.Components().NavAgents().TryGet(agent)->destination = Vec3{ 0.0F, 0.0F, 0.0F };
        static_cast<void>(scene.Runtime().Update(kFrame));
        const std::vector<Vec3> open = scene.Navigation().AgentPath(agent);
        Require(std::ranges::none_of(open, [](Vec3 corner) { return corner.z == 6.0F; }),
            "A disabled obstacle must no longer block the graph");
    }
    {
        // Without a detour the agent gets as close as it can and reports a partial path.
        kb::scene::Scene scene;
        scene.Navigation().SetMesh(Line(0, 10));
        const kb::scene::SceneEntity agent = AddAgent(scene, { 0.0F, 0.0F, 0.0F }, { 10.0F, 0.0F, 0.0F });
        static_cast<void>(AddObstacle(scene, { 5.0F, 0.0F, 0.0F },
            kb::scene::NavObstacle{ .shape = kb::scene::NavObstacleShape::Cylinder, .radius = 0.5F, .height = 2.0F, .carve = true }));
        for (int frame = 0; frame < 60 * 8; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
        Require(Agent(scene, agent).pathStatus == kb::scene::NavPathStatus::Partial && Position(scene, agent).x < 4.1F &&
                Position(scene, agent).x > 3.8F,
            "A blocked destination must give a partial path to the closest reachable node");
    }
}

void TestNonCarvingObstacleIsSteeredAround() {
    kb::scene::Scene scene;
    scene.Navigation().SetMesh(Line(0, 10));
    const kb::scene::SceneEntity agent = AddAgent(scene, { 0.0F, 0.0F, 0.0F }, { 10.0F, 0.0F, 0.0F });
    static_cast<void>(AddObstacle(scene, { 5.0F, 0.0F, 0.0F },
        kb::scene::NavObstacle{ .shape = kb::scene::NavObstacleShape::Cylinder, .radius = 0.5F, .height = 2.0F, .carve = false }));
    float closest = 100.0F;
    for (int frame = 0; frame < 60 * 12; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrame));
        closest = std::min(closest, Horizontal(Position(scene, agent), { 5.0F, 0.0F, 0.0F }));
    }
    Require(closest >= 0.9F - 0.05F, "An agent must steer around an obstacle that does not carve the graph");
    Require(Horizontal(Position(scene, agent), { 10.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F, "The agent must still arrive");
}

struct CrossingResult {
    Vec3 first{};
    Vec3 second{};
    float closest = 100.0F;
};

// Two agents walk the same line toward each other's start.
[[nodiscard]] CrossingResult RunCrossing(bool firstCreatedFirst) {
    kb::scene::Scene scene;
    scene.Navigation().SetMesh(Line(-6, 6));
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
    Require(crossing.closest >= 0.8F - 0.05F, "Agents walking toward each other must not run into each other");
    Require(Horizontal(crossing.first, { 6.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F && Horizontal(crossing.second, { -6.0F, 0.0F, 0.0F }) <= 0.1F + 1.0e-4F,
        "Both agents must reach their destinations after passing each other");
    const CrossingResult replay = RunCrossing(true);
    const CrossingResult reordered = RunCrossing(false);
    Require(std::memcmp(&crossing.first, &replay.first, sizeof(Vec3)) == 0 && std::memcmp(&crossing.second, &replay.second, sizeof(Vec3)) == 0 &&
            crossing.closest == replay.closest,
        "The same navigation input must replay to the same positions");
    Require(std::memcmp(&crossing.first, &reordered.first, sizeof(Vec3)) == 0 && std::memcmp(&crossing.second, &reordered.second, sizeof(Vec3)) == 0,
        "Navigation must not depend on the order the agents were created in");
}

void TestWithoutGraphAgentsFail() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity agent = AddAgent(scene, { 1.0F, 0.0F, 2.0F }, { 10.0F, 0.0F, 0.0F });
    for (int frame = 0; frame < 30; ++frame) static_cast<void>(scene.Runtime().Update(kFrame));
    Require(Agent(scene, agent).pathStatus == kb::scene::NavPathStatus::Failed && Horizontal(Position(scene, agent), { 1.0F, 0.0F, 2.0F }) == 0.0F,
        "Without a navigation graph an agent must stay where it is and report a failed path");
    Require(!scene.Navigation().NearestNode({}).has_value(), "An empty graph has no nearest node");
    scene.Navigation().SetMesh(Line(0, 3));
    Require(scene.Navigation().NearestNode({ 2.2F, 0.0F, 0.5F }) == 2U, "The nearest node must be the closest graph node");
    Require(scene.Navigation().Mesh().revision >= 2U, "Setting a graph must raise its revision so agents re-plan");
}

} // namespace

void RunNavigationRuntimeTests() {
    TestAgentMovesAlongGraphAndArrives();
    TestAgentFollowsGraphCorners();
    TestCarvingObstacleBlocksEdges();
    TestNonCarvingObstacleIsSteeredAround();
    TestAgentsAvoidEachOtherDeterministically();
    TestWithoutGraphAgentsFail();
}

} // namespace kb::tests
