#include "scene/prefab/ScenePrefabNodeStateWriter.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAudioListenerComponents.hpp"
#include "engine/scene/SceneAudioSourceComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneColliderComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneInputComponents.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneRigidbodyComponents.hpp"
#include "engine/scene/SceneTagsComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"

namespace kb::scene {
namespace {

template <typename T, typename Components>
void WriteOptionalComponent(Components components, SceneEntity entity, const std::optional<T>& component) {
    if (component.has_value()) {
        components.Set(entity, *component);
    } else {
        components.Remove(entity);
    }
}

} // namespace

void ScenePrefabNodeStateWriter::Write(Scene& scene, SceneObject object, SceneObject parent, const ScenePrefabNodeDesc& node) {
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return;
    }

    const SceneEntity entity = object.Entity();
    scene.Entities().SetName(object, node.name);
    static_cast<void>(scene.Hierarchy().SetParent(object, parent));
    scene.Transforms().Set(entity, node.transform);
    scene.Components().Visibility().Set(entity, node.visibility);

    SceneComponents components = scene.Components();
    WriteOptionalComponent(components.Cameras(), entity, node.components.camera);
    WriteOptionalComponent(components.MeshRenderers(), entity, node.components.meshRenderer);
    WriteOptionalComponent(components.Lights(), entity, node.components.light);
    WriteOptionalComponent(components.Inputs(), entity, node.components.input);
    WriteOptionalComponent(components.Rigidbodies(), entity, node.components.rigidbody);
    WriteOptionalComponent(components.Colliders(), entity, node.components.collider);
    WriteOptionalComponent(components.Tags(), entity, node.components.tags);
    WriteOptionalComponent(components.Behaviours(), entity, node.components.behaviour);
    WriteOptionalComponent(components.AudioSources(), entity, node.components.audioSource);
    WriteOptionalComponent(components.AudioListeners(), entity, node.components.audioListener);
}

} // namespace kb::scene
