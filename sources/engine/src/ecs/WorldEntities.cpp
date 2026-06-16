#include "engine/ecs/World.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/world/WorldComponentMutator.hpp"
#include "ecs/world/WorldEntityCatalog.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <flecs.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace kb::ecs {

Entity World::CreateEntity() {
    ValidateStructuralChangeAllowed("CreateEntity");
    if (world_ == nullptr || nativeStorage_ == nullptr) {
        throw std::runtime_error("ECS world is not initialized");
    }
    const Entity entity{ nextEntityId_++ };
    try {
        AdoptNativeEntity(entity, {});
        ecs_make_alive(world_, entity.Id());
    } catch (...) {
        if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity)) {
            nativeStorage_->DestroyEntity(entity);
        }
        if (ecs_is_alive(world_, entity.Id())) {
            ecs_delete(world_, entity.Id());
        }
        throw;
    }
    if (registries_ != nullptr) {
        registries_->Entities().Add(entity);
    }
    return entity;
}

Entity World::CreateEntity(std::string_view name) {
    Entity entity = CreateEntity();
    if (!name.empty()) {
        const std::string ownedName{ name };
        ecs_set_name(world_, entity.Id(), ownedName.c_str());
    }
    return entity;
}

std::vector<Entity> World::CreateEntitiesWithComponents(std::size_t count, std::span<const BulkComponentData> components) {
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
    entities.reserve(count);
    try {
        std::vector<NativeComponentValue> nativeComponents;
        nativeComponents.reserve(components.size());
        for (std::size_t entityIndex = 0; entityIndex < count; ++entityIndex) {
            nativeComponents.clear();
            for (const BulkComponentData& component : components) {
                const auto* bytes = static_cast<const std::uint8_t*>(component.data);
                BulkComponentData entityComponent{
                    .componentId = component.componentId,
                    .componentSize = component.componentSize,
                    .data = bytes + entityIndex * component.componentSize,
                };
                nativeComponents.push_back(MakeNativeComponentValue(entityComponent));
            }
            const Entity entity{ nextEntityId_++ };
            nativeStorage_->AdoptEntity(entity, nativeComponents);
            entities.push_back(entity);
            if (world_ != nullptr) {
                ecs_make_alive(world_, entity.Id());
                for (const BulkComponentData& component : components) {
                    const auto* bytes = static_cast<const std::uint8_t*>(component.data);
                    WorldComponentMutator::Set(world_, entity, component.componentId, component.componentSize, bytes + entityIndex * component.componentSize);
                }
            }
            if (registries_ != nullptr) {
                registries_->Entities().Add(entity);
            }
        }
    } catch (...) {
        for (Entity entity : entities) {
            if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity)) {
                nativeStorage_->DestroyEntity(entity);
            }
            if (world_ != nullptr && ecs_is_alive(world_, entity.Id())) {
                ecs_delete(world_, entity.Id());
            }
            if (registries_ != nullptr) {
                registries_->Entities().Remove(entity);
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
    try {
        std::vector<NativeComponentValue> nativeComponents;
        nativeComponents.reserve(components.size());
        for (std::size_t entityIndex = 0; entityIndex < entityIds.size(); ++entityIndex) {
            nativeComponents.clear();
            for (const BulkComponentData& component : components) {
                const auto* bytes = static_cast<const std::uint8_t*>(component.data);
                const BulkComponentData entityComponent{
                    .componentId = component.componentId,
                    .componentSize = component.componentSize,
                    .data = bytes + entityIndex * component.componentSize,
                };
                nativeComponents.push_back(MakeNativeComponentValue(entityComponent));
            }

            const Entity entity{ entityIds[entityIndex] };
            nativeStorage_->AdoptEntity(entity, nativeComponents);
            adoptedEntities.push_back(entity);
            ecs_make_alive(world_, entity.Id());
            for (const BulkComponentData& component : components) {
                const auto* bytes = static_cast<const std::uint8_t*>(component.data);
                WorldComponentMutator::Set(world_, entity, component.componentId, component.componentSize, bytes + entityIndex * component.componentSize);
            }
            if (registries_ != nullptr) {
                registries_->Entities().Add(entity);
            }
            if (entity.Id() >= nextEntityId_) {
                if (entity.Id() == std::numeric_limits<Entity::IdType>::max()) {
                    throw std::runtime_error("ECS entity id capacity exceeded");
                }
                nextEntityId_ = entity.Id() + 1U;
            }
        }
    } catch (...) {
        for (Entity entity : adoptedEntities) {
            if (nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity)) {
                nativeStorage_->DestroyEntity(entity);
            }
            if (world_ != nullptr && ecs_is_alive(world_, entity.Id())) {
                ecs_delete(world_, entity.Id());
            }
            if (registries_ != nullptr) {
                registries_->Entities().Remove(entity);
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
    if (world_ != nullptr && entity.IsValid() && ecs_is_valid(world_, entity.Id())) {
        ecs_delete(world_, entity.Id());
    }
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(entity));
    if (registries_ != nullptr) {
        registries_->Entities().Remove(entity);
    }
}

void World::SetName(Entity entity, std::string_view name) {
    ValidateEntityHandle(entity, "SetName");

    const std::string ownedName{ name };
    ecs_set_name(world_, entity.Id(), ownedName.empty() ? nullptr : ownedName.c_str());
}

bool World::IsAlive(Entity entity) const noexcept {
    return nativeStorage_ != nullptr && nativeStorage_->IsAlive(entity);
}

std::string World::Name(Entity entity) const {
    ValidateEntityHandle(entity, "Name");

    if (const char* name = ecs_get_name(world_, entity.Id()); name != nullptr) {
        return std::string{ name };
    }

    return {};
}

} // namespace kb::ecs
