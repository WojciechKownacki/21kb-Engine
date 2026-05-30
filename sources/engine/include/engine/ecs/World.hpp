#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/WorldConfig.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>

struct ecs_world_t;

namespace kb::ecs {

class ComponentRegistry;
class QueryState;
template <typename... Components>
class Query;

class World {
public:
    template <typename T>
    using ConstComponentVisitor = void (*)(Entity entity, const T& component, void* context);

    template <typename T>
    using MutableComponentVisitor = void (*)(Entity entity, T& component, void* context);

    template <typename TFirst, typename TSecond>
    using ConstComponentsVisitor = void (*)(Entity entity, const TFirst& first, const TSecond& second, void* context);

    explicit World(WorldConfig config = WorldConfig{});
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;

    [[nodiscard]] Entity CreateEntity();
    [[nodiscard]] Entity CreateEntity(std::string_view name);

    void DestroyEntity(Entity entity) noexcept;

    [[nodiscard]] bool IsAlive(Entity entity) const noexcept;
    [[nodiscard]] std::string Name(Entity entity) const;

    template <typename T>
    [[nodiscard]] ComponentId RegisterComponent(std::string_view name = {});

    template <typename T>
    [[nodiscard]] ComponentId Component() const noexcept;

    template <typename T>
    void Set(Entity entity, const T& component);

    template <typename T>
    [[nodiscard]] bool Has(Entity entity) const noexcept;

    template <typename T>
    [[nodiscard]] const T* TryGet(Entity entity) const noexcept;

    template <typename T>
    [[nodiscard]] T* TryGetMutable(Entity entity) noexcept;

    template <typename T>
    void Remove(Entity entity) noexcept;

    template <typename T>
    void MarkModified(Entity entity) noexcept;

    template <typename T>
    void ForEach(ConstComponentVisitor<T> visitor, void* context) const;

    template <typename T>
    void ForEachMutable(MutableComponentVisitor<T> visitor, void* context);

    template <typename TFirst, typename TSecond>
    void ForEach(ConstComponentsVisitor<TFirst, TSecond> visitor, void* context) const;

    template <typename... Components>
    [[nodiscard]] Query<Components...> CreateQuery();

    [[nodiscard]] bool Progress(float deltaSeconds);
    void RequestQuit() noexcept;
    [[nodiscard]] bool ShouldQuit() const noexcept;

    [[nodiscard]] ecs_world_t* NativeHandle() noexcept;
    [[nodiscard]] const ecs_world_t* NativeHandle() const noexcept;
    [[nodiscard]] const WorldConfig& Config() const noexcept;

private:
    using RawConstComponentVisitor = void (*)(Entity entity, const void* component, void* context);
    using RawMutableComponentVisitor = void (*)(Entity entity, void* component, void* context);

    template <typename T>
    static constexpr void ValidateComponentType() noexcept;

    template <typename T>
    [[nodiscard]] static std::string_view DefaultComponentName() noexcept;

    [[nodiscard]] ComponentId RegisterComponent(std::type_index type, std::string_view name, std::size_t size, std::size_t alignment);
    [[nodiscard]] ComponentId FindComponent(std::type_index type) const noexcept;
    void SetComponent(Entity entity, ComponentId componentId, std::size_t size, const void* component);
    [[nodiscard]] bool HasComponent(Entity entity, ComponentId componentId) const noexcept;
    [[nodiscard]] const void* TryGetComponent(Entity entity, ComponentId componentId) const noexcept;
    [[nodiscard]] void* TryGetMutableComponent(Entity entity, ComponentId componentId) noexcept;
    void RemoveComponent(Entity entity, ComponentId componentId) noexcept;
    void MarkComponentModified(Entity entity, ComponentId componentId) noexcept;
    void ForEachComponent(ComponentId componentId, std::size_t componentSize, RawConstComponentVisitor visitor, void* context) const;
    void ForEachMutableComponent(ComponentId componentId, std::size_t componentSize, RawMutableComponentVisitor visitor, void* context);
    void ForEachComponents(
        ComponentId firstComponentId,
        std::size_t firstComponentSize,
        ComponentId secondComponentId,
        std::size_t secondComponentSize,
        void (*visitor)(Entity entity, const void* first, const void* second, void* context),
        void* context) const;
    [[nodiscard]] QueryState* CreateQueryState(const ComponentId* componentIds, const std::size_t* componentSizes, std::size_t componentCount) const;
    void Reset() noexcept;

    ecs_world_t* world_ = nullptr;
    WorldConfig config_{};
    std::unique_ptr<ComponentRegistry> components_;
};

} // namespace kb::ecs

#include "engine/ecs/World.inl"
