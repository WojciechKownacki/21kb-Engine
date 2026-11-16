#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class Scene;

// One Post Process Volume that reaches a point, with the weight it has there.
struct ScenePostProcessVolumeInfluence {
    SceneEntity entity{};
    std::uint64_t profileAssetId = 0U;
    std::int32_t priority = 0;
    // 0 < weight <= 1: the volume's Weight, scaled down linearly across the blend zone outside its region shape.
    float weight = 0.0F;
};

// Whether the scene has any Post Process Volume; a renderer skips the volume pass for scenes without one.
[[nodiscard]] bool SceneHasPostProcessVolumes(const Scene& scene) noexcept;

// Appends to `out` the enabled volumes that reach `worldPoint`, lowest priority first (equal priorities in entity
// order), so applying them in order leaves the highest priority on top. A volume whose object has no region shape
// reaches everywhere; one with a region shape reaches inside it and BlendDistance metres around it, and none while
// that shape is disabled or invalid. Volumes with no profile or no weight at the point are left out.
void ScenePostProcessVolumeInfluences(const Scene& scene, const kb::math::DVec3& worldPoint, std::vector<ScenePostProcessVolumeInfluence>& out);

} // namespace kb::scene
