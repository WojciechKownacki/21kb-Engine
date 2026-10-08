#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/Navigation.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace kb::scene {

// What one NavAgent is following: the corners of its current path and why it was planned.
struct NavAgentRuntimeRecord {
    kb::math::Vec3 plannedDestination{};
    NavAreaMask plannedAreas = kAllNavAreas;
    float plannedRadius = 0.0F;
    std::uint64_t plannedMeshRevision = 0U;
    std::uint64_t plannedObstacleSignature = 0U;
    bool planned = false;
    NavPathStatus status = NavPathStatus::Invalid;
    std::vector<kb::math::Vec3> corners;
    std::size_t nextCorner = 0U;
};

// The scene's navigation graph and the per-agent runtime the navigation system keeps across steps.
struct SceneNavigationState {
    NavMesh mesh{};
    std::map<std::uint64_t, NavAgentRuntimeRecord> agents;
    float stepAccumulator = 0.0F;
};

} // namespace kb::scene
