#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace kb::scene {

class Scene;

// The scene's navigation graph and what its agents are doing on it. While the scene plays, the
// navigation system moves every enabled NavAgent along a path planned on this graph toward the agent's
// destination: carving NavObstacles block the graph nodes and edges they stand on, other obstacles and
// other agents are avoided by choosing velocities that do not collide (reciprocal velocity obstacles).
// The system steps at the scene's fixed step rate in entity id order, so equal input replays exactly.
// An agent with a CharacterController hands its velocity to the physics character; others move their
// Transform. Without a graph agents stay where they are and report a failed path. Destinations, node
// positions and path corners are in the graph's space: world positions minus NavMesh::origin.
class SceneNavigation final {
public:
    explicit SceneNavigation(Scene& scene) noexcept;

    // Replaces the graph. Its revision is raised above the previous graph's, so every agent re-plans.
    void SetMesh(NavMesh mesh);
    void ClearMesh();
    [[nodiscard]] const NavMesh& Mesh() const noexcept;
    // The closest node an agent with these areas may stand on; nothing for an empty graph.
    [[nodiscard]] std::optional<std::uint32_t> NearestNode(kb::math::Vec3 position, NavAreaMask areas = kAllNavAreas) const;
    // The corners the agent is following (in the graph's space), starting with the next one; empty without a path.
    [[nodiscard]] std::vector<kb::math::Vec3> AgentPath(SceneEntity agent) const;

private:
    Scene& scene_;
};

} // namespace kb::scene
