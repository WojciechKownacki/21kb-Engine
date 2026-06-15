#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/CommandBuffer.hpp"
#include "engine/ecs/WorkerPool.hpp"

#include <atomic>
#include <cstddef>
#include <exception>
#include <span>
#include <vector>

namespace {

void CountPositions(kb::ecs::Entity entity, const EcsPosition& position, void* context) {
    static_cast<void>(entity);
    auto* counters = static_cast<EcsIterationCounters*>(context);
    ++counters->visited;
    counters->sumX += position.x;
}

void CountPositionVelocity(kb::ecs::Entity entity, const EcsPosition& position, const EcsVelocity& velocity, void* context) {
    static_cast<void>(entity);
    auto* counters = static_cast<EcsIterationCounters*>(context);
    ++counters->visited;
    counters->sumX += position.x + velocity.x;
}

void RunCommandBufferDeterministicPlaybackTest() {
    kb::ecs::World world;
    kb::ecs::CommandBuffer buffer{ 3 };

    kb::ecs::CommandEntity workerOne = buffer.Worker(1).CreateEntity("WorkerOne");
    buffer.Worker(1).Set(workerOne, EcsPosition{ .x = 10.0F, .y = 1.0F });

    kb::ecs::CommandEntity workerZero = buffer.Worker(0).CreateEntity("WorkerZero");
    buffer.Worker(0).Set(workerZero, EcsPosition{ .x = 1.0F, .y = 0.0F });

    kb::ecs::CommandEntity workerTwo = buffer.Worker(2).CreateEntity("WorkerTwo");
    buffer.Worker(2).Set(workerTwo, EcsPosition{ .x = 100.0F, .y = 2.0F });
    buffer.Worker(2).SetParent(workerTwo, workerZero);

    kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);

    const kb::ecs::Entity zero = result.Resolve(workerZero);
    const kb::ecs::Entity one = result.Resolve(workerOne);
    const kb::ecs::Entity two = result.Resolve(workerTwo);

    kb::tests::Require(result.CreatedCount() == 3, "ECS command buffer did not report created deferred entities");
    kb::tests::Require(zero.Id() < one.Id() && one.Id() < two.Id(), "ECS command buffer did not create deferred entities in deterministic worker order");
    kb::tests::Require(world.Name(zero) == "WorkerZero", "ECS command buffer did not apply worker zero entity name");
    kb::tests::Require(world.Name(one) == "WorkerOne", "ECS command buffer did not apply worker one entity name");
    kb::tests::Require(world.Parent(two) == zero, "ECS command buffer did not apply deferred parent relation");

    const EcsPosition* position = world.TryGet<EcsPosition>(two);
    kb::tests::Require(position != nullptr && kb::tests::NearlyEqual(position->x, 100.0F), "ECS command buffer did not set deferred component data");
    kb::tests::Require(buffer.Empty(), "ECS command buffer did not clear commands after playback");
}

void RunCommandBufferStructuralChangesTest() {
    kb::ecs::World world;
    const kb::ecs::Entity parent = world.CreateEntity("Parent");
    const kb::ecs::Entity removedVelocity = world.CreateEntity("RemovedVelocity");
    const kb::ecs::Entity destroyed = world.CreateEntity("Destroyed");
    world.Set(removedVelocity, EcsVelocity{ .x = 5.0F, .y = 0.0F });

    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };
    kb::ecs::CommandBuffer buffer{ pool.WorkerCount() };
    std::vector<kb::ecs::CommandEntity> created(64);
    std::atomic<std::size_t> commandCount = 0;

    pool.ParallelForChunks(created.size(), 1, [&buffer, &created, &commandCount, parent](kb::ecs::WorkerContext context, const kb::ecs::WorkerPoolChunk& chunk) {
        kb::ecs::CommandBuffer::WorkerBuffer worker = buffer.Worker(context.workerIndex);
        const kb::ecs::CommandEntity entity = worker.CreateEntity();
        worker.Set(entity, EcsPosition{ .x = static_cast<float>(chunk.begin), .y = static_cast<float>(context.workerIndex) });
        worker.SetParent(entity, kb::ecs::CommandEntity::Existing(parent));
        created[chunk.begin] = entity;
        commandCount.fetch_add(3, std::memory_order_relaxed);
    });

    buffer.Worker(0).Remove<EcsVelocity>(removedVelocity);
    buffer.Worker(0).DestroyEntity(destroyed);

    kb::tests::Require(buffer.CommandCount() == commandCount.load(std::memory_order_relaxed) + 2, "ECS command buffer recorded an unexpected command count");

    kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    kb::tests::Require(result.CreatedCount() == created.size(), "ECS command buffer did not create all worker-recorded entities");
    kb::tests::Require(!world.Has<EcsVelocity>(removedVelocity), "ECS command buffer did not remove an existing component");
    kb::tests::Require(!world.IsAlive(destroyed), "ECS command buffer did not destroy an existing entity");

    for (std::size_t index = 0; index < created.size(); ++index) {
        const kb::ecs::Entity entity = result.Resolve(created[index]);
        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        kb::tests::Require(position != nullptr && kb::tests::NearlyEqual(position->x, static_cast<float>(index)), "ECS command buffer lost worker-created component data");
        kb::tests::Require(world.Parent(entity) == parent, "ECS command buffer lost worker-created parent relation");
    }

    EcsIterationCounters counters;
    world.ForEach<EcsPosition>(&CountPositions, &counters);
    kb::tests::Require(counters.visited == static_cast<int>(created.size()), "ECS command buffer created an unexpected number of positioned entities");
}

void RunCommandBufferBulkCreateSameArchetypeTest() {
    kb::ecs::World world;
    kb::ecs::CommandBuffer buffer{ 2 };

    std::vector<EcsPosition> positions;
    std::vector<EcsVelocity> velocities;
    positions.reserve(8);
    velocities.reserve(8);
    for (int index = 0; index < 8; ++index) {
        positions.push_back(EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index + 10) });
        velocities.push_back(EcsVelocity{ .x = static_cast<float>(index * 2), .y = static_cast<float>(index * 3) });
    }

    std::vector<kb::ecs::CommandEntity> bulkEntities = buffer.Worker(0).CreateEntities(
        std::span<const EcsPosition>{ positions },
        std::span<const EcsVelocity>{ velocities });
    std::vector<kb::ecs::CommandEntity> emptyEntities = buffer.Worker(0).CreateEntities(3);
    kb::ecs::CommandEntity namedEntity = buffer.Worker(1).CreateEntity("AfterBulk");
    buffer.Worker(1).SetParent(namedEntity, bulkEntities.front());

    kb::tests::Require(buffer.CommandCount() == 4, "ECS command buffer bulk create recorded unexpected command count");

    kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    kb::tests::Require(result.CreatedCount() == positions.size() + emptyEntities.size() + 1, "ECS command buffer bulk create reported unexpected created count");
    kb::tests::Require(world.NativeStorageStats().liveEntities == result.CreatedCount(), "ECS command buffer bulk create did not mirror live entities into native storage");

    const kb::ecs::ComponentId positionId = world.Component<EcsPosition>();
    const kb::ecs::ComponentId velocityId = world.Component<EcsVelocity>();

    for (std::size_t index = 0; index < bulkEntities.size(); ++index) {
        const kb::ecs::Entity entity = result.Resolve(bulkEntities[index]);
        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        const EcsVelocity* velocity = world.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && velocity != nullptr, "ECS command buffer bulk create did not create the requested archetype");
        kb::tests::Require(world.NativeStorage().HasComponent(entity, positionId), "ECS command buffer bulk create did not mirror position into native storage");
        kb::tests::Require(world.NativeStorage().HasComponent(entity, velocityId), "ECS command buffer bulk create did not mirror velocity into native storage");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, positions[index].x), "ECS command buffer bulk create lost position data");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->x, velocities[index].x), "ECS command buffer bulk create lost velocity data");
        if (index > 0) {
            kb::tests::Require(result.Resolve(bulkEntities[index - 1]).Id() < entity.Id(), "ECS command buffer bulk create did not preserve entity order");
        }
    }

    for (kb::ecs::CommandEntity deferred : emptyEntities) {
        const kb::ecs::Entity entity = result.Resolve(deferred);
        kb::tests::Require(world.IsAlive(entity), "ECS command buffer empty bulk create did not create an entity");
        kb::tests::Require(!world.Has<EcsPosition>(entity) && !world.Has<EcsVelocity>(entity), "ECS command buffer empty bulk create created the wrong archetype");
        kb::tests::Require(world.NativeStorage().IsAlive(entity), "ECS command buffer empty bulk create did not mirror entity into native storage");
        kb::tests::Require(!world.NativeStorage().HasComponent(entity, positionId) && !world.NativeStorage().HasComponent(entity, velocityId), "ECS command buffer empty bulk create mirrored the wrong native archetype");
    }

    const kb::ecs::Entity named = result.Resolve(namedEntity);
    kb::tests::Require(world.Name(named) == "AfterBulk", "ECS command buffer bulk create changed later command playback");
    kb::tests::Require(world.Parent(named) == result.Resolve(bulkEntities.front()), "ECS command buffer bulk create broke deferred relation playback");

    EcsIterationCounters counters;
    world.ForEach<EcsPosition, EcsVelocity>(&CountPositionVelocity, &counters);
    kb::tests::Require(counters.visited == static_cast<int>(positions.size()), "ECS command buffer bulk create produced unexpected query results");
}

void RunCommandBufferBulkComponentMutationTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> existingEntities;
    std::vector<kb::ecs::CommandEntity> commandEntities;
    std::vector<EcsPosition> positions;
    std::vector<EcsVelocity> velocities;
    existingEntities.reserve(6);
    commandEntities.reserve(6);
    positions.reserve(6);
    velocities.reserve(6);

    for (int index = 0; index < 6; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = -1.0F, .y = -1.0F });
        existingEntities.push_back(entity);
        commandEntities.push_back(kb::ecs::CommandEntity::Existing(entity));
        positions.push_back(EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index + 1) });
        velocities.push_back(EcsVelocity{ .x = static_cast<float>(index * 10), .y = static_cast<float>(index * 20) });
    }

    kb::ecs::CommandBuffer buffer{ 2 };
    buffer.Worker(0).Set(
        std::span<const kb::ecs::CommandEntity>{ commandEntities },
        std::span<const EcsPosition>{ positions },
        std::span<const EcsVelocity>{ velocities });

    std::vector<EcsPosition> deferredPositions{
        EcsPosition{ .x = 100.0F, .y = 101.0F },
        EcsPosition{ .x = 200.0F, .y = 201.0F },
    };
    std::vector<kb::ecs::CommandEntity> deferred = buffer.Worker(1).CreateEntities(2);
    buffer.Worker(1).Set(std::span<const kb::ecs::CommandEntity>{ deferred }, std::span<const EcsPosition>{ deferredPositions });

    buffer.Worker(1).Remove<EcsPosition, EcsVelocity>(std::span<const kb::ecs::Entity>{ existingEntities }.subspan(1, 3));

    kb::tests::Require(buffer.CommandCount() == 4, "ECS command buffer bulk component mutation recorded unexpected command count");

    kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    kb::tests::Require(result.CreatedCount() == deferred.size(), "ECS command buffer bulk component mutation reported unexpected created count");

    const kb::ecs::ComponentId positionId = world.Component<EcsPosition>();
    const kb::ecs::ComponentId velocityId = world.Component<EcsVelocity>();

    for (std::size_t index = 0; index < existingEntities.size(); ++index) {
        const kb::ecs::Entity entity = existingEntities[index];
        const bool removed = index >= 1 && index < 4;
        kb::tests::Require(world.Has<EcsPosition>(entity) != removed, "ECS command buffer bulk remove produced an unexpected position state");
        kb::tests::Require(world.Has<EcsVelocity>(entity) != removed, "ECS command buffer bulk remove produced an unexpected velocity state");
        kb::tests::Require(world.NativeStorage().HasComponent(entity, positionId) != removed, "ECS command buffer bulk remove produced an unexpected native position state");
        kb::tests::Require(world.NativeStorage().HasComponent(entity, velocityId) != removed, "ECS command buffer bulk remove produced an unexpected native velocity state");
        if (!removed) {
            const EcsPosition* position = world.TryGet<EcsPosition>(entity);
            const EcsVelocity* velocity = world.TryGet<EcsVelocity>(entity);
            kb::tests::Require(position != nullptr && velocity != nullptr, "ECS command buffer bulk set did not write expected components");
            kb::tests::Require(kb::tests::NearlyEqual(position->x, positions[index].x), "ECS command buffer bulk set lost position data");
            kb::tests::Require(kb::tests::NearlyEqual(velocity->x, velocities[index].x), "ECS command buffer bulk set lost velocity data");
        }
    }

    for (std::size_t index = 0; index < deferred.size(); ++index) {
        const kb::ecs::Entity entity = result.Resolve(deferred[index]);
        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        kb::tests::Require(position != nullptr, "ECS command buffer bulk set did not handle deferred entities");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, deferredPositions[index].x), "ECS command buffer bulk set lost deferred component data");
    }
}

void RunCommandBufferDeferredDestroySyncPointTest() {
    kb::ecs::World world;
    const kb::ecs::Entity existing = world.CreateEntity("Existing");
    world.Set(existing, EcsPosition{ .x = 1.0F, .y = 2.0F });

    kb::ecs::CommandBuffer buffer{ 2 };
    kb::ecs::CommandEntity created = buffer.Worker(0).CreateEntity("Created");
    buffer.Worker(0).Set(created, EcsPosition{ .x = 10.0F, .y = 20.0F });
    buffer.Worker(0).DestroyEntity(created);
    buffer.Worker(0).Set(created, EcsVelocity{ .x = 30.0F, .y = 40.0F });

    buffer.Worker(1).DestroyEntity(existing);
    buffer.Worker(1).Set(existing, EcsVelocity{ .x = 5.0F, .y = 6.0F });
    buffer.Worker(1).DestroyEntity(existing);

    kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    const kb::ecs::Entity resolvedCreated = result.Resolve(created);

    kb::tests::Require(result.CreatedCount() == 1, "ECS command buffer deferred destroy changed created entity accounting");
    kb::tests::Require(result.DestroyedCount() == 2, "ECS command buffer deferred destroy did not collapse duplicate destroy commands");
    kb::tests::Require(result.WasDestroyed(created), "ECS command buffer deferred destroy did not report a destroyed deferred entity");
    kb::tests::Require(result.WasDestroyed(existing), "ECS command buffer deferred destroy did not report a destroyed existing entity");
    kb::tests::Require(!world.IsAlive(resolvedCreated), "ECS command buffer kept a deferred-destroyed created entity alive after playback sync point");
    kb::tests::Require(!world.IsAlive(existing), "ECS command buffer kept a deferred-destroyed existing entity alive after playback sync point");
    kb::tests::Require(!world.Has<EcsPosition>(resolvedCreated), "ECS command buffer left component references on a destroyed deferred entity");
    kb::tests::Require(!world.Has<EcsVelocity>(existing), "ECS command buffer left component references on a destroyed existing entity");
}

void RunCommandBufferNestedCreateDestroyFromJobsTest() {
    kb::ecs::World world;
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 4 } };
    kb::ecs::CommandBuffer buffer{ pool.WorkerCount() };

    constexpr std::size_t familyCount = 32;
    std::vector<kb::ecs::CommandEntity> parents(familyCount);
    std::vector<kb::ecs::CommandEntity> survivors(familyCount);
    std::vector<kb::ecs::CommandEntity> destroyed(familyCount);

    pool.ParallelForChunks(familyCount, 1, [&buffer, &parents, &survivors, &destroyed](kb::ecs::WorkerContext context, const kb::ecs::WorkerPoolChunk& chunk) {
        kb::ecs::CommandBuffer::WorkerBuffer worker = buffer.Worker(context.workerIndex);
        const kb::ecs::CommandEntity parent = worker.CreateEntity();
        const kb::ecs::CommandEntity survivor = worker.CreateEntity();
        const kb::ecs::CommandEntity removed = worker.CreateEntity();

        worker.Set(parent, EcsPosition{ .x = static_cast<float>(chunk.begin), .y = 0.0F });
        worker.Set(survivor, EcsPosition{ .x = static_cast<float>(chunk.begin), .y = 1.0F });
        worker.Set(removed, EcsPosition{ .x = static_cast<float>(chunk.begin), .y = 2.0F });
        worker.SetParent(survivor, parent);
        worker.SetParent(removed, survivor);
        worker.DestroyEntity(removed);

        parents[chunk.begin] = parent;
        survivors[chunk.begin] = survivor;
        destroyed[chunk.begin] = removed;
    });

    kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    kb::tests::Require(result.CreatedCount() == familyCount * 3, "ECS command buffer nested create/destroy reported unexpected created count");
    kb::tests::Require(result.DestroyedCount() == familyCount, "ECS command buffer nested create/destroy reported unexpected destroyed count");

    for (std::size_t index = 0; index < familyCount; ++index) {
        const kb::ecs::Entity parent = result.Resolve(parents[index]);
        const kb::ecs::Entity survivor = result.Resolve(survivors[index]);
        const kb::ecs::Entity removed = result.Resolve(destroyed[index]);

        kb::tests::Require(world.IsAlive(parent), "ECS command buffer nested create/destroy removed a parent entity");
        kb::tests::Require(world.IsAlive(survivor), "ECS command buffer nested create/destroy removed a survivor entity");
        kb::tests::Require(!world.IsAlive(removed), "ECS command buffer nested create/destroy kept a destroyed child alive");
        kb::tests::Require(world.Parent(survivor) == parent, "ECS command buffer nested create/destroy lost survivor parent relation");
        kb::tests::Require(result.WasDestroyed(removed), "ECS command buffer nested create/destroy did not report destroyed child");
    }

    EcsIterationCounters counters;
    world.ForEach<EcsPosition>(&CountPositions, &counters);
    kb::tests::Require(counters.visited == static_cast<int>(familyCount * 2), "ECS command buffer nested create/destroy left destroyed component data visible");
}

void RunCommandBufferRollbackOnPlaybackErrorTest() {
    kb::ecs::World world;
    const kb::ecs::Entity originalParent = world.CreateEntity("OriginalParent");
    const kb::ecs::Entity existing = world.CreateEntity("Existing");
    world.Set(existing, EcsPosition{ .x = 1.0F, .y = 2.0F });
    world.SetParent(existing, originalParent);

    kb::ecs::CommandBuffer buffer{ 2 };
    const kb::ecs::CommandEntity transient = buffer.Worker(0).CreateEntity("Transient");
    buffer.Worker(0).Set(transient, EcsPosition{ .x = 10.0F, .y = 20.0F });
    buffer.Worker(0).SetParent(kb::ecs::CommandEntity::Existing(existing), transient);
    buffer.Worker(0).Set(existing, EcsPosition{ .x = 99.0F, .y = 100.0F });
    buffer.Worker(1).Set(kb::ecs::CommandEntity::Deferred(8, 0), EcsVelocity{ .x = 1.0F, .y = 1.0F });

    bool threw = false;
    try {
        static_cast<void>(buffer.Playback(world));
    } catch (const std::exception&) {
        threw = true;
    }

    kb::tests::Require(threw, "ECS command buffer playback error test did not propagate the playback error");
    kb::tests::Require(world.IsAlive(existing), "ECS command buffer rollback destroyed an existing entity");
    kb::tests::Require(world.Parent(existing) == originalParent, "ECS command buffer rollback did not restore the original parent relation");

    const EcsPosition* position = world.TryGet<EcsPosition>(existing);
    kb::tests::Require(position != nullptr && kb::tests::NearlyEqual(position->x, 1.0F), "ECS command buffer rollback did not restore original component data");
    kb::tests::Require(!world.Has<EcsVelocity>(existing), "ECS command buffer rollback introduced an unexpected component");

    EcsIterationCounters counters;
    world.ForEach<EcsPosition>(&CountPositions, &counters);
    kb::tests::Require(counters.visited == 1, "ECS command buffer rollback left transient entity component data alive");
    kb::tests::Require(!buffer.Empty(), "ECS command buffer cleared commands after a failed playback");
}

} // namespace

namespace kb::tests {

void RunEcsCommandBufferTests() {
    RunCommandBufferDeterministicPlaybackTest();
    RunCommandBufferStructuralChangesTest();
    RunCommandBufferBulkCreateSameArchetypeTest();
    RunCommandBufferBulkComponentMutationTest();
    RunCommandBufferDeferredDestroySyncPointTest();
    RunCommandBufferNestedCreateDestroyFromJobsTest();
    RunCommandBufferRollbackOnPlaybackErrorTest();
}

} // namespace kb::tests
