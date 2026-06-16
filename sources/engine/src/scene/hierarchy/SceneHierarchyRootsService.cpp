#include "scene/hierarchy/SceneHierarchyRootsService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"

namespace kb::scene {

std::vector<SceneObject> SceneHierarchyRootsService::RootObjects(Scene& scene) {
    std::vector<SceneObject> objects;
    for (const SceneEntity root : RootEntities(scene)) {
        objects.push_back(SceneAccess::MakeObject(scene, root));
    }

    return objects;
}

std::vector<SceneEntity> SceneHierarchyRootsService::RootEntities(const Scene& scene) {
    const SceneState& state = SceneAccess::State(scene);
    return SceneHierarchyCache::Roots(state);
}

} // namespace kb::scene
