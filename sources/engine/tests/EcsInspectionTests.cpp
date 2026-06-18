#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/ComponentReflectionMacros.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/World.hpp"

#include <cmath>
#include <span>
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
    kb::tests::Require(emptySnapshot.queryPlanRequests == 0, "ECS telemetry reported query work before a query was created");

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
    world.ForEachMutable<EcsPosition>(&TouchCompatMutablePosition, nullptr);

    const kb::ecs::WorldTelemetrySnapshot snapshot = world.TelemetrySnapshot();
    kb::tests::Require(snapshot.entityCount == 1, "ECS telemetry reported invalid live entity count");
    kb::tests::Require(snapshot.archetypeCount >= 1, "ECS telemetry did not report archetypes");
    kb::tests::Require(snapshot.chunkCount >= 1, "ECS telemetry did not report chunks");
    kb::tests::Require(snapshot.chunkCapacity >= snapshot.entityCount, "ECS telemetry reported invalid chunk capacity");
    kb::tests::Require(snapshot.sparseChunkCount <= snapshot.chunkCount, "ECS telemetry reported invalid sparse chunk count");
    kb::tests::Require(snapshot.tailSparseChunkCount + snapshot.fragmentedChunkCount == snapshot.sparseChunkCount, "ECS telemetry sparse chunk breakdown is inconsistent");
    kb::tests::Require(snapshot.fragmentedChunkCount == 0, "ECS telemetry reported interior chunk fragmentation after compaction");
    kb::tests::Require(snapshot.emptyChunkCount == 0, "ECS telemetry reported empty chunks still in use after compaction");
    kb::tests::Require(snapshot.chunkPoolAllocated >= snapshot.chunkPoolInUse, "ECS telemetry reported invalid chunk pool allocation");
    kb::tests::Require(snapshot.chunkPoolInUse == snapshot.chunkCount, "ECS telemetry chunk pool in-use count did not match storage chunk count");
    kb::tests::Require(snapshot.chunkPoolAcquireCount >= snapshot.chunkPoolInUse, "ECS telemetry reported invalid chunk pool acquire count");
    kb::tests::Require(snapshot.bytesPerEntity >= sizeof(EcsPosition) + sizeof(EcsVelocity), "ECS telemetry reported invalid bytes per entity");
    kb::tests::Require(snapshot.allocatedBytes >= snapshot.usedBytes, "ECS telemetry reported invalid allocated bytes");
    kb::tests::Require(
        std::fabs((snapshot.occupancyPercent + snapshot.fragmentationPercent) - 100.0) <= 0.0001,
        "ECS telemetry occupancy and fragmentation did not sum to a full chunk budget");
    kb::tests::Require(snapshot.queryPlanRequests == 2, "ECS telemetry did not count query plan requests");
    kb::tests::Require(snapshot.queryCacheHits == 1, "ECS telemetry did not count query plan cache hits");
    kb::tests::Require(snapshot.queryCacheMisses == 1, "ECS telemetry did not count query plan cache misses");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryCacheHitPercent), 50.0F), "ECS telemetry reported invalid query cache hit percent");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryCacheMissPercent), 50.0F), "ECS telemetry reported invalid query cache miss percent");
    kb::tests::Require(snapshot.queryExecutions == 1, "ECS telemetry did not count query executions");
    kb::tests::Require(snapshot.queryBatches == 1, "ECS telemetry did not count query batches");
    kb::tests::Require(snapshot.queryEntitiesVisited == 1, "ECS telemetry did not count query visited entities");
    kb::tests::Require(snapshot.queryBytesTouched == sizeof(EcsPosition) + sizeof(EcsVelocity), "ECS telemetry did not count query bytes touched");
    kb::tests::Require(snapshot.queryElapsedNanoseconds > 0, "ECS telemetry did not count query elapsed time");
    kb::tests::Require(snapshot.queryEstimatedBytesPerSecond > 0.0, "ECS telemetry did not estimate query bandwidth");
    kb::tests::Require(snapshot.queryParallelExecutions == 1, "ECS telemetry did not count parallel query executions");
    kb::tests::Require(snapshot.queryWorkerSlots == 1, "ECS telemetry did not respect query worker slot override");
    kb::tests::Require(snapshot.queryWorkerActiveSlots == 1, "ECS telemetry did not count active query worker slots");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(snapshot.queryWorkerUtilizationPercent), 100.0F), "ECS telemetry reported invalid query worker utilization");
    kb::tests::Require(snapshot.compatMutableIterations == 1, "ECS telemetry did not count compatibility mutable iteration calls");
    kb::tests::Require(snapshot.compatMutableEntitiesVisited == 1, "ECS telemetry did not count compatibility mutable visited entities");
    kb::tests::Require(snapshot.structuralChangesSinceReset == 3, "ECS telemetry reported invalid frame structural change count");
    kb::tests::Require(snapshot.totalStructuralChanges == 3, "ECS telemetry reported invalid total structural change count");

    world.ResetTelemetryFrameCounters();
    const kb::ecs::WorldTelemetrySnapshot resetSnapshot = world.TelemetrySnapshot();
    kb::tests::Require(resetSnapshot.structuralChangesSinceReset == 0, "ECS telemetry did not reset frame structural changes");
    kb::tests::Require(resetSnapshot.queryExecutions == 0, "ECS telemetry did not reset frame query executions");
    kb::tests::Require(resetSnapshot.queryBatches == 0, "ECS telemetry did not reset frame query batches");
    kb::tests::Require(resetSnapshot.queryEntitiesVisited == 0, "ECS telemetry did not reset frame query visited entities");
    kb::tests::Require(resetSnapshot.queryBytesTouched == 0, "ECS telemetry did not reset frame query bytes touched");
    kb::tests::Require(resetSnapshot.queryElapsedNanoseconds == 0, "ECS telemetry did not reset frame query elapsed time");
    kb::tests::Require(resetSnapshot.queryEstimatedBytesPerSecond == 0.0, "ECS telemetry did not reset query bandwidth estimate");
    kb::tests::Require(resetSnapshot.compatMutableIterations == 0, "ECS telemetry did not reset compatibility mutable iteration calls");
    kb::tests::Require(resetSnapshot.compatMutableEntitiesVisited == 0, "ECS telemetry did not reset compatibility mutable visited entities");
    kb::tests::Require(resetSnapshot.totalStructuralChanges == 3, "ECS telemetry reset total structural changes");
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
}

} // namespace kb::tests
