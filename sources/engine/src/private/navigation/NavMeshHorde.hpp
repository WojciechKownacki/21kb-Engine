#pragma once

#include "engine/ecs/WorkerPool.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "navigation/NavMeshCrowd.hpp"
#include "navigation/NavMeshRuntime.hpp"

#include <DetourNavMesh.h>

#include <cstdint>
#include <span>
#include <vector>

namespace kb::navigation {

// The polygons of one profile's Detour mesh as a flat graph: ground polygons with their edges and
// the portals to their neighbours, and off-mesh link polygons with their two end points. Rebuilt
// when the profile's tiles change; node indices are stable for the life of one graph.
struct NavPolygonGraph {
    static constexpr std::uint32_t kNone = 0xFFFFFFFFU;
    struct Portal {
        // The node behind the edge, the edge of this node it lies on (link nodes: the end point
        // the step lands at, 0 start or 1 end), and the part of the edge that leads there.
        std::uint32_t node = kNone;
        std::uint8_t edge = 0U;
        float from = 0.0F;
        float to = 1.0F;
    };
    std::uint64_t revision = 0U;
    std::uint64_t generation = 0U;
    // Per node: first vertex and vertex count (link nodes: two, start and end), first portal and
    // portal count, centre, area id, link radius (0 for ground polygons), Detour reference.
    std::vector<std::uint32_t> firstVertex;
    std::vector<std::uint8_t> vertexCount;
    std::vector<std::uint32_t> firstPortal;
    std::vector<std::uint8_t> portalCount;
    std::vector<kb::math::Vec3> centre;
    std::vector<std::uint8_t> area;
    std::vector<float> linkRadius;
    std::vector<dtPolyRef> reference;
    std::vector<kb::math::Vec3> vertices;
    // Portals out of each node (leading to the neighbour), and into each node (`node` is the one
    // the step comes from), both grouped by node.
    std::vector<Portal> portals;
    std::vector<std::uint32_t> portalOwner;
    std::vector<std::uint32_t> firstIncoming;
    std::vector<std::uint32_t> incoming; // index into `portals`
    // First node of every tile slot of the Detour mesh, so a reference maps to a node directly.
    std::vector<std::uint32_t> tileBase;
    // Salt of every tile slot when the graph was built (a reference of another salt is stale).
    std::vector<std::uint32_t> tileSalt;

    void Build(const dtNavMesh& mesh, std::uint64_t meshRevision, std::uint64_t meshGeneration);
    [[nodiscard]] std::size_t NodeCount() const noexcept { return centre.size(); }
    [[nodiscard]] bool IsLink(std::uint32_t node) const noexcept { return linkRadius[node] > 0.0F; }
    [[nodiscard]] std::uint32_t NodeOf(const dtNavMesh& mesh, dtPolyRef polygon) const noexcept;
};

// One shared navigation field: every reached node of a profile's graph knows its cost to the goal
// node and the portal toward it. Expanded from the goal outward (a reverse Dijkstra over the
// portals) a bounded number of nodes per step, only as far as the agents using it need.
struct NavHordeField {
    std::uint32_t profile = 0U;
    std::uint32_t goal = NavPolygonGraph::kNone;
    kb::scene::NavAreaMask areas = kb::scene::kAllNavAreas;
    std::uint64_t graphRevision = 0U;
    std::vector<float> cost;
    // Index into the graph's portals: the way out of a node toward the goal; kNone at the goal or
    // where the field has not reached yet.
    std::vector<std::uint32_t> way;
    std::vector<std::uint8_t> settled;
    // Nodes agents stand on that the field has not settled yet, and how many; expansion goes on
    // while there are any, then `lookahead` nodes further.
    std::vector<std::uint8_t> needed;
    std::size_t unmet = 0U;
    std::size_t lookahead = 0U;
    // Open nodes, a binary heap ordered by cost then node index.
    std::vector<std::pair<float, std::uint32_t>> open;
    std::size_t settledCount = 0U;
    std::uint64_t lastUsed = 0U;
    // Raised whenever the slot starts a new field, so agents notice their field was replaced.
    std::uint64_t stamp = 0U;
};

// Moves NavAgents in horde mode: agents heading for the same goal polygon share one navigation
// field instead of each planning a path, steer through the field's portal of the polygon they stand
// on, keep apart with a separation push from a spatial grid built once per step, and move along
// the polygons without leaving them. Far agents tick less often, agents that stopped sleep until
// their goal moves away. Every agent writes only itself and the grid is read in a fixed order, so
// equal input replays exactly for any number of workers.
class NavMeshHorde {
public:
    // `agents` sorted by id; `outputs` receives one entry per agent, in the same order.
    void Step(NavMeshRuntime& runtime, std::span<const NavMeshCrowdAgentInput> agents, std::span<const kb::math::Vec3> focuses,
        const kb::scene::NavigationCrowdSettings& settings, float deltaSeconds, kb::ecs::WorkerPool* workers,
        std::vector<NavMeshCrowdAgentOutput>& outputs);
    void Clear();
    // Horde figures of the last step (the horde fields of NavigationCrowdStats).
    void AddStats(kb::scene::NavigationCrowdStats& stats) const noexcept;
    [[nodiscard]] bool OnLink(std::uint64_t id) const noexcept;
    [[nodiscard]] std::size_t FieldCount() const noexcept { return fields_.size(); }

private:
    struct AgentRecord {
        std::uint64_t id = 0U;
        std::uint32_t profile = 0U;
        std::uint32_t node = NavPolygonGraph::kNone;
        dtPolyRef polygon = 0U;
        // The field the agent follows (it keeps the previous one until the new one reaches it).
        std::uint32_t field = NavPolygonGraph::kNone;
        std::uint64_t fieldStamp = 0U;
        kb::math::Vec3 position{};
        kb::math::Vec3 velocity{};
        // Crossing an off-mesh link: the link node, its landing end point and polygon, progress in metres.
        std::uint32_t linkNode = NavPolygonGraph::kNone;
        std::uint32_t linkLanding = NavPolygonGraph::kNone;
        std::uint8_t linkEnd = 0U;
        float linkTravelled = 0.0F;
        // Level of detail: 0 near, 1 mid, 2 far; steps since the last update.
        std::uint8_t detail = 0U;
        std::uint8_t idle = 0U;
        bool asleep = false;
        bool waiting = false;
        kb::math::Vec3 sleptFor{};
    };
    // A destination agents are after, with their profile and areas, the polygon it lies in and the
    // field leading there.
    struct Goal {
        std::uint32_t profile = 0U;
        kb::math::Vec3 destination{};
        kb::scene::NavAreaMask areas = 0U;
        std::uint32_t node = NavPolygonGraph::kNone;
        std::uint32_t field = NavPolygonGraph::kNone;
        std::uint64_t revision = 0U;
        bool used = false;
        [[nodiscard]] bool Same(const Goal& other) const noexcept;
        [[nodiscard]] std::uint64_t Hash() const noexcept;
    };

    void SyncGraphs(NavMeshRuntime& runtime);
    std::uint32_t GoalNode(NavMeshRuntime& runtime, std::uint32_t profile, kb::math::Vec3 destination, kb::scene::NavAreaMask areas);
    [[nodiscard]] std::uint32_t FindGoal(const Goal& key) const noexcept;
    // Enters goal `added` into the goal table, or rebuilds the table (kNone).
    void IndexGoals(std::uint32_t added);
    std::uint32_t FindField(std::uint32_t profile, std::uint32_t goal, kb::scene::NavAreaMask areas, std::size_t cacheSize);
    void ExpandFields(const NavMeshRuntime& runtime, std::size_t budget);
    std::size_t Expand(const NavMeshRuntime& runtime, NavHordeField& field, std::size_t budget);
    void BuildGrid(float cellSize);

    std::vector<NavPolygonGraph> graphs_;
    std::vector<NavHordeField> fields_;
    std::vector<AgentRecord> records_;
    std::vector<AgentRecord> scratch_;
    std::vector<Goal> goals_;
    // Goal indices by the hash of their key (open addressing, kNone for an empty slot).
    std::vector<std::uint32_t> goalTable_;
    std::vector<std::uint32_t> remap_;
    // Per agent of the step: the goal it is after, and its radius.
    std::vector<std::uint32_t> wanted_;
    std::vector<float> radii_;
    std::size_t fieldCursor_ = 0U;
    std::uint64_t fieldStamp_ = 0U;
    // Spatial grid over the step's start positions: agents sorted by cell, their positions and
    // radii in the same order, and where each cell's run starts.
    std::vector<std::uint32_t> cellStart_;
    std::vector<std::uint32_t> fill_;
    std::vector<std::uint32_t> cellOf_;
    std::vector<std::uint32_t> sorted_;
    std::vector<float> sortedX_;
    std::vector<float> sortedY_;
    std::vector<float> sortedZ_;
    std::vector<float> sortedRadius_;
    float gridMinX_ = 0.0F;
    float gridMinZ_ = 0.0F;
    float gridCell_ = 1.0F;
    std::uint32_t gridWidth_ = 0U;
    std::uint32_t gridHeight_ = 0U;
    std::uint64_t step_ = 0U;
    std::size_t expanded_ = 0U;
    std::size_t waiting_ = 0U;
    std::size_t asleep_ = 0U;
    std::size_t ticked_ = 0U;
    std::size_t onLinks_ = 0U;
    std::size_t agents_ = 0U;
    double milliseconds_ = 0.0;
    double fieldMilliseconds_ = 0.0;
};

} // namespace kb::navigation
