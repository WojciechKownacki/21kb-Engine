#include "engine/ecs/World.hpp"

#include "ecs/events/ComponentObserverStorage.hpp"
#include "ecs/world/WorldComponentMutator.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace kb::ecs {

bool World::MirrorsValueWrites(ComponentId componentId) const noexcept {
    return config_.mirrorNativeComponentChangesToBackend
        && (!config_.mirrorValueWritesOnlyForObservedComponents || std::find(observedComponentIds_.begin(), observedComponentIds_.end(), componentId) != observedComponentIds_.end());
}

ObserverId World::ObserveComponent(
    ComponentId componentId,
    std::size_t componentSize,
    ComponentEventKind event,
    RawComponentEventVisitor visitor,
    void* context,
    RawContextFree contextFree,
    bool yieldExisting) noexcept {
    if (config_.mirrorNativeComponentChangesToBackend && config_.mirrorValueWritesOnlyForObservedComponents && nativeStorage_ != nullptr && componentId != 0 && componentSize != 0
        && std::find(observedComponentIds_.begin(), observedComponentIds_.end(), componentId) == observedComponentIds_.end()) {
        // Value writes were not published while nothing observed this component: bring the backend copies up to
        // date before the first observer can look at them (the component is not observed yet, so nothing fires).
        try {
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
            observedComponentIds_.push_back(componentId);
        } catch (...) {
            if (contextFree != nullptr) {
                contextFree(context);
            }
            return 0;
        }
    }
    return ComponentObserverStorage::Create(world_, componentId, componentSize, event, visitor, context, contextFree, yieldExisting);
}

void World::DestroyObserver(ObserverId observer) noexcept {
    ComponentObserverStorage::Destroy(world_, observer);
}

} // namespace kb::ecs
