#include "engine/ecs/World.hpp"

#include "ecs/serialization/SerializedEntityComponentApplier.hpp"
#include "ecs/serialization/SerializedEntityComponentReader.hpp"
#include "ecs/serialization/SerializedWorldRestorer.hpp"
#include "ecs/serialization/SerializedWorldWriter.hpp"
#include "ecs/serialization/ChunkedWorldSnapshotBinaryCodec.hpp"
#include "ecs/ComponentRegistry.hpp"
#include "ecs/world/WorldInternalAccess.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace kb::ecs {

namespace {

[[nodiscard]] const ComponentTypeInfo* FindHeaderComponentType(const ChunkedWorldSnapshotHeader& header, ComponentId componentId) noexcept {
    for (const ComponentTypeInfo& componentType : header.componentTypes) {
        if (componentType.id == componentId) {
            return &componentType;
        }
    }
    return nullptr;
}

[[nodiscard]] bool ComponentPayloadSizeMatches(std::size_t rowCount, std::size_t componentSize, std::size_t dataSize) noexcept {
    return componentSize != 0U && rowCount <= (std::numeric_limits<std::size_t>::max() / componentSize) && dataSize == rowCount * componentSize;
}

[[nodiscard]] std::string_view ResolveComponentName(
    const ChunkedWorldSnapshotHeader& header,
    const ChunkedComponentSnapshot& component) noexcept {
    if (!component.componentName.empty()) {
        return component.componentName;
    }

    const ComponentTypeInfo* headerComponent = FindHeaderComponentType(header, component.componentId);
    return headerComponent != nullptr ? std::string_view{ headerComponent->name } : std::string_view{};
}

[[nodiscard]] const ComponentTypeInfo* ResolveRestoreComponentType(
    const World& world,
    const ChunkedWorldSnapshotHeader& header,
    const ChunkedComponentSnapshot& component) noexcept {
    const std::string_view componentName = ResolveComponentName(header, component);
    if (!componentName.empty()) {
        if (const ComponentReflection* reflection = world.Reflection(componentName);
            reflection != nullptr && reflection->Size() == component.componentSize) {
            const WorldRegistrySet* registries = WorldInternalAccess::Registries(world);
            return registries != nullptr ? registries->Components().FindInfo(reflection->Id()) : nullptr;
        }
    }

    if (const ComponentReflection* reflection = world.Reflection(component.componentId);
        reflection != nullptr && reflection->Size() == component.componentSize) {
        const WorldRegistrySet* registries = WorldInternalAccess::Registries(world);
        return registries != nullptr ? registries->Components().FindInfo(reflection->Id()) : nullptr;
    }

    const WorldRegistrySet* registries = WorldInternalAccess::Registries(world);
    const ComponentTypeInfo* componentType = registries != nullptr ? registries->Components().FindInfo(component.componentId) : nullptr;
    return componentType != nullptr && componentType->size == component.componentSize ? componentType : nullptr;
}

[[nodiscard]] const ComponentTypeInfo* ResolveRestoreComponentType(const World& world, const ComponentTypeInfo& sourceType) noexcept {
    if (!sourceType.name.empty()) {
        if (const ComponentReflection* reflection = world.Reflection(sourceType.name);
            reflection != nullptr && reflection->Size() == sourceType.size) {
            const WorldRegistrySet* registries = WorldInternalAccess::Registries(world);
            return registries != nullptr ? registries->Components().FindInfo(reflection->Id()) : nullptr;
        }
    }

    const WorldRegistrySet* registries = WorldInternalAccess::Registries(world);
    const ComponentTypeInfo* componentType = registries != nullptr ? registries->Components().FindInfo(sourceType.id) : nullptr;
    return componentType != nullptr && componentType->size == sourceType.size ? componentType : nullptr;
}

} // namespace

bool World::SerializeComponent(Entity entity, ComponentId componentId, SerializedComponent& output) const {
    ValidateEntityHandle(entity, "SerializeComponent");
    return SerializedEntityComponentReader::Read(*this, entity, componentId, output);
}

bool World::ApplySerializedComponent(Entity entity, const SerializedComponent& component) {
    ValidateEntityHandle(entity, "ApplySerializedComponent");
    return SerializedEntityComponentApplier::Apply(*this, entity, component);
}

bool World::SerializeWorld(SerializedWorld& output) const {
    return SerializedWorldWriter{ *this }.Write(output);
}

bool World::RestoreSerializedWorld(const SerializedWorld& source) {
    return SerializedWorldRestorer{ *this }.Restore(source);
}

bool World::SerializeChunkedSnapshotBinary(std::vector<std::byte>& output) const {
    return ChunkedWorldSnapshotBinaryCodec::Encode(*this, CaptureChunkedSnapshot(), output);
}

namespace {

struct BinaryChunkRestoreContext {
    const std::vector<ChunkedWorldSnapshotChunk>* chunks = nullptr;
    std::size_t nextChunk = 0;
};

ChunkedWorldSnapshotStreamReadResult ReadBinaryDecodedChunk(ChunkedWorldSnapshotChunk& output, void* context) {
    auto* restore = static_cast<BinaryChunkRestoreContext*>(context);
    if (restore == nullptr || restore->chunks == nullptr) {
        return ChunkedWorldSnapshotStreamReadResult::Error;
    }
    if (restore->nextChunk >= restore->chunks->size()) {
        return ChunkedWorldSnapshotStreamReadResult::End;
    }

    output = (*restore->chunks)[restore->nextChunk++];
    return ChunkedWorldSnapshotStreamReadResult::Chunk;
}

} // namespace

bool World::RestoreChunkedSnapshotBinary(std::span<const std::byte> source) {
    ChunkedWorldSnapshotHeader header;
    std::vector<ChunkedWorldSnapshotChunk> chunks;
    if (!ChunkedWorldSnapshotBinaryCodec::Decode(source, header, chunks)) {
        return false;
    }

    BinaryChunkRestoreContext context{
        .chunks = &chunks,
    };
    return RestoreChunkedSnapshotStream(header, &ReadBinaryDecodedChunk, &context);
}

bool World::RestoreChunkedSnapshotStream(
    const ChunkedWorldSnapshotHeader& header,
    ChunkedWorldSnapshotChunkReader chunkReader,
    void* context) {
    if (chunkReader == nullptr) {
        return false;
    }

    std::size_t restoredEntities = 0;
    ChunkedWorldSnapshotChunk chunk;
    for (;;) {
        chunk = {};
        const ChunkedWorldSnapshotStreamReadResult result = chunkReader(chunk, context);
        if (result == ChunkedWorldSnapshotStreamReadResult::End) {
            return restoredEntities == header.entityCount;
        }
        if (result == ChunkedWorldSnapshotStreamReadResult::Error) {
            return false;
        }
        if (chunk.entityIds.empty()) {
            if (!chunk.components.empty()) {
                return false;
            }
            continue;
        }

        std::vector<BulkComponentData> components;
        components.reserve(chunk.components.size());
        for (const ChunkedComponentSnapshot& component : chunk.components) {
            if (!ComponentPayloadSizeMatches(chunk.entityIds.size(), component.componentSize, component.data.size())) {
                return false;
            }

            const ComponentTypeInfo* targetComponent = ResolveRestoreComponentType(*this, header, component);
            if (targetComponent == nullptr) {
                return false;
            }

            components.push_back(BulkComponentData{
                .componentId = targetComponent->id,
                .componentSize = targetComponent->size,
                .data = component.data.data(),
            });
        }

        AdoptEntitiesWithComponents(chunk.entityIds, components);
        if (chunk.entityIds.size() > header.entityCount - restoredEntities) {
            return false;
        }
        restoredEntities += chunk.entityIds.size();
    }
}

bool World::ApplyChunkedDeltaSnapshot(const ChunkedWorldDeltaSnapshot& delta) {
    ChunkedWorldSnapshotHeader header{
        .componentTypes = delta.componentTypes,
        .entityCount = delta.entityCount,
    };

    std::vector<Entity::IdType> destroyedEntityIds(delta.destroyedEntityIds.begin(), delta.destroyedEntityIds.end());
    std::sort(destroyedEntityIds.begin(), destroyedEntityIds.end());
    if (std::adjacent_find(destroyedEntityIds.begin(), destroyedEntityIds.end()) != destroyedEntityIds.end()) {
        return false;
    }
    for (Entity::IdType entityId : destroyedEntityIds) {
        if (entityId == 0U || !IsAlive(Entity{ entityId })) {
            return false;
        }
        DestroyEntity(Entity{ entityId });
    }

    std::size_t touchedCount = 0U;
    for (const ChunkedWorldDeltaSnapshotChunk& chunk : delta.chunks) {
        if (chunk.entityIds.size() > std::numeric_limits<std::size_t>::max() - touchedCount) {
            return false;
        }
        touchedCount += chunk.entityIds.size();
    }
    std::unordered_set<Entity::IdType> touchedEntities;
    touchedEntities.reserve(touchedCount);
    for (const ChunkedWorldDeltaSnapshotChunk& chunk : delta.chunks) {
        if (chunk.entityIds.empty() || (!chunk.fullArchetype && chunk.components.empty())) {
            return false;
        }

        std::vector<ComponentId> chunkComponentIds;
        std::vector<ComponentId> targetChunkComponentIds;
        std::vector<BulkComponentData> rowComponents;
        chunkComponentIds.reserve(chunk.components.size());
        targetChunkComponentIds.reserve(chunk.components.size());
        rowComponents.reserve(chunk.components.size());
        for (const ChunkedComponentSnapshot& component : chunk.components) {
            if (!ComponentPayloadSizeMatches(chunk.entityIds.size(), component.componentSize, component.data.size())) {
                return false;
            }
            const ComponentTypeInfo* targetComponent = ResolveRestoreComponentType(*this, header, component);
            if (targetComponent == nullptr) {
                return false;
            }
            chunkComponentIds.push_back(component.componentId);
            targetChunkComponentIds.push_back(targetComponent->id);
            rowComponents.push_back(BulkComponentData{
                .componentId = targetComponent->id,
                .componentSize = targetComponent->size,
            });
        }
        std::sort(chunkComponentIds.begin(), chunkComponentIds.end());
        std::sort(targetChunkComponentIds.begin(), targetChunkComponentIds.end());
        if (std::adjacent_find(chunkComponentIds.begin(), chunkComponentIds.end()) != chunkComponentIds.end() ||
            std::adjacent_find(targetChunkComponentIds.begin(), targetChunkComponentIds.end()) != targetChunkComponentIds.end()) {
            return false;
        }

        if (!chunk.fullArchetype) {
            std::vector<Entity> entities;
            entities.reserve(chunk.entityIds.size());
            for (Entity::IdType entityId : chunk.entityIds) {
                const Entity entity{ entityId };
                if (!entity.IsValid() || !touchedEntities.insert(entityId).second || !IsAlive(entity)) {
                    return false;
                }
                entities.push_back(entity);
            }
            for (std::size_t column = 0; column < chunk.components.size(); ++column) {
                rowComponents[column].data = chunk.components[column].data.data();
                rowComponents[column].sourceCount = entities.size();
            }
            AddComponents(entities, rowComponents);
            continue;
        }

        std::vector<ComponentId> removalCandidates;
        removalCandidates.reserve(delta.componentTypes.size());
        for (const ComponentTypeInfo& componentType : delta.componentTypes) {
            const ComponentTypeInfo* targetComponent = ResolveRestoreComponentType(*this, componentType);
            if (targetComponent != nullptr &&
                !std::binary_search(targetChunkComponentIds.begin(), targetChunkComponentIds.end(), targetComponent->id)) {
                removalCandidates.push_back(targetComponent->id);
            }
        }
        std::vector<Entity> existingEntities;
        std::vector<std::size_t> existingRows;
        std::vector<Entity::IdType> newEntityIds;
        std::vector<std::size_t> newRows;
        for (std::size_t row = 0; row < chunk.entityIds.size(); ++row) {
            const Entity entity{ chunk.entityIds[row] };
            if (!entity.IsValid() || !touchedEntities.insert(entity.Id()).second) {
                return false;
            }
            if (IsAlive(entity)) {
                existingEntities.push_back(entity);
                existingRows.push_back(row);
            } else {
                newEntityIds.push_back(entity.Id());
                newRows.push_back(row);
            }
        }

        auto bindComponents = [&chunk, &rowComponents](
                                  std::span<const std::size_t> rows,
                                  std::vector<std::vector<std::byte>>& packedData,
                                  std::vector<BulkComponentData>& components) {
            components = rowComponents;
            packedData.reserve(chunk.components.size());
            for (std::size_t column = 0; column < chunk.components.size(); ++column) {
                const ChunkedComponentSnapshot& source = chunk.components[column];
                if (rows.size() == chunk.entityIds.size()) {
                    components[column].data = source.data.data();
                } else {
                    std::vector<std::byte>& packed = packedData.emplace_back(rows.size() * source.componentSize);
                    for (std::size_t index = 0; index < rows.size(); ++index) {
                        std::memcpy(
                            packed.data() + index * source.componentSize,
                            source.data.data() + rows[index] * source.componentSize,
                            source.componentSize);
                    }
                    components[column].data = packed.data();
                }
                components[column].sourceCount = rows.size();
            }
        };

        if (!existingEntities.empty()) {
            std::vector<std::vector<std::byte>> packedData;
            std::vector<BulkComponentData> components;
            bindComponents(existingRows, packedData, components);
            AddComponents(existingEntities, components);
            RemoveComponents(existingEntities, removalCandidates);
        }
        if (!newEntityIds.empty()) {
            std::vector<std::vector<std::byte>> packedData;
            std::vector<BulkComponentData> components;
            bindComponents(newRows, packedData, components);
            AdoptEntitiesWithComponents(newEntityIds, components);
        }
    }

    return nativeStorage_ == nullptr || nativeStorage_->Stats().liveEntities == delta.entityCount;
}

} // namespace kb::ecs
