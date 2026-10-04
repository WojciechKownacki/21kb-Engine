#include "engine/ecs/World.hpp"

#include "ecs/events/ComponentObserverStorage.hpp"
#include "ecs/world/WorldComponentMutator.hpp"
#include "ecs/world/WorldRegistrySet.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"

#include <array>
#include <vector>

namespace kb::ecs {

bool World::MirrorsValueWrites(ComponentId componentId) const noexcept {
    return config_.mirrorNativeComponentChangesToBackend
        && (!config_.mirrorValueWritesOnlyForObservedComponents || (registries_ != nullptr && registries_->HasComponentObserver(componentId)));
}

ObserverId World::ObserveComponent(
    ComponentId componentId,
    std::size_t componentSize,
    ComponentEventKind event,
    RawComponentEventVisitor visitor,
    void* context,
    RawContextFree contextFree,
    bool yieldExisting) noexcept {
    if (world_ == nullptr || registries_ == nullptr || componentId == 0U || componentSize == 0U || visitor == nullptr) {
        if (contextFree != nullptr) contextFree(context);
        return 0U;
    }
    try {
        if (config_.mirrorNativeComponentChangesToBackend && config_.mirrorValueWritesOnlyForObservedComponents && nativeStorage_ != nullptr
            && !registries_->HasComponentObserver(componentId)) {
            // Value writes were not published while nothing observed this component: bring the backend copies up to
            // date before the first observer can look at them (the component is not observed yet, so nothing fires).
            std::vector<QueryTableDispatchRecord> records;
            records.reserve(nativeStorage_->ChunkCount());
            const std::array componentIds{ componentId };
            nativeStorage_->CollectQueryRecords(componentIds, {}, {}, records);
            for (const QueryTableDispatchRecord& record : records) {
                const auto* componentBytes = static_cast<const std::uint8_t*>(record.fieldComponents[0]);
                for (std::size_t index = 0; index < record.entityCount; ++index) {
                    const Entity entity{ record.entityIds[index] };
                    if (BackendEntityAlive(entity)) {
                        WorldComponentMutator::SetExisting(world_, entity, componentId, componentSize, componentBytes + index * componentSize);
                    }
                }
            }
        }
        // Retain before Create: yieldExisting can reenter registration or delete
        // another receiver. The backend-owned context balances this on every
        // creation failure and at actual destruction, including raw deletion.
        registries_->RetainComponentObserver(componentId);
    } catch (...) {
        if (contextFree != nullptr) contextFree(context);
        return 0U;
    }
    return ComponentObserverStorage::Create(world_, componentId, componentSize, event, visitor, context, contextFree, yieldExisting, registries_.get());
}

void World::DestroyObserver(ObserverId observer) noexcept {
    ComponentObserverStorage::Destroy(world_, observer);
}

} // namespace kb::ecs
