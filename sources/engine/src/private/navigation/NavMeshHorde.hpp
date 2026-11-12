#pragma once

#include "engine/ecs/WorkerPool.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "navigation/NavMeshCrowd.hpp"
#include "navigation/NavMeshRuntime.hpp"

#include <DetourNavMesh.h>

#include <cstdint>
#include <span>
#include <unordered_map>
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
    // Clusters: the polygons of one tile slot that join through portals (an off-mesh link is a cluster
    // of its own). A cluster's nodes are `clusterNodes[clusterFirst[c] .. clusterFirst[c + 1])` in node
    // order, `local` is a node's place in its cluster, `signature` stands for everything a field over
    // the cluster depends on (polygons, areas, portals), `anchor` is the reference of its first node.
    std::vector<std::uint32_t> cluster;
    std::vector<std::uint32_t> local;
    std::vector<std::uint32_t> clusterFirst;
    std::vector<std::uint32_t> clusterNodes;
    std::vector<std::uint64_t> signature;
    std::vector<kb::math::Vec3> clusterCentre;
    // The way from one cluster into a neighbouring one: the polygon (or link) it leaves from, the
    // portal it uses and its cost. One per ordered pair of neighbouring clusters, the widest opening.
    struct Gateway {
        std::uint32_t from = kNone;
        std::uint32_t to = kNone;
        std::uint32_t node = kNone;
        std::uint32_t portal = kNone;
        float cost = 0.0F;
    };
    std::vector<Gateway> gateways;
    std::vector<std::uint32_t> gatewayFirst;
    // Gateways into each cluster, grouped by it (indices into `gateways`).
    std::vector<std::uint32_t> firstArriving;
    std::vector<std::uint32_t> arriving;

    void Build(const dtNavMesh& mesh, std::uint64_t meshRevision, std::uint64_t meshGeneration);
    [[nodiscard]] std::size_t NodeCount() const noexcept { return centre.size(); }
    [[nodiscard]] std::size_t ClusterCount() const noexcept { return clusterCentre.size(); }
    [[nodiscard]] bool IsLink(std::uint32_t node) const noexcept { return linkRadius[node] > 0.0F; }
    [[nodiscard]] bool IsLinkCluster(std::uint32_t c) const noexcept {
        return clusterFirst[c + 1U] - clusterFirst[c] == 1U && IsLink(clusterNodes[clusterFirst[c]]);
    }
    [[nodiscard]] std::uint32_t NodeOf(const dtNavMesh& mesh, dtPolyRef polygon) const noexcept;
};

// One navigation field over one cluster of a profile's graph: every node of the cluster knows its
// cost to the field's source node and the portal toward it (a reverse Dijkstra over the portals,
// a bounded number of nodes per step). A goal field flows to the polygon a destination lies in;
// an exit field flows to the polygon a gateway leaves from and carries the gateway's portal, so
// it is the same for every destination whose route leaves the cluster that way.
struct NavHordeField {
    std::uint32_t profile = 0U;
    std::uint32_t cluster = NavPolygonGraph::kNone;
    // kNone while the slot is free.
    std::uint32_t source = NavPolygonGraph::kNone;
    // Ordinal among the source's portals of the one the field leaves by (exit fields), else 0xFF.
    std::uint8_t exitWay = 0xFFU;
    kb::scene::NavAreaMask areas = kb::scene::kAllNavAreas;
    // The area costs and blocked areas the field was made with (a changed set makes a new field).
    std::uint64_t costs = 0U;
    // The cluster as it was built: its first node's reference and its signature, and the source's
    // reference, so the field survives a rebuilt graph in which the cluster is unchanged.
    dtPolyRef anchor = 0U;
    dtPolyRef sourceReference = 0U;
    std::uint64_t signature = 0U;
    // Per local node of the cluster: cost to the source, and the ordinal among the node's portals
    // of the way toward it (0xFF at the source of a goal field and where the field has not reached).
    std::vector<float> cost;
    std::vector<std::uint8_t> way;
    std::vector<std::uint8_t> settled;
    // Open nodes, a binary heap ordered by cost then node index.
    std::vector<std::pair<float, std::uint32_t>> open;
    // Agents are waiting for the field (set by the step, cleared by the expansion).
    bool wanted = false;
    std::uint64_t lastUsed = 0U;
    // Raised whenever the slot starts a new field, so agents notice their field was replaced.
    std::uint64_t stamp = 0U;

    [[nodiscard]] bool Reaches(const NavPolygonGraph& graph, std::uint32_t node) const noexcept {
        return graph.cluster[node] == cluster && settled[graph.local[node]] != 0U;
    }
    // The portal (index into the graph's portals) leading from `node` toward the source, or kNone.
    [[nodiscard]] std::uint32_t Way(const NavPolygonGraph& graph, std::uint32_t node) const noexcept {
        if (graph.cluster[node] != cluster) return NavPolygonGraph::kNone;
        const std::uint8_t ordinal = way[graph.local[node]];
        return ordinal == 0xFFU ? NavPolygonGraph::kNone : graph.firstPortal[node] + ordinal;
    }
};

// The cheap route toward a destination's cluster over the graph's gateways: every reached cluster
// knows its cost to the goal cluster and the gateway to leave by (a reverse Dijkstra, like a field,
// only as far as agents need). Agents outside the goal cluster follow the exit field of their
// cluster's gateway, which `exitField` remembers per cluster.
struct NavHordeRoute {
    std::uint32_t profile = 0U;
    std::uint32_t goalCluster = NavPolygonGraph::kNone;
    std::uint64_t graphRevision = 0U;
    std::vector<float> cost;
    // Index into the graph's gateways, kNone at the goal cluster and where the route has not reached.
    std::vector<std::uint32_t> gateway;
    std::vector<std::uint8_t> settled;
    std::vector<std::uint8_t> needed;
    std::size_t unmet = 0U;
    std::size_t lookahead = 0U;
    std::vector<std::pair<float, std::uint32_t>> open;
    // Per cluster: the exit field of its gateway (a slot of the field pool) and the stamp it had.
    std::vector<std::uint32_t> exitField;
    std::vector<std::uint64_t> exitStamp;
    // The clusters that have an exit field here, so the use stamps are refreshed without a walk over all clusters.
    std::vector<std::uint32_t> exitClusters;
    std::uint64_t lastUsed = 0U;
    std::uint64_t stamp = 0U;
};

// Something other than a horde agent that pushes horde agents apart: a crowd agent, an obstacle.
struct NavMeshBody {
    kb::math::Vec3 position{};
    float radius = 0.0F;
};

// Moves NavAgents in horde mode: agents heading for the same goal polygon share one navigation
// field instead of each planning a path, steer through the field's portal of the polygon they stand
// on, keep apart with a separation push from a spatial grid built once per step, and move along
// the polygons without leaving them. Far agents tick less often, agents that stopped sleep until
// their goal moves away. Every agent writes only itself and the grid is read in a fixed order, so
// equal input replays exactly for any number of workers.
class NavMeshHorde {
public:
    // `agents` sorted by id; `outputs` receives one entry per agent, in the same order. `bodies` stand
    // where they are for the step and push the agents away like other agents do.
    void Step(NavMeshRuntime& runtime, std::span<const NavMeshCrowdAgentInput> agents, std::span<const NavMeshBody> bodies,
        std::span<const kb::math::Vec3> focuses, const kb::scene::NavigationCrowdSettings& settings, float deltaSeconds,
        kb::ecs::WorkerPool* workers, std::vector<NavMeshCrowdAgentOutput>& outputs);
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
    // field and the route leading there.
    struct Goal {
        std::uint32_t profile = 0U;
        kb::math::Vec3 destination{};
        kb::scene::NavAreaMask areas = 0U;
        std::uint32_t node = NavPolygonGraph::kNone;
        std::uint32_t cluster = NavPolygonGraph::kNone;
        std::uint32_t field = NavPolygonGraph::kNone;
        std::uint32_t route = NavPolygonGraph::kNone;
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
    // The field over `source`'s cluster flowing to `source` (leaving by `exitWay` for an exit field),
    // made if it does not exist yet.
    std::uint32_t FindField(std::uint32_t profile, std::uint32_t source, std::uint8_t exitWay, kb::scene::NavAreaMask areas);
    // Frees the fields unused for a while beyond the cache size, oldest first, and rebuilds the field index.
    void SweepFields(std::size_t cacheSize);
    void RebuildFieldIndex();
    std::uint32_t FindRoute(std::uint32_t profile, std::uint32_t goalCluster, std::size_t cacheSize);
    // Fields and routes the step's agents wait for grow first, then the others, within `budget` nodes.
    void ExpandFields(const NavMeshRuntime& runtime, std::size_t budget);
    std::size_t Expand(const NavMeshRuntime& runtime, NavHordeField& field, std::size_t budget);
    std::size_t ExpandRoute(NavHordeRoute& route, std::size_t budget);
    // Notes that an agent standing in a cluster without guidance waits for it (after the parallel pass).
    void RegisterDemand(std::uint32_t goal, std::uint32_t node);
    void BuildGrid(float cellSize, std::span<const NavMeshBody> bodies);

    std::vector<NavPolygonGraph> graphs_;
    std::vector<NavHordeField> fields_;
    std::vector<NavHordeRoute> routes_;
    // Fields by the hash of their key (profile, source, exit, areas, costs), and the free slots of the pool.
    std::unordered_multimap<std::uint64_t, std::uint32_t> fieldIndex_;
    std::vector<std::uint32_t> freeFields_;
    std::vector<AgentRecord> records_;
    std::vector<AgentRecord> scratch_;
    std::vector<Goal> goals_;
    // Per agent of the step: set when the agent has no guidance yet and waits for it.
    std::vector<std::uint8_t> demand_;
    std::uint64_t costSignature_ = 0U;
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
