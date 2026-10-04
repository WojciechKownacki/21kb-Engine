#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include <unordered_map>

struct ecs_world_t;
struct ecs_iter_t;

namespace kb::ecs {

class ComponentRegistry;
class NativeArchetypeStorage;
class ComponentReflectionRegistry;
class RelationTypeRegistry;
class TagTypeRegistry;
class WorldEntityCatalog;

class WorldRegistrySet {
public:
    WorldRegistrySet();
    ~WorldRegistrySet();

    WorldRegistrySet(const WorldRegistrySet&) = delete;
    WorldRegistrySet& operator=(const WorldRegistrySet&) = delete;
    WorldRegistrySet(WorldRegistrySet&&) noexcept;
    WorldRegistrySet& operator=(WorldRegistrySet&&) noexcept;

    [[nodiscard]] ComponentRegistry& Components() noexcept;
    [[nodiscard]] const ComponentRegistry& Components() const noexcept;
    [[nodiscard]] ComponentReflectionRegistry& ComponentReflections() noexcept;
    [[nodiscard]] const ComponentReflectionRegistry& ComponentReflections() const noexcept;
    [[nodiscard]] TagTypeRegistry& Tags() noexcept;
    [[nodiscard]] const TagTypeRegistry& Tags() const noexcept;
    [[nodiscard]] RelationTypeRegistry& Relations() noexcept;
    [[nodiscard]] const RelationTypeRegistry& Relations() const noexcept;
    [[nodiscard]] WorldEntityCatalog& Entities() noexcept;
    [[nodiscard]] const WorldEntityCatalog& Entities() const noexcept;

    [[nodiscard]] bool HasComponentObserver(ComponentId componentId) const noexcept;
    void RetainComponentObserver(ComponentId componentId);
    void ReleaseComponentObserver(ComponentId componentId) noexcept;

    // Bound before backend creation emits any component callbacks. The
    // directory preserves the last mirrored owner through native destruction
    // and backend deferred removal.
    void InitializeMirroredEntityCleanup(ecs_world_t* world, const NativeArchetypeStorage* nativeStorage);
    [[nodiscard]] Entity::IdType CreateObserverEntity(ecs_world_t* world) const;
    void BindMirroredEntities(ecs_world_t* world, std::span<const Entity> entities);
    [[nodiscard]] Entity ResolveObserverEntity(Entity::IdType backendEntityId) const noexcept;
    [[nodiscard]] bool OwnsMirroredEntity(Entity entity) const noexcept;
    void CompleteMirroredEntityDeletion(ecs_world_t* world, Entity entity) noexcept;

    void Clear() noexcept;

private:
    static void DispatchMirroredEntityCleanup(ecs_iter_t* iterator) noexcept;
    [[nodiscard]] Entity::IdType MirroredEntityId(Entity::IdType backendEntityId) const noexcept;
    void ForgetMirroredEntity(Entity entity) noexcept;

    std::unique_ptr<ComponentRegistry> components_;
    std::unique_ptr<ComponentReflectionRegistry> componentReflections_;
    std::unique_ptr<TagTypeRegistry> tags_;
    std::unique_ptr<RelationTypeRegistry> relations_;
    std::unique_ptr<WorldEntityCatalog> entities_;
    // Owned behind World::registries_ so World's public object layout stays
    // unchanged and observer contexts remain valid when a World is moved.
    std::unordered_map<ComponentId, std::size_t> componentObservers_;
    // Mirrored backend IDs are created with generation zero. One full logical
    // ID certifies the exact backend ID implied by each dense generated index.
    std::vector<Entity::IdType> mirroredEntityIds_;
    std::unordered_map<Entity::IdType, Entity::IdType> sparseMirroredEntityIds_;
    const NativeArchetypeStorage* nativeStorage_ = nullptr;
    Entity::IdType cleanupEventId_ = 0U;
    Entity::IdType cleanupHelperId_ = 0U;
    Entity::IdType cleanupObserverId_ = 0U;
};

} // namespace kb::ecs
