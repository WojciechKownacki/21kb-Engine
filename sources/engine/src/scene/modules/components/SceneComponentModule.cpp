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

SceneCameraComponentQueries SceneComponentQueries::Cameras() const noexcept {
    return SceneCameraComponentQueries{ scene_ };
}

SceneMeshRendererComponentQueries SceneComponentQueries::MeshRenderers() const noexcept {
    return SceneMeshRendererComponentQueries{ scene_ };
}

SceneLightComponentQueries SceneComponentQueries::Lights() const noexcept {
    return SceneLightComponentQueries{ scene_ };
}

SceneComponentVisitors SceneComponentQueries::Visitors() const noexcept {
    return SceneComponentVisitors{ scene_ };
}

SceneVisibilityComponents SceneComponents::Visibility() const noexcept {
    return SceneVisibilityComponents{ scene_ };
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

SceneComponentVisitors SceneComponents::Visitors() const noexcept {
    return SceneComponentVisitors{ scene_ };
}

} // namespace kb::scene
