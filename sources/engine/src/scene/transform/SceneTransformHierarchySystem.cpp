#include "scene/transform/SceneTransformHierarchySystem.hpp"

#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/transform/SceneTransformBranchUpdater.hpp"
#include "scene/transform/SceneTransformRootCollector.hpp"
#include "scene/transform/TransformMath.hpp"

namespace kb::scene {

void SceneTransformHierarchySystem::Update(kb::ecs::World& world, const SceneComponentRegistry& components) const {
    const TransformComponent identity = TransformMath::Identity();
    const std::vector<SceneEntity> roots = SceneTransformRootCollector{}.Collect(world, components.TransformComponentId());

    for (const SceneEntity root : roots) {
        SceneTransformBranchUpdater{}.Update(world, components, root, identity, false);
    }
}

} // namespace kb::scene
