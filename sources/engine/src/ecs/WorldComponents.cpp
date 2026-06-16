#include "engine/ecs/World.hpp"

#include "engine/ecs/MutableComponentBorrowLocks.hpp"
#include "ecs/ComponentRegistry.hpp"
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

} // namespace

ComponentId World::RegisterComponent(std::type_index type, std::string_view name, std::size_t size, std::size_t alignment) {
    return WorldComponentRegistrar::Register(world_, registries_.get(), type, name, size, alignment);
}

ComponentId World::FindComponent(std::type_index type) const noexcept {
    return WorldComponentRegistrar::Find(registries_.get(), type);
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
            .stride = component.componentSize,
        });
    }
    return nativeComponents;
}

void World::AdoptNativeEntity(Entity entity, std::span<const BulkComponentData> components) {
    if (nativeStorage_ == nullptr) {
        return;
    }
    const std::vector<NativeComponentValue> nativeComponents = MakeNativeComponentValues(components);
    nativeStorage_->AdoptEntity(entity, nativeComponents);
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

void World::SetComponent(Entity entity, ComponentId componentId, std::size_t size, const void* component) {
    ValidateEntityHandle(entity, "SetComponent");
    if (IsAlive(entity) && !HasComponent(entity, componentId)) {
        ValidateStructuralChangeAllowed("SetComponent");
    }
    ecs_table_t* previousArchetype = EntityArchetype(entity);
    SetNativeComponent(entity, BulkComponentData{
        .componentId = componentId,
        .componentSize = size,
        .data = component,
    });
    WorldComponentMutator::Set(world_, entity, componentId, size, component);
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(entity));
}

void World::AddComponents(Entity entity, std::span<const BulkComponentData> components) {
    if (components.empty()) {
        return;
    }
    ValidateEntityHandle(entity, "AddComponents");

    ecs_table_t* previousArchetype = EntityArchetype(entity);
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
    for (const BulkComponentData& component : components) {
        WorldComponentMutator::Set(world_, entity, component.componentId, component.componentSize, component.data);
    }

    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(entity));
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

    ecs_table_t* previousArchetype = EntityArchetype(entity);

    RemoveNativeComponents(entity, requestedIds);
    for (ComponentId componentId : requestedIds) {
        WorldComponentMutator::Remove(world_, entity, componentId);
    }
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(entity));
}

bool World::HasComponent(Entity entity, ComponentId componentId) const {
    ValidateEntityHandle(entity, "HasComponent");
    return nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity)
        ? nativeStorage_->HasComponent(entity, componentId)
        : WorldComponentReader::Has(world_, entity, componentId);
}

const void* World::TryGetComponent(Entity entity, ComponentId componentId) const {
    ValidateEntityHandle(entity, "TryGetComponent");
    if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity) && nativeStorage_->HasComponent(entity, componentId)) {
        return nativeStorage_->ComponentData(entity, componentId);
    }
    return WorldComponentReader::TryGet(world_, entity, componentId);
}

void* World::TryGetMutableComponent(Entity entity, ComponentId componentId) {
    ValidateEntityHandle(entity, "TryGetMutableComponent");
    if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity) && nativeStorage_->HasComponent(entity, componentId)) {
        return nativeStorage_->MutableComponentData(entity, componentId);
    }
    return WorldComponentReader::TryGetMutable(world_, entity, componentId);
}

void World::RemoveComponent(Entity entity, ComponentId componentId) {
    ValidateEntityHandle(entity, "RemoveComponent");
    if (HasComponent(entity, componentId)) {
        ValidateStructuralChangeAllowed("RemoveComponent");
    }
    ecs_table_t* previousArchetype = EntityArchetype(entity);
    WorldComponentMutator::Remove(world_, entity, componentId);
    RemoveNativeComponents(entity, std::span<const ComponentId>{ &componentId, 1U });
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(entity));
}

void World::MarkComponentModified(Entity entity, ComponentId componentId) {
    ValidateEntityHandle(entity, "MarkComponentModified");
    WorldComponentMutator::MarkModified(world_, entity, componentId);
    if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity) && nativeStorage_->HasComponent(entity, componentId)) {
        nativeStorage_->MarkComponentModified(entity, componentId);
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
    [[maybe_unused]] StructuralChangeValidator::Guard iterationGuard = EnterIteration();
    if (nativeStorage_ == nullptr) {
        WorldComponentIterator::ForEachMutable(world_, componentId, componentSize, visitor, context);
        return;
    }

    std::vector<MutableQueryTableDispatchRecord> records;
    const std::array componentIds{ componentId };
    nativeStorage_->CollectMutableQueryRecords(componentIds, {}, {}, records);
    for (const MutableQueryTableDispatchRecord& record : records) {
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
