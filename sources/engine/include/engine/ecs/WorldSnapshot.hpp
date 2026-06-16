#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/ComponentTypeInfo.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::ecs {

struct ComponentSnapshot {
    ComponentId componentId = 0;
    std::string componentName;
    std::vector<std::byte> data;
};

struct EntitySnapshot {
    Entity::IdType id = 0;
    std::string name;
    std::vector<ComponentSnapshot> components;
};

struct WorldSnapshot {
    std::vector<ComponentTypeInfo> componentTypes;
    std::vector<EntitySnapshot> entities;
};

struct ChunkedComponentSnapshot {
    ComponentId componentId = 0;
    std::string componentName;
    std::size_t componentSize = 0;
    std::uint64_t version = 0;
    std::vector<std::byte> data;
};

struct ChunkedWorldSnapshotChunk {
    std::size_t archetypeIndex = 0;
    std::size_t chunkIndex = 0;
    std::vector<Entity::IdType> entityIds;
    std::vector<ChunkedComponentSnapshot> components;
};

struct ChunkedWorldSnapshot {
    std::vector<ComponentTypeInfo> componentTypes;
    std::vector<ChunkedWorldSnapshotChunk> chunks;
    std::size_t entityCount = 0;
};

struct ChunkedWorldDeltaSnapshotChunk {
    std::size_t archetypeIndex = 0;
    std::size_t chunkIndex = 0;
    bool fullArchetype = false;
    std::vector<Entity::IdType> entityIds;
    std::vector<ChunkedComponentSnapshot> components;
};

struct ChunkedWorldDeltaSnapshot {
    std::vector<ComponentTypeInfo> componentTypes;
    std::size_t entityCount = 0;
    std::vector<Entity::IdType> destroyedEntityIds;
    std::vector<ChunkedWorldDeltaSnapshotChunk> chunks;
};

struct ChunkedWorldSnapshotHeader {
    std::vector<ComponentTypeInfo> componentTypes;
    std::size_t entityCount = 0;
};

struct ChunkedComponentSnapshotView {
    ComponentId componentId = 0;
    std::string_view componentName;
    std::size_t componentSize = 0;
    std::uint64_t version = 0;
    std::span<const std::byte> data;
};

struct ChunkedWorldSnapshotChunkView {
    std::size_t archetypeIndex = 0;
    std::size_t chunkIndex = 0;
    std::span<const Entity::IdType> entityIds;
    std::span<const ChunkedComponentSnapshotView> components;
};

enum class ChunkedWorldSnapshotStreamReadResult {
    Chunk,
    End,
    Error,
};

using ChunkedWorldSnapshotHeaderVisitor = bool (*)(const ChunkedWorldSnapshotHeader& header, void* context);
using ChunkedWorldSnapshotChunkVisitor = bool (*)(const ChunkedWorldSnapshotChunkView& chunk, void* context);
using ChunkedWorldSnapshotChunkReader = ChunkedWorldSnapshotStreamReadResult (*)(ChunkedWorldSnapshotChunk& output, void* context);

} // namespace kb::ecs
