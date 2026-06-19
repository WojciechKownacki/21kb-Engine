#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/ComponentReflectionMacros.hpp"
#include "engine/ecs/Kernel.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldTelemetryExport.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] bool HasInspectedComponent(const kb::ecs::EntityInspection& inspection, kb::ecs::ComponentId componentId) {
    for (const kb::ecs::EntityComponentInspection& component : inspection.components) {
        if (component.id == componentId) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool HasInspectedArchetypeComponent(const kb::ecs::WorldArchetypeInspection& inspection, kb::ecs::ComponentId componentId) {
    for (const kb::ecs::EntityComponentInspection& component : inspection.components) {
        if (component.id == componentId) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const kb::ecs::WorldArchetypeInspection* FindInspectedArchetype(
    const std::vector<kb::ecs::WorldArchetypeInspection>& inspections,
    kb::ecs::ComponentId firstComponent,
    kb::ecs::ComponentId secondComponent) {
    for (const kb::ecs::WorldArchetypeInspection& inspection : inspections) {
        if (HasInspectedArchetypeComponent(inspection, firstComponent) && HasInspectedArchetypeComponent(inspection, secondComponent)) {
            return &inspection;
        }
    }
    return nullptr;
}

void TouchCompatMutablePosition(kb::ecs::Entity, EcsPosition& position, void*) {
    position.x += 1.0F;
}

void RunEntityInspectionTest() {
    kb::ecs::World world;
    const kb::ecs::ComponentId positionComponent = world.RegisterComponent<EcsPosition>("test.EcsPosition");
    const kb::ecs::ComponentId velocityComponent = world.RegisterComponent<EcsVelocity>("test.EcsVelocity");

    const kb::ecs::Entity parent = world.CreateEntity("Parent");
    const kb::ecs::Entity child = world.CreateEntity("Child");
    world.SetParent(child, parent);
    world.Set(child, EcsPosition{ .x = 1.0F, .y = 2.0F });
    world.Set(child, EcsVelocity{ .x = 3.0F, .y = 4.0F });

    const kb::ecs::EntityInspection inspection = world.InspectEntity(child);
    kb::tests::Require(inspection.entity == child, "ECS inspection returned invalid entity");
    kb::tests::Require(inspection.name == "Child", "ECS inspection returned invalid entity name");
    kb::tests::Require(inspection.parent == parent, "ECS inspection returned invalid parent");
    kb::tests::Require(inspection.components.size() == 2, "ECS inspection returned invalid component count");
    kb::tests::Require(HasInspectedComponent(inspection, positionComponent), "ECS inspection did not include position component");
    kb::tests::Require(HasInspectedComponent(inspection, velocityComponent), "ECS inspection did not include velocity component");
}

void RunWorldArchetypeInspectionTest() {
    kb::ecs::World world;
    const kb::ecs::ComponentId positionComponent = world.RegisterComponent<EcsPosition>("test.EcsPosition");
    const kb::ecs::ComponentId velocityComponent = world.RegisterComponent<EcsVelocity>("test.EcsVelocity");

    const kb::ecs::Entity positionOnly = world.CreateEntity("PositionOnly");
    world.Set(positionOnly, EcsPosition{ .x = 1.0F, .y = 2.0F });

    const kb::ecs::Entity movingA = world.CreateEntity("MovingA");
    const kb::ecs::Entity movingB = world.CreateEntity("MovingB");
    world.Set(movingA, EcsPosition{ .x = 3.0F, .y = 4.0F });
    world.Set(movingA, EcsVelocity{ .x = 5.0F, .y = 6.0F });
    world.Set(movingB, EcsPosition{ .x = 7.0F, .y = 8.0F });
    world.Set(movingB, EcsVelocity{ .x = 9.0F, .y = 10.0F });

    const std::vector<kb::ecs::WorldArchetypeInspection> inspections = world.InspectArchetypes();
    kb::tests::Require(!inspections.empty(), "ECS archetype inspection returned no storage data");

    const kb::ecs::WorldArchetypeInspection* movingInspection = FindInspectedArchetype(inspections, positionComponent, velocityComponent);
    kb::tests::Require(movingInspection != nullptr, "ECS archetype inspection did not include the moving archetype");
    kb::tests::Require(movingInspection->components.size() == 2, "ECS archetype inspection returned invalid component count");
    kb::tests::Require(movingInspection->liveEntities == 2, "ECS archetype inspection returned invalid live entity count");
    kb::tests::Require(movingInspection->chunks >= 1, "ECS archetype inspection returned no chunks");
    kb::tests::Require(movingInspection->capacity >= movingInspection->liveEntities, "ECS archetype inspection returned invalid capacity");
    kb::tests::Require(movingInspection->payloadBytes >= movingInspection->usedBytes, "ECS archetype inspection returned invalid payload bytes");
    kb::tests::Require(movingInspection->occupancyPercent > 0.0 && movingInspection->occupancyPercent <= 100.0, "ECS archetype inspection returned invalid occupancy");
    kb::tests::Require(movingInspection->wastedPercent >= 0.0 && movingInspection->wastedPercent <= 100.0, "ECS archetype inspection returned invalid wasted percent");
    kb::tests::Require(movingInspection->chunkInspections.size() == movingInspection->chunks, "ECS archetype inspection chunk count mismatch");

    std::size_t chunkLiveEntities = 0;
    for (const kb::ecs::WorldArchetypeChunkInspection& chunk : movingInspection->chunkInspections) {
        chunkLiveEntities += chunk.liveEntities;
        kb::tests::Require(chunk.capacity >= chunk.liveEntities, "ECS archetype chunk inspection returned invalid capacity");
        kb::tests::Require(chunk.payloadBytes >= chunk.usedBytes, "ECS archetype chunk inspection returned invalid payload bytes");
    }
    kb::tests::Require(chunkLiveEntities == movingInspection->liveEntities, "ECS archetype chunk inspection did not sum live entities");

    const kb::ecs::EntityComponentInspection& firstComponent = movingInspection->components[0];
    kb::tests::Require(!firstComponent.name.empty(), "ECS archetype inspection did not resolve component names");
    kb::tests::Require(firstComponent.size > 0, "ECS archetype inspection did not resolve component size");
    kb::tests::Require(firstComponent.alignment > 0, "ECS archetype inspection did not resolve component alignment");
}

[[nodiscard]] const kb::ecs::EditorEntityInspection* FindEditorEntity(const kb::ecs::EditorWorldInspection& inspection, std::string_view name) {
    for (const kb::ecs::EditorEntityInspection& entity : inspection.entities) {
        if (entity.name == name) {
            return &entity;
        }
    }
    return nullptr;
}

[[nodiscard]] const kb::ecs::EditorEntityInspection* FindEditorEntity(const kb::ecs::EditorWorldInspection& inspection, kb::ecs::Entity entity) {
    for (const kb::ecs::EditorEntityInspection& inspected : inspection.entities) {
        if (inspected.entity == entity) {
            return &inspected;
        }
    }
    return nullptr;
}

void RegisterPositionReflection(kb::ecs::World& world) {
    [[maybe_unused]] const kb::ecs::ComponentReflection* reflection = world.RegisterComponentReflection<EcsPosition>(
        "test.EcsPosition",
        {
            KB_ECS_FIELD(EcsPosition, x, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(EcsPosition, y, kb::ecs::ComponentFieldType::Float32),
        });
}

void RunEditorWorldInspectionTest() {
    kb::ecs::World world;
    RegisterPositionReflection(world);

    const kb::ecs::Entity empty = world.CreateEntity("Empty");
    const kb::ecs::Entity parent = world.CreateEntity("Parent");
    const kb::ecs::Entity child = world.CreateEntity("Child");
    world.SetParent(child, parent);
    world.Set(child, EcsPosition{ .x = 8.0F, .y = 13.0F });

    kb::ecs::EditorWorldInspection inspection;
    kb::tests::Require(world.CaptureEditorWorld(inspection), "ECS editor inspection failed");
    kb::tests::Require(inspection.entities.size() == 3, "ECS editor inspection did not include all created entities");

    const kb::ecs::EditorEntityInspection* emptyInspection = FindEditorEntity(inspection, "Empty");
    kb::tests::Require(emptyInspection != nullptr && emptyInspection->entity == empty, "ECS editor inspection did not include empty entity");
    kb::tests::Require(emptyInspection->components.empty(), "ECS editor inspection attached components to empty entity");

    const kb::ecs::EditorEntityInspection* childInspection = FindEditorEntity(inspection, "Child");
    kb::tests::Require(childInspection != nullptr && childInspection->parent == parent, "ECS editor inspection returned invalid child hierarchy");
    kb::tests::Require(childInspection->components.size() == 1, "ECS editor inspection returned invalid component count");
    kb::tests::Require(childInspection->components[0].fields.size() == 2, "ECS editor inspection did not serialize reflected fields");

    const float* x = std::get_if<float>(&childInspection->components[0].fields[0].value);
    const float* y = std::get_if<float>(&childInspection->components[0].fields[1].value);
    kb::tests::Require(x != nullptr && y != nullptr, "ECS editor inspection stored invalid field value type");
    kb::tests::Require(kb::tests::NearlyEqual(*x, 8.0F) && kb::tests::NearlyEqual(*y, 13.0F), "ECS editor inspection stored invalid field values");
}

void RunEditorComponentApplyTest() {
    kb::ecs::World world;
    RegisterPositionReflection(world);

    const kb::ecs::Entity entity = world.CreateEntity("Editable");
    world.Set(entity, EcsPosition{ .x = 1.0F, .y = 2.0F });

    kb::ecs::SerializedComponent component;
    kb::tests::Require(world.SerializeComponent(entity, world.Component<EcsPosition>(), component), "ECS editor component serialization failed");
    component.fields[0].value = 21.0F;

    kb::tests::Require(world.ApplySerializedComponent(entity, component), "ECS editor component apply failed");
    const EcsPosition* position = world.TryGet<EcsPosition>(entity);
    kb::tests::Require(position != nullptr, "ECS editor component apply removed component");
    kb::tests::Require(kb::tests::NearlyEqual(position->x, 21.0F), "ECS editor component apply did not update field");
    kb::tests::Require(kb::tests::NearlyEqual(position->y, 2.0F), "ECS editor component apply did not preserve untouched field");
}

void RunEditorEntityNamingAndChildrenTest() {
    kb::ecs::World world;

    const kb::ecs::Entity parent = world.CreateEntity("Parent");
    const kb::ecs::Entity child = world.CreateEntity("Child");
    world.SetParent(child, parent);
    world.SetName(child, "Renamed");

    const std::vector<kb::ecs::Entity> children = world.Children(parent);
    kb::tests::Require(children.size() == 1 && children[0] == child, "ECS hierarchy children API returned invalid result");
    kb::tests::Require(world.Name(child) == "Renamed", "ECS entity rename API did not update name");

    kb::ecs::EditorWorldInspection inspection;
    kb::tests::Require(world.CaptureEditorWorld(inspection), "ECS editor inspection after rename failed");
    kb::tests::Require(FindEditorEntity(inspection, "Renamed") != nullptr, "ECS editor inspection did not use renamed entity");
}

void RunEditorBulkCreateCatalogTest() {
    kb::ecs::World world;
    RegisterPositionReflection(world);

    std::vector<EcsPosition> positions{
        EcsPosition{ .x = 1.0F, .y = 2.0F },
        EcsPosition{ .x = 3.0F, .y = 4.0F },
        EcsPosition{ .x = 5.0F, .y = 6.0F },
        EcsPosition{ .x = 7.0F, .y = 8.0F },
    };
    const kb::ecs::World::BulkComponentView positionView = kb::ecs::World::MakeBulkComponentView<EcsPosition>(positions);
    const std::vector<kb::ecs::Entity> entities = world.CreateEntities(positions.size(), std::span<const kb::ecs::World::BulkComponentView>{ &positionView, 1U });

    kb::ecs::EditorWorldInspection inspection;
    kb::tests::Require(world.CaptureEditorWorld(inspection), "ECS editor inspection after bulk create failed");
    kb::tests::Require(inspection.entities.size() == entities.size(), "ECS editor inspection did not include all bulk-created entities");
    for (kb::ecs::Entity entity : entities) {
        kb::tests::Require(FindEditorEntity(inspection, entity) != nullptr, "ECS editor inspection missed a bulk-created entity");
    }
}

void RunWorldTelemetrySnapshotTest() {
    kb::ecs::World world;
    const kb::ecs::WorldTelemetrySnapshot emptySnapshot = world.TelemetrySnapshot();
    kb::tests::Require(emptySnapshot.entityCount == 0, "ECS telemetry reported live entities in an empty world");
    kb::tests::Require(emptySnapshot.chunkSizeProfile == "32KB", "ECS telemetry reported invalid default chunk size profile");
    kb::tests::Require(emptySnapshot.chunkPayloadBytes == kb::ecs::ChunkPayloadBytes(kb::ecs::ChunkSizeProfile::Chunk32KB), "ECS telemetry reported invalid default chunk payload bytes");
    kb::tests::Require(emptySnapshot.queryPlanRequests == 0, "ECS telemetry reported query work before a query was created");
    kb::tests::Require(emptySnapshot.queryPlanBuilds == 0, "ECS telemetry reported query plan builds in an empty world");
    kb::tests::Require(emptySnapshot.queryAveragePlanCacheLookupNanoseconds == 0.0, "ECS telemetry reported query cache lookup time in an empty world");
    kb::tests::Require(emptySnapshot.queryAveragePlanBuildNanoseconds == 0.0, "ECS telemetry reported query plan build time in an empty world");
    kb::tests::Require(emptySnapshot.queryRecordCacheHits == 0, "ECS telemetry reported query record cache hits in an empty world");
    kb::tests::Require(emptySnapshot.queryRecordCacheMisses == 0, "ECS telemetry reported query record cache misses in an empty world");
    kb::tests::Require(emptySnapshot.queryRecordCacheHitPercent == 0.0, "ECS telemetry reported query record cache hit percent in an empty world");
    kb::tests::Require(emptySnapshot.queryRecordCacheMissPercent == 0.0, "ECS telemetry reported query record cache miss percent in an empty world");
    kb::tests::Require(emptySnapshot.sparseChunkPercent == 0.0, "ECS telemetry reported sparse chunk pressure in an empty world");
    kb::tests::Require(emptySnapshot.queryEstimatedGigabytesPerSecond == 0.0, "ECS telemetry reported query bandwidth in an empty world");
    kb::tests::Require(emptySnapshot.queryKernelEstimatedGigabytesPerSecond == 0.0, "ECS telemetry reported query kernel bandwidth in an empty world");
    kb::tests::Require(emptySnapshot.queryAverageBytesPerEntity == 0.0, "ECS telemetry reported query bytes per entity in an empty world");
    kb::tests::Require(emptySnapshot.allocatedBytes == 0U, "ECS telemetry reported active storage bytes in an empty world");
    kb::tests::Require(emptySnapshot.sidePayloadBytes == 0U, "ECS telemetry reported side payload bytes in an empty world");
    kb::tests::Require(emptySnapshot.committedBytes == 0U, "ECS telemetry reported committed storage bytes in an empty world");
    kb::tests::Require(emptySnapshot.freeBytes == 0U, "ECS telemetry reported free storage bytes in an empty world");
    kb::tests::Require(emptySnapshot.hotOnlyChunkCapacity == 0U, "ECS telemetry reported hot-only capacity in an empty world");
    kb::tests::Require(emptySnapshot.capacityLostToNonHotStorage == 0U, "ECS telemetry reported non-hot capacity loss in an empty world");
    kb::tests::Require(emptySnapshot.peakCommittedBytes == 0U, "ECS telemetry reported peak storage bytes in an empty world");
    kb::tests::Require(emptySnapshot.chunkMetadataBytes == 0U, "ECS telemetry reported chunk metadata bytes in an empty world");
    kb::tests::Require(emptySnapshot.entityRecordBytes == 0U, "ECS telemetry reported entity record bytes in an empty world");
    kb::tests::Require(emptySnapshot.trackedBytes == 0U, "ECS telemetry reported tracked storage bytes in an empty world");
    kb::tests::Require(emptySnapshot.hotTableComponents == 0U, "ECS telemetry reported hot table component metadata in an empty world");
    kb::tests::Require(emptySnapshot.coldTableComponents == 0U, "ECS telemetry reported cold table component metadata in an empty world");
    kb::tests::Require(emptySnapshot.sparseTagComponents == 0U, "ECS telemetry reported sparse tag component metadata in an empty world");
    kb::tests::Require(emptySnapshot.sparsePayloadComponents == 0U, "ECS telemetry reported sparse payload component metadata in an empty world");
    kb::tests::Require(emptySnapshot.sharedValueComponents == 0U, "ECS telemetry reported shared value component metadata in an empty world");
    kb::tests::Require(emptySnapshot.externalBlobComponents == 0U, "ECS telemetry reported external blob component metadata in an empty world");
    kb::tests::Require(emptySnapshot.hotTableUsedBytes == 0U, "ECS telemetry reported hot table used bytes in an empty world");
    kb::tests::Require(emptySnapshot.hotTableCapacityBytes == 0U, "ECS telemetry reported hot table capacity bytes in an empty world");
    kb::tests::Require(emptySnapshot.coldTableUsedBytes == 0U, "ECS telemetry reported cold table used bytes in an empty world");
    kb::tests::Require(emptySnapshot.coldTableCapacityBytes == 0U, "ECS telemetry reported cold table capacity bytes in an empty world");
    kb::tests::Require(emptySnapshot.storageSystemAllocationCount == 0U, "ECS telemetry reported storage system allocations in an empty world");
    kb::tests::Require(emptySnapshot.archetypeTransitionInvalidationsSinceReset == 0U, "ECS telemetry reported archetype transition invalidations in an empty world");
    kb::tests::Require(emptySnapshot.totalArchetypeTransitionInvalidations == 0U, "ECS telemetry reported total archetype transition invalidations in an empty world");
    const kb::ecs::KernelBackend preferredKernelBackend = kb::ecs::PreferredKernelBackend();
    kb::tests::Require(emptySnapshot.preferredKernelBackend == kb::ecs::KernelBackendName(preferredKernelBackend), "ECS telemetry did not report the preferred kernel backend");
    kb::tests::Require(emptySnapshot.preferredKernelFloatLanes == kb::ecs::KernelBackendFloatLaneCount(preferredKernelBackend), "ECS telemetry reported invalid preferred backend lane count");
    kb::tests::Require(emptySnapshot.preferredKernelBackendCompiled == kb::ecs::IsKernelBackendCompiled(preferredKernelBackend), "ECS telemetry reported invalid preferred backend compile status");
    kb::tests::Require(emptySnapshot.preferredKernelBackendSupported == kb::ecs::IsKernelBackendSupported(preferredKernelBackend), "ECS telemetry reported invalid preferred backend hardware status");
    kb::tests::Require(emptySnapshot.preferredKernelBackendAutoSelectable == kb::ecs::IsKernelBackendAutoSelectable(preferredKernelBackend), "ECS telemetry reported invalid preferred backend auto-select status");
    kb::tests::Require(emptySnapshot.avx2KernelBackendCompiled == kb::ecs::IsKernelBackendCompiled(kb::ecs::KernelBackend::Avx2), "ECS telemetry reported invalid AVX2 compile status");
    kb::tests::Require(emptySnapshot.avx2KernelBackendSupported == kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx2), "ECS telemetry reported invalid AVX2 hardware status");
    kb::tests::Require(emptySnapshot.avx2KernelBackendAutoSelectable == kb::ecs::IsKernelBackendAutoSelectable(kb::ecs::KernelBackend::Avx2), "ECS telemetry reported invalid AVX2 auto-select status");

    const kb::ecs::Entity entity = world.CreateEntity("Telemetry");
    world.Set(entity, EcsPosition{ .x = 1.0F, .y = 2.0F });
    world.Set(entity, EcsVelocity{ .x = 3.0F, .y = 4.0F });
    world.Set(entity, EcsPosition{ .x = 5.0F, .y = 6.0F });

    kb::ecs::Query<EcsPosition, EcsVelocity> firstQuery = world.CreateQuery<EcsPosition, EcsVelocity>();
    [[maybe_unused]] kb::ecs::Query<EcsPosition, EcsVelocity> secondQuery = world.CreateQuery<EcsPosition, EcsVelocity>();
    kb::ecs::WorkerPool workerPool{ kb::ecs::WorkerPoolConfig{ .workerCount = 2 } };
    firstQuery.ForEachBatchKernel(
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 1,
            .policy = kb::ecs::QueryExecutionPolicy::ParallelRanges,
            .workerCountOverride = 1,
            .workerPool = &workerPool,
            .telemetryEnabled = true,
        },
        [](const kb::ecs::QueryBatch<EcsPosition, EcsVelocity>& batch) {
            kb::tests::Require(batch.Count() == 1, "ECS telemetry query test did not use configured range size");
        });
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    world.ForEachMutable<EcsPosition>(&TouchCompatMutablePosition, nullptr);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    const kb::ecs::WorldTelemetrySnapshot snapshot = world.TelemetrySnapshot();
    const kb::ecs::NativeEcsStorageStats storageStats = world.NativeStorageStats();
    kb::tests::Require(snapshot.entityCount == 1, "ECS telemetry reported invalid live entity count");
    kb::tests::Require(snapshot.archetypeCount >= 1, "ECS telemetry did not report archetypes");
    kb::tests::Require(snapshot.chunkSizeProfile == "32KB", "ECS telemetry lost chunk profile metadata");
    kb::tests::Require(snapshot.chunkPayloadBytes == world.NativeChunkPayloadBytes(), "ECS telemetry lost chunk payload byte metadata");
    kb::tests::Require(snapshot.chunkCount >= 1, "ECS telemetry did not report chunks");
    kb::tests::Require(snapshot.chunkCapacity >= snapshot.entityCount, "ECS telemetry reported invalid chunk capacity");
    kb::tests::Require(snapshot.hotOnlyChunkCapacity == storageStats.hotOnlyCapacity, "ECS telemetry did not mirror hot-only chunk capacity");
    kb::tests::Require(snapshot.hotOnlyChunkCapacity >= snapshot.chunkCapacity, "ECS telemetry reported invalid hot-only chunk capacity");
    kb::tests::Require(snapshot.capacityLostToNonHotStorage == storageStats.capacityLostToNonHotStorage, "ECS telemetry did not mirror non-hot capacity loss");
    kb::tests::Require(snapshot.capacityLostToNonHotStorage == 0U, "ECS telemetry reported non-hot capacity loss for an all-hot archetype");
    kb::tests::Require(snapshot.sparseChunkCount <= snapshot.chunkCount, "ECS telemetry reported invalid sparse chunk count");
    kb::tests::Require(snapshot.tailSparseChunkCount + snapshot.fragmentedChunkCount == snapshot.sparseChunkCount, "ECS telemetry sparse chunk breakdown is inconsistent");
    kb::tests::Require(snapshot.sparseChunkPercent >= snapshot.tailSparseChunkPercent, "ECS telemetry reported invalid tail sparse chunk percent");
    kb::tests::Require(snapshot.sparseChunkPercent >= snapshot.fragmentedChunkPercent, "ECS telemetry reported invalid fragmented chunk percent");
    kb::tests::Require(snapshot.emptyChunkPercent == 0.0, "ECS telemetry reported empty chunk pressure after compaction");
    kb::tests::Require(snapshot.fragmentedChunkCount == 0, "ECS telemetry reported interior chunk fragmentation after compaction");
    kb::tests::Require(snapshot.emptyChunkCount == 0, "ECS telemetry reported empty chunks still in use after compaction");
    kb::tests::Require(snapshot.chunkPoolAllocated >= snapshot.chunkPoolInUse, "ECS telemetry reported invalid chunk pool allocation");
    kb::tests::Require(snapshot.chunkPoolInUse == snapshot.chunkCount, "ECS telemetry chunk pool in-use count did not match storage chunk count");
    kb::tests::Require(snapshot.chunkPoolAcquireCount >= snapshot.chunkPoolInUse, "ECS telemetry reported invalid chunk pool acquire count");
    kb::tests::Require(snapshot.chunkPoolSystemAllocationCount >= snapshot.chunkPoolAllocated, "ECS telemetry reported invalid chunk pool system allocation count");
    kb::tests::Require(snapshot.chunkPoolPeakAllocated >= snapshot.chunkPoolAllocated, "ECS telemetry reported invalid chunk pool peak allocation");
    kb::tests::Require(snapshot.bytesPerEntity >= sizeof(EcsPosition) + sizeof(EcsVelocity), "ECS telemetry reported invalid bytes per entity");
    kb::tests::Require(snapshot.allocatedBytes >= snapshot.usedBytes, "ECS telemetry reported invalid allocated bytes");
    kb::tests::Require(snapshot.sidePayloadBytes == storageStats.activeSidePayloadBytes, "ECS telemetry did not mirror side payload bytes");
    kb::tests::Require(snapshot.committedBytes >= snapshot.allocatedBytes, "ECS telemetry reported committed bytes below active bytes");
    kb::tests::Require(snapshot.committedBytes == snapshot.allocatedBytes + snapshot.freeBytes, "ECS telemetry reported invalid storage byte split");
    kb::tests::Require(snapshot.peakCommittedBytes >= snapshot.committedBytes, "ECS telemetry reported peak committed bytes below current committed bytes");
    kb::tests::Require(snapshot.chunkMetadataBytes >= snapshot.chunkCount * sizeof(kb::ecs::Entity), "ECS telemetry reported invalid chunk metadata bytes");
    kb::tests::Require(snapshot.entityRecordBytes >= snapshot.entityCount * sizeof(kb::ecs::Entity), "ECS telemetry reported invalid entity record bytes");
    kb::tests::Require(
        snapshot.trackedBytes == snapshot.committedBytes + snapshot.sidePayloadBytes + snapshot.chunkMetadataBytes + snapshot.entityRecordBytes,
        "ECS telemetry reported invalid tracked byte total");
    kb::tests::Require(snapshot.hotTableComponents == storageStats.hotTableComponents, "ECS telemetry did not mirror hot table component metadata");
    kb::tests::Require(snapshot.hotTableComponents >= 2U, "ECS telemetry did not report hot table component metadata");
    kb::tests::Require(snapshot.coldTableComponents == storageStats.coldTableComponents, "ECS telemetry did not mirror cold table component metadata");
    kb::tests::Require(snapshot.sparseTagComponents == storageStats.sparseTagComponents, "ECS telemetry did not mirror sparse tag component metadata");
    kb::tests::Require(snapshot.sparsePayloadComponents == storageStats.sparsePayloadComponents, "ECS telemetry did not mirror sparse payload component metadata");
    kb::tests::Require(snapshot.sharedValueComponents == storageStats.sharedValueComponents, "ECS telemetry did not mirror shared value component metadata");
    kb::tests::Require(snapshot.externalBlobComponents == storageStats.externalBlobComponents, "ECS telemetry did not mirror external blob component metadata");
    kb::tests::Require(snapshot.hotTableUsedBytes == storageStats.hotTableUsedBytes, "ECS telemetry did not mirror hot table used bytes");
    kb::tests::Require(snapshot.hotTableUsedBytes >= sizeof(EcsPosition) + sizeof(EcsVelocity), "ECS telemetry reported invalid hot table used bytes");
    kb::tests::Require(snapshot.hotTableCapacityBytes == storageStats.hotTableCapacityBytes, "ECS telemetry did not mirror hot table capacity bytes");
    kb::tests::Require(snapshot.coldTableUsedBytes == storageStats.coldTableUsedBytes, "ECS telemetry did not mirror cold table used bytes");
    kb::tests::Require(snapshot.coldTableCapacityBytes == storageStats.coldTableCapacityBytes, "ECS telemetry did not mirror cold table capacity bytes");
    kb::tests::Require(snapshot.storageSystemAllocationCount == snapshot.chunkPoolSystemAllocationCount, "ECS telemetry duplicated allocator counters inconsistently");
    kb::tests::Require(snapshot.storageSystemAllocationsSinceReset == snapshot.storageSystemAllocationCount, "ECS telemetry reported invalid allocator frame count before reset");
    kb::tests::Require(
        std::fabs((snapshot.occupancyPercent + snapshot.fragmentationPercent) - 100.0) <= 0.0001,
        "ECS telemetry occupancy and fragmentation did not sum to a full chunk budget");
    kb::tests::Require(snapshot.queryPlanRequests == 2, "ECS telemetry did not count query plan requests");
    kb::tests::Require(snapshot.queryCacheHits == 1, "ECS telemetry did not count query plan cache hits");
    kb::tests::Require(snapshot.queryCacheMisses == 1, "ECS telemetry did not count query plan cache misses");
    kb::tests::Require(snapshot.queryPlanBuilds == 1, "ECS telemetry did not count query plan builds");
    kb::tests::Require(snapshot.queryPlanCacheLookupElapsedNanoseconds > 0, "ECS telemetry did not count query plan cache lookup time");
    kb::tests::Require(snapshot.queryPlanBuildElapsedNanoseconds > 0, "ECS telemetry did not count query plan build time");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryCacheHitPercent), 50.0F), "ECS telemetry reported invalid query cache hit percent");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryCacheMissPercent), 50.0F), "ECS telemetry reported invalid query cache miss percent");
    kb::tests::Require(snapshot.queryAveragePlanCacheLookupNanoseconds > 0.0, "ECS telemetry did not report average query cache lookup time");
    kb::tests::Require(snapshot.queryAveragePlanBuildNanoseconds > 0.0, "ECS telemetry did not report average query plan build time");
    kb::tests::Require(snapshot.queryRecordCacheHits == 0, "ECS telemetry counted a query record cache hit before reuse");
    kb::tests::Require(snapshot.queryRecordCacheMisses == 1, "ECS telemetry did not count the initial query record cache miss");
    kb::tests::Require(snapshot.queryRecordCacheHitPercent == 0.0, "ECS telemetry reported invalid initial query record cache hit percent");
    kb::tests::Require(snapshot.queryRecordCacheMissPercent == 100.0, "ECS telemetry reported invalid initial query record cache miss percent");
    kb::tests::Require(snapshot.queryExecutions == 1, "ECS telemetry did not count query executions");
    kb::tests::Require(snapshot.queryBatches == 1, "ECS telemetry did not count query batches");
    kb::tests::Require(snapshot.queryEntitiesVisited == 1, "ECS telemetry did not count query visited entities");
    kb::tests::Require(snapshot.queryBytesTouched == sizeof(EcsPosition) + sizeof(EcsVelocity), "ECS telemetry did not count query bytes touched");
    kb::tests::Require(snapshot.queryElapsedNanoseconds > 0, "ECS telemetry did not count query elapsed time");
    kb::tests::Require(snapshot.queryPrepareCalls == 1, "ECS telemetry did not count query prepare calls");
    kb::tests::Require(snapshot.queryPrepareRecords == 1, "ECS telemetry did not count prepared query records");
    kb::tests::Require(snapshot.queryMatchedChunks == 1, "ECS telemetry did not count query matched chunks");
    kb::tests::Require(snapshot.queryMatchedArchetypes == 1, "ECS telemetry did not count query matched archetypes");
    kb::tests::Require(snapshot.queryPrepareElapsedNanoseconds > 0, "ECS telemetry did not count query prepare time");
    kb::tests::Require(snapshot.queryKernelElapsedNanoseconds > 0, "ECS telemetry did not count query kernel time");
    kb::tests::Require(snapshot.queryElapsedNanoseconds >= snapshot.queryKernelElapsedNanoseconds, "ECS telemetry reported kernel time above executor time");
    kb::tests::Require(snapshot.queryAdaptiveExecutions == 0, "ECS telemetry incorrectly counted adaptive query execution");
    kb::tests::Require(snapshot.queryEffectiveBatchSizeTotal == 1, "ECS telemetry did not count effective query batch size");
    kb::tests::Require(snapshot.queryMaxEffectiveBatchSize == 1, "ECS telemetry did not report max effective query batch size");
    kb::tests::Require(snapshot.querySingleThreadExecutions == 0, "ECS telemetry incorrectly counted single-thread query policy");
    kb::tests::Require(snapshot.queryParallelChunkExecutions == 0, "ECS telemetry incorrectly counted chunk query policy");
    kb::tests::Require(snapshot.queryParallelRangeExecutions == 1, "ECS telemetry did not count range query policy");
    kb::tests::Require(snapshot.querySimdPreferredExecutions == 0, "ECS telemetry incorrectly counted SIMD-preferred query policy");
    kb::tests::Require(snapshot.queryDeterministicExecutions == 0, "ECS telemetry incorrectly counted deterministic query policy");
    kb::tests::Require(snapshot.queryEstimatedBytesPerSecond > 0.0, "ECS telemetry did not estimate query bandwidth");
    kb::tests::Require(snapshot.queryEstimatedGigabytesPerSecond > 0.0, "ECS telemetry did not estimate query bandwidth in GB/s");
    kb::tests::Require(snapshot.queryKernelEstimatedBytesPerSecond > 0.0, "ECS telemetry did not estimate query kernel bandwidth");
    kb::tests::Require(snapshot.queryKernelEstimatedGigabytesPerSecond > 0.0, "ECS telemetry did not estimate query kernel bandwidth in GB/s");
    kb::tests::Require(
        kb::tests::NearlyEqual(static_cast<float>(snapshot.queryAverageBytesPerEntity), static_cast<float>(sizeof(EcsPosition) + sizeof(EcsVelocity))),
        "ECS telemetry did not report query bytes per entity");
    kb::tests::Require(snapshot.queryAveragePrepareNanoseconds > 0.0, "ECS telemetry did not report average query prepare time");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryAverageMatchedChunks), 1.0F), "ECS telemetry did not report average matched chunks");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryAverageMatchedArchetypes), 1.0F), "ECS telemetry did not report average matched archetypes");
    kb::tests::Require(snapshot.queryAverageKernelNanoseconds > 0.0, "ECS telemetry did not report average query kernel time");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryAverageEffectiveBatchSize), 1.0F), "ECS telemetry did not report average effective query batch size");
    kb::tests::Require(snapshot.queryPrefetchDistanceTotal == 0, "ECS telemetry counted a prefetch distance for the default world profile");
    kb::tests::Require(snapshot.queryAveragePrefetchDistance == 0.0, "ECS telemetry reported invalid default prefetch average");
    kb::tests::Require(snapshot.queryParallelExecutions == 1, "ECS telemetry did not count parallel query executions");
    kb::tests::Require(snapshot.queryWorkerSlots == 1, "ECS telemetry did not respect query worker slot override");
    kb::tests::Require(snapshot.queryWorkerActiveSlots == 1, "ECS telemetry did not count active query worker slots");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryWorkerUtilizationPercent), 100.0F), "ECS telemetry reported invalid query worker utilization");
    kb::tests::Require(snapshot.compatMutableIterations == 1, "ECS telemetry did not count compatibility mutable iteration calls");
    kb::tests::Require(snapshot.compatMutableEntitiesVisited == 1, "ECS telemetry did not count compatibility mutable visited entities");
    kb::tests::Require(snapshot.structuralChangesSinceReset == 3, "ECS telemetry reported invalid frame structural change count");
    kb::tests::Require(snapshot.totalStructuralChanges == 3, "ECS telemetry reported invalid total structural change count");
    kb::tests::Require(snapshot.archetypeTransitionInvalidationsSinceReset == 2, "ECS telemetry reported invalid frame archetype transition invalidation count");
    kb::tests::Require(snapshot.totalArchetypeTransitionInvalidations == 2, "ECS telemetry reported invalid total archetype transition invalidation count");

    const std::string telemetryJson = kb::ecs::WorldTelemetrySnapshotToJson(snapshot);
    kb::tests::Require(telemetryJson.find("\"schema\": \"kb.ecs.world_telemetry.v1\"") != std::string::npos, "ECS telemetry JSON export omitted schema");
    kb::tests::Require(telemetryJson.find("\"chunk_size_profile\"") != std::string::npos, "ECS telemetry JSON export omitted chunk profile");
    kb::tests::Require(telemetryJson.find("\"chunk_payload_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted chunk payload bytes");
    kb::tests::Require(telemetryJson.find("\"hot_only_chunk_capacity\"") != std::string::npos, "ECS telemetry JSON export omitted hot-only chunk capacity");
    kb::tests::Require(telemetryJson.find("\"capacity_lost_to_non_hot_storage\"") != std::string::npos, "ECS telemetry JSON export omitted non-hot capacity loss");
    kb::tests::Require(telemetryJson.find("\"side_payload_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted side payload bytes");
    kb::tests::Require(telemetryJson.find("\"committed_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted committed bytes");
    kb::tests::Require(telemetryJson.find("\"peak_committed_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted peak committed bytes");
    kb::tests::Require(telemetryJson.find("\"chunk_metadata_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted chunk metadata bytes");
    kb::tests::Require(telemetryJson.find("\"entity_record_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted entity record bytes");
    kb::tests::Require(telemetryJson.find("\"tracked_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted tracked bytes");
    kb::tests::Require(telemetryJson.find("\"hot_table_components\"") != std::string::npos, "ECS telemetry JSON export omitted hot table component metadata");
    kb::tests::Require(telemetryJson.find("\"cold_table_components\"") != std::string::npos, "ECS telemetry JSON export omitted cold table component metadata");
    kb::tests::Require(telemetryJson.find("\"sparse_tag_components\"") != std::string::npos, "ECS telemetry JSON export omitted sparse tag component metadata");
    kb::tests::Require(telemetryJson.find("\"sparse_payload_components\"") != std::string::npos, "ECS telemetry JSON export omitted sparse payload component metadata");
    kb::tests::Require(telemetryJson.find("\"shared_value_components\"") != std::string::npos, "ECS telemetry JSON export omitted shared value component metadata");
    kb::tests::Require(telemetryJson.find("\"external_blob_components\"") != std::string::npos, "ECS telemetry JSON export omitted external blob component metadata");
    kb::tests::Require(telemetryJson.find("\"hot_table_used_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted hot table used bytes");
    kb::tests::Require(telemetryJson.find("\"cold_table_used_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted cold table used bytes");
    kb::tests::Require(telemetryJson.find("\"sparse_tag_used_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted sparse tag used bytes");
    kb::tests::Require(telemetryJson.find("\"sparse_payload_used_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted sparse payload used bytes");
    kb::tests::Require(telemetryJson.find("\"shared_value_used_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted shared value used bytes");
    kb::tests::Require(telemetryJson.find("\"external_blob_used_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted external blob used bytes");
    kb::tests::Require(telemetryJson.find("\"hot_table_capacity_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted hot table capacity bytes");
    kb::tests::Require(telemetryJson.find("\"cold_table_capacity_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted cold table capacity bytes");
    kb::tests::Require(telemetryJson.find("\"sparse_tag_capacity_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted sparse tag capacity bytes");
    kb::tests::Require(telemetryJson.find("\"sparse_payload_capacity_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted sparse payload capacity bytes");
    kb::tests::Require(telemetryJson.find("\"shared_value_capacity_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted shared value capacity bytes");
    kb::tests::Require(telemetryJson.find("\"external_blob_capacity_bytes\"") != std::string::npos, "ECS telemetry JSON export omitted external blob capacity bytes");
    kb::tests::Require(telemetryJson.find("\"storage_system_allocations_since_reset\"") != std::string::npos, "ECS telemetry JSON export omitted allocator frame count");
    kb::tests::Require(telemetryJson.find("\"query_plan_build_elapsed_nanoseconds\"") != std::string::npos, "ECS telemetry JSON export omitted query plan build time");
    kb::tests::Require(telemetryJson.find("\"query_average_plan_cache_lookup_nanoseconds\"") != std::string::npos, "ECS telemetry JSON export omitted query cache lookup average");
    kb::tests::Require(telemetryJson.find("\"query_record_cache_hits\"") != std::string::npos, "ECS telemetry JSON export omitted query record cache hits");
    kb::tests::Require(telemetryJson.find("\"query_record_cache_misses\"") != std::string::npos, "ECS telemetry JSON export omitted query record cache misses");
    kb::tests::Require(telemetryJson.find("\"query_record_cache_hit_percent\"") != std::string::npos, "ECS telemetry JSON export omitted query record cache hit percent");
    kb::tests::Require(telemetryJson.find("\"query_record_cache_miss_percent\"") != std::string::npos, "ECS telemetry JSON export omitted query record cache miss percent");
    kb::tests::Require(telemetryJson.find("\"query_prepare_elapsed_nanoseconds\"") != std::string::npos, "ECS telemetry JSON export omitted query prepare time");
    kb::tests::Require(telemetryJson.find("\"query_matched_chunks\"") != std::string::npos, "ECS telemetry JSON export omitted matched chunks");
    kb::tests::Require(telemetryJson.find("\"query_average_matched_archetypes\"") != std::string::npos, "ECS telemetry JSON export omitted average matched archetypes");
    kb::tests::Require(telemetryJson.find("\"query_kernel_elapsed_nanoseconds\"") != std::string::npos, "ECS telemetry JSON export omitted query kernel time");
    kb::tests::Require(telemetryJson.find("\"query_average_kernel_nanoseconds\"") != std::string::npos, "ECS telemetry JSON export omitted average query kernel time");
    kb::tests::Require(telemetryJson.find("\"query_adaptive_executions\"") != std::string::npos, "ECS telemetry JSON export omitted adaptive query count");
    kb::tests::Require(telemetryJson.find("\"query_max_effective_batch_size\"") != std::string::npos, "ECS telemetry JSON export omitted max effective batch size");
    kb::tests::Require(telemetryJson.find("\"query_average_effective_batch_size\"") != std::string::npos, "ECS telemetry JSON export omitted average effective batch size");
    kb::tests::Require(telemetryJson.find("\"query_parallel_range_executions\"") != std::string::npos, "ECS telemetry JSON export omitted query policy count");
    kb::tests::Require(telemetryJson.find("\"query_estimated_gigabytes_per_second\"") != std::string::npos, "ECS telemetry JSON export omitted GB/s");
    kb::tests::Require(telemetryJson.find("\"query_kernel_estimated_gigabytes_per_second\"") != std::string::npos, "ECS telemetry JSON export omitted query kernel GB/s");
    kb::tests::Require(telemetryJson.find("\"query_average_bytes_per_entity\"") != std::string::npos, "ECS telemetry JSON export omitted bytes/entity");
    kb::tests::Require(telemetryJson.find("\"sparse_chunk_percent\"") != std::string::npos, "ECS telemetry JSON export omitted chunk pressure");
    kb::tests::Require(telemetryJson.find("\"preferred_kernel_backend\"") != std::string::npos, "ECS telemetry JSON export omitted preferred kernel backend");
    kb::tests::Require(telemetryJson.find("\"avx2_kernel_backend_auto_selectable\"") != std::string::npos, "ECS telemetry JSON export omitted AVX2 backend auto-select status");
    kb::tests::Require(telemetryJson.find("\"archetype_transition_invalidations_since_reset\"") != std::string::npos, "ECS telemetry JSON export omitted archetype transition invalidations");

    const std::filesystem::path telemetryPath = std::filesystem::temp_directory_path() / "kb_ecs_world_telemetry_test.json";
    kb::ecs::ExportWorldTelemetrySnapshotToJsonFile(snapshot, telemetryPath);
    std::ifstream telemetryFile(telemetryPath, std::ios::binary);
    kb::tests::Require(telemetryFile.is_open(), "ECS telemetry JSON file export did not create a readable file");
    const std::string exportedTelemetry{ std::istreambuf_iterator<char>(telemetryFile), std::istreambuf_iterator<char>() };
    telemetryFile.close();
    kb::tests::Require(exportedTelemetry == telemetryJson, "ECS telemetry JSON file export did not match string export");
    std::filesystem::remove(telemetryPath);

    world.ResetTelemetryFrameCounters();
    const kb::ecs::WorldTelemetrySnapshot resetSnapshot = world.TelemetrySnapshot();
    kb::tests::Require(resetSnapshot.structuralChangesSinceReset == 0, "ECS telemetry did not reset frame structural changes");
    kb::tests::Require(resetSnapshot.queryExecutions == 0, "ECS telemetry did not reset frame query executions");
    kb::tests::Require(resetSnapshot.queryBatches == 0, "ECS telemetry did not reset frame query batches");
    kb::tests::Require(resetSnapshot.queryEntitiesVisited == 0, "ECS telemetry did not reset frame query visited entities");
    kb::tests::Require(resetSnapshot.queryBytesTouched == 0, "ECS telemetry did not reset frame query bytes touched");
    kb::tests::Require(resetSnapshot.queryElapsedNanoseconds == 0, "ECS telemetry did not reset frame query elapsed time");
    kb::tests::Require(resetSnapshot.queryPrepareCalls == 0, "ECS telemetry did not reset frame query prepare calls");
    kb::tests::Require(resetSnapshot.queryPrepareRecords == 0, "ECS telemetry did not reset frame prepared query records");
    kb::tests::Require(resetSnapshot.queryMatchedChunks == 0, "ECS telemetry did not reset frame matched chunks");
    kb::tests::Require(resetSnapshot.queryMatchedArchetypes == 0, "ECS telemetry did not reset frame matched archetypes");
    kb::tests::Require(resetSnapshot.queryPrepareElapsedNanoseconds == 0, "ECS telemetry did not reset frame query prepare time");
    kb::tests::Require(resetSnapshot.queryKernelElapsedNanoseconds == 0, "ECS telemetry did not reset frame query kernel time");
    kb::tests::Require(resetSnapshot.queryRecordCacheHits == 0, "ECS telemetry did not reset query record cache hits");
    kb::tests::Require(resetSnapshot.queryRecordCacheMisses == 0, "ECS telemetry did not reset query record cache misses");
    kb::tests::Require(resetSnapshot.queryRecordCacheHitPercent == 0.0, "ECS telemetry did not reset query record cache hit percent");
    kb::tests::Require(resetSnapshot.queryRecordCacheMissPercent == 0.0, "ECS telemetry did not reset query record cache miss percent");
    kb::tests::Require(resetSnapshot.queryAdaptiveExecutions == 0, "ECS telemetry did not reset adaptive query count");
    kb::tests::Require(resetSnapshot.queryEffectiveBatchSizeTotal == 0, "ECS telemetry did not reset effective query batch total");
    kb::tests::Require(resetSnapshot.queryMaxEffectiveBatchSize == 0, "ECS telemetry did not reset max effective query batch size");
    kb::tests::Require(resetSnapshot.querySingleThreadExecutions == 0, "ECS telemetry did not reset single-thread query policy count");
    kb::tests::Require(resetSnapshot.queryParallelChunkExecutions == 0, "ECS telemetry did not reset chunk query policy count");
    kb::tests::Require(resetSnapshot.queryParallelRangeExecutions == 0, "ECS telemetry did not reset range query policy count");
    kb::tests::Require(resetSnapshot.querySimdPreferredExecutions == 0, "ECS telemetry did not reset SIMD-preferred query policy count");
    kb::tests::Require(resetSnapshot.queryDeterministicExecutions == 0, "ECS telemetry did not reset deterministic query policy count");
    kb::tests::Require(resetSnapshot.queryEstimatedBytesPerSecond == 0.0, "ECS telemetry did not reset query bandwidth estimate");
    kb::tests::Require(resetSnapshot.queryEstimatedGigabytesPerSecond == 0.0, "ECS telemetry did not reset query GB/s estimate");
    kb::tests::Require(resetSnapshot.queryKernelEstimatedBytesPerSecond == 0.0, "ECS telemetry did not reset query kernel bandwidth estimate");
    kb::tests::Require(resetSnapshot.queryKernelEstimatedGigabytesPerSecond == 0.0, "ECS telemetry did not reset query kernel GB/s estimate");
    kb::tests::Require(resetSnapshot.queryAverageBytesPerEntity == 0.0, "ECS telemetry did not reset query bytes per entity");
    kb::tests::Require(resetSnapshot.queryAveragePrepareNanoseconds == 0.0, "ECS telemetry did not reset average query prepare time");
    kb::tests::Require(resetSnapshot.queryAverageMatchedChunks == 0.0, "ECS telemetry did not reset average matched chunks");
    kb::tests::Require(resetSnapshot.queryAverageMatchedArchetypes == 0.0, "ECS telemetry did not reset average matched archetypes");
    kb::tests::Require(resetSnapshot.queryAverageKernelNanoseconds == 0.0, "ECS telemetry did not reset average query kernel time");
    kb::tests::Require(resetSnapshot.queryAverageEffectiveBatchSize == 0.0, "ECS telemetry did not reset average effective query batch size");
    kb::tests::Require(resetSnapshot.queryPrefetchDistanceTotal == 0, "ECS telemetry did not reset query prefetch distance");
    kb::tests::Require(resetSnapshot.queryAveragePrefetchDistance == 0.0, "ECS telemetry did not reset query prefetch average");
    kb::tests::Require(resetSnapshot.compatMutableIterations == 0, "ECS telemetry did not reset compatibility mutable iteration calls");
    kb::tests::Require(resetSnapshot.compatMutableEntitiesVisited == 0, "ECS telemetry did not reset compatibility mutable visited entities");
    kb::tests::Require(resetSnapshot.storageSystemAllocationCount == snapshot.storageSystemAllocationCount, "ECS telemetry reset changed total allocator count");
    kb::tests::Require(resetSnapshot.storageSystemAllocationsSinceReset == 0U, "ECS telemetry did not reset allocator frame count");
    kb::tests::Require(resetSnapshot.totalStructuralChanges == 3, "ECS telemetry reset total structural changes");
    kb::tests::Require(resetSnapshot.archetypeTransitionInvalidationsSinceReset == 0U, "ECS telemetry did not reset frame archetype transition invalidations");
    kb::tests::Require(resetSnapshot.totalArchetypeTransitionInvalidations == 2U, "ECS telemetry reset total archetype transition invalidations");

    firstQuery.ForEachMutableBatchKernel(
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 1,
            .policy = kb::ecs::QueryExecutionPolicy::SingleThread,
            .telemetryEnabled = true,
        },
        [](kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch) {
            EcsPosition* positions = batch.Components<0>();
            for (std::size_t index = 0; index < batch.Count(); ++index) {
                positions[index].x += 1.0F;
            }
        });

    const kb::ecs::WorldTelemetrySnapshot mutableSnapshot = world.TelemetrySnapshot();
    kb::tests::Require(mutableSnapshot.queryExecutions == 1, "ECS telemetry did not count mutable query executions");
    kb::tests::Require(mutableSnapshot.queryBatches == 1, "ECS telemetry did not count mutable query batches");
    kb::tests::Require(mutableSnapshot.queryEntitiesVisited == 1, "ECS telemetry did not count mutable query visited entities");
    kb::tests::Require(
        mutableSnapshot.queryBytesTouched == 2U * (sizeof(EcsPosition) + sizeof(EcsVelocity)),
        "ECS telemetry did not count mutable query read/write bytes touched");
    kb::tests::Require(mutableSnapshot.queryPrepareCalls == 1, "ECS telemetry did not count mutable query prepare calls");
    kb::tests::Require(mutableSnapshot.queryMatchedChunks == 1, "ECS telemetry did not count mutable query matched chunks");
    kb::tests::Require(mutableSnapshot.queryRecordCacheMisses == 1, "ECS telemetry did not count mutable query record cache miss after reset");
    kb::tests::Require(mutableSnapshot.queryRecordCacheHitPercent == 0.0, "ECS telemetry reported invalid mutable query record cache hit percent");
    kb::tests::Require(mutableSnapshot.queryRecordCacheMissPercent == 100.0, "ECS telemetry reported invalid mutable query record cache miss percent");
    kb::tests::Require(mutableSnapshot.querySingleThreadExecutions == 1, "ECS telemetry did not count mutable single-thread policy");
    kb::tests::Require(mutableSnapshot.queryMaxEffectiveBatchSize == 1, "ECS telemetry did not count mutable effective batch size");
    kb::tests::Require(mutableSnapshot.queryMatchedArchetypes == 1, "ECS telemetry did not count mutable query matched archetypes");
    kb::tests::Require(mutableSnapshot.queryKernelElapsedNanoseconds > 0, "ECS telemetry did not count mutable query kernel time");
    kb::tests::Require(
        kb::tests::NearlyEqual(static_cast<float>(mutableSnapshot.queryAverageBytesPerEntity), static_cast<float>(2U * (sizeof(EcsPosition) + sizeof(EcsVelocity)))),
        "ECS telemetry did not report mutable query bytes per entity");
}

void CountPositionBatch(const kb::ecs::QueryBatch<EcsPosition>& batch, void* context) {
    auto* visited = static_cast<std::uint64_t*>(context);
    *visited += batch.Count();
}

void RunWorldQueryPrefetchTelemetryTest() {
    kb::ecs::WorldConfig config;
    config.queryPrefetchDistance = 7;
    config.executionGrainSize = 4;
    kb::ecs::World world(config);
    for (int index = 0; index < 8; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
    }

    kb::ecs::Query<EcsPosition> query = world.CreateQuery<EcsPosition>();
    std::uint64_t visited = 0;
    query.ForEachBatch(kb::ecs::QueryExecutionSettings{ .telemetryEnabled = true }, &CountPositionBatch, &visited);
    kb::tests::Require(visited == 8, "ECS prefetch telemetry test did not visit all entities");

    kb::ecs::WorldTelemetrySnapshot snapshot = world.TelemetrySnapshot();
    kb::tests::Require(snapshot.queryExecutions == 1, "ECS prefetch telemetry did not count the default query");
    kb::tests::Require(snapshot.queryPrefetchDistanceTotal == 7, "ECS query did not inherit the world's default prefetch distance");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryAveragePrefetchDistance), 7.0F), "ECS query reported an invalid default prefetch average");

    world.ResetTelemetryFrameCounters();
    visited = 0;
    query.ForEachBatch(
        kb::ecs::QueryExecutionSettings{
            .prefetchDistance = 3,
            .telemetryEnabled = true,
        },
        &CountPositionBatch,
        &visited);
    kb::tests::Require(visited == 8, "ECS explicit prefetch telemetry test did not visit all entities");

    snapshot = world.TelemetrySnapshot();
    kb::tests::Require(snapshot.queryExecutions == 1, "ECS prefetch telemetry did not count the explicit query");
    kb::tests::Require(snapshot.queryPrefetchDistanceTotal == 3, "ECS query did not honor explicit prefetch override");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryAveragePrefetchDistance), 3.0F), "ECS query reported an invalid explicit prefetch average");
}

} // namespace

namespace kb::tests {

void RunEcsInspectionTests() {
    RunEntityInspectionTest();
    RunWorldArchetypeInspectionTest();
    RunEditorWorldInspectionTest();
    RunEditorComponentApplyTest();
    RunEditorEntityNamingAndChildrenTest();
    RunEditorBulkCreateCatalogTest();
    RunWorldTelemetrySnapshotTest();
    RunWorldQueryPrefetchTelemetryTest();
}

} // namespace kb::tests
