#pragma once

#include "engine/scene/Navigation.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "navigation/NavMeshRuntime.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

class dtObstacleAvoidanceQuery;

namespace kb::navigation {

struct NavCrowdAgentInput {
    std::uint64_t id = 0U;
    kb::scene::NavAgent agent{};
    // Where the agent's transform stands, in the mesh's space.
    kb::math::Vec3 position{};
};

struct NavCrowdAgentOutput {
    kb::math::Vec3 position{};
    kb::math::Vec3 velocity{};
    float remainingDistance = 0.0F;
    kb::scene::NavPathStatus status = kb::scene::NavPathStatus::Invalid;
    bool onLink = false;
};

// A circle agents steer around without it changing the mesh (a non-carving NavObstacle).
struct NavCrowdCircle {
    kb::math::Vec3 center{};
    float radius = 0.0F;
    float bottom = 0.0F;
    float top = 0.0F;
};

struct NavCrowdAgentState;

// Moves agents over the polygon meshes of a NavMeshRuntime: path corridors planned with a bounded
// number of searches per step, string pulling, visibility and topology shortcuts, separation,
// velocity obstacle sampling against neighbours, walls and obstacle circles, collision resolution,
// off-mesh link traversal, and a distance level of detail. Agents are processed in id order and
// every agent decides from the same snapshot, so equal input replays exactly.
class NavCrowd {
public:
    NavCrowd();
    ~NavCrowd();
    NavCrowd(const NavCrowd&) = delete;
    NavCrowd& operator=(const NavCrowd&) = delete;

    // `agents` sorted by id. `outputs` receives one entry per agent, in the same order.
    void Step(NavMeshRuntime& runtime, std::span<const NavCrowdAgentInput> agents, std::span<const NavCrowdCircle> circles,
        std::span<const kb::math::Vec3> focuses, const kb::scene::NavCrowdSettings& settings, float deltaSeconds,
        std::vector<NavCrowdAgentOutput>& outputs);
    void Clear();
    [[nodiscard]] const kb::scene::NavCrowdStats& Stats() const noexcept { return stats_; }
    [[nodiscard]] bool OnLink(std::uint64_t id) const noexcept;
    // The corners an agent steers through next, in the mesh's space.
    [[nodiscard]] std::vector<kb::math::Vec3> Corners(std::uint64_t id) const;

private:
    struct AvoidanceDeleter {
        void operator()(dtObstacleAvoidanceQuery* query) const noexcept;
    };

    std::map<std::uint64_t, std::unique_ptr<NavCrowdAgentState>> agents_;
    std::unique_ptr<dtObstacleAvoidanceQuery, AvoidanceDeleter> avoidance_;
    std::uint64_t step_ = 0U;
    std::uint64_t nextRequest_ = 1U;
    kb::scene::NavCrowdStats stats_{};
};

} // namespace kb::navigation
