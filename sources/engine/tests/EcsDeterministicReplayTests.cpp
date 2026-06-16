#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/System.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/World.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ReplayResult {
    std::vector<std::string> executionOrder;
    std::int64_t checksum = 0;
};

class ReplayIntegrateSystem final : public kb::ecs::System {
public:
    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Replay.01.Integrate";
    }

    [[nodiscard]] kb::ecs::SystemAccess DeclareAccess(kb::ecs::World& world) const override {
        kb::ecs::SystemAccess access;
        access.Read<EcsVelocity>(world);
        access.Write<EcsPosition>(world);
        return access;
    }

    void OnUpdate(kb::ecs::World& world, float deltaSeconds) override {
        struct Context {
            kb::ecs::World* world = nullptr;
            float deltaSeconds = 0.0F;
        } context{ .world = &world, .deltaSeconds = deltaSeconds };

        world.ForEachMutable<EcsPosition>(
            [](kb::ecs::Entity entity, EcsPosition& position, void* userData) {
                const auto* updateContext = static_cast<const Context*>(userData);
                const EcsVelocity* velocity = updateContext->world->TryGet<EcsVelocity>(entity);
                if (velocity != nullptr) {
                    position.x += velocity->x * updateContext->deltaSeconds;
                    position.y += velocity->y * updateContext->deltaSeconds;
                }
            },
            &context);
    }
};

class ReplayDampenVelocitySystem final : public kb::ecs::System {
public:
    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Replay.02.DampenVelocity";
    }

    [[nodiscard]] kb::ecs::SystemAccess DeclareAccess(kb::ecs::World& world) const override {
        kb::ecs::SystemAccess access;
        access.Write<EcsVelocity>(world);
        access.After("Replay.01.Integrate");
        return access;
    }

    void OnUpdate(kb::ecs::World& world, float deltaSeconds) override {
        static_cast<void>(deltaSeconds);
        world.ForEachMutable<EcsVelocity>(
            [](kb::ecs::Entity, EcsVelocity& velocity, void*) {
                velocity.x *= 0.875F;
                velocity.y *= 0.875F;
            },
            nullptr);
    }
};

class ReplayOffsetPositionSystem final : public kb::ecs::System {
public:
    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Replay.03.OffsetPosition";
    }

    [[nodiscard]] kb::ecs::SystemAccess DeclareAccess(kb::ecs::World& world) const override {
        kb::ecs::SystemAccess access;
        access.Write<EcsPosition>(world);
        access.After("Replay.02.DampenVelocity");
        return access;
    }

    void OnUpdate(kb::ecs::World& world, float deltaSeconds) override {
        static_cast<void>(deltaSeconds);
        world.ForEachMutable<EcsPosition>(
            [](kb::ecs::Entity, EcsPosition& position, void*) {
                position.x += 0.25F;
                position.y -= 0.125F;
            },
            nullptr);
    }
};

void AccumulateReplayChecksum(kb::ecs::Entity entity, const EcsPosition& position, const EcsVelocity& velocity, void* context) {
    auto* checksum = static_cast<std::int64_t*>(context);
    const std::int64_t id = static_cast<std::int64_t>(entity.Id());
    const std::int64_t positionX = static_cast<std::int64_t>(position.x * 10'000.0F);
    const std::int64_t positionY = static_cast<std::int64_t>(position.y * 10'000.0F);
    const std::int64_t velocityX = static_cast<std::int64_t>(velocity.x * 10'000.0F);
    const std::int64_t velocityY = static_cast<std::int64_t>(velocity.y * 10'000.0F);
    *checksum += (id * 31) ^ (positionX * 17) ^ (positionY * 13) ^ (velocityX * 7) ^ velocityY;
}

[[nodiscard]] ReplayResult RunReplay() {
    kb::ecs::World world;
    for (int index = 0; index < 48; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index % 7) });
        world.Set(entity, EcsVelocity{ .x = 1.0F + static_cast<float>(index % 5), .y = -0.5F + static_cast<float>(index % 3) });
    }

    kb::ecs::WorkerPoolConfig workerPool;
    workerPool.singleThreaded = true;
    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{
        .mode = kb::ecs::SystemSchedulingMode::Deterministic,
        .parallelExecutionEnabled = false,
        .workerPool = workerPool,
    } };
    scheduler.Add(std::make_unique<ReplayOffsetPositionSystem>(), world);
    scheduler.Add(std::make_unique<ReplayDampenVelocitySystem>(), world);
    scheduler.Add(std::make_unique<ReplayIntegrateSystem>(), world);

    const std::vector<std::string> executionOrder = scheduler.ExecutionOrderSnapshot();
    for (int frame = 0; frame < 8; ++frame) {
        scheduler.Update(world, 1.0F / 60.0F);
    }

    ReplayResult result{ .executionOrder = executionOrder };
    world.ForEach<EcsPosition, EcsVelocity>(&AccumulateReplayChecksum, &result.checksum);
    scheduler.Shutdown(world);
    return result;
}

void RunDeterministicReplayProducesStableResultTest() {
    const ReplayResult first = RunReplay();
    const ReplayResult second = RunReplay();

    kb::tests::Require(first.executionOrder == second.executionOrder, "ECS deterministic replay produced different system order");
    kb::tests::Require(first.executionOrder.size() == 3U, "ECS deterministic replay omitted systems");
    kb::tests::Require(first.executionOrder[0] == "Replay.01.Integrate", "ECS deterministic replay ignored explicit dependency order");
    kb::tests::Require(first.checksum == second.checksum, "ECS deterministic replay produced different component results");
}

} // namespace

namespace kb::tests {

void RunEcsDeterministicReplayTests() {
    RunDeterministicReplayProducesStableResultTest();
}

} // namespace kb::tests
