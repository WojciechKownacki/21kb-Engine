#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"

namespace kb::scene {

SceneComponentQueries::SceneComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

SceneComponents::SceneComponents(Scene& scene) noexcept
    : scene_(scene) {}

SceneVisibilityComponentQueries SceneComponentQueries::Visibility() const noexcept {
    return SceneVisibilityComponentQueries{ scene_ };
}

SceneBehaviourComponentQueries SceneComponentQueries::Behaviours() const noexcept {
    return SceneBehaviourComponentQueries{ scene_ };
}

SceneCameraComponentQueries SceneComponentQueries::Cameras() const noexcept {
    return SceneCameraComponentQueries{ scene_ };
}

SceneMeshRendererComponentQueries SceneComponentQueries::MeshRenderers() const noexcept {
    return SceneMeshRendererComponentQueries{ scene_ };
}

SceneLightComponentQueries SceneComponentQueries::Lights() const noexcept {
    return SceneLightComponentQueries{ scene_ };
}

SceneInputComponentQueries SceneComponentQueries::Inputs() const noexcept {
    return SceneInputComponentQueries{ scene_ };
}

SceneRigidbodyComponentQueries SceneComponentQueries::Rigidbodies() const noexcept {
    return SceneRigidbodyComponentQueries{ scene_ };
}

SceneColliderComponentQueries SceneComponentQueries::Colliders() const noexcept {
    return SceneColliderComponentQueries{ scene_ };
}

SceneComponentVisitors SceneComponentQueries::Visitors() const noexcept {
    return SceneComponentVisitors{ scene_ };
}

SceneVisibilityComponents SceneComponents::Visibility() const noexcept {
    return SceneVisibilityComponents{ scene_ };
}

SceneBehaviourComponents SceneComponents::Behaviours() const noexcept {
    return SceneBehaviourComponents{ scene_ };
}

SceneCameraComponents SceneComponents::Cameras() const noexcept {
    return SceneCameraComponents{ scene_ };
}

SceneMeshRendererComponents SceneComponents::MeshRenderers() const noexcept {
    return SceneMeshRendererComponents{ scene_ };
}

SceneLightComponents SceneComponents::Lights() const noexcept {
    return SceneLightComponents{ scene_ };
}

SceneInputComponents SceneComponents::Inputs() const noexcept {
    return SceneInputComponents{ scene_ };
}

SceneRigidbodyComponents SceneComponents::Rigidbodies() const noexcept {
    return SceneRigidbodyComponents{ scene_ };
}

SceneColliderComponents SceneComponents::Colliders() const noexcept {
    return SceneColliderComponents{ scene_ };
}

SceneComponentVisitors SceneComponents::Visitors() const noexcept {
    return SceneComponentVisitors{ scene_ };
}

} // namespace kb::scene
