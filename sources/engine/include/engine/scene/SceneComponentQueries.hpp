#pragma once

#include "engine/scene/SceneAudioListenerComponents.hpp"
#include "engine/scene/SceneAudioSourceComponents.hpp"
#include "engine/scene/SceneAnimatorComponents.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneCharacterControllerComponents.hpp"
#include "engine/scene/SceneColliderComponents.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneInputComponents.hpp"
#include "engine/scene/SceneJointComponents.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneNavigationComponents.hpp"
#include "engine/scene/SceneRigidbodyComponents.hpp"
#include "engine/scene/SceneRegionShapeComponents.hpp"
#include "engine/scene/SceneTagsComponents.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"
#include "engine/scene/SceneUIDocuments.hpp"

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
    [[nodiscard]] SceneCharacterControllerComponentQueries CharacterControllers() const noexcept;
    [[nodiscard]] SceneJointComponentQueries Joints() const noexcept;
    [[nodiscard]] SceneTagsComponentQueries Tags() const noexcept;
    [[nodiscard]] SceneRegionShapeComponentQueries RegionShapes() const noexcept;
    [[nodiscard]] SceneAudioSourceComponentQueries AudioSources() const noexcept;
    [[nodiscard]] SceneAudioListenerComponentQueries AudioListeners() const noexcept;
    [[nodiscard]] SceneAnimatorComponentQueries Animators() const noexcept;
    [[nodiscard]] SceneUIDocumentComponentQueries UIDocuments() const noexcept;
    [[nodiscard]] SceneNavAgentComponentQueries NavAgents() const noexcept;
    [[nodiscard]] SceneNavObstacleComponentQueries NavObstacles() const noexcept;
    [[nodiscard]] SceneComponentVisitors Visitors() const noexcept;

private:
    const Scene& scene_;
};

} // namespace kb::scene
