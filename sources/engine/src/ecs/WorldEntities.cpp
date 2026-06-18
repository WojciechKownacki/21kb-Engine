#include "engine/ecs/World.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/FlecsEntityIds.hpp"
#include "ecs/world/WorldEntityCatalog.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <flecs.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace kb::ecs {

void World::BulkInitFlecsEntities(std::span<const Entity> entities, std::span<const BulkComponentData> components) {
    if (world_ == nullptr || entities.empty()) {
        return;
    }
    if (entities.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
        throw std::runtime_error("ECS bulk create entity count exceeds Flecs limits");
    }
    if (components.size() >= FLECS_ID_DESC_MAX) {
        throw std::runtime_error("ECS bulk create component count exceeds Flecs bulk descriptor limits");
    }

    std::vector<ecs_entity_t> entityIds;
    entityIds.reserve(entities.size());
    for (Entity entity : entities) {
        entityIds.push_back(FlecsEntityId(entity));
    }

    std::array<void*, FLECS_ID_DESC_MAX> componentData{};
    std::vector<std::vector<std::byte>> expandedComponentData;
    expandedComponentData.reserve(components.size());
    ecs_bulk_desc_t descriptor{};
    descriptor.entities = entityIds.data();
    descriptor.count = static_cast<int32_t>(entityIds.size());
    descriptor.data = componentData.data();
    for (std::size_t index = 0; index < components.size(); ++index) {
        const BulkComponentData& component = components[index];
        descriptor.ids[index] = component.componentId;

        const std::size_t sourceCount = component.sourceCount == 0U ? entities.size() : component.sourceCount;
        if (sourceCount == entities.size()) {
            componentData[index] = const_cast<void*>(component.data);
            continue;
        }

        std::vector<std::byte>& expanded = expandedComponentData.emplace_back();
        expanded.resize(entities.size() * component.componentSize);
        const auto* source = static_cast<const std::byte*>(component.data);
        for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
            const std::size_t sourceIndex = entityIndex % sourceCount;
            const std::byte* row = source + (sourceIndex * component.componentSize);
            std::copy(row, row + component.componentSize, expanded.data() + (entityIndex * component.componentSize));
        }
        componentData[index] = expanded.data();
    }

    if (ecs_bulk_init(world_, &descriptor) == nullptr) {
        throw std::runtime_error("ECS bulk create failed to populate Flecs entities");
    }
}

Entity World::CreateEntity() {
    ValidateStructuralChangeAllowed("CreateEntity");
    if (world_ == nullptr || nativeStorage_ == nullptr) {
        throw std::runtime_error("ECS world is not initialized");
    }
    const Entity entity = nativeStorage_->CreateEntity();
    try {
        ecs_make_alive(world_, FlecsEntityId(entity));
    } catch (...) {
        if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity)) {
            nativeStorage_->DestroyEntity(entity);
        }
        if (ecs_is_alive(world_, FlecsEntityId(entity))) {
            ecs_delete(world_, FlecsEntityId(entity));
        }
        throw;
    }
    if (registries_ != nullptr) {
        registries_->Entities().Add(entity);
    }
    RecordStructuralChange();
    return entity;
}

Entity World::CreateEntity(std::string_view name) {
    Entity entity = CreateEntity();
    if (!name.empty()) {
        const std::string ownedName{ name };
        ecs_set_name(world_, FlecsEntityId(entity), ownedName.c_str());
    }
    return entity;
}

std::vector<Entity> World::CreateEntities(std::size_t count, std::span<const BulkComponentView> components) {
    std::vector<BulkComponentData> componentData;
    componentData.reserve(components.size());
    for (const BulkComponentView& component : components) {
        if (component.registerComponent == nullptr || component.componentSize == 0) {
            throw std::invalid_argument("ECS bulk create component view is incomplete");
        }
        const std::size_t sourceCount = component.sourceCount == 0U ? component.componentCount : component.sourceCount;
        const std::size_t componentCount = component.componentCount == 0U && sourceCount == 1U ? count : component.componentCount;
        if (componentCount != count || sourceCount == 0U || sourceCount > count || (count % sourceCount) != 0U) {
            throw std::invalid_argument("ECS bulk create component counts must match entity count");
        }
        if (count != 0 && component.data == nullptr) {
            throw std::invalid_argument("ECS bulk create component payload is null");
        }
        if (component.componentSize != 0 && sourceCount > std::numeric_limits<std::size_t>::max() / component.componentSize) {
            throw std::length_error("ECS bulk create component payload exceeds addressable size");
        }
        componentData.push_back(BulkComponentData{
            .componentId = component.registerComponent(*this),
            .componentSize = component.componentSize,
            .componentCount = componentCount,
            .sourceCount = sourceCount,
            .data = component.data,
        });
    }
    return CreateEntitiesWithComponents(count, std::span<const BulkComponentData>{ componentData }, true);
}

std::vector<Entity> World::CreateEntitiesNativeOnly(std::size_t count, std::span<const BulkComponentView> components) {
    std::vector<BulkComponentData> componentData;
    componentData.reserve(components.size());
    for (const BulkComponentView& component : components) {
        if (component.registerComponent == nullptr || component.componentSize == 0) {
            throw std::invalid_argument("ECS bulk create component view is incomplete");
        }
        const std::size_t sourceCount = component.sourceCount == 0U ? component.componentCount : component.sourceCount;
        const std::size_t componentCount = component.componentCount == 0U && sourceCount == 1U ? count : component.componentCount;
        if (componentCount != count || sourceCount == 0U || sourceCount > count || (count % sourceCount) != 0U) {
            throw std::invalid_argument("ECS bulk create component counts must match entity count");
        }
        if (count != 0 && component.data == nullptr) {
            throw std::invalid_argument("ECS bulk create component payload is null");
        }
        if (component.componentSize != 0 && sourceCount > std::numeric_limits<std::size_t>::max() / component.componentSize) {
            throw std::length_error("ECS bulk create component payload exceeds addressable size");
        }
        componentData.push_back(BulkComponentData{
            .componentId = component.registerComponent(*this),
            .componentSize = component.componentSize,
            .componentCount = componentCount,
            .sourceCount = sourceCount,
            .data = component.data,
        });
    }
    return CreateEntitiesWithComponents(count, std::span<const BulkComponentData>{ componentData }, false);
}

std::vector<Entity> World::CreateEntitiesWithComponents(std::size_t count, std::span<const BulkComponentData> components, bool mirrorBackend) {
    if (count == 0) {
        return {};
    }
    ValidateStructuralChangeAllowed("CreateEntitiesWithComponents");
    if (world_ == nullptr || nativeStorage_ == nullptr) {
        throw std::runtime_error("ECS world is not initialized");
    }

    std::vector<ComponentId> componentIds;
    componentIds.reserve(components.size());
    for (const BulkComponentData& component : components) {
        if (component.componentId == 0 || component.componentSize == 0 || component.data == nullptr) {
            throw std::invalid_argument("ECS bulk create received invalid component data");
        }
        const std::size_t sourceCount = component.sourceCount == 0U ? count : component.sourceCount;
        if (component.componentCount != 0U && component.componentCount != count) {
            throw std::invalid_argument("ECS bulk create component count does not match entity count");
        }
        if (sourceCount == 0U || sourceCount > count || (count % sourceCount) != 0U) {
            throw std::invalid_argument("ECS bulk create component source count must divide entity count");
        }
        if (std::find(componentIds.begin(), componentIds.end(), component.componentId) != componentIds.end()) {
            throw std::invalid_argument("ECS bulk create received duplicate component data");
        }
        if (registries_ != nullptr) {
            const ComponentTypeInfo* componentInfo = registries_->Components().FindInfo(component.componentId);
            if (componentInfo == nullptr) {
                throw std::invalid_argument("ECS bulk create received an unregistered component id");
            }
            if (componentInfo->size != component.componentSize) {
                throw std::invalid_argument("ECS bulk create component payload size does not match registered component type");
            }
        }
        componentIds.push_back(component.componentId);
    }

    std::vector<Entity> entities;
    try {
        const std::vector<NativeBulkComponentColumn> nativeComponents = MakeNativeBulkComponentColumns(components);
        entities = nativeStorage_->CreateEntities(count, nativeComponents);
        if (mirrorBackend) {
            BulkInitFlecsEntities(entities, components);
        }
        if (registries_ != nullptr) {
            registries_->Entities().AddMany(entities);
        }
    } catch (...) {
        if (registries_ != nullptr) {
            registries_->Entities().RemoveMany(entities);
        }
        for (Entity entity : entities) {
            if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity)) {
                nativeStorage_->DestroyEntity(entity);
            }
            if (world_ != nullptr && ecs_is_alive(world_, FlecsEntityId(entity))) {
                ecs_delete(world_, FlecsEntityId(entity));
            }
        }
        throw;
    }

    InvalidateQueryPlansForArchetypeChange(nullptr, EntityArchetype(entities.front()));
    return entities;
}

void World::AdoptEntitiesWithComponents(std::span<const Entity::IdType> entityIds, std::span<const BulkComponentData> components) {
    if (entityIds.empty()) {
        return;
    }
    ValidateStructuralChangeAllowed("AdoptEntitiesWithComponents");
    if (world_ == nullptr || nativeStorage_ == nullptr) {
        throw std::runtime_error("ECS world is not initialized");
    }

    std::vector<Entity::IdType> sortedEntityIds(entityIds.begin(), entityIds.end());
    std::sort(sortedEntityIds.begin(), sortedEntityIds.end());
    for (std::size_t index = 0; index < sortedEntityIds.size(); ++index) {
        const Entity::IdType entityId = sortedEntityIds[index];
        if (entityId == 0 || (index > 0U && sortedEntityIds[index - 1U] == entityId) || IsAlive(Entity{ entityId })) {
            throw std::invalid_argument("ECS chunk restore received an invalid or duplicate entity id");
        }
    }

    std::vector<ComponentId> componentIds;
    componentIds.reserve(components.size());
    for (const BulkComponentData& component : components) {
        if (component.componentId == 0 || component.componentSize == 0 || component.data == nullptr) {
            throw std::invalid_argument("ECS chunk restore received invalid component data");
        }
        if (std::find(componentIds.begin(), componentIds.end(), component.componentId) != componentIds.end()) {
            throw std::invalid_argument("ECS chunk restore received duplicate component data");
        }
        if (registries_ != nullptr) {
            const ComponentTypeInfo* componentInfo = registries_->Components().FindInfo(component.componentId);
            if (componentInfo == nullptr) {
                throw std::invalid_argument("ECS chunk restore received an unregistered component id");
            }
            if (componentInfo->size != component.componentSize) {
                throw std::invalid_argument("ECS chunk restore component payload size does not match registered component type");
            }
        }
        componentIds.push_back(component.componentId);
    }

    std::vector<Entity> adoptedEntities;
    adoptedEntities.reserve(entityIds.size());
    for (Entity::IdType entityId : entityIds) {
        adoptedEntities.push_back(Entity{ entityId });
    }

    try {
        const std::vector<NativeBulkComponentColumn> nativeComponents = MakeNativeBulkComponentColumns(components);
        nativeStorage_->AdoptEntities(adoptedEntities, nativeComponents);
        BulkInitFlecsEntities(adoptedEntities, components);
        if (registries_ != nullptr) {
            registries_->Entities().AddMany(adoptedEntities);
        }
    } catch (...) {
        if (registries_ != nullptr) {
            registries_->Entities().RemoveMany(adoptedEntities);
        }
        for (Entity entity : adoptedEntities) {
            if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity)) {
                nativeStorage_->DestroyEntity(entity);
            }
            if (world_ != nullptr && ecs_is_alive(world_, FlecsEntityId(entity))) {
                ecs_delete(world_, FlecsEntityId(entity));
            }
        }
        throw;
    }

    InvalidateQueryPlansForArchetypeChange(nullptr, EntityArchetype(Entity{ entityIds.front() }));
}

void World::DestroyEntity(Entity entity) {
    ValidateEntityHandle(entity, "DestroyEntity");
    if (IsAlive(entity)) {
        ValidateStructuralChangeAllowed("DestroyEntity");
    }
    ecs_table_t* previousArchetype = EntityArchetype(entity);
    DestroyNativeEntity(entity);
    if (world_ != nullptr && entity.IsValid() && ecs_is_valid(world_, FlecsEntityId(entity))) {
        ecs_delete(world_, FlecsEntityId(entity));
    }
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(entity));
    if (registries_ != nullptr) {
        registries_->Entities().Remove(entity);
    }
}

void World::SetName(Entity entity, std::string_view name) {
    ValidateEntityHandle(entity, "SetName");
    if (!BackendEntityAlive(entity)) {
        return;
    }

    const std::string ownedName{ name };
    ecs_set_name(world_, FlecsEntityId(entity), ownedName.empty() ? nullptr : ownedName.c_str());
}

bool World::IsAlive(Entity entity) const noexcept {
    return nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity);
}

Entity World::ResolveAliveEntity(Entity::IdType entityIdWithoutGeneration) const noexcept {
    return nativeStorage_ != nullptr ? nativeStorage_->ResolveAliveEntity(entityIdWithoutGeneration) : Entity{};
}

std::string World::Name(Entity entity) const {
    ValidateEntityHandle(entity, "Name");

    if (world_ == nullptr || !ecs_is_alive(world_, FlecsEntityId(entity))) {
        return {};
    }
    if (const char* name = ecs_get_name(world_, FlecsEntityId(entity)); name != nullptr) {
        return std::string{ name };
    }

    return {};
}

} // namespace kb::ecs
