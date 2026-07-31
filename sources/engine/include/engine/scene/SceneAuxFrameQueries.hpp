#pragma once

#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

// Runtime eligibility is deliberately stricter than schema validity: an
// auxiliary frame needs a live owner with a CameraComponent as well.
[[nodiscard]] bool SceneAuxFrameIsRenderable(const Scene& scene, SceneEntity entity) noexcept;

} // namespace kb::scene
