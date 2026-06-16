#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/CommandBuffer.hpp"
#include "engine/ecs/World.hpp"

#include <cstddef>
#include <cstring>
#include <vector>

namespace {

template <typename T>
[[nodiscard]] T ReadSnapshotComponent(const kb::ecs::ComponentSnapshot& snapshot) {
    kb::tests::Require(snapshot.data.size() == sizeof(T), "ECS snapshot component size mismatch");

    T value{};
    std::memcpy(&value, snapshot.data.data(), sizeof(T));
    return value;
}

[[nodiscard]] const kb::ecs::EntitySnapshot* FindSnapshotEntity(const kb::ecs::WorldSnapshot& snapshot, kb::ecs::Entity entity) {
    for (const kb::ecs::EntitySnapshot& entitySnapshot : snapshot.entities) {
        if (entitySnapshot.id == entity.Id()) {
            return &entitySnapshot;
        }
    }
    return nullptr;
}

[[nodiscard]] const kb::ecs::ComponentSnapshot* FindSnapshotComponent(const kb::ecs::EntitySnapshot& snapshot, kb::ecs::ComponentId componentId) {
    for (const kb::ecs::ComponentSnapshot& componentSnapshot : snapshot.components) {
        if (componentSnapshot.componentId == componentId) {
            return &componentSnapshot;
        }
    }
    return nullptr;
}

[[nodiscard]] const kb::ecs::ChunkedComponentSnapshot* FindChunkComponent(const kb::ecs::ChunkedWorldSnapshotChunk& chunk, kb::ecs::ComponentId componentId) {
    for (const kb::ecs::ChunkedComponentSnapshot& componentSnapshot : chunk.components) {
        if (componentSnapshot.componentId == componentId) {
            return &componentSnapshot;
        }
    }
    return nullptr;
}

template <typename T>
[[nodiscard]] T ReadChunkComponent(const kb::ecs::ChunkedComponentSnapshot& snapshot, std::size_t row) {
    const std::size_t offset = row * sizeof(T);
    kb::tests::Require(snapshot.componentSize == sizeof(T), "ECS chunked snapshot component size mismatch");
    kb::tests::Require(offset + sizeof(T) <= snapshot.data.size(), "ECS chunked snapshot component row exceeded column data");

    T value{};
    std::memcpy(&value, snapshot.data.data() + offset, sizeof(T));
    return value;
}

void RegisterSnapshotTestReflection(kb::ecs::World& world) {
    const kb::ecs::ComponentReflection* positionReflection = world.RegisterComponentReflection<EcsPosition>(
        "test.EcsPosition",
        {
            { "x", kb::ecs::ComponentFieldType::Float32, offsetof(EcsPosition, x), sizeof(float) },
            { "y", kb::ecs::ComponentFieldType::Float32, offsetof(EcsPosition, y), sizeof(float) },
        });
    const kb::ecs::ComponentReflection* velocityReflection = world.RegisterComponentReflection<EcsVelocity>(
        "test.EcsVelocity",
        {
            { "x", kb::ecs::ComponentFieldType::Float32, offsetof(EcsVelocity, x), sizeof(float) },
            { "y", kb::ecs::ComponentFieldType::Float32, offsetof(EcsVelocity, y), sizeof(float) },
        });
    kb::tests::Require(positionReflection != nullptr && velocityReflection != nullptr, "ECS snapshot test reflection registration failed");
}

void RegisterSnapshotTestReflectionReversed(kb::ecs::World& world) {
    const kb::ecs::ComponentReflection* velocityReflection = world.RegisterComponentReflection<EcsVelocity>(
        "test.EcsVelocity",
        {
            { "x", kb::ecs::ComponentFieldType::Float32, offsetof(EcsVelocity, x), sizeof(float) },
            { "y", kb::ecs::ComponentFieldType::Float32, offsetof(EcsVelocity, y), sizeof(float) },
        });
    const kb::ecs::ComponentReflection* positionReflection = world.RegisterComponentReflection<EcsPosition>(
        "test.EcsPosition",
        {
            { "x", kb::ecs::ComponentFieldType::Float32, offsetof(EcsPosition, x), sizeof(float) },
            { "y", kb::ecs::ComponentFieldType::Float32, offsetof(EcsPosition, y), sizeof(float) },
        });
    kb::tests::Require(positionReflection != nullptr && velocityReflection != nullptr, "ECS snapshot test reflection registration failed");
}

void RegisterSnapshotDeltaTestReflection(kb::ecs::World& world) {
    RegisterSnapshotTestReflection(world);
    const kb::ecs::ComponentReflection* markerReflection = world.RegisterComponentReflection<EcsQueryMarker>(
        "test.EcsQueryMarker",
        {
            { "value", kb::ecs::ComponentFieldType::Int32, offsetof(EcsQueryMarker, value), sizeof(int) },
        });
    kb::tests::Require(markerReflection != nullptr, "ECS delta snapshot marker reflection registration failed");
}

void RegisterSnapshotDeltaTestReflectionReversed(kb::ecs::World& world) {
    RegisterSnapshotTestReflectionReversed(world);
    const kb::ecs::ComponentReflection* markerReflection = world.RegisterComponentReflection<EcsQueryMarker>(
        "test.EcsQueryMarker",
        {
            { "value", kb::ecs::ComponentFieldType::Int32, offsetof(EcsQueryMarker, value), sizeof(int) },
        });
    kb::tests::Require(markerReflection != nullptr, "ECS delta snapshot marker reflection registration failed");
}

struct StreamCaptureContext {
    kb::ecs::ChunkedWorldSnapshotHeader header;
    std::vector<kb::ecs::ChunkedWorldSnapshotChunk> chunks;
};

bool CaptureStreamHeader(const kb::ecs::ChunkedWorldSnapshotHeader& header, void* context) {
    auto* capture = static_cast<StreamCaptureContext*>(context);
    capture->header = header;
    return true;
}

bool CaptureStreamChunk(const kb::ecs::ChunkedWorldSnapshotChunkView& chunk, void* context) {
    auto* capture = static_cast<StreamCaptureContext*>(context);
    kb::ecs::ChunkedWorldSnapshotChunk& stored = capture->chunks.emplace_back();
    stored.archetypeIndex = chunk.archetypeIndex;
    stored.chunkIndex = chunk.chunkIndex;
    stored.entityIds.assign(chunk.entityIds.begin(), chunk.entityIds.end());
    stored.components.reserve(chunk.components.size());
    for (const kb::ecs::ChunkedComponentSnapshotView& component : chunk.components) {
        kb::ecs::ChunkedComponentSnapshot& storedComponent = stored.components.emplace_back();
        storedComponent.componentId = component.componentId;
        storedComponent.componentName = std::string{ component.componentName };
        storedComponent.componentSize = component.componentSize;
        storedComponent.version = component.version;
        storedComponent.data.assign(component.data.begin(), component.data.end());
    }
    return true;
}

struct StreamRestoreContext {
    const std::vector<kb::ecs::ChunkedWorldSnapshotChunk>* chunks = nullptr;
    std::size_t nextChunk = 0;
};

kb::ecs::ChunkedWorldSnapshotStreamReadResult ReadStreamChunk(kb::ecs::ChunkedWorldSnapshotChunk& output, void* context) {
    auto* restore = static_cast<StreamRestoreContext*>(context);
    if (restore->chunks == nullptr) {
        return kb::ecs::ChunkedWorldSnapshotStreamReadResult::Error;
    }
    if (restore->nextChunk >= restore->chunks->size()) {
        return kb::ecs::ChunkedWorldSnapshotStreamReadResult::End;
    }

    output = (*restore->chunks)[restore->nextChunk++];
    return kb::ecs::ChunkedWorldSnapshotStreamReadResult::Chunk;
}

[[nodiscard]] bool DeltaContainsDestroyedEntity(const kb::ecs::ChunkedWorldDeltaSnapshot& delta, kb::ecs::Entity entity) {
    for (kb::ecs::Entity::IdType entityId : delta.destroyedEntityIds) {
        if (entityId == entity.Id()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool DeltaContainsPartialComponentChunk(
    const kb::ecs::ChunkedWorldDeltaSnapshot& delta,
    kb::ecs::ComponentId includedComponent,
    kb::ecs::ComponentId excludedComponent) {
    for (const kb::ecs::ChunkedWorldDeltaSnapshotChunk& chunk : delta.chunks) {
        if (chunk.fullArchetype) {
            continue;
        }

        bool included = false;
        bool excluded = false;
        for (const kb::ecs::ChunkedComponentSnapshot& component : chunk.components) {
            included = included || component.componentId == includedComponent;
            excluded = excluded || component.componentId == excludedComponent;
        }
        if (included && !excluded) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool DeltaContainsFullChunk(const kb::ecs::ChunkedWorldDeltaSnapshot& delta) {
    for (const kb::ecs::ChunkedWorldDeltaSnapshotChunk& chunk : delta.chunks) {
        if (chunk.fullArchetype) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool DeltaContainsOnlyComponentColumns(const kb::ecs::ChunkedWorldDeltaSnapshot& delta, kb::ecs::ComponentId componentId) {
    bool sawChangedColumn = false;
    for (const kb::ecs::ChunkedWorldDeltaSnapshotChunk& chunk : delta.chunks) {
        if (chunk.fullArchetype || chunk.components.empty()) {
            return false;
        }
        for (const kb::ecs::ChunkedComponentSnapshot& component : chunk.components) {
            if (component.componentId != componentId) {
                return false;
            }
            sawChangedColumn = true;
        }
    }
    return sawChangedColumn;
}

void RunWorldSnapshotTest() {
    kb::ecs::World world;
    const kb::ecs::ComponentId positionComponent = world.RegisterComponent<EcsPosition>("test.EcsPosition");
    const kb::ecs::ComponentId velocityComponent = world.RegisterComponent<EcsVelocity>("test.EcsVelocity");

    const kb::ecs::Entity mover = world.CreateEntity("Mover");
    const kb::ecs::Entity staticEntity = world.CreateEntity("Static");
    world.Set(mover, EcsPosition{ .x = 3.0F, .y = 4.0F });
    world.Set(mover, EcsVelocity{ .x = 1.0F, .y = 2.0F });
    world.Set(staticEntity, EcsPosition{ .x = 9.0F, .y = 8.0F });

    const kb::ecs::WorldSnapshot snapshot = world.CaptureSnapshot();
    kb::tests::Require(snapshot.componentTypes.size() == 2, "ECS snapshot did not capture component type registry");
    kb::tests::Require(snapshot.entities.size() == 2, "ECS snapshot did not capture component-owning entities");

    const kb::ecs::EntitySnapshot* moverSnapshot = FindSnapshotEntity(snapshot, mover);
    kb::tests::Require(moverSnapshot != nullptr && moverSnapshot->name == "Mover", "ECS snapshot did not capture entity identity");

    const kb::ecs::ComponentSnapshot* positionSnapshot = FindSnapshotComponent(*moverSnapshot, positionComponent);
    const kb::ecs::ComponentSnapshot* velocitySnapshot = FindSnapshotComponent(*moverSnapshot, velocityComponent);
    kb::tests::Require(positionSnapshot != nullptr && velocitySnapshot != nullptr, "ECS snapshot did not capture all entity components");

    const EcsPosition position = ReadSnapshotComponent<EcsPosition>(*positionSnapshot);
    const EcsVelocity velocity = ReadSnapshotComponent<EcsVelocity>(*velocitySnapshot);
    kb::tests::Require(kb::tests::NearlyEqual(position.x, 3.0F) && kb::tests::NearlyEqual(position.y, 4.0F), "ECS snapshot captured invalid position data");
    kb::tests::Require(kb::tests::NearlyEqual(velocity.x, 1.0F) && kb::tests::NearlyEqual(velocity.y, 2.0F), "ECS snapshot captured invalid velocity data");
}

void RunChunkedWorldSnapshotTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;

    kb::ecs::World world{ config };
    const kb::ecs::ComponentId positionComponent = world.RegisterComponent<EcsPosition>("test.EcsPosition");
    const kb::ecs::ComponentId velocityComponent = world.RegisterComponent<EcsVelocity>("test.EcsVelocity");

    constexpr std::size_t entityCount = 1100;
    kb::ecs::Entity firstEntity;
    for (std::size_t index = 0; index < entityCount; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        if (index == 0U) {
            firstEntity = entity;
        }
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index + 1U) });
        world.Set(entity, EcsVelocity{ .x = static_cast<float>(index + 2U), .y = static_cast<float>(index + 3U) });
    }

    const kb::ecs::ChunkedWorldSnapshot snapshot = world.CaptureChunkedSnapshot();
    kb::tests::Require(snapshot.componentTypes.size() == 2U, "ECS chunked snapshot did not capture component type registry");
    kb::tests::Require(snapshot.entityCount == entityCount, "ECS chunked snapshot did not capture live entity count");
    kb::tests::Require(snapshot.chunks.size() == 2U, "ECS chunked snapshot did not preserve native chunk grouping");

    std::size_t capturedEntities = 0;
    for (const kb::ecs::ChunkedWorldSnapshotChunk& chunk : snapshot.chunks) {
        kb::tests::Require(!chunk.entityIds.empty(), "ECS chunked snapshot captured an empty chunk");
        kb::tests::Require(chunk.components.size() == 2U, "ECS chunked snapshot did not capture archetype component columns");

        const kb::ecs::ChunkedComponentSnapshot* positions = FindChunkComponent(chunk, positionComponent);
        const kb::ecs::ChunkedComponentSnapshot* velocities = FindChunkComponent(chunk, velocityComponent);
        kb::tests::Require(positions != nullptr && velocities != nullptr, "ECS chunked snapshot missed a component column");
        kb::tests::Require(positions->componentName == "test.EcsPosition", "ECS chunked snapshot did not preserve component metadata");
        kb::tests::Require(positions->data.size() == chunk.entityIds.size() * sizeof(EcsPosition), "ECS chunked snapshot position column size mismatch");
        kb::tests::Require(velocities->data.size() == chunk.entityIds.size() * sizeof(EcsVelocity), "ECS chunked snapshot velocity column size mismatch");

        for (std::size_t row = 0; row < chunk.entityIds.size(); ++row) {
            const std::size_t sourceIndex = static_cast<std::size_t>(chunk.entityIds[row] - firstEntity.Id());
            const EcsPosition position = ReadChunkComponent<EcsPosition>(*positions, row);
            const EcsVelocity velocity = ReadChunkComponent<EcsVelocity>(*velocities, row);
            kb::tests::Require(kb::tests::NearlyEqual(position.x, static_cast<float>(sourceIndex)), "ECS chunked snapshot captured invalid position x");
            kb::tests::Require(kb::tests::NearlyEqual(position.y, static_cast<float>(sourceIndex + 1U)), "ECS chunked snapshot captured invalid position y");
            kb::tests::Require(kb::tests::NearlyEqual(velocity.x, static_cast<float>(sourceIndex + 2U)), "ECS chunked snapshot captured invalid velocity x");
            kb::tests::Require(kb::tests::NearlyEqual(velocity.y, static_cast<float>(sourceIndex + 3U)), "ECS chunked snapshot captured invalid velocity y");
        }

        capturedEntities += chunk.entityIds.size();
    }
    kb::tests::Require(capturedEntities == entityCount, "ECS chunked snapshot captured an invalid number of entity ids");
}

void RunChunkedWorldSnapshotStreamRoundTripTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;

    kb::ecs::World source{ config };
    RegisterSnapshotTestReflection(source);

    constexpr std::size_t entityCount = 257;
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(entityCount);
    for (std::size_t index = 0; index < entityCount; ++index) {
        const kb::ecs::Entity entity = source.CreateEntity();
        source.Set(entity, EcsPosition{ .x = static_cast<float>(index + 10U), .y = static_cast<float>(index + 11U) });
        source.Set(entity, EcsVelocity{ .x = static_cast<float>(index + 12U), .y = static_cast<float>(index + 13U) });
        entities.push_back(entity);
    }

    StreamCaptureContext capture;
    kb::tests::Require(
        source.StreamChunkedSnapshot(&CaptureStreamHeader, &CaptureStreamChunk, &capture),
        "ECS chunked snapshot streaming failed");
    kb::tests::Require(capture.header.entityCount == entityCount, "ECS chunked snapshot stream captured invalid entity count");
    kb::tests::Require(!capture.chunks.empty(), "ECS chunked snapshot stream did not emit chunks");

    kb::ecs::World restored{ config };
    RegisterSnapshotTestReflectionReversed(restored);
    StreamRestoreContext restore{
        .chunks = &capture.chunks,
    };
    kb::tests::Require(
        restored.RestoreChunkedSnapshotStream(capture.header, &ReadStreamChunk, &restore),
        "ECS chunked snapshot stream restore failed");

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = entities[index];
        kb::tests::Require(restored.IsAlive(entity), "ECS chunked snapshot stream restore did not preserve entity id");

        const EcsPosition* position = restored.TryGet<EcsPosition>(entity);
        const EcsVelocity* velocity = restored.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && velocity != nullptr, "ECS chunked snapshot stream restore missed components");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, static_cast<float>(index + 10U)), "ECS chunked snapshot stream restored invalid position x");
        kb::tests::Require(kb::tests::NearlyEqual(position->y, static_cast<float>(index + 11U)), "ECS chunked snapshot stream restored invalid position y");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->x, static_cast<float>(index + 12U)), "ECS chunked snapshot stream restored invalid velocity x");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->y, static_cast<float>(index + 13U)), "ECS chunked snapshot stream restored invalid velocity y");
    }
}

void RunChunkedWorldSnapshotBinaryRoundTripTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;

    kb::ecs::World source{ config };
    RegisterSnapshotTestReflection(source);

    constexpr std::size_t entityCount = 389;
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(entityCount);
    for (std::size_t index = 0; index < entityCount; ++index) {
        const kb::ecs::Entity entity = source.CreateEntity();
        source.Set(entity, EcsPosition{ .x = static_cast<float>(index + 20U), .y = static_cast<float>(index + 21U) });
        source.Set(entity, EcsVelocity{ .x = static_cast<float>(index + 22U), .y = static_cast<float>(index + 23U) });
        entities.push_back(entity);
    }

    std::vector<std::byte> bytes;
    kb::tests::Require(source.SerializeChunkedSnapshotBinary(bytes), "ECS binary chunked snapshot serialization failed");
    kb::tests::Require(!bytes.empty(), "ECS binary chunked snapshot did not write bytes");

    std::vector<std::byte> unsupportedVersion = bytes;
    unsupportedVersion[8] = static_cast<std::byte>(2U);
    kb::ecs::World rejected{ config };
    RegisterSnapshotTestReflection(rejected);
    kb::tests::Require(
        !rejected.RestoreChunkedSnapshotBinary(unsupportedVersion),
        "ECS binary chunked snapshot accepted an unsupported format version");

    kb::ecs::World restored{ config };
    RegisterSnapshotTestReflectionReversed(restored);
    kb::tests::Require(
        restored.RestoreChunkedSnapshotBinary(bytes),
        "ECS binary chunked snapshot restore failed");

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = entities[index];
        kb::tests::Require(restored.IsAlive(entity), "ECS binary chunked snapshot restore did not preserve entity id");

        const EcsPosition* position = restored.TryGet<EcsPosition>(entity);
        const EcsVelocity* velocity = restored.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && velocity != nullptr, "ECS binary chunked snapshot restore missed components");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, static_cast<float>(index + 20U)), "ECS binary chunked snapshot restored invalid position x");
        kb::tests::Require(kb::tests::NearlyEqual(position->y, static_cast<float>(index + 21U)), "ECS binary chunked snapshot restored invalid position y");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->x, static_cast<float>(index + 22U)), "ECS binary chunked snapshot restored invalid velocity x");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->y, static_cast<float>(index + 23U)), "ECS binary chunked snapshot restored invalid velocity y");
    }
}

void RunChunkedWorldDeltaSnapshotApplyTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;

    kb::ecs::World source{ config };
    RegisterSnapshotDeltaTestReflection(source);

    constexpr std::size_t entityCount = 64;
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(entityCount);
    for (std::size_t index = 0; index < entityCount; ++index) {
        const kb::ecs::Entity entity = source.CreateEntity();
        source.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index + 1U) });
        source.Set(entity, EcsVelocity{ .x = static_cast<float>(index + 2U), .y = static_cast<float>(index + 3U) });
        entities.push_back(entity);
    }

    const kb::ecs::ChunkedWorldSnapshot baseline = source.CaptureChunkedSnapshot();
    kb::ecs::World target{ config };
    RegisterSnapshotDeltaTestReflectionReversed(target);
    StreamRestoreContext restoreBaseline{
        .chunks = &baseline.chunks,
    };
    kb::ecs::ChunkedWorldSnapshotHeader baselineHeader{
        .componentTypes = baseline.componentTypes,
        .entityCount = baseline.entityCount,
    };
    kb::tests::Require(
        target.RestoreChunkedSnapshotStream(baselineHeader, &ReadStreamChunk, &restoreBaseline),
        "ECS delta snapshot baseline restore failed");

    source.Set(entities[3], EcsPosition{ .x = 500.0F, .y = 501.0F });
    const kb::ecs::ChunkedWorldDeltaSnapshot componentDelta = source.CaptureChunkedDeltaSnapshot(baseline);
    kb::tests::Require(componentDelta.destroyedEntityIds.empty(), "ECS component delta captured destroyed entities");
    kb::tests::Require(
        DeltaContainsPartialComponentChunk(componentDelta, source.Component<EcsPosition>(), source.Component<EcsVelocity>()),
        "ECS component delta did not emit a partial changed component chunk");
    kb::tests::Require(target.ApplyChunkedDeltaSnapshot(componentDelta), "ECS component delta apply failed");

    const EcsPosition* targetChangedPosition = target.TryGet<EcsPosition>(entities[3]);
    const EcsVelocity* targetUnchangedVelocity = target.TryGet<EcsVelocity>(entities[3]);
    kb::tests::Require(targetChangedPosition != nullptr && targetUnchangedVelocity != nullptr, "ECS component delta missed target components");
    kb::tests::Require(
        kb::tests::NearlyEqual(targetChangedPosition->x, 500.0F) && kb::tests::NearlyEqual(targetChangedPosition->y, 501.0F),
        "ECS component delta did not update changed component data");
    kb::tests::Require(
        kb::tests::NearlyEqual(targetUnchangedVelocity->x, 5.0F) && kb::tests::NearlyEqual(targetUnchangedVelocity->y, 6.0F),
        "ECS component delta modified unchanged component data");

    const kb::ecs::ChunkedWorldSnapshot structuralBaseline = source.CaptureChunkedSnapshot();
    source.Remove<EcsVelocity>(entities[5]);
    source.DestroyEntity(entities[7]);
    const kb::ecs::Entity created = source.CreateEntity();
    source.Set(created, EcsPosition{ .x = 700.0F, .y = 701.0F });
    source.Set(created, EcsVelocity{ .x = 702.0F, .y = 703.0F });

    const kb::ecs::ChunkedWorldDeltaSnapshot structuralDelta = source.CaptureChunkedDeltaSnapshot(structuralBaseline);
    kb::tests::Require(DeltaContainsDestroyedEntity(structuralDelta, entities[7]), "ECS structural delta missed destroyed entity");
    kb::tests::Require(DeltaContainsFullChunk(structuralDelta), "ECS structural delta did not emit full archetype chunks");
    kb::tests::Require(target.ApplyChunkedDeltaSnapshot(structuralDelta), "ECS structural delta apply failed");

    kb::tests::Require(!target.IsAlive(entities[7]), "ECS structural delta did not destroy removed entity");
    kb::tests::Require(target.IsAlive(created), "ECS structural delta did not create new entity");
    kb::tests::Require(target.TryGet<EcsVelocity>(entities[5]) == nullptr, "ECS structural delta did not remove absent component");

    const EcsPosition* removedVelocityPosition = target.TryGet<EcsPosition>(entities[5]);
    const EcsPosition* createdPosition = target.TryGet<EcsPosition>(created);
    const EcsVelocity* createdVelocity = target.TryGet<EcsVelocity>(created);
    kb::tests::Require(removedVelocityPosition != nullptr, "ECS structural delta removed retained component");
    kb::tests::Require(createdPosition != nullptr && createdVelocity != nullptr, "ECS structural delta missed created entity components");
    kb::tests::Require(
        kb::tests::NearlyEqual(createdPosition->x, 700.0F) && kb::tests::NearlyEqual(createdPosition->y, 701.0F),
        "ECS structural delta restored invalid created position");
    kb::tests::Require(
        kb::tests::NearlyEqual(createdVelocity->x, 702.0F) && kb::tests::NearlyEqual(createdVelocity->y, 703.0F),
        "ECS structural delta restored invalid created velocity");

    kb::tests::Require(
        target.CaptureChunkedSnapshot().entityCount == source.CaptureChunkedSnapshot().entityCount,
        "ECS structural delta left target entity count out of sync");
}

void RunChunkedWorldDeltaSnapshotAfterBulkComponentWriteTest() {
    kb::ecs::WorldConfig config;
    config.chunkSizeProfile = kb::ecs::ChunkSizeProfile::Chunk16KB;

    kb::ecs::World source{ config };
    RegisterSnapshotTestReflection(source);

    constexpr std::size_t entityCount = 96;
    std::vector<kb::ecs::Entity> entities;
    std::vector<EcsPosition> initialPositions;
    std::vector<EcsVelocity> velocities;
    entities.reserve(entityCount);
    initialPositions.reserve(entityCount);
    velocities.reserve(entityCount);
    for (std::size_t index = 0; index < entityCount; ++index) {
        const kb::ecs::Entity entity = source.CreateEntity();
        entities.push_back(entity);
        initialPositions.push_back(EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index + 1U) });
        velocities.push_back(EcsVelocity{ .x = static_cast<float>(index + 2U), .y = static_cast<float>(index + 3U) });
        source.Set(entity, initialPositions.back());
        source.Set(entity, velocities.back());
    }

    const kb::ecs::ChunkedWorldSnapshot baseline = source.CaptureChunkedSnapshot();
    kb::ecs::World target{ config };
    RegisterSnapshotTestReflectionReversed(target);
    StreamRestoreContext restoreBaseline{
        .chunks = &baseline.chunks,
    };
    const kb::ecs::ChunkedWorldSnapshotHeader baselineHeader{
        .componentTypes = baseline.componentTypes,
        .entityCount = baseline.entityCount,
    };
    kb::tests::Require(
        target.RestoreChunkedSnapshotStream(baselineHeader, &ReadStreamChunk, &restoreBaseline),
        "ECS bulk write delta baseline restore failed");

    std::vector<EcsPosition> updatedPositions;
    updatedPositions.reserve(entityCount);
    for (std::size_t index = 0; index < entityCount; ++index) {
        updatedPositions.push_back(EcsPosition{ .x = static_cast<float>(index + 1000U), .y = static_cast<float>(index + 2000U) });
    }

    kb::ecs::CommandBuffer writeBuffer;
    writeBuffer.Worker(0).Set(std::span<const kb::ecs::Entity>{ entities }, std::span<const EcsPosition>{ updatedPositions });
    static_cast<void>(writeBuffer.Playback(source));

    const kb::ecs::ChunkedWorldDeltaSnapshot delta = source.CaptureChunkedDeltaSnapshot(baseline);
    kb::tests::Require(delta.entityCount == entityCount, "ECS bulk write delta changed entity count");
    kb::tests::Require(delta.destroyedEntityIds.empty(), "ECS bulk write delta captured destroyed entities");
    kb::tests::Require(
        DeltaContainsOnlyComponentColumns(delta, source.Component<EcsPosition>()),
        "ECS bulk write delta included unchanged component columns or full chunks");

    kb::tests::Require(target.ApplyChunkedDeltaSnapshot(delta), "ECS bulk write delta apply failed");
    for (std::size_t index = 0; index < entityCount; ++index) {
        const EcsPosition* position = target.TryGet<EcsPosition>(entities[index]);
        const EcsVelocity* velocity = target.TryGet<EcsVelocity>(entities[index]);
        kb::tests::Require(position != nullptr && velocity != nullptr, "ECS bulk write delta target missed components");
        kb::tests::Require(
            kb::tests::NearlyEqual(position->x, updatedPositions[index].x) && kb::tests::NearlyEqual(position->y, updatedPositions[index].y),
            "ECS bulk write delta did not apply changed position");
        kb::tests::Require(
            kb::tests::NearlyEqual(velocity->x, velocities[index].x) && kb::tests::NearlyEqual(velocity->y, velocities[index].y),
            "ECS bulk write delta modified unchanged velocity");
    }
}

} // namespace

namespace kb::tests {

void RunEcsSnapshotTests() {
    RunWorldSnapshotTest();
    RunChunkedWorldSnapshotTest();
    RunChunkedWorldSnapshotStreamRoundTripTest();
    RunChunkedWorldSnapshotBinaryRoundTripTest();
    RunChunkedWorldDeltaSnapshotApplyTest();
    RunChunkedWorldDeltaSnapshotAfterBulkComponentWriteTest();
}

} // namespace kb::tests
