#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneNavigation.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace kb::navigation {
class NavMeshCrowd;
class NavMeshRuntime;
}

namespace kb::scene {

// A navigation mesh asset a ContentInstance of kind NavigationMesh places in the scene.
struct PlacedNavMeshRecord {
    std::uint64_t assetId = 0U;
    // SceneNavigation::AddNavMesh handle once the asset is loaded and added; 0 before.
    std::uint64_t handle = 0U;
    bool requested = false;
    bool failed = false;
};

// The scene's polygon navigation meshes, the crowd moving its agents and what the navigation system
// keeps across steps.
struct SceneNavigationState {
    // SceneNavigation::Origin: the world position Detour's float space is centred on.
    kb::math::DVec3 origin{};
    float stepAccumulator = 0.0F;
    // Polygon navigation meshes and the crowd moving agents over them (created on first use).
    std::shared_ptr<kb::navigation::NavMeshRuntime> polygons;
    std::shared_ptr<kb::navigation::NavMeshCrowd> crowd;
    NavigationCrowdSettings crowdSettings{};
    std::vector<kb::math::DVec3> crowdFocuses;
    bool crowdFocusesSet = false;
    // Keyed by the ContentInstance owner's entity id.
    std::map<std::uint64_t, PlacedNavMeshRecord> placedMeshes;
    // Corners of the last path Navigation.FindPath found from a script, for Navigation.PathCorner.
    std::vector<kb::math::DVec3> scriptPath;
};

} // namespace kb::scene
