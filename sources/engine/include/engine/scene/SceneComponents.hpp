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
#include "engine/scene/SceneGuideCurveComponents.hpp"
#include "engine/scene/SceneContentInstanceComponents.hpp"
#include "engine/scene/SceneStreamFocusComponents.hpp"
#include "engine/scene/SceneWorldBackdropComponents.hpp"
#include "engine/scene/SceneAmbientRadianceComponents.hpp"
#include "engine/scene/SceneDetailSwitchComponents.hpp"
#include "engine/scene/SceneTagsComponents.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"
#include "engine/scene/SceneUIDocuments.hpp"

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
    [[nodiscard]] SceneCharacterControllerComponents CharacterControllers() const noexcept;
    [[nodiscard]] SceneJointComponents Joints() const noexcept;
    [[nodiscard]] SceneTagsComponents Tags() const noexcept;
    [[nodiscard]] SceneRegionShapeComponents RegionShapes() const noexcept;
    [[nodiscard]] SceneGuideCurveComponents GuideCurves() const noexcept;
    [[nodiscard]] SceneContentInstanceComponents ContentInstances() const noexcept;
    [[nodiscard]] SceneStreamFocusComponents StreamFocuses() const noexcept;
    [[nodiscard]] SceneWorldBackdropComponents WorldBackdrops() const noexcept;
    [[nodiscard]] SceneAmbientRadianceComponents AmbientRadiances() const noexcept;
    [[nodiscard]] SceneDetailSwitchComponents DetailSwitches() const noexcept;
    [[nodiscard]] SceneAudioSourceComponents AudioSources() const noexcept;
    [[nodiscard]] SceneAudioListenerComponents AudioListeners() const noexcept;
    [[nodiscard]] SceneAnimatorComponents Animators() const noexcept;
    [[nodiscard]] SceneUIDocumentComponents UIDocuments() const noexcept;
    [[nodiscard]] SceneNavAgentComponents NavAgents() const noexcept;
    [[nodiscard]] SceneNavObstacleComponents NavObstacles() const noexcept;
    [[nodiscard]] SceneComponentVisitors Visitors() const noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
