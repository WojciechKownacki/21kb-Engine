#pragma once

#include "engine/ecs/ChunkSizeProfile.hpp"
#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/WorldConfig.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::ecs {

struct NativeComponentType {
    ComponentId id = 0;
    std::size_t size = 0;
    std::size_t alignment = 1;
};

struct NativeComponentValue {
    NativeComponentType type{};
    const void* data = nullptr;
};

struct NativeEcsStorageStats {
    std::size_t chunks = 0;
    std::size_t usedBytes = 0;
    std::size_t wastedBytes = 0;
    std::size_t archetypeCount = 0;
    std::size_t liveEntities = 0;
};

struct NativeArchetypeMatch {
    std::size_t archetypeIndex = 0;
    std::size_t liveEntities = 0;
    std::uint64_t version = 0;
};

class NativeArchetypeStorage {
public:
    explicit NativeArchetypeStorage(WorldConfig config = WorldConfig{});
    ~NativeArchetypeStorage();

    NativeArchetypeStorage(const NativeArchetypeStorage&) = delete;
    NativeArchetypeStorage& operator=(const NativeArchetypeStorage&) = delete;
    NativeArchetypeStorage(NativeArchetypeStorage&&) noexcept;
    NativeArchetypeStorage& operator=(NativeArchetypeStorage&&) noexcept;

    [[nodiscard]] Entity CreateEntity(std::span<const NativeComponentValue> components = {});
    void DestroyEntity(Entity entity);
    [[nodiscard]] bool IsAlive(Entity entity) const noexcept;

    void AddComponents(Entity entity, std::span<const NativeComponentValue> components);
    void RemoveComponents(Entity entity, std::span<const ComponentId> componentIds);

    void SetComponent(Entity entity, ComponentId componentId, const void* data, std::size_t size);
    [[nodiscard]] void* MutableComponentData(Entity entity, ComponentId componentId);
    [[nodiscard]] const void* ComponentData(Entity entity, ComponentId componentId) const;
    [[nodiscard]] bool HasComponent(Entity entity, ComponentId componentId) const;
    [[nodiscard]] bool EntityArchetypeMatches(Entity entity, std::span<const ComponentId> requiredComponentIds) const;
    [[nodiscard]] std::vector<NativeArchetypeMatch> MatchingArchetypes(std::span<const ComponentId> requiredComponentIds) const;

    [[nodiscard]] std::uint64_t ArchetypeVersion(Entity entity) const;
    [[nodiscard]] std::uint64_t ComponentVersion(Entity entity, ComponentId componentId) const;
    [[nodiscard]] std::size_t ChunkPayloadBytes() const noexcept;
    [[nodiscard]] NativeEcsStorageStats Stats() const noexcept;

private:
    class Impl;

    Impl* impl_ = nullptr;
};

} // namespace kb::ecs
