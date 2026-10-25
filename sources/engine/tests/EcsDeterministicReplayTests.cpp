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
        kb::ecs::Query<EcsPosition, EcsVelocity> query = world.CreateQuery<EcsPosition, EcsVelocity>();
        query.ForEachMutableBatchKernel(
            kb::ecs::QueryExecutionSettings{
                .iterationOrder = kb::ecs::QueryIterationOrder::Deterministic,
                .policy = kb::ecs::QueryExecutionPolicy::Deterministic,
                .reductionMode = kb::ecs::QueryReductionMode::Deterministic,
            },
            [deltaSeconds](kb::ecs::MutableQueryBatch<EcsPosition, EcsVelocity>& batch) {
                EcsPosition* positions = batch.Components<0>();
                const EcsVelocity* velocities = batch.Components<1>();
                for (std::size_t row = 0; row < batch.Count(); ++row) {
                    positions[row].x += velocities[row].x * deltaSeconds;
                    positions[row].y += velocities[row].y * deltaSeconds;
                }
            });
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
        kb::ecs::Query<EcsVelocity> query = world.CreateQuery<EcsVelocity>();
        query.ForEachMutableBatchKernel(
            kb::ecs::QueryExecutionSettings{
                .iterationOrder = kb::ecs::QueryIterationOrder::Deterministic,
                .policy = kb::ecs::QueryExecutionPolicy::Deterministic,
                .reductionMode = kb::ecs::QueryReductionMode::Deterministic,
            },
            [](kb::ecs::MutableQueryBatch<EcsVelocity>& batch) {
                EcsVelocity* velocities = batch.Components<0>();
                for (std::size_t row = 0; row < batch.Count(); ++row) {
                    velocities[row].x *= 0.875F;
                    velocities[row].y *= 0.875F;
                }
            });
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
        kb::ecs::Query<EcsPosition> query = world.CreateQuery<EcsPosition>();
        query.ForEachMutableBatchKernel(
            kb::ecs::QueryExecutionSettings{
                .iterationOrder = kb::ecs::QueryIterationOrder::Deterministic,
                .policy = kb::ecs::QueryExecutionPolicy::Deterministic,
                .reductionMode = kb::ecs::QueryReductionMode::Deterministic,
            },
            [](kb::ecs::MutableQueryBatch<EcsPosition>& batch) {
                EcsPosition* positions = batch.Components<0>();
                for (std::size_t row = 0; row < batch.Count(); ++row) {
                    positions[row].x += 0.25F;
                    positions[row].y -= 0.125F;
                }
            });
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

[[nodiscard]] ReplayResult RunReplay(std::size_t workerCount) {
    kb::ecs::World world;
    for (int index = 0; index < 48; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = static_cast<float>(index % 7) });
        world.Set(entity, EcsVelocity{ .x = 1.0F + static_cast<float>(index % 5), .y = -0.5F + static_cast<float>(index % 3) });
    }

    kb::ecs::WorkerPoolConfig workerPool;
    workerPool.singleThreaded = workerCount == 0U;
    workerPool.workerCount = workerCount;
    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{
        .mode = kb::ecs::SystemSchedulingMode::Deterministic,
        .parallelExecutionEnabled = workerCount != 0U,
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
    const ReplayResult first = RunReplay(0U);
    const ReplayResult second = RunReplay(0U);

    kb::tests::Require(first.executionOrder == second.executionOrder, "ECS deterministic replay produced different system order");
    kb::tests::Require(first.executionOrder.size() == 3U, "ECS deterministic replay omitted systems");
    kb::tests::Require(first.executionOrder[0] == "Replay.01.Integrate", "ECS deterministic replay ignored explicit dependency order");
    kb::tests::Require(first.checksum == second.checksum, "ECS deterministic replay produced different component results");
}

// Documented scope (docs/ecs_runtime_contracts.md): with the Deterministic scheduling mode and the
// Deterministic query policy, the same registration order and the same fixed step give the same
// system order and the same component values whatever the number of workers.
void RunDeterministicReplayIgnoresWorkerCountTest() {
    const ReplayResult reference = RunReplay(0U);
    for (const std::size_t workerCount : { std::size_t{ 1U }, std::size_t{ 2U }, std::size_t{ 4U }, std::size_t{ 7U } }) {
        const ReplayResult result = RunReplay(workerCount);
        const std::string message = "ECS deterministic replay changed with " + std::to_string(workerCount) + " workers";
        kb::tests::Require(result.executionOrder == reference.executionOrder && result.checksum == reference.checksum, message.c_str());
    }
}

// Golden result of the replay above. It is an exact-value contract for one toolchain family, not a
// promise of bitwise identical floating point on every platform: a Linux or ARM run that differs
// from this value is evidence about that platform (for example fused multiply-add contraction) and
// has to be recorded, not silently re-baselined.
constexpr std::int64_t kReplayGoldenChecksumMsvcX64 = 536938034;

void RunDeterministicReplayGoldenTest() {
    const ReplayResult result = RunReplay(0U);
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
    const std::string message = "ECS deterministic replay golden checksum changed: got " + std::to_string(result.checksum);
    kb::tests::Require(result.checksum == kReplayGoldenChecksumMsvcX64, message.c_str());
#else
    static_cast<void>(result);
    static_cast<void>(kReplayGoldenChecksumMsvcX64);
#endif
}

} // namespace

namespace kb::tests {

void RunEcsDeterministicReplayTests() {
    RunDeterministicReplayProducesStableResultTest();
    RunDeterministicReplayIgnoresWorkerCountTest();
    RunDeterministicReplayGoldenTest();
}

} // namespace kb::tests
