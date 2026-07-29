#include "scene/components/SceneNavigationComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneNavigationComponentStore::SceneNavigationComponentStore(kb::ecs::World& world) noexcept : world_(&world) {}

#define KB_NAVIGATION_STORE(Name, Type) \
bool SceneNavigationComponentStore::Has##Name(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<Type>(world_, entity); } \
const Type* SceneNavigationComponentStore::TryGet##Name(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<Type>(world_, entity); } \
Type* SceneNavigationComponentStore::TryGet##Name(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<Type>(world_, entity); } \
void SceneNavigationComponentStore::Set##Name(SceneEntity entity, const Type& component) { SceneComponentStorageAccess::Set<Type>(world_, entity, component); } \
void SceneNavigationComponentStore::Remove##Name(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<Type>(world_, entity); } \
void SceneNavigationComponentStore::Mark##Name##Modified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<Type>(world_, entity); }

KB_NAVIGATION_STORE(NavAgent, NavAgent)
KB_NAVIGATION_STORE(NavObstacle, NavObstacle)

#undef KB_NAVIGATION_STORE

} // namespace kb::scene
