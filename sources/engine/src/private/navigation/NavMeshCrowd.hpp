#pragma once

#include "engine/scene/Navigation.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "navigation/NavMeshRuntime.hpp"

#include <DetourNavMesh.h>

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

class dtCrowd;

namespace kb::navigation {

struct NavMeshCrowdAgentInput {
    std::uint64_t id = 0U;
    kb::scene::NavAgent agent{};
    // Where the agent's transform stands, in the mesh's space.
    kb::math::Vec3 position{};
};

struct NavMeshCrowdAgentOutput {
    kb::math::Vec3 position{};
    kb::math::Vec3 velocity{};
    float remainingDistance = 0.0F;
    kb::scene::NavPathStatus status = kb::scene::NavPathStatus::Invalid;
    bool onLink = false;
};

// A non-carving NavObstacle agents steer around, as a circle in the mesh's space.
struct NavMeshCrowdObstacle {
    std::uint64_t id = 0U;
    kb::math::Vec3 center{};
    float radius = 0.0F;
    float height = 0.0F;
};

// Moves NavAgents over the polygon meshes of a NavMeshRuntime with DetourCrowd: one dtCrowd per
// agent profile (a crowd walks one Detour mesh), each agent a crowd member whose parameters come
// from its NavAgent. The crowd plans and follows path corridors, separates, samples velocities
// against neighbours and walls, resolves overlaps and crosses off-mesh links; this class hands it
// destinations (a bounded number per step), maps area masks to crowd filters, stops agents at
// their stopping distance, shapes link crossings by link kind and lowers the update work of agents
// far from every focus by switching crowd update flags. Non-carving obstacles join every crowd as
// members that never move. Agents are added in id order and the crowd runs on one thread, so
// equal input replays exactly.
class NavMeshCrowd {
public:
    NavMeshCrowd();
    ~NavMeshCrowd();
    NavMeshCrowd(const NavMeshCrowd&) = delete;
    NavMeshCrowd& operator=(const NavMeshCrowd&) = delete;

    // `agents` and `obstacles` sorted by id. `outputs` receives one entry per agent, in the same order.
    void Step(NavMeshRuntime& runtime, std::span<const NavMeshCrowdAgentInput> agents, std::span<const NavMeshCrowdObstacle> obstacles,
        std::span<const kb::math::Vec3> focuses, const kb::scene::NavigationCrowdSettings& settings, float deltaSeconds,
        std::vector<NavMeshCrowdAgentOutput>& outputs);
    void Clear();
    [[nodiscard]] const kb::scene::NavigationCrowdStats& Stats() const noexcept { return stats_; }
    [[nodiscard]] bool OnLink(std::uint64_t id) const noexcept;
    // The corners an agent steers through next, in the mesh's space.
    [[nodiscard]] std::vector<kb::math::Vec3> Corners(std::uint64_t id) const;

private:
    struct CrowdDeleter {
        void operator()(dtCrowd* crowd) const noexcept;
    };
    struct ProfileCrowd {
        std::unique_ptr<dtCrowd, CrowdDeleter> crowd;
        int capacity = 0;
        std::uint64_t meshGeneration = 0U;
    };
    struct AgentRecord {
        std::uint32_t profile = 0U;
        // Slot in the profile's crowd; -1 while the agent is not a member.
        int index = -1;
        int filter = 0;
        // The destination handed to the crowd, and whether one was.
        kb::math::Vec3 destination{};
        bool requested = false;
        std::uint64_t requestOrder = 0U;
        // The destination has no polygon near it (yet): retried when the tiles change.
        bool destinationMissing = false;
        // Stopped within the stopping distance of the end of its path.
        bool arrived = false;
        kb::math::Vec3 arrivedAt{};
        std::uint64_t meshRevision = 0U;
        kb::math::Vec3 position{};
        float remaining = 0.0F;
        kb::scene::NavPathStatus status = kb::scene::NavPathStatus::Invalid;
        // The off-mesh link the agent heads for next, and the one it is crossing.
        dtPolyRef nextLink = 0U;
        bool onLink = false;
        kb::scene::NavLinkKind linkKind = kb::scene::NavLinkKind::Walk;
        kb::math::Vec3 linkStart{};
        kb::math::Vec3 linkEnd{};
    };
    struct ObstacleRecord {
        NavMeshCrowdObstacle obstacle{};
        // Slot per profile crowd; -1 while not a member.
        std::vector<int> indices;
    };

    void ResetProfiles(std::size_t count);
    void Detach(AgentRecord& record);

    std::vector<ProfileCrowd> crowds_;
    std::map<std::uint64_t, AgentRecord> agents_;
    std::map<std::uint64_t, ObstacleRecord> obstacles_;
    std::uint64_t step_ = 0U;
    std::uint64_t nextRequest_ = 1U;
    kb::scene::NavigationCrowdStats stats_{};
};

} // namespace kb::navigation
