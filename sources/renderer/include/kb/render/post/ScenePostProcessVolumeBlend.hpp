#pragma once

#include "kb/render/post/ScenePostProcessSettings.hpp"

#include "engine/math/DVec3.hpp"

#include <cstdint>
#include <functional>
#include <optional>

namespace kb::scene { class Scene; }

namespace kb::render {

// Moves `base` toward `layer` by `weight` (0..1): numbers interpolate; switches and choices (on/off, tonemap
// operator, metering mode, fog profile, global illumination method) take the layer's value once the weight is
// above one half.
[[nodiscard]] ScenePostProcessSettings BlendScenePostProcessSettings(
    const ScenePostProcessSettings& base, const ScenePostProcessSettings& layer, float weight) noexcept;

// The settings of a post-process profile asset by id; nothing when it cannot be loaded.
using ScenePostProcessProfileLoader = std::function<std::optional<ScenePostProcessSettings>(std::uint64_t)>;

// The settings a camera at `cameraWorldPosition` sees: `base` with every Post Process Volume of `scene` that
// reaches the camera applied over it in order of priority, each by its weight there (see
// kb::scene::ScenePostProcessVolumeInfluences). Nothing when no volume reaches the camera, so the caller keeps
// its own settings untouched. A volume whose profile `load` cannot supply sets `unavailableProfileAssetId` and
// ends the resolve with nothing.
[[nodiscard]] std::optional<ScenePostProcessSettings> ResolveScenePostProcessVolumes(
    const kb::scene::Scene& scene, const kb::math::DVec3& cameraWorldPosition, const ScenePostProcessSettings& base,
    const ScenePostProcessProfileLoader& load, std::uint64_t& unavailableProfileAssetId);

} // namespace kb::render
