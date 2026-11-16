#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// A post-process profile that applies to the camera only where it is: everywhere when the object has no
// Region Shape, otherwise inside the Region Shape of the same object and BlendDistance metres around it.
// A camera inside gets the profile at the full Weight; in the blend zone the weight falls linearly to 0 at
// the edge of the zone. Volumes that reach the camera are applied over the scene's own profile in order of
// Priority, the highest last, each moving the result toward its profile by its weight (the renderer's
// BlendScenePostProcessSettings).
struct PostProcessVolumeComponent {
    static constexpr std::string_view StableId = "kb21.scene.post-process-volume";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t profileAssetId = 0U;
    std::int32_t priority = 0;
    float weight = 1.0F;
    float blendDistance = 0.0F;
    bool enabled = true;
    bool operator==(const PostProcessVolumeComponent&) const = default;
};

[[nodiscard]] inline bool IsPostProcessVolumeComponentValid(const PostProcessVolumeComponent& value) noexcept {
    return std::isfinite(value.weight) && value.weight >= 0.0F && value.weight <= 1.0F &&
        std::isfinite(value.blendDistance) && value.blendDistance >= 0.0F && value.blendDistance <= 1.0e6F &&
        value.priority >= -1000 && value.priority <= 1000;
}

} // namespace kb::scene
