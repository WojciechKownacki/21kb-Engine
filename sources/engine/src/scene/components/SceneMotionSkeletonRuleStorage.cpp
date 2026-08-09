#include "scene/components/SceneMotionSkeletonRuleComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneMotionSkeletonRuleComponentStore::SceneMotionSkeletonRuleComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) { static_cast<void>(componentId); }

bool SceneMotionSkeletonRuleComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<MotionSkeletonRuleComponent>(world_, entity);
}
const MotionSkeletonRuleComponent* SceneMotionSkeletonRuleComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<MotionSkeletonRuleComponent>(world_, entity);
}
MotionSkeletonRuleComponent* SceneMotionSkeletonRuleComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<MotionSkeletonRuleComponent>(world_, entity);
}
void SceneMotionSkeletonRuleComponentStore::Set(SceneEntity entity, const MotionSkeletonRuleComponent& rule) {
    SceneComponentStorageAccess::Set<MotionSkeletonRuleComponent>(world_, entity, rule);
}
void SceneMotionSkeletonRuleComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<MotionSkeletonRuleComponent>(world_, entity);
}
void SceneMotionSkeletonRuleComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<MotionSkeletonRuleComponent>(world_, entity);
}

} // namespace kb::scene
