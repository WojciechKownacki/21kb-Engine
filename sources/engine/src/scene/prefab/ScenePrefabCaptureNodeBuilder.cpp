#include "scene/prefab/ScenePrefabCaptureNodeBuilder.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneObject.hpp"
#include "scene/prefab/ScenePrefabComponentSnapshot.hpp"

namespace kb::scene {

ScenePrefabNodeDesc ScenePrefabCaptureNodeBuilder::Build(Scene& scene, SceneObject object, std::uint32_t parentNode) {
    return ScenePrefabNodeDesc{
        .name = object.Name(),
        .parentNode = parentNode,
        .transform = object.Transform(),
        .visibility = scene.Components().Visibility().Get(object.Entity()),
        .components = ScenePrefabComponentSnapshot::Capture(scene, object),
    };
}

} // namespace kb::scene
