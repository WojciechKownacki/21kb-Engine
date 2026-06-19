#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/CommandBuffer.hpp"
#include "engine/ecs/WorkerPool.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <utility>
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

void RunCommandBufferTrustedPlaybackBulkStructuralTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> transientEntities;
    std::vector<kb::ecs::Entity> markerEntities;
    std::vector<EcsPosition> createdPositions;
    std::vector<EcsVelocity> createdVelocities;
    std::vector<EcsQueryMarker> markers;
    transientEntities.reserve(4U);
    markerEntities.reserve(8U);
    createdPositions.reserve(4U);
    createdVelocities.reserve(4U);
    markers.reserve(4U);

    for (std::size_t index = 0; index < 4U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 1.0F });
        world.Set(entity, EcsVelocity{ .x = 0.5F, .y = 0.25F });
        transientEntities.push_back(entity);
    }

    for (std::size_t index = 0; index < 8U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(10U + index), .y = 2.0F });
        if (index >= 4U) {
            world.Set(entity, EcsQueryMarker{ .value = static_cast<int>(index) });
        }
        markerEntities.push_back(entity);
    }

    for (std::size_t index = 0; index < 4U; ++index) {
        createdPositions.push_back(EcsPosition{ .x = static_cast<float>(100U + index), .y = 3.0F });
        createdVelocities.push_back(EcsVelocity{ .x = static_cast<float>(200U + index), .y = 4.0F });
        markers.push_back(EcsQueryMarker{ .value = static_cast<int>(1000U + index) });
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    kb::ecs::CommandBuffer::WorkerBuffer worker = buffer.Worker(0);
    worker.DestroyEntities(std::span<const kb::ecs::Entity>{ transientEntities });
    const std::vector<kb::ecs::CommandEntity> createdRefs = worker.CreateEntities(std::span<const EcsPosition>{ createdPositions }, std::span<const EcsVelocity>{ createdVelocities });
    worker.AddMissingBorrowed(std::span<const kb::ecs::Entity>{ markerEntities.data(), 4U }, std::span<const EcsQueryMarker>{ markers });
    worker.RemoveExisting<EcsQueryMarker>(std::span<const kb::ecs::Entity>{ markerEntities.data() + 4U, 4U });

    const kb::ecs::CommandBufferPlaybackResult result = buffer.PlaybackTrusted(world);
    kb::tests::Require(result.CreatedCount() == 4U, "ECS trusted command buffer playback reported invalid created count");
    kb::tests::Require(result.DestroyedCount() == 4U, "ECS trusted command buffer playback reported invalid destroyed count");
    kb::tests::Require(buffer.Empty(), "ECS trusted command buffer playback did not clear commands");

    for (kb::ecs::Entity entity : transientEntities) {
        kb::tests::Require(!world.IsAlive(entity), "ECS trusted command buffer playback failed to destroy a transient entity");
    }
    for (std::size_t index = 0; index < createdRefs.size(); ++index) {
        const kb::ecs::Entity entity = result.Resolve(createdRefs[index]);
        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        const EcsVelocity* velocity = world.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && velocity != nullptr, "ECS trusted command buffer playback failed to create component payloads");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, createdPositions[index].x), "ECS trusted command buffer playback lost created position data");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->x, createdVelocities[index].x), "ECS trusted command buffer playback lost created velocity data");
    }
    for (std::size_t index = 0; index < 4U; ++index) {
        const EcsQueryMarker* marker = world.TryGet<EcsQueryMarker>(markerEntities[index]);
        kb::tests::Require(marker != nullptr && marker->value == markers[index].value, "ECS trusted command buffer playback failed to set marker data");
        kb::tests::Require(!world.Has<EcsQueryMarker>(markerEntities[index + 4U]), "ECS trusted command buffer playback failed to remove marker data");
    }
}

void RunCommandBufferTrustedPlaybackKeepsGenericBulkSetSafeTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> entities;
    std::vector<EcsPosition> positions;
    entities.reserve(3U);
    positions.reserve(3U);

    for (std::size_t index = 0; index < 3U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 1.0F });
        entities.push_back(entity);
        positions.push_back(EcsPosition{ .x = static_cast<float>(100U + index), .y = 2.0F });
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    buffer.Worker(0).SetBorrowed(std::span<const kb::ecs::Entity>{ entities }, std::span<const EcsPosition>{ positions });

    const kb::ecs::CommandBufferPlaybackResult result = buffer.PlaybackTrusted(world);
    kb::tests::Require(result.CreatedCount() == 0U, "ECS trusted bulk set safety test reported unexpected creates");
    kb::tests::Require(result.DestroyedCount() == 0U, "ECS trusted bulk set safety test reported unexpected destroys");

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const EcsPosition* position = world.TryGet<EcsPosition>(entities[index]);
        kb::tests::Require(position != nullptr, "ECS trusted generic bulk set removed an existing component");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, positions[index].x), "ECS trusted generic bulk set failed to update existing data");
        kb::tests::Require(kb::tests::NearlyEqual(position->y, positions[index].y), "ECS trusted generic bulk set failed to update existing payload");
    }
}

void RunCommandBufferTrustedPlaybackHonorsNativeOnlyConfigTest() {
    kb::ecs::WorldConfig config;
    config.mirrorEntitiesToBackend = false;
    config.mirrorNativeComponentChangesToBackend = false;
    kb::ecs::World world{ config };

    std::vector<EcsPosition> positions{
        EcsPosition{ .x = 1.0F, .y = 2.0F },
        EcsPosition{ .x = 3.0F, .y = 4.0F },
    };
    std::vector<EcsVelocity> velocities{
        EcsVelocity{ .x = 5.0F, .y = 6.0F },
        EcsVelocity{ .x = 7.0F, .y = 8.0F },
    };

    kb::ecs::CommandBuffer buffer{ 1 };
    const std::vector<kb::ecs::CommandEntity> deferred =
        buffer.Worker(0).CreateEntities(std::span<const EcsPosition>{ positions }, std::span<const EcsVelocity>{ velocities });

    const kb::ecs::CommandBufferPlaybackResult result = buffer.PlaybackTrusted(world);
    kb::tests::Require(result.CreatedCount() == deferred.size(), "ECS trusted native-only playback reported invalid created count");
    for (std::size_t index = 0; index < deferred.size(); ++index) {
        const kb::ecs::Entity entity = result.Resolve(deferred[index]);
        kb::tests::Require(world.IsAlive(entity), "ECS trusted native-only playback failed to create a native entity");
        kb::tests::Require(!world.BackendEntityAlive(entity), "ECS trusted native-only playback mirrored an entity into the compatibility backend");
        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        const EcsVelocity* velocity = world.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && velocity != nullptr, "ECS trusted native-only playback failed to write native components");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, positions[index].x), "ECS trusted native-only playback lost position payload");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->x, velocities[index].x), "ECS trusted native-only playback lost velocity payload");
    }
}

struct CommandBufferDeterministicSnapshot {
    std::vector<kb::ecs::Entity::IdType> entityIds;
    std::vector<kb::ecs::Entity::IdType> parentIds;
    std::vector<float> positionX;
};

[[nodiscard]] std::uint32_t NextDeterministicValue(std::uint32_t& state) noexcept {
    state = state * 1'664'525U + 1'013'904'223U;
    return state;
}

[[nodiscard]] CommandBufferDeterministicSnapshot BuildDeterministicStructuralPlayback(std::uint32_t seed) {
    kb::ecs::World world;
    kb::ecs::CommandBuffer buffer{ 4 };
    std::vector<kb::ecs::CommandEntity> commands(32);
    std::uint32_t state = seed;

    for (std::size_t index = 0; index < commands.size(); ++index) {
        const std::size_t workerIndex = NextDeterministicValue(state) % 4U;
        kb::ecs::CommandBuffer::WorkerBuffer worker = buffer.Worker(workerIndex);
        commands[index] = worker.CreateEntity();
        worker.Set(commands[index], EcsPosition{ .x = static_cast<float>(NextDeterministicValue(state) % 10'000U), .y = static_cast<float>(workerIndex) });
        if (index > 0U) {
            worker.SetParent(commands[index], commands[index - 1U]);
        }
    }

    const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    CommandBufferDeterministicSnapshot snapshot;
    snapshot.entityIds.reserve(commands.size());
    snapshot.parentIds.reserve(commands.size());
    snapshot.positionX.reserve(commands.size());
    for (kb::ecs::CommandEntity command : commands) {
        const kb::ecs::Entity entity = result.Resolve(command);
        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        snapshot.entityIds.push_back(entity.Id());
        snapshot.parentIds.push_back(world.Parent(entity).Id());
        snapshot.positionX.push_back(position == nullptr ? -1.0F : position->x);
    }
    return snapshot;
}

void RunCommandBufferMultiWorkerDeterministicStructuralChangesTest() {
    const CommandBufferDeterministicSnapshot first = BuildDeterministicStructuralPlayback(0xC0FFEEU);
    const CommandBufferDeterministicSnapshot second = BuildDeterministicStructuralPlayback(0xC0FFEEU);

    kb::tests::Require(first.entityIds == second.entityIds, "ECS command buffer multi-worker structural playback produced nondeterministic entity ids");
    kb::tests::Require(first.parentIds == second.parentIds, "ECS command buffer multi-worker structural playback produced nondeterministic parent relations");
    kb::tests::Require(first.positionX == second.positionX, "ECS command buffer multi-worker structural playback produced nondeterministic component values");
}

void RunCommandBufferRandomStructuralChangeStressTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> liveEntities;
    liveEntities.reserve(256U);
    for (std::size_t index = 0; index < 64U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 1.0F });
        world.Set(entity, EcsVelocity{ .x = 0.5F, .y = 0.25F });
        liveEntities.push_back(entity);
    }

    std::uint32_t randomState = 0x51A7EEDU;
    std::size_t createdTotal = 0U;
    std::size_t destroyedTotal = 0U;
    for (std::size_t round = 0; round < 64U; ++round) {
        kb::ecs::CommandBuffer buffer{ 4 };
        std::vector<kb::ecs::CommandEntity> createdThisRound;
        createdThisRound.reserve(8U);
        std::size_t destroyedThisRound = 0U;

        for (std::size_t operation = 0; operation < 32U; ++operation) {
            const std::size_t workerIndex = NextDeterministicValue(randomState) % buffer.WorkerCount();
            kb::ecs::CommandBuffer::WorkerBuffer worker = buffer.Worker(workerIndex);
            const std::uint32_t action = NextDeterministicValue(randomState) % 5U;
            if (action == 0U || liveEntities.empty()) {
                const kb::ecs::CommandEntity created = worker.CreateEntity();
                worker.Set(created, EcsPosition{ .x = static_cast<float>(round * 100U + operation), .y = static_cast<float>(workerIndex) });
                worker.Set(created, EcsVelocity{ .x = 1.0F, .y = 2.0F });
                createdThisRound.push_back(created);
                continue;
            }

            const std::size_t entityIndex = NextDeterministicValue(randomState) % liveEntities.size();
            const kb::ecs::Entity entity = liveEntities[entityIndex];
            if (action == 1U) {
                worker.DestroyEntity(entity);
                liveEntities[entityIndex] = liveEntities.back();
                liveEntities.pop_back();
                ++destroyedThisRound;
            } else if (action == 2U) {
                worker.Set(entity, EcsPosition{ .x = static_cast<float>(round), .y = static_cast<float>(operation) });
            } else if (action == 3U) {
                worker.Set(entity, EcsQueryMarker{ .value = static_cast<int>(round + operation) });
            } else {
                worker.Remove<EcsVelocity>(entity);
            }
        }

        const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
        for (kb::ecs::CommandEntity commandEntity : createdThisRound) {
            liveEntities.push_back(result.Resolve(commandEntity));
        }
        createdTotal += createdThisRound.size();
        destroyedTotal += destroyedThisRound;

        kb::tests::Require(result.CreatedCount() == createdThisRound.size(), "Random structural stress created count diverged");
        kb::tests::Require(result.DestroyedCount() == destroyedThisRound, "Random structural stress destroyed count diverged");
        kb::tests::Require(world.NativeStorageStats().liveEntities == liveEntities.size(), "Random structural stress native storage live count diverged");
        for (kb::ecs::Entity entity : liveEntities) {
            kb::tests::Require(world.IsAlive(entity), "Random structural stress retained a stale entity");
            kb::tests::Require(world.NativeStorage().IsAlive(entity), "Random structural stress native storage missed a live entity");
            kb::tests::Require(world.Has<EcsPosition>(entity), "Random structural stress lost required position data");
        }
    }

    kb::tests::Require(createdTotal > 0U, "Random structural stress did not create entities");
    kb::tests::Require(destroyedTotal > 0U, "Random structural stress did not destroy entities");
}

void RunCommandBufferBulkParentChangesTest() {
    kb::ecs::World world;
    const kb::ecs::Entity existingParent = world.CreateEntity("BulkParentRoot");

    kb::ecs::CommandBuffer buffer{ 2 };
    std::vector<kb::ecs::CommandEntity> created = buffer.Worker(0).CreateEntities(3);
    std::vector<kb::ecs::CommandEntity> parents{
        kb::ecs::CommandEntity::Existing(existingParent),
        created[0],
        created[1],
    };
    buffer.Worker(1).SetParents(std::span<const kb::ecs::CommandEntity>{ created }, std::span<const kb::ecs::CommandEntity>{ parents });
    kb::tests::Require(buffer.CommandCount() == 2U, "ECS command buffer bulk parent setup recorded unexpected command count");

    const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    const kb::ecs::Entity first = result.Resolve(created[0]);
    const kb::ecs::Entity second = result.Resolve(created[1]);
    const kb::ecs::Entity third = result.Resolve(created[2]);

    kb::tests::Require(world.Parent(first) == existingParent, "ECS command buffer bulk parent setup did not use existing parent");
    kb::tests::Require(world.Parent(second) == first, "ECS command buffer bulk parent setup did not resolve first deferred parent");
    kb::tests::Require(world.Parent(third) == second, "ECS command buffer bulk parent setup did not resolve second deferred parent");

    kb::ecs::CommandBuffer clearBuffer{ 1 };
    std::vector<kb::ecs::Entity> children{ first, second, third };
    clearBuffer.Worker(0).ClearParents(std::span<const kb::ecs::Entity>{ children });
    static_cast<void>(clearBuffer.Playback(world));

    kb::tests::Require(!world.Parent(first).IsValid(), "ECS command buffer bulk clear parent did not clear first parent");
    kb::tests::Require(!world.Parent(second).IsValid(), "ECS command buffer bulk clear parent did not clear second parent");
    kb::tests::Require(!world.Parent(third).IsValid(), "ECS command buffer bulk clear parent did not clear third parent");
}

void RunCommandBufferKnownAcyclicNewEntityParentChangesTest() {
    kb::ecs::World world;
    const kb::ecs::Entity existingParent = world.CreateEntity("KnownAcyclicParentRoot");

    kb::ecs::CommandBuffer buffer{ 2 };
    std::vector<kb::ecs::CommandEntity> created = buffer.Worker(0).CreateEntities(4);
    const std::vector<kb::ecs::CommandEntity> parents{
        kb::ecs::CommandEntity::Existing(existingParent),
        created[0],
        created[0],
        created[2],
    };
    buffer.Worker(1).SetParentsForNewEntitiesKnownAcyclic(
        std::span<const kb::ecs::CommandEntity>{ created },
        std::span<const kb::ecs::CommandEntity>{ parents });

    const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    const kb::ecs::Entity root = result.Resolve(created[0]);
    const kb::ecs::Entity firstChild = result.Resolve(created[1]);
    const kb::ecs::Entity secondChild = result.Resolve(created[2]);
    const kb::ecs::Entity leaf = result.Resolve(created[3]);

    kb::tests::Require(result.PlaybackStats().parentCommands == created.size(), "ECS command buffer known-acyclic parent batch reported invalid parent telemetry");
    kb::tests::Require(result.PlaybackStats().createPhaseNanoseconds > 0, "ECS command buffer known-acyclic parent batch did not report create phase timing");
    kb::tests::Require(result.PlaybackStats().applyPhaseNanoseconds > 0, "ECS command buffer known-acyclic parent batch did not report apply phase timing");
    kb::tests::Require(result.PlaybackStats().parentApplyNanoseconds > 0, "ECS command buffer known-acyclic parent batch did not report parent apply timing");
    kb::tests::Require(world.Parent(root) == existingParent, "ECS command buffer known-acyclic parent batch did not use existing parent");
    kb::tests::Require(world.Parent(firstChild) == root, "ECS command buffer known-acyclic parent batch did not resolve first child parent");
    kb::tests::Require(world.Parent(secondChild) == root, "ECS command buffer known-acyclic parent batch did not resolve second child parent");
    kb::tests::Require(world.Parent(leaf) == secondChild, "ECS command buffer known-acyclic parent batch did not resolve nested parent");
}

void RunCommandBufferKnownAcyclicMovedParentBatchTest() {
    kb::ecs::World world;
    const kb::ecs::Entity existingParent = world.CreateEntity("KnownAcyclicMovedParentRoot");

    kb::ecs::CommandBuffer buffer{ 1 };
    std::vector<kb::ecs::CommandEntity> created = buffer.Worker(0).CreateEntities(3);
    std::vector<kb::ecs::CommandEntity> children{
        created[0],
        created[1],
        created[2],
    };
    std::vector<kb::ecs::CommandEntity> parents{
        kb::ecs::CommandEntity::Existing(existingParent),
        created[0],
        created[1],
    };
    buffer.Worker(0).SetParentsForNewEntitiesKnownAcyclic(std::move(children), std::move(parents));

    const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    const kb::ecs::Entity root = result.Resolve(created[0]);
    const kb::ecs::Entity child = result.Resolve(created[1]);
    const kb::ecs::Entity leaf = result.Resolve(created[2]);

    kb::tests::Require(result.PlaybackStats().parentCommands == created.size(), "ECS command buffer moved parent batch reported invalid parent telemetry");
    kb::tests::Require(world.Parent(root) == existingParent, "ECS command buffer moved parent batch did not use existing parent");
    kb::tests::Require(world.Parent(child) == root, "ECS command buffer moved parent batch did not resolve child parent");
    kb::tests::Require(world.Parent(leaf) == child, "ECS command buffer moved parent batch did not resolve leaf parent");
}

void RunCommandBufferBorrowedBulkCreateTest() {
    kb::ecs::World world;
    std::vector<EcsPosition> positions{
        EcsPosition{ .x = 1.0F, .y = 2.0F },
        EcsPosition{ .x = 3.0F, .y = 4.0F },
        EcsPosition{ .x = 5.0F, .y = 6.0F },
    };
    std::vector<EcsVelocity> velocities{
        EcsVelocity{ .x = 10.0F, .y = 20.0F },
        EcsVelocity{ .x = 30.0F, .y = 40.0F },
        EcsVelocity{ .x = 50.0F, .y = 60.0F },
    };
    std::vector<kb::ecs::CommandBuffer::BulkComponentView> views{
        kb::ecs::CommandBuffer::MakeBulkComponentView<EcsPosition>(std::span<const EcsPosition>{ positions }),
        kb::ecs::CommandBuffer::MakeBulkComponentView<EcsVelocity>(std::span<const EcsVelocity>{ velocities }),
    };

    kb::ecs::CommandBuffer buffer{ 1 };
    const std::vector<kb::ecs::CommandEntity> deferred = buffer.Worker(0).CreateEntitiesBorrowed(positions.size(), std::span<const kb::ecs::CommandBuffer::BulkComponentView>{ views });
    const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);

    kb::tests::Require(result.CreatedCount() == positions.size(), "ECS borrowed bulk create did not create the expected entity count");
    kb::tests::Require(
        result.PlaybackStats().componentBytesCopied == (positions.size() * sizeof(EcsPosition)) + (velocities.size() * sizeof(EcsVelocity)),
        "ECS borrowed bulk create did not report component traffic");
    for (std::size_t index = 0; index < deferred.size(); ++index) {
        const kb::ecs::Entity entity = result.Resolve(deferred[index]);
        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        const EcsVelocity* velocity = world.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && kb::tests::NearlyEqual(position->x, positions[index].x), "ECS borrowed bulk create wrote an invalid position");
        kb::tests::Require(velocity != nullptr && kb::tests::NearlyEqual(velocity->y, velocities[index].y), "ECS borrowed bulk create wrote an invalid velocity");
    }
}

void RunCommandBufferBorrowedPatternBulkCreateTest() {
    kb::ecs::World world;
    constexpr std::size_t kEntityCount = 6U;
    std::array<EcsPosition, 2> positions{
        EcsPosition{ .x = 1.0F, .y = 2.0F },
        EcsPosition{ .x = 3.0F, .y = 4.0F },
    };
    std::array<EcsVelocity, 2> velocities{
        EcsVelocity{ .x = 10.0F, .y = 20.0F },
        EcsVelocity{ .x = 30.0F, .y = 40.0F },
    };
    std::vector<kb::ecs::CommandBuffer::BulkComponentView> views{
        kb::ecs::CommandBuffer::MakeBulkComponentView<EcsPosition>(std::span<const EcsPosition>{ positions }),
        kb::ecs::CommandBuffer::MakeBulkComponentView<EcsVelocity>(std::span<const EcsVelocity>{ velocities }),
    };
    for (kb::ecs::CommandBuffer::BulkComponentView& view : views) {
        view.componentCount = kEntityCount;
        view.sourceCount = positions.size();
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    const std::vector<kb::ecs::CommandEntity> deferred = buffer.Worker(0).CreateEntitiesBorrowed(kEntityCount, std::span<const kb::ecs::CommandBuffer::BulkComponentView>{ views });
    const kb::ecs::CommandBufferPlaybackResult::Stats estimate = buffer.EstimatePlaybackStats();
    const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);

    kb::tests::Require(estimate.componentBytesCopied == (kEntityCount * sizeof(EcsPosition)) + (kEntityCount * sizeof(EcsVelocity)), "ECS borrowed pattern bulk create estimate did not report logical component traffic");
    kb::tests::Require(result.PlaybackStats().componentBytesCopied == estimate.componentBytesCopied, "ECS borrowed pattern bulk create playback stats diverged from estimate");
    for (std::size_t index = 0; index < deferred.size(); ++index) {
        const std::size_t sourceIndex = index % positions.size();
        const kb::ecs::Entity entity = result.Resolve(deferred[index]);
        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        const EcsVelocity* velocity = world.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && kb::tests::NearlyEqual(position->x, positions[sourceIndex].x), "ECS borrowed pattern bulk create wrote an invalid position");
        kb::tests::Require(velocity != nullptr && kb::tests::NearlyEqual(velocity->y, velocities[sourceIndex].y), "ECS borrowed pattern bulk create wrote an invalid velocity");
    }
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
    const kb::ecs::CommandBufferPlaybackResult::Stats& stats = result.PlaybackStats();
    kb::tests::Require(stats.structuralCommands == 4U, "ECS command buffer bulk create reported invalid structural command telemetry");
    kb::tests::Require(stats.createCommands == 1U, "ECS command buffer bulk create reported invalid single create telemetry");
    kb::tests::Require(stats.bulkCreateCommands == 2U, "ECS command buffer bulk create reported invalid batch create telemetry");
    kb::tests::Require(stats.parentCommands == 1U, "ECS command buffer bulk create reported invalid parent telemetry");
    kb::tests::Require(
        stats.componentBytesCopied == positions.size() * sizeof(EcsPosition) + velocities.size() * sizeof(EcsVelocity),
        "ECS command buffer bulk create reported invalid copied byte telemetry");

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

void RunCommandBufferRuntimeBulkCreateArchetypeTest() {
    kb::ecs::World world;
    kb::ecs::CommandBuffer buffer{ 1 };

    std::vector<EcsPosition> positions{
        EcsPosition{ .x = 1.0F, .y = 2.0F },
        EcsPosition{ .x = 3.0F, .y = 4.0F },
        EcsPosition{ .x = 5.0F, .y = 6.0F },
    };
    std::vector<EcsVelocity> velocities{
        EcsVelocity{ .x = 10.0F, .y = 20.0F },
        EcsVelocity{ .x = 30.0F, .y = 40.0F },
        EcsVelocity{ .x = 50.0F, .y = 60.0F },
    };
    std::vector<kb::ecs::CommandBuffer::BulkComponentView> components{
        kb::ecs::CommandBuffer::MakeBulkComponentView<EcsPosition>(std::span<const EcsPosition>{ positions }),
        kb::ecs::CommandBuffer::MakeBulkComponentView<EcsVelocity>(std::span<const EcsVelocity>{ velocities }),
    };

    std::vector<kb::ecs::CommandEntity> entities = buffer.Worker(0).CreateEntities(positions.size(), std::span<const kb::ecs::CommandBuffer::BulkComponentView>{ components });
    kb::tests::Require(buffer.CommandCount() == 1, "ECS command buffer runtime bulk create did not record one archetype command");

    const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    const kb::ecs::ComponentId positionId = world.Component<EcsPosition>();
    const kb::ecs::ComponentId velocityId = world.Component<EcsVelocity>();
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = result.Resolve(entities[index]);
        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        const EcsVelocity* velocity = world.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && velocity != nullptr, "ECS command buffer runtime bulk create did not create the requested archetype");
        kb::tests::Require(world.NativeStorage().HasComponent(entity, positionId) && world.NativeStorage().HasComponent(entity, velocityId), "ECS command buffer runtime bulk create did not mirror native components");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, positions[index].x), "ECS command buffer runtime bulk create lost position data");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->x, velocities[index].x), "ECS command buffer runtime bulk create lost velocity data");
    }
}

void RunCommandBufferPlaybackBudgetTest() {
    kb::ecs::World world;
    const kb::ecs::Entity parent = world.CreateEntity("PlaybackBudgetParent");

    std::vector<EcsPosition> positions{
        EcsPosition{ .x = 1.0F, .y = 0.0F },
        EcsPosition{ .x = 2.0F, .y = 0.0F },
        EcsPosition{ .x = 3.0F, .y = 0.0F },
        EcsPosition{ .x = 4.0F, .y = 0.0F },
    };

    kb::ecs::CommandBuffer buffer{ 1 };
    std::vector<kb::ecs::CommandEntity> created = buffer.Worker(0).CreateEntities(std::span<const EcsPosition>{ positions });
    std::vector<kb::ecs::CommandEntity> parents(created.size(), kb::ecs::CommandEntity::Existing(parent));
    buffer.Worker(0).SetParents(std::span<const kb::ecs::CommandEntity>{ created }, std::span<const kb::ecs::CommandEntity>{ parents });

    const kb::ecs::CommandBufferPlaybackResult::Stats estimate = buffer.EstimatePlaybackStats();
    kb::tests::Require(estimate.structuralCommands == 2U, "ECS command buffer playback estimate reported invalid structural command count");
    kb::tests::Require(estimate.bulkCreateCommands == 1U, "ECS command buffer playback estimate reported invalid bulk create count");
    kb::tests::Require(estimate.parentCommands == created.size(), "ECS command buffer playback estimate reported invalid parent count");
    kb::tests::Require(estimate.componentBytesCopied == positions.size() * sizeof(EcsPosition), "ECS command buffer playback estimate reported invalid copied bytes");

    kb::ecs::CommandBufferPlaybackBudget tightBudget;
    tightBudget.maxStructuralCommands = 1;
    kb::tests::Require(!buffer.FitsPlaybackBudget(tightBudget), "ECS command buffer accepted a structural budget that is too small");

    bool rejected = false;
    try {
        static_cast<void>(buffer.Playback(world, tightBudget));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    kb::tests::Require(rejected, "ECS command buffer did not reject playback over budget");
    kb::tests::Require(buffer.CommandCount() == 2U, "ECS command buffer budget rejection consumed commands");
    kb::tests::Require(world.NativeStorageStats().liveEntities == 1U, "ECS command buffer budget rejection mutated the world");

    kb::ecs::CommandBufferPlaybackBudget exactBudget;
    exactBudget.maxStructuralCommands = estimate.structuralCommands;
    exactBudget.maxBulkCreateCommands = estimate.bulkCreateCommands;
    exactBudget.maxParentCommands = estimate.parentCommands;
    exactBudget.maxComponentBytesCopied = estimate.componentBytesCopied;
    kb::tests::Require(buffer.FitsPlaybackBudget(exactBudget), "ECS command buffer rejected an exact playback budget");

    const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world, exactBudget);
    kb::tests::Require(result.CreatedCount() == created.size(), "ECS command buffer budgeted playback created an unexpected entity count");
    kb::tests::Require(result.PlaybackStats().componentBytesCopied == estimate.componentBytesCopied, "ECS command buffer budgeted playback stats diverged from estimate");
    kb::tests::Require(buffer.Empty(), "ECS command buffer budgeted playback did not clear commands");
}

void RunCommandBufferPlaybackSliceTest() {
    kb::ecs::World world;
    const kb::ecs::Entity parent = world.CreateEntity("SliceParent");

    std::vector<EcsPosition> positions{
        EcsPosition{ .x = 10.0F, .y = 0.0F },
        EcsPosition{ .x = 20.0F, .y = 0.0F },
        EcsPosition{ .x = 30.0F, .y = 0.0F },
    };

    kb::ecs::CommandBuffer buffer{ 1 };
    std::vector<kb::ecs::CommandEntity> created = buffer.Worker(0).CreateEntities(std::span<const EcsPosition>{ positions });
    std::vector<kb::ecs::CommandEntity> parents(created.size(), kb::ecs::CommandEntity::Existing(parent));
    buffer.Worker(0).SetParents(std::span<const kb::ecs::CommandEntity>{ created }, std::span<const kb::ecs::CommandEntity>{ parents });

    kb::ecs::CommandBufferPlaybackBudget budget;
    budget.maxStructuralCommands = 1U;
    budget.maxBulkCreateCommands = 1U;
    budget.maxParentCommands = created.size();
    budget.maxComponentBytesCopied = positions.size() * sizeof(EcsPosition);

    kb::ecs::CommandBufferPlaybackState state;
    const kb::ecs::CommandBufferPlaybackSlice createSlice = buffer.PlaybackSlice(world, budget, state);
    kb::tests::Require(createSlice.madeProgress && !createSlice.complete, "ECS command buffer slice did not stop after the create budget");
    kb::tests::Require(createSlice.stats.structuralCommands == 1U && createSlice.stats.bulkCreateCommands == 1U, "ECS command buffer create slice reported invalid stats");
    kb::tests::Require(state.Started() && !state.Complete(), "ECS command buffer slice state did not record partial progress");
    kb::tests::Require(state.Result().CreatedCount() == created.size(), "ECS command buffer slice did not expose created deferred entities");

    for (kb::ecs::CommandEntity commandEntity : created) {
        const kb::ecs::Entity entity = state.Result().Resolve(commandEntity);
        kb::tests::Require(world.IsAlive(entity), "ECS command buffer slice did not create a deferred entity");
        kb::tests::Require(world.Parent(entity) == kb::ecs::Entity{}, "ECS command buffer slice applied parent work before the next slice");
    }

    const kb::ecs::CommandBufferPlaybackSlice parentSlice = buffer.PlaybackSlice(world, budget, state);
    kb::tests::Require(parentSlice.madeProgress && parentSlice.complete, "ECS command buffer slice did not finish on the second budgeted step");
    kb::tests::Require(parentSlice.stats.structuralCommands == 1U && parentSlice.stats.parentCommands == created.size(), "ECS command buffer parent slice reported invalid stats");
    kb::tests::Require(state.Complete(), "ECS command buffer slice state did not complete");
    kb::tests::Require(buffer.Empty(), "ECS command buffer slice playback did not clear commands after completion");

    for (std::size_t index = 0; index < created.size(); ++index) {
        const kb::ecs::Entity entity = state.Result().Resolve(created[index]);
        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        kb::tests::Require(position != nullptr && kb::tests::NearlyEqual(position->x, positions[index].x), "ECS command buffer slice lost created component data");
        kb::tests::Require(world.Parent(entity) == parent, "ECS command buffer slice did not resolve deferred parent commands");
    }
}

void RunCommandBufferPlaybackSliceDestroyBudgetTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(3U);
    for (std::size_t index = 0; index < 3U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        entities.push_back(entity);
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    for (kb::ecs::Entity entity : entities) {
        buffer.Worker(0).DestroyEntity(entity);
    }

    kb::ecs::CommandBufferPlaybackBudget budget;
    budget.maxStructuralCommands = 1U;
    budget.maxDestroyCommands = 1U;

    kb::ecs::CommandBufferPlaybackState state;
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::CommandBufferPlaybackSlice slice = buffer.PlaybackSlice(world, budget, state);
        kb::tests::Require(slice.madeProgress && !slice.complete, "ECS command buffer destroy slice did not schedule one destroy command");
        kb::tests::Require(slice.stats.destroyCommands == 1U, "ECS command buffer destroy slice reported invalid scheduled destroy stats");
        kb::tests::Require(state.Result().DestroyedCount() == index + 1U, "ECS command buffer destroy slice did not preserve cumulative destroyed refs");
        kb::tests::Require(slice.destroyedEntitiesApplied == 0U, "ECS command buffer destroy slice spent destroy budget twice");
        for (kb::ecs::Entity entity : entities) {
            kb::tests::Require(world.IsAlive(entity), "ECS command buffer destroy slice destroyed before the sync phase");
        }
    }

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::CommandBufferPlaybackSlice slice = buffer.PlaybackSlice(world, budget, state);
        kb::tests::Require(slice.madeProgress, "ECS command buffer destroy sync slice did not make progress");
        kb::tests::Require(slice.destroyedEntitiesApplied == 1U, "ECS command buffer destroy sync slice exceeded the destroy budget");
        for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
            kb::tests::Require(world.IsAlive(entities[entityIndex]) == (entityIndex > index), "ECS command buffer destroy sync slice applied the wrong entity range");
        }
    }

    kb::tests::Require(state.Complete(), "ECS command buffer destroy slice state did not complete");
    kb::tests::Require(buffer.Empty(), "ECS command buffer destroy slice playback did not clear commands after completion");
}

void RunCommandBufferPlaybackSliceBulkDestroyRangeBudgetTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(5U);
    for (std::size_t index = 0; index < 5U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        entities.push_back(entity);
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    buffer.Worker(0).DestroyEntities(std::span<const kb::ecs::Entity>{ entities });

    kb::ecs::CommandBufferPlaybackBudget budget;
    budget.maxStructuralCommands = 1U;
    budget.maxDestroyCommands = 2U;

    kb::ecs::CommandBufferPlaybackState state;
    std::vector<std::size_t> scheduledPerSlice;
    std::size_t appliedDuringScheduling = 0;
    while (state.Result().DestroyedCount() < entities.size()) {
        const kb::ecs::CommandBufferPlaybackSlice slice = buffer.PlaybackSlice(world, budget, state);
        kb::tests::Require(slice.madeProgress && !slice.complete, "ECS command buffer bulk destroy slice did not schedule a partial range");
        scheduledPerSlice.push_back(slice.stats.destroyCommands);
        kb::tests::Require(slice.stats.destroyCommands <= budget.maxDestroyCommands, "ECS command buffer bulk destroy slice exceeded the schedule budget");
        kb::tests::Require(
            slice.stats.destroyCommands + slice.destroyedEntitiesApplied <= budget.maxDestroyCommands,
            "ECS command buffer bulk destroy slice exceeded the shared destroy budget");
        appliedDuringScheduling += slice.destroyedEntitiesApplied;
        for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
            kb::tests::Require(
                world.IsAlive(entities[entityIndex]) == (entityIndex >= appliedDuringScheduling),
                "ECS command buffer bulk destroy slice applied an invalid early sync range");
        }
    }

    kb::tests::Require(
        scheduledPerSlice == std::vector<std::size_t>{ 2U, 2U, 1U },
        "ECS command buffer bulk destroy slice did not split the scheduled range by budget");
    kb::tests::Require(state.Result().PlaybackStats().structuralCommands == 1U, "ECS command buffer bulk destroy split counted the bulk command more than once");
    kb::tests::Require(state.Result().PlaybackStats().destroyCommands == entities.size(), "ECS command buffer bulk destroy split lost scheduled destroys");

    std::size_t applied = appliedDuringScheduling;
    while (!state.Complete()) {
        const kb::ecs::CommandBufferPlaybackSlice slice = buffer.PlaybackSlice(world, budget, state);
        kb::tests::Require(slice.madeProgress, "ECS command buffer bulk destroy sync did not progress");
        kb::tests::Require(slice.destroyedEntitiesApplied <= budget.maxDestroyCommands, "ECS command buffer bulk destroy sync exceeded the destroy budget");
        applied += slice.destroyedEntitiesApplied;
    }

    kb::tests::Require(applied == entities.size(), "ECS command buffer bulk destroy sync did not apply all scheduled destroys");
    for (kb::ecs::Entity entity : entities) {
        kb::tests::Require(!world.IsAlive(entity), "ECS command buffer bulk destroy sync left an entity alive");
    }
    kb::tests::Require(buffer.Empty(), "ECS command buffer bulk destroy slice playback did not clear commands after completion");
}

void RunCommandBufferPlaybackSliceBulkSetRangeBudgetTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> entities;
    std::vector<kb::ecs::CommandEntity> commandEntities;
    std::vector<EcsPosition> positions;
    std::vector<EcsVelocity> velocities;
    entities.reserve(5U);
    commandEntities.reserve(5U);
    positions.reserve(5U);
    velocities.reserve(5U);

    for (std::size_t index = 0; index < 5U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        entities.push_back(entity);
        commandEntities.push_back(kb::ecs::CommandEntity::Existing(entity));
        positions.push_back(EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index + 10U) });
        velocities.push_back(EcsVelocity{ .x = static_cast<float>(index + 20U), .y = static_cast<float>(index + 30U) });
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    buffer.Worker(0).Set(
        std::span<const kb::ecs::CommandEntity>{ commandEntities },
        std::span<const EcsPosition>{ positions },
        std::span<const EcsVelocity>{ velocities });

    kb::ecs::CommandBufferPlaybackBudget budget;
    budget.maxStructuralCommands = 1U;
    budget.maxComponentSetCommands = 4U;
    budget.maxComponentBytesCopied = 2U * (sizeof(EcsPosition) + sizeof(EcsVelocity));

    kb::ecs::CommandBufferPlaybackState state;
    std::vector<std::size_t> setPerSlice;
    while (!state.Complete()) {
        const kb::ecs::CommandBufferPlaybackSlice slice = buffer.PlaybackSlice(world, budget, state);
        kb::tests::Require(slice.madeProgress, "ECS command buffer bulk set slice did not progress");
        kb::tests::Require(slice.stats.componentSetCommands <= budget.maxComponentSetCommands, "ECS command buffer bulk set slice exceeded component budget");
        kb::tests::Require(slice.stats.componentBytesCopied <= budget.maxComponentBytesCopied, "ECS command buffer bulk set slice exceeded byte budget");
        setPerSlice.push_back(slice.stats.componentSetCommands);
    }

    kb::tests::Require(
        setPerSlice == std::vector<std::size_t>{ 4U, 4U, 2U },
        "ECS command buffer bulk set slice did not split component writes by budget");
    kb::tests::Require(state.Result().PlaybackStats().structuralCommands == 1U, "ECS command buffer bulk set split counted the bulk command more than once");
    kb::tests::Require(state.Result().PlaybackStats().componentSetCommands == entities.size() * 2U, "ECS command buffer bulk set split lost component writes");

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const EcsPosition* position = world.TryGet<EcsPosition>(entities[index]);
        const EcsVelocity* velocity = world.TryGet<EcsVelocity>(entities[index]);
        kb::tests::Require(position != nullptr && velocity != nullptr, "ECS command buffer bulk set slice did not add both components");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, positions[index].x), "ECS command buffer bulk set slice lost position data");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->y, velocities[index].y), "ECS command buffer bulk set slice lost velocity data");
    }
    kb::tests::Require(buffer.Empty(), "ECS command buffer bulk set slice playback did not clear commands after completion");
}

void RunCommandBufferPlaybackSliceBulkRemoveRangeBudgetTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(5U);
    for (std::size_t index = 0; index < 5U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = static_cast<float>(index), .y = 1.0F });
        entities.push_back(entity);
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    buffer.Worker(0).Remove<EcsPosition, EcsVelocity>(std::span<const kb::ecs::Entity>{ entities });

    kb::ecs::CommandBufferPlaybackBudget budget;
    budget.maxStructuralCommands = 1U;
    budget.maxComponentRemoveCommands = 4U;

    kb::ecs::CommandBufferPlaybackState state;
    std::vector<std::size_t> removedPerSlice;
    while (!state.Complete()) {
        const kb::ecs::CommandBufferPlaybackSlice slice = buffer.PlaybackSlice(world, budget, state);
        kb::tests::Require(slice.madeProgress, "ECS command buffer bulk remove slice did not progress");
        kb::tests::Require(slice.stats.componentRemoveCommands <= budget.maxComponentRemoveCommands, "ECS command buffer bulk remove slice exceeded component budget");
        removedPerSlice.push_back(slice.stats.componentRemoveCommands);
    }

    kb::tests::Require(
        removedPerSlice == std::vector<std::size_t>{ 4U, 4U, 2U },
        "ECS command buffer bulk remove slice did not split component removals by budget");
    kb::tests::Require(state.Result().PlaybackStats().structuralCommands == 1U, "ECS command buffer bulk remove split counted the bulk command more than once");
    kb::tests::Require(state.Result().PlaybackStats().componentRemoveCommands == entities.size() * 2U, "ECS command buffer bulk remove split lost component removals");

    for (kb::ecs::Entity entity : entities) {
        kb::tests::Require(world.IsAlive(entity), "ECS command buffer bulk remove slice destroyed an entity");
        kb::tests::Require(!world.Has<EcsPosition>(entity) && !world.Has<EcsVelocity>(entity), "ECS command buffer bulk remove slice left a removed component");
    }
    kb::tests::Require(buffer.Empty(), "ECS command buffer bulk remove slice playback did not clear commands after completion");
}

void RunCommandBufferPlaybackSliceBulkParentRangeBudgetTest() {
    kb::ecs::World world;
    const kb::ecs::Entity parent = world.CreateEntity("BulkParent");
    std::vector<kb::ecs::Entity> entities;
    std::vector<kb::ecs::CommandEntity> children;
    std::vector<kb::ecs::CommandEntity> parents;
    entities.reserve(5U);
    children.reserve(5U);
    parents.reserve(5U);
    for (std::size_t index = 0; index < 5U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        entities.push_back(entity);
        children.push_back(kb::ecs::CommandEntity::Existing(entity));
        parents.push_back(kb::ecs::CommandEntity::Existing(parent));
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    buffer.Worker(0).SetParents(std::span<const kb::ecs::CommandEntity>{ children }, std::span<const kb::ecs::CommandEntity>{ parents });

    kb::ecs::CommandBufferPlaybackBudget budget;
    budget.maxStructuralCommands = 1U;
    budget.maxParentCommands = 2U;

    kb::ecs::CommandBufferPlaybackState state;
    std::vector<std::size_t> parentsPerSlice;
    while (!state.Complete()) {
        const kb::ecs::CommandBufferPlaybackSlice slice = buffer.PlaybackSlice(world, budget, state);
        kb::tests::Require(slice.madeProgress, "ECS command buffer bulk parent slice did not progress");
        kb::tests::Require(slice.stats.parentCommands <= budget.maxParentCommands, "ECS command buffer bulk parent slice exceeded parent budget");
        parentsPerSlice.push_back(slice.stats.parentCommands);
    }

    kb::tests::Require(
        parentsPerSlice == std::vector<std::size_t>{ 2U, 2U, 1U },
        "ECS command buffer bulk parent slice did not split parent writes by budget");
    kb::tests::Require(state.Result().PlaybackStats().structuralCommands == 1U, "ECS command buffer bulk parent split counted the bulk command more than once");
    kb::tests::Require(state.Result().PlaybackStats().parentCommands == entities.size(), "ECS command buffer bulk parent split lost parent writes");

    for (kb::ecs::Entity entity : entities) {
        kb::tests::Require(world.Parent(entity) == parent, "ECS command buffer bulk parent slice left an invalid parent");
    }
    kb::tests::Require(buffer.Empty(), "ECS command buffer bulk parent slice playback did not clear commands after completion");
}

void RunCommandBufferPlaybackSliceBulkClearParentRangeBudgetTest() {
    kb::ecs::World world;
    const kb::ecs::Entity parent = world.CreateEntity("BulkClearParent");
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(5U);
    for (std::size_t index = 0; index < 5U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.SetParent(entity, parent);
        entities.push_back(entity);
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    buffer.Worker(0).ClearParents(std::span<const kb::ecs::Entity>{ entities });

    kb::ecs::CommandBufferPlaybackBudget budget;
    budget.maxStructuralCommands = 1U;
    budget.maxParentCommands = 2U;

    kb::ecs::CommandBufferPlaybackState state;
    std::vector<std::size_t> clearedPerSlice;
    while (!state.Complete()) {
        const kb::ecs::CommandBufferPlaybackSlice slice = buffer.PlaybackSlice(world, budget, state);
        kb::tests::Require(slice.madeProgress, "ECS command buffer bulk parent clear slice did not progress");
        kb::tests::Require(slice.stats.parentCommands <= budget.maxParentCommands, "ECS command buffer bulk parent clear slice exceeded parent budget");
        clearedPerSlice.push_back(slice.stats.parentCommands);
    }

    kb::tests::Require(
        clearedPerSlice == std::vector<std::size_t>{ 2U, 2U, 1U },
        "ECS command buffer bulk parent clear slice did not split clears by budget");
    kb::tests::Require(state.Result().PlaybackStats().structuralCommands == 1U, "ECS command buffer bulk parent clear split counted the bulk command more than once");
    kb::tests::Require(state.Result().PlaybackStats().parentCommands == entities.size(), "ECS command buffer bulk parent clear split lost clears");

    for (kb::ecs::Entity entity : entities) {
        kb::tests::Require(world.Parent(entity) == kb::ecs::Entity{}, "ECS command buffer bulk parent clear slice left a parent relation");
    }
    kb::tests::Require(buffer.Empty(), "ECS command buffer bulk parent clear slice playback did not clear commands after completion");
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
    const kb::ecs::CommandBufferPlaybackResult::Stats& stats = result.PlaybackStats();
    kb::tests::Require(stats.structuralCommands == 4U, "ECS command buffer bulk mutation reported invalid structural command telemetry");
    kb::tests::Require(stats.bulkCreateCommands == 1U, "ECS command buffer bulk mutation reported invalid batch create telemetry");
    kb::tests::Require(stats.componentSetCommands == 14U, "ECS command buffer bulk mutation reported invalid component set telemetry");
    kb::tests::Require(stats.componentRemoveCommands == 6U, "ECS command buffer bulk mutation reported invalid component remove telemetry");
    kb::tests::Require(
        stats.componentBytesCopied == positions.size() * sizeof(EcsPosition) + velocities.size() * sizeof(EcsVelocity) + deferredPositions.size() * sizeof(EcsPosition),
        "ECS command buffer bulk mutation reported invalid copied byte telemetry");

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

void RunCommandBufferBorrowedBulkSetSliceTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> entities;
    std::vector<EcsPosition> positions;
    entities.reserve(6U);
    positions.reserve(6U);

    for (int index = 0; index < 6; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = -1.0F, .y = -1.0F });
        entities.push_back(entity);
        positions.push_back(EcsPosition{ .x = static_cast<float>(50 + index), .y = static_cast<float>(70 + index) });
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    buffer.Worker(0).SetBorrowed(std::span<const kb::ecs::Entity>{ entities }, std::span<const EcsPosition>{ positions });

    kb::ecs::CommandBufferPlaybackBudget budget;
    budget.maxStructuralCommands = 1U;
    budget.maxComponentSetCommands = 2U;
    budget.maxComponentBytesCopied = 2U * sizeof(EcsPosition);

    kb::ecs::CommandBufferPlaybackState state;
    std::vector<std::size_t> setsPerSlice;
    while (!state.Complete()) {
        const kb::ecs::CommandBufferPlaybackSlice slice = buffer.PlaybackSlice(world, budget, state);
        kb::tests::Require(slice.madeProgress, "ECS command buffer borrowed bulk set slice did not progress");
        kb::tests::Require(slice.stats.componentSetCommands <= budget.maxComponentSetCommands, "ECS command buffer borrowed bulk set slice exceeded set budget");
        setsPerSlice.push_back(slice.stats.componentSetCommands);
    }

    kb::tests::Require(
        setsPerSlice == std::vector<std::size_t>{ 2U, 2U, 2U },
        "ECS command buffer borrowed bulk set slice did not split writes by budget");
    kb::tests::Require(state.Result().PlaybackStats().componentBytesCopied == positions.size() * sizeof(EcsPosition), "ECS command buffer borrowed bulk set reported invalid byte telemetry");

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const EcsPosition* position = world.TryGet<EcsPosition>(entities[index]);
        kb::tests::Require(position != nullptr, "ECS command buffer borrowed bulk set missed a component");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, positions[index].x), "ECS command buffer borrowed bulk set used an invalid payload offset");
        kb::tests::Require(kb::tests::NearlyEqual(position->y, positions[index].y), "ECS command buffer borrowed bulk set lost payload data");
    }
}

void RunCommandBufferBulkPlaybackOrderGuaranteeTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> entities;
    std::vector<EcsVelocity> initialVelocities;
    std::vector<EcsVelocity> replacementVelocities;
    std::vector<EcsPosition> finalPositions;
    entities.reserve(6U);
    initialVelocities.reserve(6U);
    replacementVelocities.reserve(6U);
    finalPositions.reserve(6U);

    for (int index = 0; index < 6; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        world.Set(entity, EcsVelocity{ .x = static_cast<float>(index + 1), .y = 1.0F });
        entities.push_back(entity);
        initialVelocities.push_back(EcsVelocity{ .x = static_cast<float>(index + 1), .y = 1.0F });
        replacementVelocities.push_back(EcsVelocity{ .x = static_cast<float>(100 + index), .y = 2.0F });
        finalPositions.push_back(EcsPosition{ .x = static_cast<float>(200 + index), .y = 3.0F });
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    buffer.Worker(0).Remove<EcsVelocity>(std::span<const kb::ecs::Entity>{ entities });
    buffer.Worker(0).Set(std::span<const kb::ecs::Entity>{ entities }, std::span<const EcsVelocity>{ replacementVelocities });
    buffer.Worker(0).DestroyEntities(std::span<const kb::ecs::Entity>{ entities.data() + 4U, 2U });
    buffer.Worker(0).Set(std::span<const kb::ecs::Entity>{ entities }, std::span<const EcsPosition>{ finalPositions });

    const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world);
    const kb::ecs::CommandBufferPlaybackResult::Stats& stats = result.PlaybackStats();
    kb::tests::Require(stats.structuralCommands == 4U, "ECS command buffer playback order reported invalid structural command count");
    kb::tests::Require(stats.componentRemoveCommands == entities.size(), "ECS command buffer playback order reported invalid remove count");
    kb::tests::Require(stats.componentSetCommands == entities.size() * 2U, "ECS command buffer playback order reported invalid set count");
    kb::tests::Require(stats.destroyCommands == 2U, "ECS command buffer playback order reported invalid destroy count");
    kb::tests::Require(result.DestroyedCount() == 2U, "ECS command buffer playback order did not report destroyed entities");

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const kb::ecs::Entity entity = entities[index];
        const bool destroyed = index >= 4U;
        kb::tests::Require(world.IsAlive(entity) != destroyed, "ECS command buffer playback order produced invalid entity lifetime");
        kb::tests::Require(result.WasDestroyed(entity) == destroyed, "ECS command buffer playback order reported invalid destroyed membership");
        if (destroyed) {
            continue;
        }

        const EcsPosition* position = world.TryGet<EcsPosition>(entity);
        const EcsVelocity* velocity = world.TryGet<EcsVelocity>(entity);
        kb::tests::Require(position != nullptr && velocity != nullptr, "ECS command buffer playback order lost a surviving component");
        kb::tests::Require(kb::tests::NearlyEqual(position->x, finalPositions[index].x), "ECS command buffer playback order did not apply the final position set");
        kb::tests::Require(kb::tests::NearlyEqual(velocity->x, replacementVelocities[index].x), "ECS command buffer playback order did not re-add velocity after remove");
        kb::tests::Require(!kb::tests::NearlyEqual(velocity->x, initialVelocities[index].x), "ECS command buffer playback order kept stale velocity data");
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
    bool destroyedComponentReadRejected = false;
    try {
        static_cast<void>(world.Has<EcsPosition>(resolvedCreated));
    } catch (const std::out_of_range&) {
        destroyedComponentReadRejected = true;
    }
    kb::tests::Require(destroyedComponentReadRejected, "ECS command buffer allowed component access on a destroyed deferred entity");

    bool existingComponentReadRejected = false;
    try {
        static_cast<void>(world.Has<EcsVelocity>(existing));
    } catch (const std::out_of_range&) {
        existingComponentReadRejected = true;
    }
    kb::tests::Require(existingComponentReadRejected, "ECS command buffer allowed component access on a destroyed existing entity");
}

void RunCommandBufferBulkDestroyBudgetTest() {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> entities;
    entities.reserve(5U);
    for (std::size_t index = 0; index < 5U; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 0.0F });
        entities.push_back(entity);
    }

    kb::ecs::CommandBuffer rejected{ 1 };
    rejected.Worker(0).DestroyEntities(std::span<const kb::ecs::Entity>{ entities.data(), 3U });
    kb::ecs::CommandBufferPlaybackBudget tightBudget;
    tightBudget.maxDestroyCommands = 2U;
    kb::tests::Require(!rejected.FitsPlaybackBudget(tightBudget), "ECS command buffer accepted a bulk destroy budget that is too small");
    bool rejectedBudget = false;
    try {
        static_cast<void>(rejected.Playback(world, tightBudget));
    } catch (const std::runtime_error&) {
        rejectedBudget = true;
    }
    kb::tests::Require(rejectedBudget, "ECS command buffer bulk destroy did not reject an exceeded budget");
    for (kb::ecs::Entity entity : entities) {
        kb::tests::Require(world.IsAlive(entity), "ECS command buffer bulk destroy budget rejection mutated the world");
    }

    kb::ecs::CommandBuffer buffer{ 1 };
    buffer.Worker(0).DestroyEntities(std::span<const kb::ecs::Entity>{ entities.data(), 3U });
    const kb::ecs::CommandBufferPlaybackResult::Stats estimate = buffer.EstimatePlaybackStats();
    kb::tests::Require(estimate.structuralCommands == 1U, "ECS command buffer bulk destroy estimate did not group structural work");
    kb::tests::Require(estimate.destroyCommands == 3U, "ECS command buffer bulk destroy estimate reported invalid destroy count");

    kb::ecs::CommandBufferPlaybackBudget exactBudget;
    exactBudget.maxStructuralCommands = estimate.structuralCommands;
    exactBudget.maxDestroyCommands = estimate.destroyCommands;
    const kb::ecs::CommandBufferPlaybackResult result = buffer.Playback(world, exactBudget);
    kb::tests::Require(result.PlaybackStats().structuralCommands == 1U, "ECS command buffer bulk destroy playback did not group structural work");
    kb::tests::Require(result.DestroyedCount() == 3U, "ECS command buffer bulk destroy reported invalid destroyed count");
    for (std::size_t index = 0; index < entities.size(); ++index) {
        kb::tests::Require(world.IsAlive(entities[index]) == (index >= 3U), "ECS command buffer bulk destroy applied an invalid entity set");
    }
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

void RunCommandBufferRollbackBulkCreateRestoresWorldTest() {
    kb::ecs::World world;
    const kb::ecs::Entity originalParent = world.CreateEntity("BulkRollbackParent");
    const kb::ecs::Entity existing = world.CreateEntity("BulkRollbackExisting");
    world.Set(existing, EcsPosition{ .x = 1.0F, .y = 2.0F });
    world.SetParent(existing, originalParent);

    std::vector<EcsPosition> positions{
        EcsPosition{ .x = 10.0F, .y = 0.0F },
        EcsPosition{ .x = 20.0F, .y = 0.0F },
        EcsPosition{ .x = 30.0F, .y = 0.0F },
        EcsPosition{ .x = 40.0F, .y = 0.0F },
    };

    kb::ecs::CommandBuffer buffer{ 2 };
    std::vector<kb::ecs::CommandEntity> created = buffer.Worker(0).CreateEntities(std::span<const EcsPosition>{ positions });
    buffer.Worker(0).SetParent(kb::ecs::CommandEntity::Existing(existing), created.front());
    buffer.Worker(0).Set(existing, EcsPosition{ .x = 99.0F, .y = 100.0F });
    buffer.Worker(1).Set(kb::ecs::CommandEntity::Deferred(4, 0), EcsVelocity{ .x = 1.0F, .y = 1.0F });

    bool threw = false;
    try {
        static_cast<void>(buffer.Playback(world));
    } catch (const std::exception&) {
        threw = true;
    }

    kb::tests::Require(threw, "ECS command buffer bulk rollback test did not propagate playback error");
    kb::tests::Require(world.IsAlive(originalParent) && world.IsAlive(existing), "ECS command buffer bulk rollback destroyed existing entities");
    kb::tests::Require(world.Parent(existing) == originalParent, "ECS command buffer bulk rollback did not restore existing parent");
    const EcsPosition* restoredPosition = world.TryGet<EcsPosition>(existing);
    kb::tests::Require(restoredPosition != nullptr && kb::tests::NearlyEqual(restoredPosition->x, 1.0F), "ECS command buffer bulk rollback did not restore existing component");
    kb::tests::Require(world.NativeStorageStats().liveEntities == 2U, "ECS command buffer bulk rollback left created native entities alive");

    EcsIterationCounters counters;
    world.ForEach<EcsPosition>(&CountPositions, &counters);
    kb::tests::Require(counters.visited == 1, "ECS command buffer bulk rollback left bulk-created component data visible");
    kb::tests::Require(!buffer.Empty(), "ECS command buffer cleared commands after failed bulk rollback playback");
}

} // namespace

namespace kb::tests {

void RunEcsCommandBufferTests() {
    RunCommandBufferDeterministicPlaybackTest();
    RunCommandBufferMultiWorkerDeterministicStructuralChangesTest();
    RunCommandBufferRandomStructuralChangeStressTest();
    RunCommandBufferBulkParentChangesTest();
    RunCommandBufferKnownAcyclicNewEntityParentChangesTest();
    RunCommandBufferKnownAcyclicMovedParentBatchTest();
    RunCommandBufferTrustedPlaybackBulkStructuralTest();
    RunCommandBufferTrustedPlaybackKeepsGenericBulkSetSafeTest();
    RunCommandBufferTrustedPlaybackHonorsNativeOnlyConfigTest();
    RunCommandBufferBorrowedBulkCreateTest();
    RunCommandBufferBorrowedPatternBulkCreateTest();
    RunCommandBufferStructuralChangesTest();
    RunCommandBufferBulkCreateSameArchetypeTest();
    RunCommandBufferRuntimeBulkCreateArchetypeTest();
    RunCommandBufferPlaybackBudgetTest();
    RunCommandBufferPlaybackSliceTest();
    RunCommandBufferPlaybackSliceDestroyBudgetTest();
    RunCommandBufferPlaybackSliceBulkDestroyRangeBudgetTest();
    RunCommandBufferPlaybackSliceBulkSetRangeBudgetTest();
    RunCommandBufferPlaybackSliceBulkRemoveRangeBudgetTest();
    RunCommandBufferPlaybackSliceBulkParentRangeBudgetTest();
    RunCommandBufferPlaybackSliceBulkClearParentRangeBudgetTest();
    RunCommandBufferBulkComponentMutationTest();
    RunCommandBufferBorrowedBulkSetSliceTest();
    RunCommandBufferBulkPlaybackOrderGuaranteeTest();
    RunCommandBufferDeferredDestroySyncPointTest();
    RunCommandBufferBulkDestroyBudgetTest();
    RunCommandBufferNestedCreateDestroyFromJobsTest();
    RunCommandBufferRollbackOnPlaybackErrorTest();
    RunCommandBufferRollbackBulkCreateRestoresWorldTest();
}

} // namespace kb::tests
