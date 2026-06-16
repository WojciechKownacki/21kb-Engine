#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/World.hpp"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <array>
#include <span>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace kb::ecs {

class CommandEntity {
public:
    constexpr CommandEntity() noexcept = default;

    [[nodiscard]] static constexpr CommandEntity Existing(Entity entity) noexcept {
        CommandEntity ref;
        ref.entity_ = entity;
        return ref;
    }

    [[nodiscard]] static constexpr CommandEntity Deferred(std::size_t workerIndex, std::size_t localIndex) noexcept {
        CommandEntity ref;
        ref.deferred_ = true;
        ref.workerIndex_ = workerIndex;
        ref.localIndex_ = localIndex;
        return ref;
    }

    [[nodiscard]] constexpr bool IsDeferred() const noexcept { return deferred_; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return deferred_ || entity_.IsValid(); }
    [[nodiscard]] constexpr Entity ExistingEntity() const noexcept { return deferred_ ? Entity{} : entity_; }
    [[nodiscard]] constexpr std::size_t WorkerIndex() const noexcept { return workerIndex_; }
    [[nodiscard]] constexpr std::size_t LocalIndex() const noexcept { return localIndex_; }

private:
    Entity entity_;
    std::size_t workerIndex_ = 0;
    std::size_t localIndex_ = 0;
    bool deferred_ = false;
};

class CommandBufferPlaybackResult {
public:
    [[nodiscard]] Entity Resolve(CommandEntity entity) const;
    [[nodiscard]] std::size_t CreatedCount() const noexcept;
    [[nodiscard]] bool WasDestroyed(CommandEntity entity) const;
    [[nodiscard]] bool WasDestroyed(Entity entity) const noexcept;
    [[nodiscard]] std::size_t DestroyedCount() const noexcept;

private:
    std::vector<std::vector<Entity>> createdEntities_;
    std::vector<Entity> destroyedEntities_;

    friend class CommandBuffer;
};

class CommandBuffer {
public:
    using RegisterComponentFn = ComponentId (*)(World&);

    struct BulkComponentView {
        RegisterComponentFn registerComponent = nullptr;
        std::size_t componentSize = 0;
        std::size_t componentCount = 0;
        const void* data = nullptr;
    };

    class WorkerBuffer {
    public:
        WorkerBuffer() noexcept = default;

        [[nodiscard]] CommandEntity CreateEntity(std::string_view name = {});
        [[nodiscard]] std::vector<CommandEntity> CreateEntities(std::size_t count);
        [[nodiscard]] std::vector<CommandEntity> CreateEntities(std::size_t count, std::span<const BulkComponentView> components);

        template <typename... Components>
        [[nodiscard]] std::vector<CommandEntity> CreateEntities(std::span<const Components>... components);

        void DestroyEntity(CommandEntity entity);
        void DestroyEntity(Entity entity);

        template <typename T>
        void Set(CommandEntity entity, const T& component);

        template <typename T>
        void Set(Entity entity, const T& component);

        template <typename... Components>
        void Set(std::span<const CommandEntity> entities, std::span<const Components>... components);

        template <typename... Components>
        void Set(std::span<const Entity> entities, std::span<const Components>... components);

        template <typename T>
        void Remove(CommandEntity entity);

        template <typename T>
        void Remove(Entity entity);

        template <typename... Components>
        void Remove(std::span<const CommandEntity> entities);

        template <typename... Components>
        void Remove(std::span<const Entity> entities);

        void SetParent(CommandEntity child, CommandEntity parent);
        void SetParent(Entity child, Entity parent);
        void SetParents(std::span<const CommandEntity> children, std::span<const CommandEntity> parents);
        void SetParents(std::span<const Entity> children, std::span<const Entity> parents);
        void ClearParent(CommandEntity child);
        void ClearParent(Entity child);
        void ClearParents(std::span<const CommandEntity> children);
        void ClearParents(std::span<const Entity> children);

    private:
        WorkerBuffer(CommandBuffer& owner, std::size_t workerIndex) noexcept;

        CommandBuffer* owner_ = nullptr;
        std::size_t workerIndex_ = 0;

        friend class CommandBuffer;
    };

    explicit CommandBuffer(std::size_t workerCount = 1);

    [[nodiscard]] WorkerBuffer Worker(std::size_t workerIndex);
    [[nodiscard]] std::size_t WorkerCount() const noexcept;
    [[nodiscard]] std::size_t CommandCount() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;

    template <typename T>
    [[nodiscard]] static BulkComponentView MakeBulkComponentView(std::span<const T> components) noexcept;

    CommandBufferPlaybackResult Playback(World& world);
    void Clear() noexcept;

private:
    enum class CommandKind : std::uint8_t {
        CreateEntity,
        CreateEntities,
        DestroyEntity,
        SetComponent,
        RemoveComponent,
        SetComponents,
        RemoveComponents,
        SetParent,
        SetParents,
        ClearParent,
        ClearParents,
    };

    using SetComponentFn = void (*)(World&, CommandEntity, std::span<const std::byte>, const CommandBufferPlaybackResult&);
    using RemoveComponentFn = void (*)(World&, CommandEntity, const CommandBufferPlaybackResult&);
    using FindComponentFn = ComponentId (*)(const World&);

    struct ComponentCommand {
        SetComponentFn set = nullptr;
        RemoveComponentFn remove = nullptr;
        RegisterComponentFn registerComponent = nullptr;
        FindComponentFn findComponent = nullptr;
        std::vector<std::byte> bytes;
    };

    struct BulkComponentCommand {
        RegisterComponentFn registerComponent = nullptr;
        std::size_t componentSize = 0;
        std::vector<std::byte> bytes;
    };

    struct BulkRemoveComponentCommand {
        FindComponentFn findComponent = nullptr;
    };

    struct Command {
        CommandKind kind = CommandKind::CreateEntity;
        CommandEntity first;
        CommandEntity second;
        std::vector<CommandEntity> entities;
        std::vector<CommandEntity> parents;
        std::string name;
        std::size_t count = 0;
        ComponentCommand component;
        std::vector<BulkComponentCommand> bulkComponents;
        std::vector<BulkRemoveComponentCommand> bulkRemoveComponents;
    };

    struct Lane {
        std::vector<Command> commands;
        std::size_t nextDeferredEntity = 0;
    };

    [[nodiscard]] Lane& LaneFor(std::size_t workerIndex);
    void Push(std::size_t workerIndex, Command command);
    [[nodiscard]] CommandEntity AllocateDeferredEntity(std::size_t workerIndex);
    [[nodiscard]] std::vector<CommandEntity> AllocateDeferredEntities(std::size_t workerIndex, std::size_t count);
    [[nodiscard]] static Entity ResolveForPlayback(CommandEntity entity, const CommandBufferPlaybackResult& result);

    template <typename T>
    static void ApplySetComponent(World& world, CommandEntity entity, std::span<const std::byte> bytes, const CommandBufferPlaybackResult& result);

    template <typename T>
    static void ApplyRemoveComponent(World& world, CommandEntity entity, const CommandBufferPlaybackResult& result);

    template <typename T>
    static ComponentId RegisterBulkComponent(World& world);

    template <typename T>
    static ComponentId FindBulkComponent(const World& world) noexcept;

    template <typename T>
    static void AppendBulkComponent(Command& command, std::span<const T> components);

    template <typename T>
    static void AppendBulkRemoveComponent(Command& command);

    std::vector<Lane> lanes_;
};

template <typename... Components>
std::vector<CommandEntity> CommandBuffer::WorkerBuffer::CreateEntities(std::span<const Components>... components) {
    static_assert(sizeof...(Components) > 0, "ECS command buffer bulk component create requires at least one component span");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS command buffer components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS command buffer components must be trivially destructible");
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }

    const std::array<std::size_t, sizeof...(Components)> sizes{ components.size()... };
    const std::size_t count = sizes.front();
    if (!std::all_of(sizes.begin(), sizes.end(), [count](std::size_t size) { return size == count; })) {
        throw std::invalid_argument("ECS command buffer bulk create component spans must have the same size");
    }

    std::vector<CommandEntity> entities = owner_->AllocateDeferredEntities(workerIndex_, count);
    if (count == 0) {
        return entities;
    }

    Command command;
    command.kind = CommandKind::CreateEntities;
    command.first = entities.front();
    command.count = count;
    command.bulkComponents.reserve(sizeof...(Components));

    (CommandBuffer::AppendBulkComponent<Components>(command, components), ...);

    owner_->Push(workerIndex_, std::move(command));
    return entities;
}

template <typename T>
void CommandBuffer::WorkerBuffer::Set(CommandEntity entity, const T& component) {
    static_assert(std::is_trivially_copyable_v<T>, "ECS command buffer components must be trivially copyable");
    static_assert(std::is_trivially_destructible_v<T>, "ECS command buffer components must be trivially destructible");
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }

    Command command;
    command.kind = CommandKind::SetComponent;
    command.first = entity;
    command.component.set = &CommandBuffer::ApplySetComponent<T>;
    command.component.registerComponent = &CommandBuffer::RegisterBulkComponent<T>;
    command.component.bytes.resize(sizeof(T));
    const auto* source = reinterpret_cast<const std::byte*>(&component);
    std::copy(source, source + sizeof(T), command.component.bytes.begin());
    owner_->Push(workerIndex_, std::move(command));
}

template <typename T>
void CommandBuffer::WorkerBuffer::Set(Entity entity, const T& component) {
    Set(CommandEntity::Existing(entity), component);
}

template <typename... Components>
void CommandBuffer::WorkerBuffer::Set(std::span<const CommandEntity> entities, std::span<const Components>... components) {
    static_assert(sizeof...(Components) > 0, "ECS command buffer bulk component set requires at least one component span");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS command buffer components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS command buffer components must be trivially destructible");
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }

    const std::array<std::size_t, sizeof...(Components)> sizes{ components.size()... };
    const std::size_t count = entities.size();
    if (!std::all_of(sizes.begin(), sizes.end(), [count](std::size_t size) { return size == count; })) {
        throw std::invalid_argument("ECS command buffer bulk component set spans must match entity count");
    }
    if (count == 0) {
        return;
    }

    Command command;
    command.kind = CommandKind::SetComponents;
    command.entities.assign(entities.begin(), entities.end());
    command.count = count;
    command.bulkComponents.reserve(sizeof...(Components));
    (CommandBuffer::AppendBulkComponent<Components>(command, components), ...);
    owner_->Push(workerIndex_, std::move(command));
}

template <typename... Components>
void CommandBuffer::WorkerBuffer::Set(std::span<const Entity> entities, std::span<const Components>... components) {
    std::vector<CommandEntity> commandEntities;
    commandEntities.reserve(entities.size());
    for (Entity entity : entities) {
        commandEntities.push_back(CommandEntity::Existing(entity));
    }
    Set(std::span<const CommandEntity>{ commandEntities }, components...);
}

template <typename T>
void CommandBuffer::WorkerBuffer::Remove(CommandEntity entity) {
    static_assert(std::is_trivially_copyable_v<T>, "ECS command buffer components must be trivially copyable");
    static_assert(std::is_trivially_destructible_v<T>, "ECS command buffer components must be trivially destructible");
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }

    Command command;
    command.kind = CommandKind::RemoveComponent;
    command.first = entity;
    command.component.remove = &CommandBuffer::ApplyRemoveComponent<T>;
    command.component.findComponent = &CommandBuffer::FindBulkComponent<T>;
    owner_->Push(workerIndex_, std::move(command));
}

template <typename T>
void CommandBuffer::WorkerBuffer::Remove(Entity entity) {
    Remove<T>(CommandEntity::Existing(entity));
}

template <typename... Components>
void CommandBuffer::WorkerBuffer::Remove(std::span<const CommandEntity> entities) {
    static_assert(sizeof...(Components) > 0, "ECS command buffer bulk component remove requires at least one component type");
    if (owner_ == nullptr) {
        throw std::logic_error("ECS command buffer worker buffer is not bound to an owner");
    }
    if (entities.empty()) {
        return;
    }

    Command command;
    command.kind = CommandKind::RemoveComponents;
    command.entities.assign(entities.begin(), entities.end());
    command.count = entities.size();
    command.bulkRemoveComponents.reserve(sizeof...(Components));
    (CommandBuffer::AppendBulkRemoveComponent<Components>(command), ...);
    owner_->Push(workerIndex_, std::move(command));
}

template <typename... Components>
void CommandBuffer::WorkerBuffer::Remove(std::span<const Entity> entities) {
    std::vector<CommandEntity> commandEntities;
    commandEntities.reserve(entities.size());
    for (Entity entity : entities) {
        commandEntities.push_back(CommandEntity::Existing(entity));
    }
    Remove<Components...>(std::span<const CommandEntity>{ commandEntities });
}

template <typename T>
void CommandBuffer::ApplySetComponent(World& world, CommandEntity entity, std::span<const std::byte> bytes, const CommandBufferPlaybackResult& result) {
    if (bytes.size() != sizeof(T)) {
        throw std::logic_error("ECS command buffer component payload size mismatch");
    }

    T component{};
    std::copy(bytes.begin(), bytes.end(), reinterpret_cast<std::byte*>(&component));
    world.Set(ResolveForPlayback(entity, result), component);
}

template <typename T>
void CommandBuffer::ApplyRemoveComponent(World& world, CommandEntity entity, const CommandBufferPlaybackResult& result) {
    world.Remove<T>(ResolveForPlayback(entity, result));
}

template <typename T>
ComponentId CommandBuffer::RegisterBulkComponent(World& world) {
    return world.RegisterComponent<T>();
}

template <typename T>
ComponentId CommandBuffer::FindBulkComponent(const World& world) noexcept {
    return world.Component<T>();
}

template <typename T>
void CommandBuffer::AppendBulkComponent(Command& command, std::span<const T> components) {
    if (components.size() > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        throw std::length_error("ECS command buffer bulk component payload exceeds addressable size");
    }

    BulkComponentCommand component;
    component.registerComponent = &CommandBuffer::RegisterBulkComponent<T>;
    component.componentSize = sizeof(T);
    component.bytes.resize(sizeof(T) * components.size());
    const auto* source = reinterpret_cast<const std::byte*>(components.data());
    std::copy(source, source + component.bytes.size(), component.bytes.begin());
    command.bulkComponents.push_back(std::move(component));
}

template <typename T>
void CommandBuffer::AppendBulkRemoveComponent(Command& command) {
    static_assert(std::is_trivially_copyable_v<T>, "ECS command buffer components must be trivially copyable");
    static_assert(std::is_trivially_destructible_v<T>, "ECS command buffer components must be trivially destructible");
    BulkRemoveComponentCommand component;
    component.findComponent = &CommandBuffer::FindBulkComponent<T>;
    command.bulkRemoveComponents.push_back(component);
}

template <typename T>
CommandBuffer::BulkComponentView CommandBuffer::MakeBulkComponentView(std::span<const T> components) noexcept {
    static_assert(std::is_trivially_copyable_v<T>, "ECS command buffer components must be trivially copyable");
    static_assert(std::is_trivially_destructible_v<T>, "ECS command buffer components must be trivially destructible");
    return BulkComponentView{
        .registerComponent = &CommandBuffer::RegisterBulkComponent<T>,
        .componentSize = sizeof(T),
        .componentCount = components.size(),
        .data = components.data(),
    };
}

} // namespace kb::ecs
