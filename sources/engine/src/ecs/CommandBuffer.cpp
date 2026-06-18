#include "engine/ecs/CommandBuffer.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kb::ecs {
namespace {

using CommandBufferStatsClock = std::chrono::steady_clock;

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

[[nodiscard]] bool ExceedsPlaybackBudget(const CommandBufferPlaybackResult::Stats& stats, const CommandBufferPlaybackBudget& budget) noexcept {
    return stats.structuralCommands > budget.maxStructuralCommands || stats.createCommands > budget.maxCreateCommands ||
        stats.bulkCreateCommands > budget.maxBulkCreateCommands || stats.destroyCommands > budget.maxDestroyCommands ||
        stats.componentSetCommands > budget.maxComponentSetCommands || stats.componentRemoveCommands > budget.maxComponentRemoveCommands ||
        stats.parentCommands > budget.maxParentCommands || stats.componentBytesCopied > budget.maxComponentBytesCopied;
}

void AddPlaybackStats(CommandBufferPlaybackResult::Stats& target, const CommandBufferPlaybackResult::Stats& source) noexcept {
    target.structuralCommands += source.structuralCommands;
    target.createCommands += source.createCommands;
    target.bulkCreateCommands += source.bulkCreateCommands;
    target.destroyCommands += source.destroyCommands;
    target.componentSetCommands += source.componentSetCommands;
    target.componentRemoveCommands += source.componentRemoveCommands;
    target.parentCommands += source.parentCommands;
    target.componentBytesCopied += source.componentBytesCopied;
    target.createPhaseNanoseconds += source.createPhaseNanoseconds;
    target.applyPhaseNanoseconds += source.applyPhaseNanoseconds;
    target.parentApplyNanoseconds += source.parentApplyNanoseconds;
    target.destroyPhaseNanoseconds += source.destroyPhaseNanoseconds;
}

[[nodiscard]] std::uint64_t ElapsedCommandBufferNanoseconds(CommandBufferStatsClock::time_point start, CommandBufferStatsClock::time_point end) noexcept {
    const std::uint64_t nanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    return nanoseconds == 0U ? 1U : nanoseconds;
}

[[nodiscard]] bool ParentBatchIsLocallyAcyclic(std::span<const Entity> children, std::span<const Entity> parents) {
    if (children.size() != parents.size()) {
        return false;
    }

    constexpr std::size_t kNoParentIndex = std::numeric_limits<std::size_t>::max();
    std::unordered_map<Entity::IdType, std::size_t> childIndices;
    childIndices.reserve(children.size());
    for (std::size_t index = 0; index < children.size(); ++index) {
        const Entity child = children[index];
        if (!child.IsValid() || child == parents[index] || !childIndices.emplace(child.Id(), index).second) {
            return false;
        }
    }

    std::vector<std::size_t> parentIndices(children.size(), kNoParentIndex);
    for (std::size_t index = 0; index < parents.size(); ++index) {
        const Entity parent = parents[index];
        if (!parent.IsValid()) {
            continue;
        }
        const auto found = childIndices.find(parent.Id());
        if (found != childIndices.end()) {
            parentIndices[index] = found->second;
        }
    }

    std::vector<std::uint8_t> colors(children.size(), 0U);
    for (std::size_t index = 0; index < children.size(); ++index) {
        std::size_t cursor = index;
        while (cursor != kNoParentIndex) {
            if (colors[cursor] == 1U) {
                return false;
            }
            if (colors[cursor] == 2U) {
                break;
            }
            colors[cursor] = 1U;
            cursor = parentIndices[cursor];
        }

        cursor = index;
        while (cursor != kNoParentIndex && colors[cursor] == 1U) {
            colors[cursor] = 2U;
            cursor = parentIndices[cursor];
        }
    }
    return true;
}

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

const CommandBufferPlaybackResult::Stats& CommandBufferPlaybackResult::PlaybackStats() const noexcept {
    return stats_;
}

bool CommandBufferPlaybackState::Started() const noexcept {
    return phase_ != Phase::NotStarted;
}

bool CommandBufferPlaybackState::Complete() const noexcept {
    return phase_ == Phase::Complete;
}

const CommandBufferPlaybackResult& CommandBufferPlaybackState::Result() const noexcept {
    return result_;
}

void CommandBufferPlaybackState::Reset() noexcept {
    result_ = CommandBufferPlaybackResult{};
    playbackCreatedIds_.clear();
    destroyedIds_.clear();
    scratchEntities_.clear();
    scratchParentEntities_.clear();
    scratchComponentData_.clear();
    scratchComponentIds_.clear();
    workerIndex_ = 0;
    commandIndex_ = 0;
    commandElementIndex_ = 0;
    destroyIndex_ = 0;
    phase_ = Phase::NotStarted;
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

std::vector<CommandEntity> CommandBuffer::WorkerBuffer::CreateEntities(std::size_t count, std::span<const BulkComponentView> components) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }
    if (components.empty()) {
        return CreateEntities(count);
    }

    for (const BulkComponentView& component : components) {
        if (component.registerComponent == nullptr || component.componentSize == 0) {
            throw std::invalid_argument("ECS command buffer runtime bulk create component is incomplete");
        }
        const std::size_t sourceCount = component.sourceCount == 0U ? component.componentCount : component.sourceCount;
        if (component.componentCount != count || sourceCount == 0U || sourceCount > count || (count % sourceCount) != 0U) {
            throw std::invalid_argument("ECS command buffer runtime bulk create component counts must match entity count");
        }
        if (count != 0 && component.data == nullptr) {
            throw std::invalid_argument("ECS command buffer runtime bulk create component payload is null");
        }
        if (component.componentSize != 0 && count > std::numeric_limits<std::size_t>::max() / component.componentSize) {
            throw std::length_error("ECS command buffer runtime bulk create component payload exceeds addressable size");
        }
    }

    std::vector<CommandEntity> entities = owner_->AllocateDeferredEntities(workerIndex_, count);
    if (count == 0) {
        return entities;
    }

    Command command;
    command.kind = CommandKind::CreateEntities;
    command.first = entities.front();
    command.count = count;
    command.bulkComponents.reserve(components.size());
    for (const BulkComponentView& componentView : components) {
        BulkComponentCommand component;
        component.registerComponent = componentView.registerComponent;
        component.componentSize = componentView.componentSize;
        component.sourceCount = componentView.sourceCount == 0U ? componentView.componentCount : componentView.sourceCount;
        component.bytes.resize(componentView.componentSize * component.sourceCount);
        const auto* source = static_cast<const std::byte*>(componentView.data);
        std::copy(source, source + component.bytes.size(), component.bytes.begin());
        command.bulkComponents.push_back(std::move(component));
    }

    owner_->Push(workerIndex_, std::move(command));
    return entities;
}

std::vector<CommandEntity> CommandBuffer::WorkerBuffer::CreateEntitiesBorrowed(std::size_t count, std::span<const BulkComponentView> components) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }
    if (components.empty()) {
        return CreateEntities(count);
    }

    for (const BulkComponentView& component : components) {
        if (component.registerComponent == nullptr || component.componentSize == 0) {
            throw std::invalid_argument("ECS command buffer borrowed bulk create component is incomplete");
        }
        const std::size_t sourceCount = component.sourceCount == 0U ? component.componentCount : component.sourceCount;
        if (component.componentCount != count || sourceCount == 0U || sourceCount > count || (count % sourceCount) != 0U) {
            throw std::invalid_argument("ECS command buffer borrowed bulk create component counts must match entity count");
        }
        if (count != 0 && component.data == nullptr) {
            throw std::invalid_argument("ECS command buffer borrowed bulk create component payload is null");
        }
        if (component.componentSize != 0 && count > std::numeric_limits<std::size_t>::max() / component.componentSize) {
            throw std::length_error("ECS command buffer borrowed bulk create component payload exceeds addressable size");
        }
    }

    std::vector<CommandEntity> entities = owner_->AllocateDeferredEntities(workerIndex_, count);
    if (count == 0) {
        return entities;
    }

    Command command;
    command.kind = CommandKind::CreateEntities;
    command.first = entities.front();
    command.count = count;
    command.bulkComponents.reserve(components.size());
    for (const BulkComponentView& componentView : components) {
        BulkComponentCommand component;
        component.registerComponent = componentView.registerComponent;
        component.componentSize = componentView.componentSize;
        component.sourceCount = componentView.sourceCount == 0U ? componentView.componentCount : componentView.sourceCount;
        component.borrowedData = componentView.data;
        component.borrowedCount = component.sourceCount;
        command.bulkComponents.push_back(std::move(component));
    }

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

void CommandBuffer::WorkerBuffer::DestroyEntities(std::span<const CommandEntity> entities) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }
    if (entities.empty()) {
        return;
    }

    Command command;
    command.kind = CommandKind::DestroyEntities;
    command.entities.assign(entities.begin(), entities.end());
    command.count = entities.size();
    owner_->Push(workerIndex_, std::move(command));
}

void CommandBuffer::WorkerBuffer::DestroyEntities(std::span<const Entity> entities) {
    std::vector<CommandEntity> commandEntities;
    commandEntities.reserve(entities.size());
    for (Entity entity : entities) {
        commandEntities.push_back(CommandEntity::Existing(entity));
    }
    DestroyEntities(std::span<const CommandEntity>{ commandEntities });
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

void CommandBuffer::WorkerBuffer::SetParents(std::span<const CommandEntity> children, std::span<const CommandEntity> parents) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }
    if (children.size() != parents.size()) {
        throw std::invalid_argument("ECS command buffer bulk parent changes require matching child and parent counts");
    }
    if (children.empty()) {
        return;
    }

    Command command;
    command.kind = CommandKind::SetParents;
    command.entities.assign(children.begin(), children.end());
    command.parents.assign(parents.begin(), parents.end());
    command.count = children.size();
    owner_->Push(workerIndex_, std::move(command));
}

void CommandBuffer::WorkerBuffer::SetParentsForNewEntitiesKnownAcyclic(std::span<const CommandEntity> children, std::span<const CommandEntity> parents) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }
    if (children.size() != parents.size()) {
        throw std::invalid_argument("ECS command buffer new-entity parent changes require matching child and parent counts");
    }
    if (children.empty()) {
        return;
    }

    Command command;
    command.kind = CommandKind::SetParents;
    command.entities.assign(children.begin(), children.end());
    command.parents.assign(parents.begin(), parents.end());
    command.count = children.size();
    command.parentBatchKnownAcyclicForNewEntities = true;
    owner_->Push(workerIndex_, std::move(command));
}

void CommandBuffer::WorkerBuffer::SetParents(std::span<const Entity> children, std::span<const Entity> parents) {
    if (children.size() != parents.size()) {
        throw std::invalid_argument("ECS command buffer bulk parent changes require matching child and parent counts");
    }

    std::vector<CommandEntity> commandChildren;
    std::vector<CommandEntity> commandParents;
    commandChildren.reserve(children.size());
    commandParents.reserve(parents.size());
    for (Entity child : children) {
        commandChildren.push_back(CommandEntity::Existing(child));
    }
    for (Entity parent : parents) {
        commandParents.push_back(CommandEntity::Existing(parent));
    }
    SetParents(std::span<const CommandEntity>{ commandChildren }, std::span<const CommandEntity>{ commandParents });
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

void CommandBuffer::WorkerBuffer::ClearParents(std::span<const CommandEntity> children) {
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }
    if (children.empty()) {
        return;
    }

    Command command;
    command.kind = CommandKind::ClearParents;
    command.entities.assign(children.begin(), children.end());
    command.count = children.size();
    owner_->Push(workerIndex_, std::move(command));
}

void CommandBuffer::WorkerBuffer::ClearParents(std::span<const Entity> children) {
    std::vector<CommandEntity> commandChildren;
    commandChildren.reserve(children.size());
    for (Entity child : children) {
        commandChildren.push_back(CommandEntity::Existing(child));
    }
    ClearParents(std::span<const CommandEntity>{ commandChildren });
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

CommandBufferPlaybackResult::Stats CommandBuffer::EstimatePlaybackStats() const {
    CommandBufferPlaybackResult::Stats stats;
    for (const Lane& lane : lanes_) {
        for (const Command& command : lane.commands) {
            AddPlaybackStats(stats, EstimateCommandStats(command));
        }
    }
    return stats;
}

bool CommandBuffer::FitsPlaybackBudget(const CommandBufferPlaybackBudget& budget) const {
    return !ExceedsPlaybackBudget(EstimatePlaybackStats(), budget);
}

CommandBufferPlaybackResult CommandBuffer::Playback(World& world, const CommandBufferPlaybackBudget& budget) {
    if (!FitsPlaybackBudget(budget)) {
        throw std::runtime_error("ECS command buffer playback budget exceeded");
    }
    return Playback(world);
}

CommandBufferPlaybackResult CommandBuffer::Playback(World& world) {
    CommandBufferPlaybackResult result;
    result.createdEntities_.resize(lanes_.size());
    std::size_t deferredEntityCount = 0;
    for (const Lane& lane : lanes_) {
        deferredEntityCount += lane.nextDeferredEntity;
    }
    std::unordered_set<Entity::IdType> playbackCreatedIds;
    playbackCreatedIds.reserve(deferredEntityCount);

    std::vector<PlaybackComponentSnapshot> componentRollback;
    std::vector<PlaybackParentSnapshot> parentRollback;
    std::vector<Entity> playbackScratchEntities;
    std::vector<Entity> playbackScratchParentEntities;

    auto isPlaybackCreated = [&playbackCreatedIds](Entity entity) {
        return entity.IsValid() && playbackCreatedIds.find(entity.Id()) != playbackCreatedIds.end();
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
        const auto createPhaseStart = CommandBufferStatsClock::now();
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

                    ++result.stats_.structuralCommands;
                    ++result.stats_.bulkCreateCommands;
                    std::vector<World::BulkComponentData> components;
                    components.reserve(command.bulkComponents.size());
                    for (const BulkComponentCommand& component : command.bulkComponents) {
                        if (component.registerComponent == nullptr) {
                            throw std::logic_error("ECS command buffer bulk create component is missing a register function");
                        }
                        const std::size_t componentBytes = command.count * component.componentSize;
                        const std::size_t sourceCount = component.sourceCount == 0U ? command.count : component.sourceCount;
                        const std::size_t sourceBytes = sourceCount * component.componentSize;
                        const bool borrowed = component.borrowedData != nullptr;
                        if (component.componentSize == 0 || sourceCount == 0U || sourceCount > command.count || (command.count % sourceCount) != 0U ||
                            (borrowed ? component.borrowedCount != sourceCount : component.bytes.size() != sourceBytes)) {
                            throw std::logic_error("ECS command buffer bulk create component payload size mismatch");
                        }
                        components.push_back(World::BulkComponentData{
                            .componentId = component.registerComponent(world),
                            .componentSize = component.componentSize,
                            .componentCount = command.count,
                            .sourceCount = sourceCount,
                            .data = borrowed ? component.borrowedData : component.bytes.data(),
                        });
                        result.stats_.componentBytesCopied += componentBytes;
                    }

                    const std::vector<Entity> entities = world.CreateEntitiesWithComponents(command.count, components);
                    if (entities.size() != command.count) {
                        throw std::runtime_error("ECS command buffer bulk create returned an unexpected entity count");
                    }
                    for (std::size_t index = 0; index < entities.size(); ++index) {
                        result.createdEntities_[workerIndex][command.first.LocalIndex() + index] = entities[index];
                        playbackCreatedIds.insert(entities[index].Id());
                    }
                    continue;
                }
                if (!command.first.IsDeferred() || command.first.WorkerIndex() != workerIndex || command.first.LocalIndex() >= result.createdEntities_[workerIndex].size()) {
                    throw std::logic_error("ECS command buffer create command has an invalid deferred entity");
                }
                ++result.stats_.structuralCommands;
                ++result.stats_.createCommands;
                Entity createdEntity = world.CreateEntity(command.name);
                result.createdEntities_[workerIndex][command.first.LocalIndex()] = createdEntity;
                playbackCreatedIds.insert(createdEntity.Id());
            }
        }
        result.stats_.createPhaseNanoseconds = ElapsedCommandBufferNanoseconds(createPhaseStart, CommandBufferStatsClock::now());

        const auto applyPhaseStart = CommandBufferStatsClock::now();
        for (const Lane& lane : lanes_) {
            for (const Command& command : lane.commands) {
                switch (command.kind) {
                case CommandKind::CreateEntity:
                case CommandKind::CreateEntities:
                    break;
                case CommandKind::DestroyEntity:
                    ++result.stats_.structuralCommands;
                    ++result.stats_.destroyCommands;
                    scheduleDestroy(ResolveForPlayback(command.first, result));
                    break;
                case CommandKind::DestroyEntities:
                    if (command.entities.size() != command.count) {
                        throw std::logic_error("ECS command buffer bulk destroy command has an invalid entity count");
                    }
                    ++result.stats_.structuralCommands;
                    result.stats_.destroyCommands += command.count;
                    for (CommandEntity commandEntity : command.entities) {
                        scheduleDestroy(ResolveForPlayback(commandEntity, result));
                    }
                    break;
                case CommandKind::SetComponent: {
                    if (command.component.set == nullptr || command.component.registerComponent == nullptr) {
                        throw std::logic_error("ECS command buffer set component command is missing an apply function");
                    }
                    const Entity entity = ResolveForPlayback(command.first, result);
                    const ComponentId componentId = command.component.registerComponent(world);
                    snapshotComponent(entity, componentId);
                    command.component.set(world, command.first, command.component.bytes, result);
                    ++result.stats_.structuralCommands;
                    ++result.stats_.componentSetCommands;
                    result.stats_.componentBytesCopied += command.component.bytes.size();
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
                    ++result.stats_.structuralCommands;
                    ++result.stats_.componentRemoveCommands;
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

                    std::vector<Entity> entities;
                    entities.reserve(command.entities.size());
                    for (CommandEntity commandEntity : command.entities) {
                        const Entity entity = ResolveForPlayback(commandEntity, result);
                        entities.push_back(entity);
                        for (ComponentId componentId : componentIds) {
                            snapshotComponent(entity, componentId);
                        }
                    }

                    std::vector<World::BulkComponentData> components;
                    components.reserve(command.bulkComponents.size());
                    for (std::size_t componentIndex = 0; componentIndex < command.bulkComponents.size(); ++componentIndex) {
                        const BulkComponentCommand& component = command.bulkComponents[componentIndex];
                        components.push_back(World::BulkComponentData{
                            .componentId = componentIds[componentIndex],
                            .componentSize = component.componentSize,
                            .data = component.bytes.data(),
                        });
                        result.stats_.componentBytesCopied += component.bytes.size();
                    }
                    world.AddComponents(entities, components);
                    ++result.stats_.structuralCommands;
                    result.stats_.componentSetCommands += command.count * command.bulkComponents.size();
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
                    std::vector<Entity> entities;
                    entities.reserve(command.entities.size());
                    for (CommandEntity commandEntity : command.entities) {
                        const Entity entity = ResolveForPlayback(commandEntity, result);
                        entities.push_back(entity);
                        for (ComponentId componentId : componentIds) {
                            snapshotComponent(entity, componentId);
                        }
                    }
                    world.RemoveComponents(entities, componentIds);
                    ++result.stats_.structuralCommands;
                    result.stats_.componentRemoveCommands += command.count * componentIds.size();
                    break;
                }
                case CommandKind::SetParent: {
                    const auto parentApplyStart = CommandBufferStatsClock::now();
                    const Entity child = ResolveForPlayback(command.first, result);
                    const Entity parent = ResolveForPlayback(command.second, result);
                    snapshotParent(child);
                    world.SetParent(child, parent);
                    ++result.stats_.structuralCommands;
                    ++result.stats_.parentCommands;
                    result.stats_.parentApplyNanoseconds += ElapsedCommandBufferNanoseconds(parentApplyStart, CommandBufferStatsClock::now());
                    break;
                }
                case CommandKind::SetParents: {
                    const auto parentApplyStart = CommandBufferStatsClock::now();
                    if (command.entities.size() != command.count || command.parents.size() != command.count) {
                        throw std::logic_error("ECS command buffer bulk parent command has an invalid entity count");
                    }
                    playbackScratchEntities.clear();
                    playbackScratchParentEntities.clear();
                    playbackScratchEntities.reserve(command.count);
                    playbackScratchParentEntities.reserve(command.count);
                    for (std::size_t index = 0; index < command.count; ++index) {
                        const Entity child = ResolveForPlayback(command.entities[index], result);
                        const Entity parent = ResolveForPlayback(command.parents[index], result);
                        playbackScratchEntities.push_back(child);
                        playbackScratchParentEntities.push_back(parent);
                    }
                    if (command.parentBatchKnownAcyclicForNewEntities) {
                        world.SetParentsForNewEntitiesKnownAcyclic(playbackScratchEntities, playbackScratchParentEntities);
                    } else if (std::all_of(playbackScratchEntities.begin(), playbackScratchEntities.end(), isPlaybackCreated) &&
                        ParentBatchIsLocallyAcyclic(playbackScratchEntities, playbackScratchParentEntities)) {
                        world.SetParentsForNewEntitiesKnownAcyclic(playbackScratchEntities, playbackScratchParentEntities);
                    } else {
                        for (Entity child : playbackScratchEntities) {
                            snapshotParent(child);
                        }
                        world.SetParents(playbackScratchEntities, playbackScratchParentEntities);
                    }
                    ++result.stats_.structuralCommands;
                    result.stats_.parentCommands += command.count;
                    result.stats_.parentApplyNanoseconds += ElapsedCommandBufferNanoseconds(parentApplyStart, CommandBufferStatsClock::now());
                    break;
                }
                case CommandKind::ClearParent: {
                    const auto parentApplyStart = CommandBufferStatsClock::now();
                    const Entity child = ResolveForPlayback(command.first, result);
                    snapshotParent(child);
                    world.ClearParent(child);
                    ++result.stats_.structuralCommands;
                    ++result.stats_.parentCommands;
                    result.stats_.parentApplyNanoseconds += ElapsedCommandBufferNanoseconds(parentApplyStart, CommandBufferStatsClock::now());
                    break;
                }
                case CommandKind::ClearParents: {
                    const auto parentApplyStart = CommandBufferStatsClock::now();
                    if (command.entities.size() != command.count) {
                        throw std::logic_error("ECS command buffer bulk clear parent command has an invalid entity count");
                    }
                    playbackScratchEntities.clear();
                    playbackScratchEntities.reserve(command.count);
                    for (CommandEntity commandEntity : command.entities) {
                        const Entity child = ResolveForPlayback(commandEntity, result);
                        playbackScratchEntities.push_back(child);
                        snapshotParent(child);
                    }
                    world.ClearParents(playbackScratchEntities);
                    ++result.stats_.structuralCommands;
                    result.stats_.parentCommands += command.count;
                    result.stats_.parentApplyNanoseconds += ElapsedCommandBufferNanoseconds(parentApplyStart, CommandBufferStatsClock::now());
                    break;
                }
                }
            }
        }
        result.stats_.applyPhaseNanoseconds = ElapsedCommandBufferNanoseconds(applyPhaseStart, CommandBufferStatsClock::now());

        const auto destroyPhaseStart = CommandBufferStatsClock::now();
        for (Entity entity : result.destroyedEntities_) {
            world.DestroyEntity(entity);
        }
        result.stats_.destroyPhaseNanoseconds = result.destroyedEntities_.empty() ? 0U : ElapsedCommandBufferNanoseconds(destroyPhaseStart, CommandBufferStatsClock::now());

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

CommandBufferPlaybackSlice CommandBuffer::PlaybackSlice(World& world, const CommandBufferPlaybackBudget& budget, CommandBufferPlaybackState& state) {
    CommandBufferPlaybackSlice slice;
    if (state.phase_ == CommandBufferPlaybackState::Phase::Complete) {
        slice.complete = true;
        return slice;
    }

    if (state.phase_ == CommandBufferPlaybackState::Phase::NotStarted) {
        state.Reset();
        state.phase_ = CommandBufferPlaybackState::Phase::Create;
        state.result_.createdEntities_.resize(lanes_.size());
        std::size_t deferredEntityCount = 0;
        for (std::size_t workerIndex = 0; workerIndex < lanes_.size(); ++workerIndex) {
            const Lane& lane = lanes_[workerIndex];
            state.result_.createdEntities_[workerIndex].resize(lane.nextDeferredEntity);
            deferredEntityCount += lane.nextDeferredEntity;
        }
        state.playbackCreatedIds_.reserve(deferredEntityCount);
    }

    auto projectedFits = [&budget, &slice](const CommandBufferPlaybackResult::Stats& cost) {
        CommandBufferPlaybackResult::Stats projected = slice.stats;
        AddPlaybackStats(projected, cost);
        return !ExceedsPlaybackBudget(projected, budget);
    };

    auto consumeCost = [&slice, &state](const CommandBufferPlaybackResult::Stats& cost) {
        AddPlaybackStats(slice.stats, cost);
        AddPlaybackStats(state.result_.stats_, cost);
        slice.madeProgress = true;
    };

    auto requireProgressForOversizedCommand = [&slice](const CommandBufferPlaybackResult::Stats& cost, const CommandBufferPlaybackBudget& currentBudget) {
        if (!slice.madeProgress && ExceedsPlaybackBudget(cost, currentBudget)) {
            throw std::runtime_error("ECS command buffer playback slice budget cannot fit the next structural command");
        }
    };

    auto isPlaybackCreated = [&state](Entity entity) {
        return entity.IsValid() && state.playbackCreatedIds_.find(entity.Id()) != state.playbackCreatedIds_.end();
    };

    auto scheduleDestroy = [&state](Entity entity) {
        if (!entity.IsValid()) {
            return;
        }
        if (state.destroyedIds_.insert(entity.Id()).second) {
            state.result_.destroyedEntities_.push_back(entity);
        }
    };

    auto applyCreateCommand = [&world, &state](std::size_t workerIndex, const Command& command) {
        if (command.kind == CommandKind::CreateEntities) {
            if (!command.first.IsDeferred() || command.first.WorkerIndex() != workerIndex || command.count == 0 ||
                command.first.LocalIndex() + command.count > state.result_.createdEntities_[workerIndex].size()) {
                throw std::logic_error("ECS command buffer bulk create command has an invalid deferred entity range");
            }

            state.scratchComponentData_.clear();
            state.scratchComponentData_.reserve(command.bulkComponents.size());
            for (const BulkComponentCommand& component : command.bulkComponents) {
                if (component.registerComponent == nullptr) {
                    throw std::logic_error("ECS command buffer bulk create component is missing a register function");
                }
                const std::size_t sourceCount = component.sourceCount == 0U ? command.count : component.sourceCount;
                const std::size_t sourceBytes = sourceCount * component.componentSize;
                const bool borrowed = component.borrowedData != nullptr;
                if (component.componentSize == 0 || sourceCount == 0U || sourceCount > command.count || (command.count % sourceCount) != 0U ||
                    (borrowed ? component.borrowedCount != sourceCount : component.bytes.size() != sourceBytes)) {
                    throw std::logic_error("ECS command buffer bulk create component payload size mismatch");
                }
                state.scratchComponentData_.push_back(World::BulkComponentData{
                    .componentId = component.registerComponent(world),
                    .componentSize = component.componentSize,
                    .componentCount = command.count,
                    .sourceCount = sourceCount,
                    .data = borrowed ? component.borrowedData : component.bytes.data(),
                });
            }

            state.scratchEntities_ = world.CreateEntitiesWithComponents(command.count, state.scratchComponentData_);
            if (state.scratchEntities_.size() != command.count) {
                throw std::runtime_error("ECS command buffer bulk create returned an unexpected entity count");
            }
            for (std::size_t index = 0; index < state.scratchEntities_.size(); ++index) {
                state.result_.createdEntities_[workerIndex][command.first.LocalIndex() + index] = state.scratchEntities_[index];
                state.playbackCreatedIds_.insert(state.scratchEntities_[index].Id());
            }
            return;
        }

        if (!command.first.IsDeferred() || command.first.WorkerIndex() != workerIndex ||
            command.first.LocalIndex() >= state.result_.createdEntities_[workerIndex].size()) {
            throw std::logic_error("ECS command buffer create command has an invalid deferred entity");
        }
        const Entity createdEntity = world.CreateEntity(command.name);
        state.result_.createdEntities_[workerIndex][command.first.LocalIndex()] = createdEntity;
        state.playbackCreatedIds_.insert(createdEntity.Id());
    };

    auto applyCommand = [&world, &state, &isPlaybackCreated, &scheduleDestroy](const Command& command) {
        switch (command.kind) {
        case CommandKind::CreateEntity:
        case CommandKind::CreateEntities:
            break;
        case CommandKind::DestroyEntity:
            scheduleDestroy(ResolveForPlayback(command.first, state.result_));
            break;
        case CommandKind::DestroyEntities:
            if (command.entities.size() != command.count) {
                throw std::logic_error("ECS command buffer bulk destroy command has an invalid entity count");
            }
            for (CommandEntity commandEntity : command.entities) {
                scheduleDestroy(ResolveForPlayback(commandEntity, state.result_));
            }
            break;
        case CommandKind::SetComponent:
            if (command.component.set == nullptr || command.component.registerComponent == nullptr) {
                throw std::logic_error("ECS command buffer set component command is missing an apply function");
            }
            static_cast<void>(command.component.registerComponent(world));
            command.component.set(world, command.first, command.component.bytes, state.result_);
            break;
        case CommandKind::RemoveComponent:
            if (command.component.remove == nullptr || command.component.findComponent == nullptr) {
                throw std::logic_error("ECS command buffer remove component command is missing an apply function");
            }
            command.component.remove(world, command.first, state.result_);
            break;
        case CommandKind::SetComponents: {
            if (command.entities.size() != command.count) {
                throw std::logic_error("ECS command buffer bulk component set command has an invalid entity count");
            }

            state.scratchComponentData_.clear();
            state.scratchComponentData_.reserve(command.bulkComponents.size());
            for (const BulkComponentCommand& component : command.bulkComponents) {
                if (component.registerComponent == nullptr) {
                    throw std::logic_error("ECS command buffer bulk component set is missing a register function");
                }
                if (component.componentSize == 0 || component.bytes.size() != command.count * component.componentSize) {
                    throw std::logic_error("ECS command buffer bulk component set payload size mismatch");
                }
                state.scratchComponentData_.push_back(World::BulkComponentData{
                    .componentId = component.registerComponent(world),
                    .componentSize = component.componentSize,
                    .data = component.bytes.data(),
                });
            }

            state.scratchEntities_.clear();
            state.scratchEntities_.reserve(command.entities.size());
            for (CommandEntity commandEntity : command.entities) {
                state.scratchEntities_.push_back(ResolveForPlayback(commandEntity, state.result_));
            }
            world.AddComponents(state.scratchEntities_, state.scratchComponentData_);
            break;
        }
        case CommandKind::RemoveComponents: {
            if (command.entities.size() != command.count) {
                throw std::logic_error("ECS command buffer bulk component remove command has an invalid entity count");
            }

            state.scratchComponentIds_.clear();
            state.scratchComponentIds_.reserve(command.bulkRemoveComponents.size());
            for (const BulkRemoveComponentCommand& component : command.bulkRemoveComponents) {
                if (component.findComponent == nullptr) {
                    throw std::logic_error("ECS command buffer bulk component remove is missing a component lookup function");
                }
                const ComponentId componentId = component.findComponent(world);
                if (componentId != 0) {
                    state.scratchComponentIds_.push_back(componentId);
                }
            }
            state.scratchEntities_.clear();
            state.scratchEntities_.reserve(command.entities.size());
            for (CommandEntity commandEntity : command.entities) {
                state.scratchEntities_.push_back(ResolveForPlayback(commandEntity, state.result_));
            }
            world.RemoveComponents(state.scratchEntities_, state.scratchComponentIds_);
            break;
        }
        case CommandKind::SetParent: {
            const Entity child = ResolveForPlayback(command.first, state.result_);
            const Entity parent = ResolveForPlayback(command.second, state.result_);
            world.SetParent(child, parent);
            break;
        }
        case CommandKind::SetParents: {
            if (command.entities.size() != command.count || command.parents.size() != command.count) {
                throw std::logic_error("ECS command buffer bulk parent command has an invalid entity count");
            }
            state.scratchEntities_.clear();
            state.scratchParentEntities_.clear();
            state.scratchEntities_.reserve(command.count);
            state.scratchParentEntities_.reserve(command.count);
            for (std::size_t index = 0; index < command.count; ++index) {
                state.scratchEntities_.push_back(ResolveForPlayback(command.entities[index], state.result_));
                state.scratchParentEntities_.push_back(ResolveForPlayback(command.parents[index], state.result_));
            }
            if (command.parentBatchKnownAcyclicForNewEntities) {
                world.SetParentsForNewEntitiesKnownAcyclic(state.scratchEntities_, state.scratchParentEntities_);
            } else if (std::all_of(state.scratchEntities_.begin(), state.scratchEntities_.end(), isPlaybackCreated) &&
                ParentBatchIsLocallyAcyclic(state.scratchEntities_, state.scratchParentEntities_)) {
                world.SetParentsForNewEntitiesKnownAcyclic(state.scratchEntities_, state.scratchParentEntities_);
            } else {
                world.SetParents(state.scratchEntities_, state.scratchParentEntities_);
            }
            break;
        }
        case CommandKind::ClearParent:
            world.ClearParent(ResolveForPlayback(command.first, state.result_));
            break;
        case CommandKind::ClearParents:
            if (command.entities.size() != command.count) {
                throw std::logic_error("ECS command buffer bulk clear parent command has an invalid entity count");
            }
            state.scratchEntities_.clear();
            state.scratchEntities_.reserve(command.count);
            for (std::size_t index = 0; index < command.count; ++index) {
                state.scratchEntities_.push_back(ResolveForPlayback(command.entities[index], state.result_));
            }
            world.ClearParents(state.scratchEntities_);
            break;
        }
    };

    while (state.phase_ == CommandBufferPlaybackState::Phase::Create) {
        if (state.workerIndex_ >= lanes_.size()) {
            state.phase_ = CommandBufferPlaybackState::Phase::Apply;
            state.workerIndex_ = 0;
            state.commandIndex_ = 0;
            break;
        }

        const Lane& lane = lanes_[state.workerIndex_];
        if (state.commandIndex_ >= lane.commands.size()) {
            ++state.workerIndex_;
            state.commandIndex_ = 0;
            continue;
        }

        const Command& command = lane.commands[state.commandIndex_];
        if (!IsCreateCommand(command.kind)) {
            ++state.commandIndex_;
            continue;
        }

        const CommandBufferPlaybackResult::Stats cost = EstimateCommandStats(command);
        if (!projectedFits(cost)) {
            requireProgressForOversizedCommand(cost, budget);
            return slice;
        }
        applyCreateCommand(state.workerIndex_, command);
        consumeCost(cost);
        ++state.commandIndex_;
    }

    while (state.phase_ == CommandBufferPlaybackState::Phase::Apply) {
        if (state.workerIndex_ >= lanes_.size()) {
            state.phase_ = CommandBufferPlaybackState::Phase::Destroy;
            state.workerIndex_ = 0;
            state.commandIndex_ = 0;
            break;
        }

        const Lane& lane = lanes_[state.workerIndex_];
        if (state.commandIndex_ >= lane.commands.size()) {
            ++state.workerIndex_;
            state.commandIndex_ = 0;
            continue;
        }

        const Command& command = lane.commands[state.commandIndex_];
        if (IsCreateCommand(command.kind)) {
            ++state.commandIndex_;
            continue;
        }

        if (command.kind == CommandKind::DestroyEntities) {
            if (command.entities.size() != command.count || state.commandElementIndex_ > command.count) {
                throw std::logic_error("ECS command buffer bulk destroy command has an invalid entity count");
            }
            if (state.commandElementIndex_ == command.count) {
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
                continue;
            }

            const std::size_t remainingDestroyBudget =
                budget.maxDestroyCommands > slice.stats.destroyCommands ? budget.maxDestroyCommands - slice.stats.destroyCommands : 0U;
            if (remainingDestroyBudget == 0U) {
                if (!slice.madeProgress) {
                    throw std::runtime_error("ECS command buffer playback slice destroy schedule budget is zero while pending destroys remain");
                }
                return slice;
            }

            const std::size_t remainingCommandEntities = command.count - state.commandElementIndex_;
            const std::size_t entitiesThisSlice = std::min(remainingCommandEntities, remainingDestroyBudget);
            CommandBufferPlaybackResult::Stats cost;
            cost.structuralCommands = state.commandElementIndex_ == 0U ? 1U : 0U;
            cost.destroyCommands = entitiesThisSlice;
            if (!projectedFits(cost)) {
                requireProgressForOversizedCommand(cost, budget);
                return slice;
            }

            const std::size_t end = state.commandElementIndex_ + entitiesThisSlice;
            for (std::size_t index = state.commandElementIndex_; index < end; ++index) {
                scheduleDestroy(ResolveForPlayback(command.entities[index], state.result_));
            }
            state.commandElementIndex_ = end;
            consumeCost(cost);
            if (state.commandElementIndex_ == command.count) {
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
            }
            continue;
        }

        if (command.kind == CommandKind::SetComponents) {
            if (command.entities.size() != command.count || state.commandElementIndex_ > command.count) {
                throw std::logic_error("ECS command buffer bulk component set command has an invalid entity count");
            }
            if (state.commandElementIndex_ == command.count) {
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
                continue;
            }

            std::size_t bytesPerEntity = 0;
            for (const BulkComponentCommand& component : command.bulkComponents) {
                if (component.registerComponent == nullptr) {
                    throw std::logic_error("ECS command buffer bulk component set is missing a register function");
                }
                if (component.componentSize == 0 || component.bytes.size() != command.count * component.componentSize) {
                    throw std::logic_error("ECS command buffer bulk component set payload size mismatch");
                }
                bytesPerEntity += component.componentSize;
            }
            const std::size_t componentCount = command.bulkComponents.size();
            if (componentCount == 0 || bytesPerEntity == 0) {
                throw std::logic_error("ECS command buffer bulk component set command has no component payload");
            }

            const std::size_t remainingSetBudget =
                budget.maxComponentSetCommands > slice.stats.componentSetCommands ? budget.maxComponentSetCommands - slice.stats.componentSetCommands : 0U;
            const std::size_t remainingByteBudget =
                budget.maxComponentBytesCopied > slice.stats.componentBytesCopied ? budget.maxComponentBytesCopied - slice.stats.componentBytesCopied : 0U;
            const std::size_t remainingCommandEntities = command.count - state.commandElementIndex_;
            const std::size_t entitiesBySetBudget = remainingSetBudget / componentCount;
            const std::size_t entitiesByByteBudget = remainingByteBudget / bytesPerEntity;
            const std::size_t entitiesThisSlice = std::min({ remainingCommandEntities, entitiesBySetBudget, entitiesByByteBudget });
            if (entitiesThisSlice == 0U) {
                if (!slice.madeProgress) {
                    throw std::runtime_error("ECS command buffer playback slice component set budget cannot fit one entity");
                }
                return slice;
            }

            CommandBufferPlaybackResult::Stats cost;
            cost.structuralCommands = state.commandElementIndex_ == 0U ? 1U : 0U;
            cost.componentSetCommands = entitiesThisSlice * componentCount;
            cost.componentBytesCopied = entitiesThisSlice * bytesPerEntity;
            if (!projectedFits(cost)) {
                requireProgressForOversizedCommand(cost, budget);
                return slice;
            }

            const std::size_t begin = state.commandElementIndex_;
            const std::size_t end = begin + entitiesThisSlice;
            state.scratchEntities_.clear();
            state.scratchEntities_.reserve(entitiesThisSlice);
            for (std::size_t index = begin; index < end; ++index) {
                state.scratchEntities_.push_back(ResolveForPlayback(command.entities[index], state.result_));
            }

            state.scratchComponentData_.clear();
            state.scratchComponentData_.reserve(componentCount);
            for (const BulkComponentCommand& component : command.bulkComponents) {
                state.scratchComponentData_.push_back(World::BulkComponentData{
                    .componentId = component.registerComponent(world),
                    .componentSize = component.componentSize,
                    .data = component.bytes.data() + (begin * component.componentSize),
                });
            }

            world.AddComponents(state.scratchEntities_, state.scratchComponentData_);
            state.commandElementIndex_ = end;
            consumeCost(cost);
            if (state.commandElementIndex_ == command.count) {
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
            }
            continue;
        }

        if (command.kind == CommandKind::RemoveComponents) {
            if (command.entities.size() != command.count || state.commandElementIndex_ > command.count) {
                throw std::logic_error("ECS command buffer bulk component remove command has an invalid entity count");
            }
            if (state.commandElementIndex_ == command.count) {
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
                continue;
            }

            state.scratchComponentIds_.clear();
            state.scratchComponentIds_.reserve(command.bulkRemoveComponents.size());
            for (const BulkRemoveComponentCommand& component : command.bulkRemoveComponents) {
                if (component.findComponent == nullptr) {
                    throw std::logic_error("ECS command buffer bulk component remove is missing a component lookup function");
                }
                const ComponentId componentId = component.findComponent(world);
                if (componentId != 0) {
                    state.scratchComponentIds_.push_back(componentId);
                }
            }

            if (state.scratchComponentIds_.empty()) {
                CommandBufferPlaybackResult::Stats cost;
                cost.structuralCommands = state.commandElementIndex_ == 0U ? 1U : 0U;
                if (!projectedFits(cost)) {
                    requireProgressForOversizedCommand(cost, budget);
                    return slice;
                }
                state.commandElementIndex_ = command.count;
                consumeCost(cost);
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
                continue;
            }

            const std::size_t remainingRemoveBudget =
                budget.maxComponentRemoveCommands > slice.stats.componentRemoveCommands ? budget.maxComponentRemoveCommands - slice.stats.componentRemoveCommands : 0U;
            const std::size_t remainingCommandEntities = command.count - state.commandElementIndex_;
            const std::size_t entitiesThisSlice = std::min(remainingCommandEntities, remainingRemoveBudget / state.scratchComponentIds_.size());
            if (entitiesThisSlice == 0U) {
                if (!slice.madeProgress) {
                    throw std::runtime_error("ECS command buffer playback slice component remove budget cannot fit one entity");
                }
                return slice;
            }

            CommandBufferPlaybackResult::Stats cost;
            cost.structuralCommands = state.commandElementIndex_ == 0U ? 1U : 0U;
            cost.componentRemoveCommands = entitiesThisSlice * state.scratchComponentIds_.size();
            if (!projectedFits(cost)) {
                requireProgressForOversizedCommand(cost, budget);
                return slice;
            }

            const std::size_t begin = state.commandElementIndex_;
            const std::size_t end = begin + entitiesThisSlice;
            state.scratchEntities_.clear();
            state.scratchEntities_.reserve(entitiesThisSlice);
            for (std::size_t index = begin; index < end; ++index) {
                state.scratchEntities_.push_back(ResolveForPlayback(command.entities[index], state.result_));
            }
            world.RemoveComponents(state.scratchEntities_, state.scratchComponentIds_);
            state.commandElementIndex_ = end;
            consumeCost(cost);
            if (state.commandElementIndex_ == command.count) {
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
            }
            continue;
        }

        if (command.kind == CommandKind::SetParents) {
            if (command.entities.size() != command.count || command.parents.size() != command.count || state.commandElementIndex_ > command.count) {
                throw std::logic_error("ECS command buffer bulk parent command has an invalid entity count");
            }
            if (state.commandElementIndex_ == command.count) {
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
                continue;
            }

            const std::size_t remainingParentBudget =
                budget.maxParentCommands > slice.stats.parentCommands ? budget.maxParentCommands - slice.stats.parentCommands : 0U;
            if (remainingParentBudget == 0U) {
                if (!slice.madeProgress) {
                    throw std::runtime_error("ECS command buffer playback slice parent budget cannot fit one relation");
                }
                return slice;
            }

            const std::size_t remainingCommandEntities = command.count - state.commandElementIndex_;
            const std::size_t entitiesThisSlice = std::min(remainingCommandEntities, remainingParentBudget);
            CommandBufferPlaybackResult::Stats cost;
            cost.structuralCommands = state.commandElementIndex_ == 0U ? 1U : 0U;
            cost.parentCommands = entitiesThisSlice;
            if (!projectedFits(cost)) {
                requireProgressForOversizedCommand(cost, budget);
                return slice;
            }

            const std::size_t begin = state.commandElementIndex_;
            const std::size_t end = begin + entitiesThisSlice;
            state.scratchEntities_.clear();
            state.scratchParentEntities_.clear();
            state.scratchEntities_.reserve(entitiesThisSlice);
            state.scratchParentEntities_.reserve(entitiesThisSlice);
            for (std::size_t index = begin; index < end; ++index) {
                state.scratchEntities_.push_back(ResolveForPlayback(command.entities[index], state.result_));
                state.scratchParentEntities_.push_back(ResolveForPlayback(command.parents[index], state.result_));
            }

            const bool wholeCommand = begin == 0U && entitiesThisSlice == command.count;
            if (command.parentBatchKnownAcyclicForNewEntities) {
                world.SetParentsForNewEntitiesKnownAcyclic(state.scratchEntities_, state.scratchParentEntities_);
            } else if (wholeCommand && std::all_of(state.scratchEntities_.begin(), state.scratchEntities_.end(), isPlaybackCreated) &&
                ParentBatchIsLocallyAcyclic(state.scratchEntities_, state.scratchParentEntities_)) {
                world.SetParentsForNewEntitiesKnownAcyclic(state.scratchEntities_, state.scratchParentEntities_);
            } else {
                world.SetParents(state.scratchEntities_, state.scratchParentEntities_);
            }

            state.commandElementIndex_ = end;
            consumeCost(cost);
            if (state.commandElementIndex_ == command.count) {
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
            }
            continue;
        }

        if (command.kind == CommandKind::ClearParents) {
            if (command.entities.size() != command.count || state.commandElementIndex_ > command.count) {
                throw std::logic_error("ECS command buffer bulk clear parent command has an invalid entity count");
            }
            if (state.commandElementIndex_ == command.count) {
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
                continue;
            }

            const std::size_t remainingParentBudget =
                budget.maxParentCommands > slice.stats.parentCommands ? budget.maxParentCommands - slice.stats.parentCommands : 0U;
            if (remainingParentBudget == 0U) {
                if (!slice.madeProgress) {
                    throw std::runtime_error("ECS command buffer playback slice parent clear budget cannot fit one relation");
                }
                return slice;
            }

            const std::size_t remainingCommandEntities = command.count - state.commandElementIndex_;
            const std::size_t entitiesThisSlice = std::min(remainingCommandEntities, remainingParentBudget);
            CommandBufferPlaybackResult::Stats cost;
            cost.structuralCommands = state.commandElementIndex_ == 0U ? 1U : 0U;
            cost.parentCommands = entitiesThisSlice;
            if (!projectedFits(cost)) {
                requireProgressForOversizedCommand(cost, budget);
                return slice;
            }

            const std::size_t begin = state.commandElementIndex_;
            const std::size_t end = begin + entitiesThisSlice;
            state.scratchEntities_.clear();
            state.scratchEntities_.reserve(entitiesThisSlice);
            for (std::size_t index = begin; index < end; ++index) {
                state.scratchEntities_.push_back(ResolveForPlayback(command.entities[index], state.result_));
            }
            world.ClearParents(state.scratchEntities_);

            state.commandElementIndex_ = end;
            consumeCost(cost);
            if (state.commandElementIndex_ == command.count) {
                ++state.commandIndex_;
                state.commandElementIndex_ = 0;
            }
            continue;
        }

        const CommandBufferPlaybackResult::Stats cost = EstimateCommandStats(command);
        if (!projectedFits(cost)) {
            requireProgressForOversizedCommand(cost, budget);
            return slice;
        }
        applyCommand(command);
        consumeCost(cost);
        ++state.commandIndex_;
    }

    if (state.phase_ == CommandBufferPlaybackState::Phase::Destroy) {
        const std::size_t destroyBudget =
            budget.maxDestroyCommands > slice.stats.destroyCommands ? budget.maxDestroyCommands - slice.stats.destroyCommands : 0U;
        while (state.destroyIndex_ < state.result_.destroyedEntities_.size()) {
            if (slice.destroyedEntitiesApplied >= destroyBudget) {
                if (!slice.madeProgress && destroyBudget == 0U) {
                    throw std::runtime_error("ECS command buffer playback slice destroy budget is zero while pending destroys remain");
                }
                return slice;
            }
            const Entity entity = state.result_.destroyedEntities_[state.destroyIndex_++];
            if (world.IsAlive(entity)) {
                world.DestroyEntity(entity);
            }
            ++slice.destroyedEntitiesApplied;
            slice.madeProgress = true;
        }
        state.phase_ = CommandBufferPlaybackState::Phase::Complete;
        Clear();
        slice.complete = true;
    }

    return slice;
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

CommandBufferPlaybackResult::Stats CommandBuffer::EstimateCommandStats(const Command& command) {
    CommandBufferPlaybackResult::Stats stats;
    switch (command.kind) {
    case CommandKind::CreateEntity:
        ++stats.structuralCommands;
        ++stats.createCommands;
        break;
    case CommandKind::CreateEntities:
        ++stats.structuralCommands;
        ++stats.bulkCreateCommands;
        for (const BulkComponentCommand& component : command.bulkComponents) {
            stats.componentBytesCopied += component.componentSize * command.count;
        }
        break;
    case CommandKind::DestroyEntity:
        ++stats.structuralCommands;
        ++stats.destroyCommands;
        break;
    case CommandKind::DestroyEntities:
        ++stats.structuralCommands;
        stats.destroyCommands += command.count;
        break;
    case CommandKind::SetComponent:
        ++stats.structuralCommands;
        ++stats.componentSetCommands;
        stats.componentBytesCopied += command.component.bytes.size();
        break;
    case CommandKind::RemoveComponent:
        ++stats.structuralCommands;
        ++stats.componentRemoveCommands;
        break;
    case CommandKind::SetComponents:
        ++stats.structuralCommands;
        stats.componentSetCommands += command.count * command.bulkComponents.size();
        for (const BulkComponentCommand& component : command.bulkComponents) {
            stats.componentBytesCopied += component.bytes.size();
        }
        break;
    case CommandKind::RemoveComponents:
        ++stats.structuralCommands;
        stats.componentRemoveCommands += command.count * command.bulkRemoveComponents.size();
        break;
    case CommandKind::SetParent:
    case CommandKind::ClearParent:
        ++stats.structuralCommands;
        ++stats.parentCommands;
        break;
    case CommandKind::SetParents:
    case CommandKind::ClearParents:
        ++stats.structuralCommands;
        stats.parentCommands += command.count;
        break;
    }
    return stats;
}

bool CommandBuffer::IsCreateCommand(CommandKind kind) noexcept {
    return kind == CommandKind::CreateEntity || kind == CommandKind::CreateEntities;
}

} // namespace kb::ecs
