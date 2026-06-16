#include "engine/ecs/World.hpp"

#include "ecs/QueryStateFactory.hpp"

#include <flecs.h>

#include <span>
#include <stdexcept>
#include <string>

namespace kb::ecs {

QueryState* World::CreateQueryState(
    const ComponentId* componentIds,
    const std::size_t* componentSizes,
    std::size_t componentCount,
    std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> optionalComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::span<const ComponentId> changedComponentIds) const {
    if (world_ == nullptr || nativeStorage_ == nullptr || componentIds == nullptr || componentSizes == nullptr || componentCount == 0) {
        return nullptr;
    }

    return QueryStateFactory::Create(
        nativeStorage_.get(),
        std::span<const ComponentId>{ componentIds, componentCount },
        std::span<const std::size_t>{ componentSizes, componentCount },
        requiredComponentIds,
        optionalComponentIds,
        excludedComponentIds,
        changedComponentIds,
        config_,
        mutableComponentBorrowLocks_.get(),
        structuralChangeValidator_.get());
}

ecs_table_t* World::EntityArchetype(Entity entity) const noexcept {
    if (world_ == nullptr || !entity.IsValid() || !ecs_is_alive(world_, entity.Id())) {
        return nullptr;
    }
    return ecs_get_table(world_, entity.Id());
}

void World::InvalidateQueryPlansForArchetypeChange(ecs_table_t* previousArchetype, ecs_table_t* currentArchetype) noexcept {
    static_cast<void>(previousArchetype);
    static_cast<void>(currentArchetype);
}

void World::ValidateStructuralChangeAllowed(std::string_view operation) const {
    if (structuralChangeValidator_ != nullptr) {
        structuralChangeValidator_->ValidateStructuralChange(operation);
    }
}

void World::ValidateEntityHandle(Entity entity, std::string_view operation) const {
    if (!entity.IsValid()) {
        throw std::invalid_argument("ECS " + std::string{ operation } + " received an invalid entity handle");
    }
    if (!IsAlive(entity)) {
        throw std::out_of_range("ECS " + std::string{ operation } + " received a stale entity handle");
    }
}

void World::ValidateOptionalEntityHandle(Entity entity, std::string_view operation) const {
    if (entity.IsValid()) {
        ValidateEntityHandle(entity, operation);
    }
}

StructuralChangeValidator::Guard World::EnterIteration() const noexcept {
    return structuralChangeValidator_ != nullptr ? structuralChangeValidator_->EnterIteration() : StructuralChangeValidator::Guard{};
}

} // namespace kb::ecs
