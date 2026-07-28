#include "scene/entities/SceneEntityDuplicateService.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"

namespace kb::scene {
namespace {

void CopyOptionalComponents(Scene& scene, SceneObject source, SceneObject target) {
    SceneComponents components = scene.Components();
    const SceneEntity sourceEntity = source.Entity();
    const SceneEntity targetEntity = target.Entity();
    if (const CameraComponent* camera = components.Cameras().TryGet(sourceEntity)) {
        components.Cameras().Set(targetEntity, *camera);
    }
    if (const MeshRendererComponent* meshRenderer = components.MeshRenderers().TryGet(sourceEntity)) {
        components.MeshRenderers().Set(targetEntity, *meshRenderer);
    }
    if (const LightComponent* light = components.Lights().TryGet(sourceEntity)) {
        components.Lights().Set(targetEntity, *light);
    }
    if (const Animator* animator = components.Animators().TryGet(sourceEntity)) {
        components.Animators().Set(targetEntity, *animator);
    }
}

SceneObject DuplicateBranch(Scene& scene, SceneObject source, SceneObject parent) {
    if (!source.IsValid() || !scene.Entities().IsAlive(source)) {
        return {};
    }

    SceneObject duplicate = scene.Entities().CreateObject(SceneObjectDesc{
        .name = scene.Entities().Name(source),
        .parent = parent,
        .transform = scene.Transforms().Get(source),
        .visibility = scene.Components().Visibility().Get(source.Entity()),
    });
    CopyOptionalComponents(scene, source, duplicate);

    for (const SceneEntity child : scene.Hierarchy().ChildEntities(source.Entity())) {
        static_cast<void>(DuplicateBranch(scene, SceneAccess::MakeObject(scene, child), duplicate));
    }
    return duplicate;
}

} // namespace

SceneObject SceneEntityDuplicateService::Duplicate(Scene& scene, SceneObject object) {
    return DuplicateBranch(scene, object, scene.Hierarchy().Parent(object));
}

std::vector<SceneObject> SceneEntityDuplicateService::Duplicate(Scene& scene, std::span<const SceneObject> objects) {
    std::vector<SceneObject> duplicates;
    duplicates.reserve(objects.size());
    for (const SceneObject object : objects) {
        SceneObject duplicate = Duplicate(scene, object);
        if (duplicate.IsValid()) {
            duplicates.push_back(duplicate);
        }
    }
    return duplicates;
}

} // namespace kb::scene
