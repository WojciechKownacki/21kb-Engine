#include "engine/ecs/CommandBuffer.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace kb::ecs {
namespace {

struct PlaybackComponentSnapshot {
    Entity entity;
    ComponentId componentId = 0;
    bool existed = false;
    std::vector<std::byte> bytes;
};

struct PlaybackParentSnapshot {
    Entity child;
    Entity parent;
};

} // namespace

Entity CommandBufferPlaybackResult::Resolve(CommandEntity entity) const {
    if (!entity.IsDeferred()) {
        return entity.ExistingEntity();
    }
    if (entity.WorkerIndex() >= createdEntities_.size()) {
        throw std::out_of_range("ECS command buffer deferred entity worker index is invalid");
    }
    const std::vector<Entity>& laneEntities = createdEntities_[entity.WorkerIndex()];
    if (entity.LocalIndex() >= laneEntities.size()) {
        throw std::out_of_range("ECS command buffer deferred entity local index is invalid");
    }
    return laneEntities[entity.LocalIndex()];
}

std::size_t CommandBufferPlaybackResult::CreatedCount() const noexcept {
    return std::accumulate(createdEntities_.begin(), createdEntities_.end(), std::size_t{ 0 }, [](std::size_t total, const std::vector<Entity>& lane) {
        return total + lane.size();
    });
}

bool CommandBufferPlaybackResult::WasDestroyed(CommandEntity entity) const {
    return WasDestroyed(Resolve(entity));
}

bool CommandBufferPlaybackResult::WasDestroyed(Entity entity) const noexcept {
    return std::any_of(destroyedEntities_.begin(), destroyedEntities_.end(), [entity](Entity destroyed) {
        return destroyed == entity;
    });
}

std::size_t CommandBufferPlaybackResult::DestroyedCount() const noexcept {
    return destroyedEntities_.size();
}

CommandBuffer::WorkerBuffer::WorkerBuffer(CommandBuffer& owner, std::size_t workerIndex) noexcept
    : owner_(&owner)
    , workerIndex_(workerIndex) {}

CommandEntity CommandBuffer::WorkerBuffer::CreateEntity(std::string_view name) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }

    const CommandEntity entity = owner_->AllocateDeferredEntity(workerIndex_);
    Command command;
    command.kind = CommandKind::CreateEntity;
    command.first = entity;
    command.name = std::string{ name };
    owner_->Push(workerIndex_, std::move(command));
    return entity;
}

std::vector<CommandEntity> CommandBuffer::WorkerBuffer::CreateEntities(std::size_t count) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }

    std::vector<CommandEntity> entities = owner_->AllocateDeferredEntities(workerIndex_, count);
    if (count == 0) {
        return entities;
    }

    Command command;
    command.kind = CommandKind::CreateEntities;
    command.first = entities.front();
    command.count = count;
    owner_->Push(workerIndex_, std::move(command));
    return entities;
}

void CommandBuffer::WorkerBuffer::DestroyEntity(CommandEntity entity) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }

    Command command;
    command.kind = CommandKind::DestroyEntity;
    command.first = entity;
    owner_->Push(workerIndex_, std::move(command));
}

void CommandBuffer::WorkerBuffer::DestroyEntity(Entity entity) {
    DestroyEntity(CommandEntity::Existing(entity));
}

void CommandBuffer::WorkerBuffer::SetParent(CommandEntity child, CommandEntity parent) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }

    Command command;
    command.kind = CommandKind::SetParent;
    command.first = child;
    command.second = parent;
    owner_->Push(workerIndex_, std::move(command));
}

void CommandBuffer::WorkerBuffer::SetParent(Entity child, Entity parent) {
    SetParent(CommandEntity::Existing(child), CommandEntity::Existing(parent));
}

void CommandBuffer::WorkerBuffer::ClearParent(CommandEntity child) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }

    Command command;
    command.kind = CommandKind::ClearParent;
    command.first = child;
    owner_->Push(workerIndex_, std::move(command));
}

void CommandBuffer::WorkerBuffer::ClearParent(Entity child) {
    ClearParent(CommandEntity::Existing(child));
}

CommandBuffer::CommandBuffer(std::size_t workerCount)
    : lanes_(workerCount == 0 ? 1 : workerCount) {}

CommandBuffer::WorkerBuffer CommandBuffer::Worker(std::size_t workerIndex) {
    static_cast<void>(LaneFor(workerIndex));
    return WorkerBuffer{ *this, workerIndex };
}

std::size_t CommandBuffer::WorkerCount() const noexcept {
    return lanes_.size();
}

std::size_t CommandBuffer::CommandCount() const noexcept {
    return std::accumulate(lanes_.begin(), lanes_.end(), std::size_t{ 0 }, [](std::size_t total, const Lane& lane) {
        return total + lane.commands.size();
    });
}

bool CommandBuffer::Empty() const noexcept {
    return CommandCount() == 0;
}

CommandBufferPlaybackResult CommandBuffer::Playback(World& world) {
    CommandBufferPlaybackResult result;
    result.createdEntities_.resize(lanes_.size());

    std::vector<PlaybackComponentSnapshot> componentRollback;
    std::vector<PlaybackParentSnapshot> parentRollback;

    auto isPlaybackCreated = [&result](Entity entity) {
        if (!entity.IsValid()) {
            return false;
        }
        for (const std::vector<Entity>& laneEntities : result.createdEntities_) {
            if (std::find(laneEntities.begin(), laneEntities.end(), entity) != laneEntities.end()) {
                return true;
            }
        }
        return false;
    };

    auto snapshotComponent = [&world, &componentRollback, &isPlaybackCreated](Entity entity, ComponentId componentId) {
        if (componentId == 0 || !world.IsAlive(entity) || isPlaybackCreated(entity)) {
            return;
        }
        const auto alreadyCaptured = std::any_of(componentRollback.begin(), componentRollback.end(), [entity, componentId](const PlaybackComponentSnapshot& snapshot) {
            return snapshot.entity == entity && snapshot.componentId == componentId;
        });
        if (alreadyCaptured) {
            return;
        }

        PlaybackComponentSnapshot snapshot;
        snapshot.entity = entity;
        snapshot.componentId = componentId;
        const void* component = world.TryGetComponent(entity, componentId);
        snapshot.existed = component != nullptr;
        if (component != nullptr) {
            const ComponentTypeInfo* componentInfo = world.registries_ == nullptr ? nullptr : world.registries_->Components().FindInfo(componentId);
            if (componentInfo == nullptr || componentInfo->size == 0) {
                throw std::logic_error("ECS command buffer rollback cannot capture an unknown component type");
            }
            snapshot.bytes.resize(componentInfo->size);
            const auto* source = static_cast<const std::byte*>(component);
            std::copy(source, source + snapshot.bytes.size(), snapshot.bytes.begin());
        }
        componentRollback.push_back(std::move(snapshot));
    };

    auto snapshotParent = [&world, &parentRollback, &isPlaybackCreated](Entity child) {
        if (!world.IsAlive(child) || isPlaybackCreated(child)) {
            return;
        }
        const auto alreadyCaptured = std::any_of(parentRollback.begin(), parentRollback.end(), [child](const PlaybackParentSnapshot& snapshot) {
            return snapshot.child == child;
        });
        if (!alreadyCaptured) {
            parentRollback.push_back(PlaybackParentSnapshot{
                .child = child,
                .parent = world.Parent(child),
            });
        }
    };

    auto rollbackPlayback = [&world, &result, &componentRollback, &parentRollback]() {
        for (auto it = parentRollback.rbegin(); it != parentRollback.rend(); ++it) {
            if (!world.IsAlive(it->child)) {
                continue;
            }
            if (it->parent.IsValid() && world.IsAlive(it->parent)) {
                world.SetParent(it->child, it->parent);
            } else {
                world.ClearParent(it->child);
            }
        }
        for (auto it = componentRollback.rbegin(); it != componentRollback.rend(); ++it) {
            if (!world.IsAlive(it->entity)) {
                continue;
            }
            if (it->existed) {
                world.SetComponent(it->entity, it->componentId, it->bytes.size(), it->bytes.data());
            } else {
                world.RemoveComponent(it->entity, it->componentId);
            }
        }
        for (auto laneIt = result.createdEntities_.rbegin(); laneIt != result.createdEntities_.rend(); ++laneIt) {
            for (auto entityIt = laneIt->rbegin(); entityIt != laneIt->rend(); ++entityIt) {
                if (world.IsAlive(*entityIt)) {
                    world.DestroyEntity(*entityIt);
                }
            }
        }
    };

    std::unordered_set<Entity::IdType> destroyedIds;
    auto scheduleDestroy = [&result, &destroyedIds](Entity entity) {
        if (!entity.IsValid()) {
            return;
        }
        if (destroyedIds.insert(entity.Id()).second) {
            result.destroyedEntities_.push_back(entity);
        }
    };

    try {
        for (std::size_t workerIndex = 0; workerIndex < lanes_.size(); ++workerIndex) {
            const Lane& lane = lanes_[workerIndex];
            result.createdEntities_[workerIndex].resize(lane.nextDeferredEntity);
            for (const Command& command : lane.commands) {
                if (command.kind != CommandKind::CreateEntity) {
                    if (command.kind != CommandKind::CreateEntities) {
                        continue;
                    }

                    if (!command.first.IsDeferred() || command.first.WorkerIndex() != workerIndex || command.count == 0 ||
                        command.first.LocalIndex() + command.count > result.createdEntities_[workerIndex].size()) {
                        throw std::logic_error("ECS command buffer bulk create command has an invalid deferred entity range");
                    }

                    std::vector<World::BulkComponentData> components;
                    components.reserve(command.bulkComponents.size());
                    for (const BulkComponentCommand& component : command.bulkComponents) {
                        if (component.registerComponent == nullptr) {
                            throw std::logic_error("ECS command buffer bulk create component is missing a register function");
                        }
                        if (component.componentSize == 0 || component.bytes.size() != command.count * component.componentSize) {
                            throw std::logic_error("ECS command buffer bulk create component payload size mismatch");
                        }
                        components.push_back(World::BulkComponentData{
                            .componentId = component.registerComponent(world),
                            .componentSize = component.componentSize,
                            .data = component.bytes.data(),
                        });
                    }

                    const std::vector<Entity> entities = world.CreateEntitiesWithComponents(command.count, components);
                    if (entities.size() != command.count) {
                        throw std::runtime_error("ECS command buffer bulk create returned an unexpected entity count");
                    }
                    for (std::size_t index = 0; index < entities.size(); ++index) {
                        result.createdEntities_[workerIndex][command.first.LocalIndex() + index] = entities[index];
                    }
                    continue;
                }
                if (!command.first.IsDeferred() || command.first.WorkerIndex() != workerIndex || command.first.LocalIndex() >= result.createdEntities_[workerIndex].size()) {
                    throw std::logic_error("ECS command buffer create command has an invalid deferred entity");
                }
                result.createdEntities_[workerIndex][command.first.LocalIndex()] = world.CreateEntity(command.name);
            }
        }

        for (const Lane& lane : lanes_) {
            for (const Command& command : lane.commands) {
                switch (command.kind) {
                case CommandKind::CreateEntity:
                case CommandKind::CreateEntities:
                    break;
                case CommandKind::DestroyEntity:
                    scheduleDestroy(ResolveForPlayback(command.first, result));
                    break;
                case CommandKind::SetComponent: {
                    if (command.component.set == nullptr || command.component.registerComponent == nullptr) {
                        throw std::logic_error("ECS command buffer set component command is missing an apply function");
                    }
                    const Entity entity = ResolveForPlayback(command.first, result);
                    const ComponentId componentId = command.component.registerComponent(world);
                    snapshotComponent(entity, componentId);
                    command.component.set(world, command.first, command.component.bytes, result);
                    break;
                }
                case CommandKind::RemoveComponent: {
                    if (command.component.remove == nullptr || command.component.findComponent == nullptr) {
                        throw std::logic_error("ECS command buffer remove component command is missing an apply function");
                    }
                    const Entity entity = ResolveForPlayback(command.first, result);
                    const ComponentId componentId = command.component.findComponent(world);
                    snapshotComponent(entity, componentId);
                    command.component.remove(world, command.first, result);
                    break;
                }
                case CommandKind::SetComponents: {
                    if (command.entities.size() != command.count) {
                        throw std::logic_error("ECS command buffer bulk component set command has an invalid entity count");
                    }

                    std::vector<ComponentId> componentIds;
                    componentIds.reserve(command.bulkComponents.size());
                    for (const BulkComponentCommand& component : command.bulkComponents) {
                        if (component.registerComponent == nullptr) {
                            throw std::logic_error("ECS command buffer bulk component set is missing a register function");
                        }
                        if (component.componentSize == 0 || component.bytes.size() != command.count * component.componentSize) {
                            throw std::logic_error("ECS command buffer bulk component set payload size mismatch");
                        }
                        componentIds.push_back(component.registerComponent(world));
                    }

                    std::vector<World::BulkComponentData> components;
                    components.resize(command.bulkComponents.size());
                    for (std::size_t entityIndex = 0; entityIndex < command.entities.size(); ++entityIndex) {
                        const Entity entity = ResolveForPlayback(command.entities[entityIndex], result);
                        for (ComponentId componentId : componentIds) {
                            snapshotComponent(entity, componentId);
                        }
                        for (std::size_t componentIndex = 0; componentIndex < command.bulkComponents.size(); ++componentIndex) {
                            const BulkComponentCommand& component = command.bulkComponents[componentIndex];
                            components[componentIndex] = World::BulkComponentData{
                                .componentId = componentIds[componentIndex],
                                .componentSize = component.componentSize,
                                .data = component.bytes.data() + (entityIndex * component.componentSize),
                            };
                        }
                        world.AddComponents(entity, components);
                    }
                    break;
                }
                case CommandKind::RemoveComponents: {
                    if (command.entities.size() != command.count) {
                        throw std::logic_error("ECS command buffer bulk component remove command has an invalid entity count");
                    }

                    std::vector<ComponentId> componentIds;
                    componentIds.reserve(command.bulkRemoveComponents.size());
                    for (const BulkRemoveComponentCommand& component : command.bulkRemoveComponents) {
                        if (component.findComponent == nullptr) {
                            throw std::logic_error("ECS command buffer bulk component remove is missing a component lookup function");
                        }
                        const ComponentId componentId = component.findComponent(world);
                        if (componentId != 0) {
                            componentIds.push_back(componentId);
                        }
                    }
                    for (CommandEntity commandEntity : command.entities) {
                        const Entity entity = ResolveForPlayback(commandEntity, result);
                        for (ComponentId componentId : componentIds) {
                            snapshotComponent(entity, componentId);
                        }
                        world.RemoveComponents(entity, componentIds);
                    }
                    break;
                }
                case CommandKind::SetParent: {
                    const Entity child = ResolveForPlayback(command.first, result);
                    const Entity parent = ResolveForPlayback(command.second, result);
                    snapshotParent(child);
                    world.SetParent(child, parent);
                    break;
                }
                case CommandKind::ClearParent: {
                    const Entity child = ResolveForPlayback(command.first, result);
                    snapshotParent(child);
                    world.ClearParent(child);
                    break;
                }
                }
            }
        }

        for (Entity entity : result.destroyedEntities_) {
            world.DestroyEntity(entity);
        }

        Clear();
        return result;
    } catch (...) {
        const std::exception_ptr original = std::current_exception();
        try {
            rollbackPlayback();
        } catch (...) {
            std::throw_with_nested(std::runtime_error("ECS command buffer rollback failed"));
        }
        std::rethrow_exception(original);
    }
}

void CommandBuffer::Clear() noexcept {
    for (Lane& lane : lanes_) {
        lane.commands.clear();
        lane.nextDeferredEntity = 0;
    }
}

CommandBuffer::Lane& CommandBuffer::LaneFor(std::size_t workerIndex) {
    if (workerIndex >= lanes_.size()) {
        throw std::out_of_range("ECS command buffer worker index is outside the configured worker lanes");
    }
    return lanes_[workerIndex];
}

void CommandBuffer::Push(std::size_t workerIndex, Command command) {
    LaneFor(workerIndex).commands.push_back(std::move(command));
}

CommandEntity CommandBuffer::AllocateDeferredEntity(std::size_t workerIndex) {
    Lane& lane = LaneFor(workerIndex);
    const std::size_t localIndex = lane.nextDeferredEntity;
    ++lane.nextDeferredEntity;
    return CommandEntity::Deferred(workerIndex, localIndex);
}

std::vector<CommandEntity> CommandBuffer::AllocateDeferredEntities(std::size_t workerIndex, std::size_t count) {
    Lane& lane = LaneFor(workerIndex);
    const std::size_t firstLocalIndex = lane.nextDeferredEntity;
    if (count > std::numeric_limits<std::size_t>::max() - firstLocalIndex) {
        throw std::length_error("ECS command buffer deferred entity range exceeds addressable size");
    }

    lane.nextDeferredEntity += count;

    std::vector<CommandEntity> entities;
    entities.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        entities.push_back(CommandEntity::Deferred(workerIndex, firstLocalIndex + index));
    }
    return entities;
}

Entity CommandBuffer::ResolveForPlayback(CommandEntity entity, const CommandBufferPlaybackResult& result) {
    return result.Resolve(entity);
}

} // namespace kb::ecs
