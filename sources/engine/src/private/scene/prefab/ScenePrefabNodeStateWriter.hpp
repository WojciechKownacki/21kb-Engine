#pragma once

#include "engine/scene/SceneAudioListenerComponents.hpp"
#include "engine/scene/SceneAudioSourceComponents.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneCharacterControllerComponents.hpp"
#include "engine/scene/SceneColliderComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneInputComponents.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneNavigationComponents.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/SceneRigidbodyComponents.hpp"
#include "engine/scene/SceneRegionShapeComponents.hpp"
#include "engine/scene/SceneGuideCurveComponents.hpp"
#include "engine/scene/SceneContentInstanceComponents.hpp"
#include "engine/scene/SceneTagsComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"
#include "engine/scene/SceneUIDocuments.hpp"

namespace kb::scene {

class Scene;
class SceneObject;
class SceneState;

struct ScenePrefabNodeStateWriterContext {
    ScenePrefabNodeStateWriterContext(Scene& scene, SceneState& state) noexcept;
    ~ScenePrefabNodeStateWriterContext() noexcept;

    ScenePrefabNodeStateWriterContext(const ScenePrefabNodeStateWriterContext&) = delete;
    ScenePrefabNodeStateWriterContext& operator=(const ScenePrefabNodeStateWriterContext&) = delete;

    SceneState& state;
    bool previousSuppressPrefabDirtyTracking = false;
    SceneEntities entities;
    SceneHierarchyAccess hierarchy;
    SceneTransforms transforms;
    SceneVisibilityComponents visibility;
    SceneCameraComponents cameras;
    SceneMeshRendererComponents meshRenderers;
    SceneLightComponents lights;
    SceneInputComponents inputs;
    SceneRigidbodyComponents rigidbodies;
    SceneColliderComponents colliders;
    SceneCharacterControllerComponents characterControllers;
    SceneTagsComponents tags;
    SceneRegionShapeComponents regionShapes;
    SceneGuideCurveComponents guideCurves;
    SceneContentInstanceComponents contentInstances;
    SceneBehaviourComponents behaviours;
    SceneAudioSourceComponents audioSources;
    SceneAudioListenerComponents audioListeners;
    SceneAnimatorComponents animators;
    SceneUIDocumentComponents uiDocuments;
    SceneNavAgentComponents navAgents;
    SceneNavObstacleComponents navObstacles;
};

class ScenePrefabNodeStateWriter {
public:
    ScenePrefabNodeStateWriter() = delete;

    static void Write(Scene& scene, SceneObject object, SceneObject parent, const ScenePrefabNodeDesc& node);
    static void Write(ScenePrefabNodeStateWriterContext& context, SceneObject object, SceneObject parent, const ScenePrefabNodeDesc& node);
};

} // namespace kb::scene
