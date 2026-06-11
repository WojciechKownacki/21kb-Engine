#pragma once

#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneColliderComponents.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneInputComponents.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneRigidbodyComponents.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"

namespace kb::scene {

class Scene;

class SceneComponentQueries {
public:
    explicit SceneComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] SceneVisibilityComponentQueries Visibility() const noexcept;
    [[nodiscard]] SceneBehaviourComponentQueries Behaviours() const noexcept;
    [[nodiscard]] SceneCameraComponentQueries Cameras() const noexcept;
    [[nodiscard]] SceneMeshRendererComponentQueries MeshRenderers() const noexcept;
    [[nodiscard]] SceneLightComponentQueries Lights() const noexcept;
    [[nodiscard]] SceneInputComponentQueries Inputs() const noexcept;
    [[nodiscard]] SceneRigidbodyComponentQueries Rigidbodies() const noexcept;
    [[nodiscard]] SceneColliderComponentQueries Colliders() const noexcept;
    [[nodiscard]] SceneComponentVisitors Visitors() const noexcept;

private:
    const Scene& scene_;
};

} // namespace kb::scene
