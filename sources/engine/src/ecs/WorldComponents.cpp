#include "engine/ecs/World.hpp"

#include "engine/ecs/MutableComponentBorrowLocks.hpp"
#include "ecs/ComponentRegistry.hpp"
#include "ecs/FlecsEntityIds.hpp"
#include "ecs/world/WorldComponentIterator.hpp"
#include "ecs/world/WorldComponentMutator.hpp"
#include "ecs/world/WorldComponentReader.hpp"
#include "ecs/world/WorldComponentRegistrar.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <flecs.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace kb::ecs {
namespace {

[[nodiscard]] std::vector<ecs_id_t> SortedUniqueIds(std::vector<ecs_id_t> ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

[[nodiscard]] std::size_t ResolveBulkComponentSourceCount(std::size_t componentSourceCount, std::size_t rowCount) {
    const std::size_t sourceCount = componentSourceCount == 0U ? rowCount : componentSourceCount;
    if (sourceCount == 0U || sourceCount > rowCount || (rowCount % sourceCount) != 0U) {
        throw std::invalid_argument("ECS bulk component source count must divide entity count");
    }
    return sourceCount;
}

[[nodiscard]] const void* BulkComponentRowData(const void* data, std::size_t componentSize, std::size_t componentSourceCount, std::size_t rowIndex, std::size_t rowCount) {
    const std::size_t sourceCount = ResolveBulkComponentSourceCount(componentSourceCount, rowCount);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const std::size_t sourceIndex = sourceCount == 1U ? 0U : rowIndex % sourceCount;
    return bytes + (sourceIndex * componentSize);
}

} // namespace

ComponentId World::RegisterComponent(
    std::type_index type,
    std::string_view name,
    std::size_t size,
    std::size_t alignment,
    ComponentRegistrationOptions options) {
    return WorldComponentRegistrar::Register(world_, registries_.get(), type, name, size, alignment, options);
}

ComponentId World::FindComponent(std::type_index type) const noexcept {
    return WorldComponentRegistrar::Find(registries_.get(), type);
}

ComponentStorageClass World::ComponentStorage(ComponentId componentId) const noexcept {
    if (componentId == 0 || registries_ == nullptr) {
        return ComponentStorageClass::HotTable;
    }
    const ComponentTypeInfo* componentInfo = registries_->Components().FindInfo(componentId);
    return componentInfo == nullptr ? ComponentStorageClass::HotTable : componentInfo->storageClass;
}

NativeComponentValue World::MakeNativeComponentValue(const BulkComponentData& component) const {
    if (component.componentId == 0 || component.componentSize == 0 || component.data == nullptr) {
        throw std::invalid_argument("ECS native storage received invalid component data");
    }
    if (registries_ == nullptr) {
        throw std::runtime_error("ECS native storage requires component registries");
    }
    const ComponentTypeInfo* componentInfo = registries_->Components().FindInfo(component.componentId);
    if (componentInfo == nullptr) {
        throw std::invalid_argument("ECS native storage received an unregistered component id");
    }
    if (componentInfo->size != component.componentSize) {
        throw std::invalid_argument("ECS native storage component payload size does not match registered component type");
    }
    return NativeComponentValue{
        .type = NativeComponentType{
            .id = component.componentId,
            .size = componentInfo->size,
            .alignment = componentInfo->alignment,
            .storageClass = componentInfo->storageClass,
        },
        .data = component.data,
    };
}

std::vector<NativeComponentValue> World::MakeNativeComponentValues(std::span<const BulkComponentData> components) const {
    std::vector<NativeComponentValue> nativeComponents;
    nativeComponents.reserve(components.size());
    for (const BulkComponentData& component : components) {
        nativeComponents.push_back(MakeNativeComponentValue(component));
    }
    return nativeComponents;
}

std::vector<NativeBulkComponentColumn> World::MakeNativeBulkComponentColumns(std::span<const BulkComponentData> components) const {
    std::vector<NativeBulkComponentColumn> nativeComponents;
    nativeComponents.reserve(components.size());
    for (const BulkComponentData& component : components) {
        const NativeComponentValue nativeComponent = MakeNativeComponentValue(component);
        nativeComponents.push_back(NativeBulkComponentColumn{
            .type = nativeComponent.type,
            .data = component.data,
            .stride = component.sourceCount == 1U ? 0U : component.componentSize,
            .sourceCount = component.sourceCount,
        });
    }
    return nativeComponents;
}

void World::DestroyNativeEntity(Entity entity) noexcept {
    if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity)) {
        nativeStorage_->DestroyEntity(entity);
    }
}

void World::SetNativeComponent(Entity entity, const BulkComponentData& component) {
    if (nativeStorage_ == nullptr) {
        return;
    }
    if (!nativeStorage_->IsAlive(entity)) {
        throw std::runtime_error("ECS native storage mirror is missing a live world entity");
    }

    const NativeComponentValue nativeComponent = MakeNativeComponentValue(component);
    if (nativeStorage_->HasComponent(entity, component.componentId)) {
        nativeStorage_->SetComponent(entity, component.componentId, component.data, component.componentSize);
        return;
    }
    const std::array nativeComponents{ nativeComponent };
    nativeStorage_->AddComponents(entity, nativeComponents);
}

void World::AddNativeComponents(Entity entity, std::span<const BulkComponentData> components) {
    if (nativeStorage_ == nullptr || components.empty()) {
        return;
    }
    if (!nativeStorage_->IsAlive(entity)) {
        throw std::runtime_error("ECS native storage mirror is missing a live world entity");
    }

    const std::vector<NativeComponentValue> nativeComponents = MakeNativeComponentValues(components);
    std::vector<NativeComponentValue> addedComponents;
    addedComponents.reserve(nativeComponents.size());
    for (const NativeComponentValue& component : nativeComponents) {
        if (nativeStorage_->HasComponent(entity, component.type.id)) {
            nativeStorage_->SetComponent(entity, component.type.id, component.data, component.type.size);
        } else {
            addedComponents.push_back(component);
        }
    }
    if (!addedComponents.empty()) {
        nativeStorage_->AddComponents(entity, addedComponents);
    }
}

void World::AddNativeComponents(std::span<const Entity> entities, std::span<const BulkComponentData> components) {
    if (nativeStorage_ == nullptr || entities.empty() || components.empty()) {
        return;
    }

    const std::vector<NativeBulkComponentColumn> nativeComponents = MakeNativeBulkComponentColumns(components);
    bool anyExisting = false;
    bool anyMissing = false;
    for (Entity entity : entities) {
        if (!nativeStorage_->IsAlive(entity)) {
            throw std::runtime_error("ECS native storage mirror is missing a live world entity");
        }
        for (const NativeBulkComponentColumn& component : nativeComponents) {
            const bool hasComponent = nativeStorage_->HasComponent(entity, component.type.id);
            anyExisting = anyExisting || hasComponent;
            anyMissing = anyMissing || !hasComponent;
        }
    }

    if (!anyMissing) {
        nativeStorage_->SetComponents(entities, nativeComponents);
        return;
    }
    if (!anyExisting) {
        nativeStorage_->AddComponents(entities, nativeComponents);
        return;
    }

    for (std::size_t index = 0; index < entities.size(); ++index) {
        std::vector<BulkComponentData> entityComponents;
        entityComponents.reserve(components.size());
        for (const BulkComponentData& component : components) {
            entityComponents.push_back(BulkComponentData{
                .componentId = component.componentId,
                .componentSize = component.componentSize,
                .data = BulkComponentRowData(component.data, component.componentSize, component.sourceCount, index, entities.size()),
            });
        }
        AddNativeComponents(entities[index], entityComponents);
    }
}

void World::RemoveNativeComponents(Entity entity, std::span<const ComponentId> componentIds) noexcept {
    if (nativeStorage_ == nullptr || componentIds.empty() || !nativeStorage_->IsAlive(entity)) {
        return;
    }

    std::vector<ComponentId> existingComponentIds;
    existingComponentIds.reserve(componentIds.size());
    for (ComponentId componentId : componentIds) {
        if (componentId != 0 && nativeStorage_->HasComponent(entity, componentId)) {
            existingComponentIds.push_back(componentId);
        }
    }
    if (!existingComponentIds.empty()) {
        nativeStorage_->RemoveComponents(entity, existingComponentIds);
    }
}

void World::RemoveNativeComponents(std::span<const Entity> entities, std::span<const ComponentId> componentIds) {
    if (nativeStorage_ == nullptr || entities.empty() || componentIds.empty()) {
        return;
    }

    std::vector<ComponentId> existingComponentIds;
    existingComponentIds.reserve(componentIds.size());
    for (ComponentId componentId : componentIds) {
        if (componentId != 0) {
            existingComponentIds.push_back(componentId);
        }
    }
    existingComponentIds = SortedUniqueIds(std::move(existingComponentIds));
    if (existingComponentIds.empty()) {
        return;
    }

    bool allEntitiesHaveAllComponents = true;
    for (Entity entity : entities) {
        if (!nativeStorage_->IsAlive(entity)) {
            allEntitiesHaveAllComponents = false;
            break;
        }
        for (ComponentId componentId : existingComponentIds) {
            if (!nativeStorage_->HasComponent(entity, componentId)) {
                allEntitiesHaveAllComponents = false;
                break;
            }
        }
        if (!allEntitiesHaveAllComponents) {
            break;
        }
    }

    if (allEntitiesHaveAllComponents) {
        nativeStorage_->RemoveComponents(entities, existingComponentIds);
        return;
    }

    for (Entity entity : entities) {
        RemoveNativeComponents(entity, existingComponentIds);
    }
}

void World::SetComponent(Entity entity, ComponentId componentId, std::size_t size, const void* component) {
    ValidateEntityHandle(entity, "SetComponent");
    if (IsAlive(entity) && !HasComponent(entity, componentId)) {
        ValidateStructuralChangeAllowed("SetComponent");
    }
    const bool backendAlive = BackendEntityAlive(entity);
    ecs_table_t* previousArchetype = backendAlive ? EntityArchetype(entity) : nullptr;
    SetNativeComponent(entity, BulkComponentData{
        .componentId = componentId,
        .componentSize = size,
        .data = component,
    });
    if (backendAlive && config_.mirrorNativeComponentChangesToBackend) {
        WorldComponentMutator::Set(world_, entity, componentId, size, component);
    }
    InvalidateQueryPlansForArchetypeChange(previousArchetype, backendAlive ? EntityArchetype(entity) : nullptr);
}

void World::SetComponentColumn(std::span<const Entity> entities, const BulkComponentData& component) {
    if (entities.empty()) {
        return;
    }
    if (component.componentId == 0 || component.componentSize == 0 || component.data == nullptr) {
        throw std::invalid_argument("ECS bulk component set received invalid component data");
    }
    if (component.sourceCount != entities.size()) {
        throw std::invalid_argument("ECS bulk component set requires one component value per entity");
    }

    bool structuralChange = false;
    for (Entity entity : entities) {
        ValidateEntityHandle(entity, "SetComponentColumn");
        if (IsAlive(entity) && !HasComponent(entity, component.componentId)) {
            ValidateStructuralChangeAllowed("SetComponentColumn");
            structuralChange = true;
        }
    }

    AddNativeComponents(entities, std::span<const BulkComponentData>{ &component, 1U });

    if (config_.mirrorNativeComponentChangesToBackend) {
        const auto* componentBytes = static_cast<const std::uint8_t*>(component.data);
        for (std::size_t index = 0; index < entities.size(); ++index) {
            const Entity entity = entities[index];
            if (BackendEntityAlive(entity)) {
                WorldComponentMutator::Set(
                    world_,
                    entity,
                    component.componentId,
                    component.componentSize,
                    componentBytes + (index * component.componentSize));
            }
        }
    }

    if (structuralChange) {
        RecordStructuralChange(entities.size());
    }
}

void World::AddComponents(Entity entity, std::span<const BulkComponentData> components) {
    if (components.empty()) {
        return;
    }
    ValidateEntityHandle(entity, "AddComponents");

    const bool backendAlive = BackendEntityAlive(entity);
    ecs_table_t* previousArchetype = backendAlive ? EntityArchetype(entity) : nullptr;
    bool hasMissingComponent = false;

    std::vector<ComponentId> validatedComponentIds;
    validatedComponentIds.reserve(components.size());

    for (const BulkComponentData& component : components) {
        if (component.componentId == 0 || component.componentSize == 0 || component.data == nullptr) {
            throw std::invalid_argument("ECS bulk component mutation received invalid component data");
        }
        if (std::find(validatedComponentIds.begin(), validatedComponentIds.end(), component.componentId) != validatedComponentIds.end()) {
            throw std::invalid_argument("ECS bulk component mutation received duplicate component data");
        }
        if (registries_ != nullptr) {
            const ComponentTypeInfo* componentInfo = registries_->Components().FindInfo(component.componentId);
            if (componentInfo == nullptr) {
                throw std::invalid_argument("ECS bulk component mutation received an unregistered component id");
            }
            if (componentInfo->size != component.componentSize) {
                throw std::invalid_argument("ECS bulk component payload size does not match registered component type");
            }
        }
        hasMissingComponent = hasMissingComponent || !HasComponent(entity, component.componentId);
        validatedComponentIds.push_back(component.componentId);
    }

    if (hasMissingComponent) {
        ValidateStructuralChangeAllowed("AddComponents");
    }

    AddNativeComponents(entity, components);
    if (backendAlive && config_.mirrorNativeComponentChangesToBackend) {
        for (const BulkComponentData& component : components) {
            WorldComponentMutator::Set(world_, entity, component.componentId, component.componentSize, component.data);
        }
    }

    InvalidateQueryPlansForArchetypeChange(previousArchetype, backendAlive ? EntityArchetype(entity) : nullptr);
}

void World::AddComponents(std::span<const Entity> entities, std::span<const BulkComponentData> components) {
    if (entities.empty() || components.empty()) {
        return;
    }

    bool hasMissingComponent = false;
    std::vector<ComponentId> validatedComponentIds;
    validatedComponentIds.reserve(components.size());
    for (const BulkComponentData& component : components) {
        if (component.componentId == 0 || component.componentSize == 0 || component.data == nullptr) {
            throw std::invalid_argument("ECS bulk component mutation received invalid component data");
        }
        if (std::find(validatedComponentIds.begin(), validatedComponentIds.end(), component.componentId) != validatedComponentIds.end()) {
            throw std::invalid_argument("ECS bulk component mutation received duplicate component data");
        }
        if (registries_ != nullptr) {
            const ComponentTypeInfo* componentInfo = registries_->Components().FindInfo(component.componentId);
            if (componentInfo == nullptr) {
                throw std::invalid_argument("ECS bulk component mutation received an unregistered component id");
            }
            if (componentInfo->size != component.componentSize) {
                throw std::invalid_argument("ECS bulk component payload size does not match registered component type");
            }
        }
        validatedComponentIds.push_back(component.componentId);
    }

    for (Entity entity : entities) {
        ValidateEntityHandle(entity, "AddComponents");
        for (ComponentId componentId : validatedComponentIds) {
            hasMissingComponent = hasMissingComponent || !HasComponent(entity, componentId);
        }
    }
    if (hasMissingComponent) {
        ValidateStructuralChangeAllowed("AddComponents");
    }

    AddNativeComponents(entities, components);
    for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
        const Entity entity = entities[entityIndex];
        if (!config_.mirrorNativeComponentChangesToBackend || !BackendEntityAlive(entity)) {
            continue;
        }
        for (const BulkComponentData& component : components) {
            WorldComponentMutator::Set(
                world_,
                entity,
                component.componentId,
                component.componentSize,
                BulkComponentRowData(component.data, component.componentSize, component.sourceCount, entityIndex, entities.size()));
        }
    }
    InvalidateQueryPlansForArchetypeChange(nullptr, nullptr);
}

void World::AddMissingComponentsTrusted(std::span<const Entity> entities, std::span<const BulkComponentData> components) {
    if (entities.empty() || components.empty()) {
        return;
    }
    ValidateStructuralChangeAllowed("AddMissingComponentsTrusted");

    std::vector<ComponentId> validatedComponentIds;
    validatedComponentIds.reserve(components.size());
    for (const BulkComponentData& component : components) {
        if (component.componentId == 0 || component.componentSize == 0 || component.data == nullptr) {
            throw std::invalid_argument("ECS trusted bulk component add received invalid component data");
        }
        if (std::find(validatedComponentIds.begin(), validatedComponentIds.end(), component.componentId) != validatedComponentIds.end()) {
            throw std::invalid_argument("ECS trusted bulk component add received duplicate component data");
        }
        if (registries_ != nullptr) {
            const ComponentTypeInfo* componentInfo = registries_->Components().FindInfo(component.componentId);
            if (componentInfo == nullptr) {
                throw std::invalid_argument("ECS trusted bulk component add received an unregistered component id");
            }
            if (componentInfo->size != component.componentSize) {
                throw std::invalid_argument("ECS trusted bulk component add payload size does not match registered component type");
            }
        }
        validatedComponentIds.push_back(component.componentId);
    }

    for (Entity entity : entities) {
        ValidateEntityHandle(entity, "AddMissingComponentsTrusted");
    }

    if (nativeStorage_ != nullptr) {
        const std::vector<NativeBulkComponentColumn> nativeComponents = MakeNativeBulkComponentColumns(components);
        nativeStorage_->AddComponents(entities, nativeComponents);
    }
    if (config_.mirrorNativeComponentChangesToBackend) {
        for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
            const Entity entity = entities[entityIndex];
            if (!BackendEntityAlive(entity)) {
                continue;
            }
            for (const BulkComponentData& component : components) {
                WorldComponentMutator::Set(
                    world_,
                    entity,
                    component.componentId,
                    component.componentSize,
                    BulkComponentRowData(component.data, component.componentSize, component.sourceCount, entityIndex, entities.size()));
            }
        }
    }
    InvalidateQueryPlansForArchetypeChange(nullptr, nullptr);
}

void World::RemoveComponents(Entity entity, std::span<const ComponentId> componentIds) {
    if (componentIds.empty()) {
        return;
    }
    ValidateEntityHandle(entity, "RemoveComponents");

    std::vector<ecs_id_t> requestedIds;
    requestedIds.reserve(componentIds.size());
    for (ComponentId componentId : componentIds) {
        if (componentId != 0) {
            requestedIds.push_back(componentId);
        }
    }
    requestedIds = SortedUniqueIds(std::move(requestedIds));
    if (requestedIds.empty()) {
        return;
    }

    bool hasExistingComponent = false;
    for (ComponentId componentId : requestedIds) {
        hasExistingComponent = hasExistingComponent || HasComponent(entity, componentId);
    }
    if (hasExistingComponent) {
        ValidateStructuralChangeAllowed("RemoveComponents");
    }

    const bool backendAlive = BackendEntityAlive(entity);
    ecs_table_t* previousArchetype = backendAlive ? EntityArchetype(entity) : nullptr;

    RemoveNativeComponents(entity, requestedIds);
    if (backendAlive && config_.mirrorNativeComponentChangesToBackend) {
        for (ComponentId componentId : requestedIds) {
            WorldComponentMutator::Remove(world_, entity, componentId);
        }
    }
    InvalidateQueryPlansForArchetypeChange(previousArchetype, backendAlive ? EntityArchetype(entity) : nullptr);
}

void World::RemoveComponents(std::span<const Entity> entities, std::span<const ComponentId> componentIds) {
    if (entities.empty() || componentIds.empty()) {
        return;
    }

    std::vector<ecs_id_t> requestedIds;
    requestedIds.reserve(componentIds.size());
    for (ComponentId componentId : componentIds) {
        if (componentId != 0) {
            requestedIds.push_back(componentId);
        }
    }
    requestedIds = SortedUniqueIds(std::move(requestedIds));
    if (requestedIds.empty()) {
        return;
    }

    bool hasExistingComponent = false;
    for (Entity entity : entities) {
        ValidateEntityHandle(entity, "RemoveComponents");
        for (ComponentId componentId : requestedIds) {
            hasExistingComponent = hasExistingComponent || HasComponent(entity, componentId);
        }
    }
    if (hasExistingComponent) {
        ValidateStructuralChangeAllowed("RemoveComponents");
    }

    RemoveNativeComponents(entities, requestedIds);
    for (Entity entity : entities) {
        if (!config_.mirrorNativeComponentChangesToBackend || !BackendEntityAlive(entity)) {
            continue;
        }
        for (ComponentId componentId : requestedIds) {
            WorldComponentMutator::Remove(world_, entity, componentId);
        }
    }
    InvalidateQueryPlansForArchetypeChange(nullptr, nullptr);
}

void World::RemoveExistingComponentsTrusted(std::span<const Entity> entities, std::span<const ComponentId> componentIds) {
    if (entities.empty() || componentIds.empty()) {
        return;
    }
    ValidateStructuralChangeAllowed("RemoveExistingComponentsTrusted");

    std::vector<ecs_id_t> requestedIds;
    requestedIds.reserve(componentIds.size());
    for (ComponentId componentId : componentIds) {
        if (componentId != 0) {
            requestedIds.push_back(componentId);
        }
    }
    requestedIds = SortedUniqueIds(std::move(requestedIds));
    if (requestedIds.empty()) {
        return;
    }

    for (Entity entity : entities) {
        ValidateEntityHandle(entity, "RemoveExistingComponentsTrusted");
    }

    if (nativeStorage_ != nullptr) {
        nativeStorage_->RemoveComponents(entities, requestedIds);
    }
    if (config_.mirrorNativeComponentChangesToBackend) {
        for (Entity entity : entities) {
            if (!BackendEntityAlive(entity)) {
                continue;
            }
            for (ComponentId componentId : requestedIds) {
                WorldComponentMutator::Remove(world_, entity, componentId);
            }
        }
    }
    InvalidateQueryPlansForArchetypeChange(nullptr, nullptr);
}

bool World::HasComponent(Entity entity, ComponentId componentId) const {
    ValidateEntityHandle(entity, "HasComponent");
    return nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity)
        ? nativeStorage_->HasComponent(entity, componentId)
        : BackendEntityAlive(entity) && WorldComponentReader::Has(world_, entity, componentId);
}

const void* World::TryGetComponent(Entity entity, ComponentId componentId) const {
    ValidateEntityHandle(entity, "TryGetComponent");
    if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity) && nativeStorage_->HasComponent(entity, componentId)) {
        return nativeStorage_->ComponentData(entity, componentId);
    }
    return BackendEntityAlive(entity) ? WorldComponentReader::TryGet(world_, entity, componentId) : nullptr;
}

void* World::TryGetMutableComponent(Entity entity, ComponentId componentId) {
    ValidateEntityHandle(entity, "TryGetMutableComponent");
    if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity) && nativeStorage_->HasComponent(entity, componentId)) {
        return nativeStorage_->MutableComponentData(entity, componentId);
    }
    return BackendEntityAlive(entity) ? WorldComponentReader::TryGetMutable(world_, entity, componentId) : nullptr;
}

void World::RemoveComponent(Entity entity, ComponentId componentId) {
    ValidateEntityHandle(entity, "RemoveComponent");
    if (HasComponent(entity, componentId)) {
        ValidateStructuralChangeAllowed("RemoveComponent");
    }
    const bool backendAlive = BackendEntityAlive(entity);
    ecs_table_t* previousArchetype = backendAlive ? EntityArchetype(entity) : nullptr;
    if (backendAlive) {
        WorldComponentMutator::Remove(world_, entity, componentId);
    }
    RemoveNativeComponents(entity, std::span<const ComponentId>{ &componentId, 1U });
    InvalidateQueryPlansForArchetypeChange(previousArchetype, backendAlive ? EntityArchetype(entity) : nullptr);
}

void World::MarkComponentModified(Entity entity, ComponentId componentId) {
    ValidateEntityHandle(entity, "MarkComponentModified");
    if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity) && nativeStorage_->HasComponent(entity, componentId)) {
        if (config_.mirrorNativeComponentChangesToBackend && BackendEntityAlive(entity) && WorldComponentReader::Has(world_, entity, componentId)) {
            const ComponentTypeInfo* componentInfo = registries_ == nullptr ? nullptr : registries_->Components().FindInfo(componentId);
            if (componentInfo != nullptr) {
                WorldComponentMutator::Set(world_, entity, componentId, componentInfo->size, nativeStorage_->ComponentData(entity, componentId));
            }
        }
        nativeStorage_->MarkComponentModified(entity, componentId);
        return;
    }
    if (BackendEntityAlive(entity)) {
        WorldComponentMutator::MarkModified(world_, entity, componentId);
    }
}

void World::ForEachComponent(ComponentId componentId, std::size_t componentSize, RawConstComponentVisitor visitor, void* context) const {
    if (visitor == nullptr) {
        return;
    }
    [[maybe_unused]] StructuralChangeValidator::Guard iterationGuard = EnterIteration();
    if (nativeStorage_ == nullptr) {
        WorldComponentIterator::ForEach(world_, componentId, componentSize, visitor, context);
        return;
    }

    std::vector<QueryTableDispatchRecord> records;
    records.reserve(nativeStorage_->ChunkCount());
    const std::array componentIds{ componentId };
    nativeStorage_->CollectQueryRecords(componentIds, {}, {}, records);
    for (const QueryTableDispatchRecord& record : records) {
        const auto* componentBytes = static_cast<const std::uint8_t*>(record.fieldComponents[0]);
        for (std::size_t index = 0; index < record.entityCount; ++index) {
            visitor(Entity{ record.entityIds[index] }, componentBytes + index * componentSize, context);
        }
    }
}

void World::ForEachMutableComponent(ComponentId componentId, std::size_t componentSize, RawMutableComponentVisitor visitor, void* context) {
    if (visitor == nullptr) {
        return;
    }
    ++telemetryCounters_.compatMutableIterations;
    [[maybe_unused]] StructuralChangeValidator::Guard iterationGuard = EnterIteration();
    if (nativeStorage_ == nullptr) {
        WorldComponentIterator::ForEachMutable(world_, componentId, componentSize, visitor, context);
        return;
    }

    std::vector<MutableQueryTableDispatchRecord> records;
    records.reserve(nativeStorage_->ChunkCount());
    const std::array componentIds{ componentId };
    nativeStorage_->CollectMutableQueryRecords(componentIds, {}, {}, records);
    for (const MutableQueryTableDispatchRecord& record : records) {
        telemetryCounters_.compatMutableEntitiesVisited += record.entityCount;
        auto* componentBytes = static_cast<std::uint8_t*>(record.fieldComponents[0]);
#if !defined(NDEBUG)
        const MutableComponentBorrowRange borrowRange{
            .componentId = componentId,
            .data = componentBytes,
            .bytes = componentSize * record.entityCount,
        };
        MutableComponentBorrowLocks::Guard borrowGuard = mutableComponentBorrowLocks_ != nullptr
            ? mutableComponentBorrowLocks_->Acquire(std::span<const MutableComponentBorrowRange>{ &borrowRange, 1U })
            : MutableComponentBorrowLocks::Guard{};
        static_cast<void>(borrowGuard);
#endif
        for (std::size_t index = 0; index < record.entityCount; ++index) {
            visitor(Entity{ record.entityIds[index] }, componentBytes + index * componentSize, context);
        }
        nativeStorage_->MarkArchetypeComponentsModified(record.nativeArchetypeIndex, componentIds);
    }
}

void World::ForEachComponents(
    ComponentId firstComponentId,
    std::size_t firstComponentSize,
    ComponentId secondComponentId,
    std::size_t secondComponentSize,
    void (*visitor)(Entity entity, const void* first, const void* second, void* context),
    void* context) const {
    if (visitor == nullptr) {
        return;
    }
    [[maybe_unused]] StructuralChangeValidator::Guard iterationGuard = EnterIteration();
    if (nativeStorage_ == nullptr) {
        WorldComponentIterator::ForEachPair(world_, firstComponentId, firstComponentSize, secondComponentId, secondComponentSize, visitor, context);
        return;
    }

    std::vector<QueryTableDispatchRecord> records;
    records.reserve(nativeStorage_->ChunkCount());
    const std::array componentIds{ firstComponentId, secondComponentId };
    nativeStorage_->CollectQueryRecords(componentIds, {}, {}, records);
    for (const QueryTableDispatchRecord& record : records) {
        const auto* firstBytes = static_cast<const std::uint8_t*>(record.fieldComponents[0]);
        const auto* secondBytes = static_cast<const std::uint8_t*>(record.fieldComponents[1]);
        for (std::size_t index = 0; index < record.entityCount; ++index) {
            visitor(
                Entity{ record.entityIds[index] },
                firstBytes + index * firstComponentSize,
                secondBytes + index * secondComponentSize,
                context);
        }
    }
}

} // namespace kb::ecs
