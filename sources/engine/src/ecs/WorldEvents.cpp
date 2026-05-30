#include "engine/ecs/World.hpp"

#include "ecs/events/ComponentObserverStorage.hpp"

namespace kb::ecs {

ObserverId World::ObserveComponent(
    ComponentId componentId,
    std::size_t componentSize,
    ComponentEventKind event,
    RawComponentEventVisitor visitor,
    void* context,
    RawContextFree contextFree,
    bool yieldExisting) noexcept {
    return ComponentObserverStorage::Create(world_, componentId, componentSize, event, visitor, context, contextFree, yieldExisting);
}

void World::DestroyObserver(ObserverId observer) noexcept {
    ComponentObserverStorage::Destroy(world_, observer);
}

} // namespace kb::ecs
