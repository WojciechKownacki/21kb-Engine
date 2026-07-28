#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/ComponentEvent.hpp"
#include "engine/ecs/ComponentReflection.hpp"
#include "engine/ecs/ComponentStoragePolicy.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/ecs/QueryFilter.hpp"
#include "engine/ecs/StructuralChangeValidator.hpp"
#include "engine/ecs/TypeIds.hpp"
#include "engine/ecs/WorldConfig.hpp"
#include "engine/ecs/WorldEditorInspection.hpp"
#include "engine/ecs/WorldInspection.hpp"
#include "engine/ecs/WorldSerialization.hpp"
#include "engine/ecs/WorldSnapshot.hpp"
#include "engine/ecs/WorldTelemetry.hpp"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

struct ecs_world_t;
struct ecs_table_t;

namespace kb::ecs {

class QueryState;
class QueryPlan;
class MutableComponentBorrowLocks;
class WorldInternalAccess;
class WorldRegistrySet;
class CommandBuffer;
class CommandBufferPlaybackState;
template <typename... Components>
class Query;

class World {
public:
    using BulkComponentRegisterFn = ComponentId (*)(World&);
    using BulkComponentRegisterWithOptionsFn = ComponentId (*)(World&, ComponentRegistrationOptions);

    struct BulkComponentView {
        BulkComponentRegisterFn registerComponent = nullptr;
        BulkComponentRegisterWithOptionsFn registerComponentWithOptions = nullptr;
        ComponentRegistrationOptions registrationOptions{};
        std::size_t componentSize = 0;
        std::size_t componentCount = 0;
        std::size_t sourceCount = 0;
        const void* data = nullptr;
    };

    explicit World(WorldConfig config = WorldConfig{});
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;

    template <typename T>
    [[nodiscard]] static BulkComponentView MakeBulkComponentView(std::span<const T> components) noexcept;

    template <typename T>
    [[nodiscard]] static BulkComponentView MakeBulkComponentView(std::span<const T> components, ComponentRegistrationOptions options) noexcept;

    template <typename T>
    [[nodiscard]] static BulkComponentView MakeBulkComponentBroadcastView(const T& component) noexcept;

    template <typename T>
    [[nodiscard]] static BulkComponentView MakeBulkComponentBroadcastView(const T& component, ComponentRegistrationOptions options) noexcept;

    [[nodiscard]] std::vector<Entity> CreateEntities(std::size_t count, std::span<const BulkComponentView> components);
    [[nodiscard]] std::vector<Entity> CreateEntitiesNativeOnly(std::size_t count, std::span<const BulkComponentView> components);
    void CreateEntitiesInto(std::vector<Entity>& output, std::size_t count, std::span<const BulkComponentView> components);
    void CreateEntitiesNativeOnlyInto(std::vector<Entity>& output, std::size_t count, std::span<const BulkComponentView> components);

    // LIB-078: the same guard kb::ecs::Query<T...>::ForEach/ForEachBatch/
    // ForEachMutableBatch already enter internally (QueryState.cpp),
    // exposed publicly so an external, non-Query-based manual iteration
    // wrapper (e.g. kb::library::Query<T>) can protect its own loop
    // against structural changes with this exact mechanism instead of
    // duplicating StructuralChangeValidator's logic or reimplementing a
    // second guard. Read-only, const, noexcept — safe to expose: the only
    // effect is incrementing/decrementing an atomic counter for the
    // guard's lifetime.
    [[nodiscard]] StructuralChangeValidator::Guard EnterIteration() const noexcept;
    void ValidateStructuralChangeAllowed(std::string_view operation) const;

#include "engine/ecs/world/WorldEntityApi.inl"
#include "engine/ecs/world/WorldComponentApi.inl"
#include "engine/ecs/world/WorldQueryApi.inl"
#include "engine/ecs/world/WorldEventApi.inl"
#include "engine/ecs/world/WorldRelationApi.inl"
#include "engine/ecs/world/WorldSerializationApi.inl"
#include "engine/ecs/world/WorldRuntimeApi.inl"

private:
    friend class WorldInternalAccess;
    friend class CommandBuffer;
    friend class CommandBufferPlaybackState;

    struct BulkComponentData {
        ComponentId componentId = 0;
        std::size_t componentSize = 0;
        std::size_t componentCount = 0;
        std::size_t sourceCount = 0;
        const void* data = nullptr;
    };

#include "engine/ecs/world/WorldPrivateComponentApi.inl"
#include "engine/ecs/world/WorldPrivateQueryApi.inl"
#include "engine/ecs/world/WorldPrivateEventApi.inl"
#include "engine/ecs/world/WorldPrivateTypeApi.inl"
#include "engine/ecs/world/WorldPrivateLifecycleApi.inl"

    [[nodiscard]] std::vector<Entity> CreateEntitiesWithComponents(std::size_t count, std::span<const BulkComponentData> components, bool mirrorBackend = true);
    void CreateEntitiesWithComponentsInto(std::vector<Entity>& output, std::size_t count, std::span<const BulkComponentData> components, bool mirrorBackend = true);
    void AdoptEntitiesWithComponents(std::span<const Entity::IdType> entityIds, std::span<const BulkComponentData> components);
    void DestroyEntitiesTrusted(std::span<const Entity> entities);
    void BulkInitFlecsEntities(std::span<const Entity> entities, std::span<const BulkComponentData> components);
    void AddComponents(Entity entity, std::span<const BulkComponentData> components);
    void AddComponents(std::span<const Entity> entities, std::span<const BulkComponentData> components);
    void AddMissingComponentsTrusted(std::span<const Entity> entities, std::span<const BulkComponentData> components);
    void RemoveComponents(Entity entity, std::span<const ComponentId> componentIds);
    void RemoveComponents(std::span<const Entity> entities, std::span<const ComponentId> componentIds);
    void RemoveExistingComponentsTrusted(std::span<const Entity> entities, std::span<const ComponentId> componentIds);
    void SetParentsForNewEntitiesKnownAcyclic(std::span<const Entity> children, std::span<const Entity> parents);
    void ValidateEntityHandle(Entity entity, std::string_view operation) const;
    void ValidateOptionalEntityHandle(Entity entity, std::string_view operation) const;
    [[nodiscard]] Entity ResolveAliveEntity(Entity::IdType entityIdWithoutGeneration) const noexcept;
    [[nodiscard]] NativeComponentValue MakeNativeComponentValue(const BulkComponentData& component) const;
    [[nodiscard]] std::vector<NativeComponentValue> MakeNativeComponentValues(std::span<const BulkComponentData> components) const;
    [[nodiscard]] std::vector<NativeBulkComponentColumn> MakeNativeBulkComponentColumns(std::span<const BulkComponentData> components) const;
    void DestroyNativeEntity(Entity entity) noexcept;
    void RecordStructuralChange(std::size_t count = 1) const noexcept;
    [[nodiscard]] std::shared_ptr<QueryPlan> FindCachedQueryPlan(
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> optionalComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::span<const ComponentId> changedComponentIds) const;
    void StoreCachedQueryPlan(
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> optionalComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::span<const ComponentId> changedComponentIds,
        std::shared_ptr<QueryPlan> plan) const;
    void SetNativeComponent(Entity entity, const BulkComponentData& component);
    void AddNativeComponents(Entity entity, std::span<const BulkComponentData> components);
    void AddNativeComponents(std::span<const Entity> entities, std::span<const BulkComponentData> components);
    void RemoveNativeComponents(Entity entity, std::span<const ComponentId> componentIds) noexcept;
    void RemoveNativeComponents(std::span<const Entity> entities, std::span<const ComponentId> componentIds);

    ecs_world_t* world_ = nullptr;
    WorldConfig config_{};
    struct QueryPlanCacheEntry {
        std::vector<ComponentId> componentIds;
        std::vector<std::size_t> componentSizes;
        std::vector<ComponentId> requiredComponentIds;
        std::vector<ComponentId> optionalComponentIds;
        std::vector<ComponentId> excludedComponentIds;
        std::vector<ComponentId> changedComponentIds;
        std::shared_ptr<QueryPlan> plan;
    };

    std::unique_ptr<WorldRegistrySet> registries_;
    std::unique_ptr<NativeArchetypeStorage> nativeStorage_;
    std::unique_ptr<MutableComponentBorrowLocks> mutableComponentBorrowLocks_;
    std::unique_ptr<StructuralChangeValidator> structuralChangeValidator_;
    mutable WorldTelemetryCounters telemetryCounters_{};
    mutable std::vector<QueryPlanCacheEntry> queryPlanCache_;
};

} // namespace kb::ecs

#include "engine/ecs/World.inl"
