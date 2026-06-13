#pragma once

#include "engine/scene/SceneAudioListenerComponents.hpp"
#include "engine/scene/SceneAudioSourceComponents.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneColliderComponents.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneInputComponents.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneRigidbodyComponents.hpp"
#include "engine/scene/SceneTagsComponents.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"

namespace kb::scene {

class Scene;

class SceneComponents {
public:
    explicit SceneComponents(Scene& scene) noexcept;

    [[nodiscard]] SceneVisibilityComponents Visibility() const noexcept;
    [[nodiscard]] SceneBehaviourComponents Behaviours() const noexcept;
    [[nodiscard]] SceneCameraComponents Cameras() const noexcept;
    [[nodiscard]] SceneMeshRendererComponents MeshRenderers() const noexcept;
    [[nodiscard]] SceneLightComponents Lights() const noexcept;
    [[nodiscard]] SceneInputComponents Inputs() const noexcept;
    [[nodiscard]] SceneRigidbodyComponents Rigidbodies() const noexcept;
    [[nodiscard]] SceneColliderComponents Colliders() const noexcept;
    [[nodiscard]] SceneTagsComponents Tags() const noexcept;
    [[nodiscard]] SceneAudioSourceComponents AudioSources() const noexcept;
    [[nodiscard]] SceneAudioListenerComponents AudioListeners() const noexcept;
    [[nodiscard]] SceneComponentVisitors Visitors() const noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
