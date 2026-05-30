#pragma once

#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"

namespace kb::scene {

class Scene;

class SceneComponents {
public:
    explicit SceneComponents(Scene& scene) noexcept;

    [[nodiscard]] SceneVisibilityComponents Visibility() const noexcept;
    [[nodiscard]] SceneCameraComponents Cameras() const noexcept;
    [[nodiscard]] SceneMeshRendererComponents MeshRenderers() const noexcept;
    [[nodiscard]] SceneLightComponents Lights() const noexcept;
    [[nodiscard]] SceneComponentVisitors Visitors() const noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
