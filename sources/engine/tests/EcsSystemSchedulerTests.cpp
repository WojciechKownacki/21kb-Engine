#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/System.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/World.hpp"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::mutex g_recordingSystemMutex;

class RecordingSystem final : public kb::ecs::System {
public:
    RecordingSystem(
        std::string name,
        kb::ecs::SystemAccess access,
        std::vector<std::string>& executionOrder)
        : name_(std::move(name))
        , access_(std::move(access))
        , executionOrder_(executionOrder) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return name_;
    }

    [[nodiscard]] kb::ecs::SystemAccess DeclareAccess(kb::ecs::World& world) const override {
        static_cast<void>(world);
        return access_;
    }

    void OnUpdate(kb::ecs::World& world, float deltaSeconds) override {
        static_cast<void>(world);
        static_cast<void>(deltaSeconds);
        const std::scoped_lock lock{ g_recordingSystemMutex };
        executionOrder_.push_back(name_);
    }

private:
    std::string name_;
    kb::ecs::SystemAccess access_;
    std::vector<std::string>& executionOrder_;
};

class ConcurrentProbeSystem final : public kb::ecs::System {
public:
    ConcurrentProbeSystem(std::string name, kb::ecs::SystemAccess access, std::atomic<int>& running, std::atomic<int>& maxRunning)
        : name_(std::move(name))
        , access_(std::move(access))
        , running_(running)
        , maxRunning_(maxRunning) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return name_;
    }

    [[nodiscard]] kb::ecs::SystemAccess DeclareAccess(kb::ecs::World& world) const override {
        static_cast<void>(world);
        return access_;
    }

    void OnUpdate(kb::ecs::World& world, float deltaSeconds) override {
        static_cast<void>(world);
        static_cast<void>(deltaSeconds);
        const int active = running_.fetch_add(1, std::memory_order_acq_rel) + 1;
        int observed = maxRunning_.load(std::memory_order_acquire);
        while (active > observed && !maxRunning_.compare_exchange_weak(observed, active, std::memory_order_acq_rel)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds{ 25 });
        running_.fetch_sub(1, std::memory_order_acq_rel);
    }

private:
    std::string name_;
    kb::ecs::SystemAccess access_;
    std::atomic<int>& running_;
    std::atomic<int>& maxRunning_;
};

[[nodiscard]] kb::ecs::SystemAccess ReadPosition(kb::ecs::World& world) {
    kb::ecs::SystemAccess access;
    access.Read<EcsPosition>(world);
    return access;
}

[[nodiscard]] kb::ecs::SystemAccess WritePosition(kb::ecs::World& world) {
    kb::ecs::SystemAccess access;
    access.Write<EcsPosition>(world);
    return access;
}

void RunExplicitOrderingReordersSystemsTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::SystemAccess readerAccess;
    readerAccess.After("Writer");
    kb::ecs::SystemAccess writerAccess;

    kb::ecs::SystemScheduler scheduler;
    scheduler.Add(std::make_unique<RecordingSystem>("Reader", std::move(readerAccess), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("Writer", std::move(writerAccess), executionOrder), world);

    scheduler.Update(world, 0.0F);

    kb::tests::Require(executionOrder.size() == 2U, "ECS scheduler did not execute both systems");
    kb::tests::Require(executionOrder[0] == "Writer", "ECS scheduler ignored explicit RunAfter dependency");
    kb::tests::Require(executionOrder[1] == "Reader", "ECS scheduler produced invalid explicit dependency order");

    scheduler.Shutdown(world);
}

void RunReadWriteDependencyCycleTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::SystemAccess readerAccess = ReadPosition(world);
    readerAccess.After("Writer");
    kb::ecs::SystemAccess writerAccess = WritePosition(world);

    kb::ecs::SystemScheduler scheduler;
    scheduler.Add(std::make_unique<RecordingSystem>("Reader", std::move(readerAccess), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("Writer", std::move(writerAccess), executionOrder), world);

    bool detectedCycle = false;
    try {
        scheduler.Update(world, 0.0F);
    } catch (const std::runtime_error&) {
        detectedCycle = true;
    }

    kb::tests::Require(detectedCycle, "ECS scheduler did not detect a read/write dependency cycle");
    kb::tests::Require(executionOrder.empty(), "ECS scheduler executed systems after detecting a dependency cycle");

    scheduler.Shutdown(world);
}

void RunDeterministicModeOrdersIndependentSystemsByNameTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .mode = kb::ecs::SystemSchedulingMode::Deterministic } };
    scheduler.Add(std::make_unique<RecordingSystem>("Gamma", kb::ecs::SystemAccess{}, executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("Alpha", kb::ecs::SystemAccess{}, executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("Beta", kb::ecs::SystemAccess{}, executionOrder), world);

    const std::vector<std::string> snapshot = scheduler.ExecutionOrderSnapshot();
    kb::tests::Require(snapshot.size() == 3U, "ECS deterministic scheduler snapshot omitted systems");
    kb::tests::Require(snapshot[0] == "Alpha", "ECS deterministic scheduler did not sort independent systems by name");
    kb::tests::Require(snapshot[1] == "Beta", "ECS deterministic scheduler produced an invalid independent order");
    kb::tests::Require(snapshot[2] == "Gamma", "ECS deterministic scheduler produced an invalid independent order");

    scheduler.Update(world, 0.0F);

    kb::tests::Require(executionOrder == snapshot, "ECS deterministic scheduler runtime order differed from its snapshot");

    scheduler.Shutdown(world);
}

void RunDeterministicModeOrdersConflictingSystemsByNameTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .mode = kb::ecs::SystemSchedulingMode::Deterministic } };
    scheduler.Add(std::make_unique<RecordingSystem>("WriteB", WritePosition(world), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("WriteA", WritePosition(world), executionOrder), world);

    scheduler.Update(world, 0.0F);

    kb::tests::Require(executionOrder.size() == 2U, "ECS deterministic scheduler did not execute both conflicting systems");
    kb::tests::Require(executionOrder[0] == "WriteA", "ECS deterministic scheduler did not stabilize read/write conflict order");
    kb::tests::Require(executionOrder[1] == "WriteB", "ECS deterministic scheduler produced an invalid read/write conflict order");

    scheduler.Shutdown(world);
}

void RunSyncPointCreatesRegistrationOrderBarrierTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::SystemAccess syncPointAccess;
    syncPointAccess.RequireSyncPoint(kb::ecs::SystemSyncPoint::StructuralChanges);

    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .mode = kb::ecs::SystemSchedulingMode::Deterministic } };
    scheduler.Add(std::make_unique<RecordingSystem>("BeforeZ", WritePosition(world), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("BarrierM", std::move(syncPointAccess), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("AfterA", ReadPosition(world), executionOrder), world);

    const std::vector<std::string> snapshot = scheduler.ExecutionOrderSnapshot();
    kb::tests::Require(snapshot.size() == 3U, "ECS sync point scheduler snapshot omitted systems");
    kb::tests::Require(snapshot[0] == "BeforeZ", "ECS sync point did not keep earlier systems before the barrier");
    kb::tests::Require(snapshot[1] == "BarrierM", "ECS sync point did not execute at its registration boundary");
    kb::tests::Require(snapshot[2] == "AfterA", "ECS sync point did not keep later systems after the barrier");

    scheduler.Update(world, 0.0F);

    kb::tests::Require(executionOrder == snapshot, "ECS sync point runtime order differed from its snapshot");

    scheduler.Shutdown(world);
}

void RunSyncPointRequiresRuntimeBoundaryReasonTest() {
    kb::ecs::SystemAccess access;

    bool rejected = false;
    try {
        access.RequireSyncPoint(kb::ecs::SystemSyncPoint::None);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    kb::tests::Require(rejected, "ECS system access accepted a sync point without a runtime boundary reason");
}

void RunSchedulerRejectsNullSystemTest() {
    kb::ecs::World world;
    kb::ecs::SystemScheduler scheduler;

    bool rejected = false;
    try {
        scheduler.Add(nullptr, world);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    kb::tests::Require(rejected, "ECS scheduler accepted a null system registration");
}

void RunReadOnlySystemsShareExecutionStageTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .debugTraceEnabled = true } };
    scheduler.Add(std::make_unique<RecordingSystem>("ReadA", ReadPosition(world), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("ReadB", ReadPosition(world), executionOrder), world);

    scheduler.Update(world, 0.0F);

    const kb::ecs::SystemSchedulerTrace& trace = scheduler.LastDebugTrace();
    kb::tests::Require(trace.events.size() == 2U, "ECS scheduler trace omitted read-only systems");
    kb::tests::Require(trace.events[0].stageIndex == 0U, "ECS scheduler did not place the first read-only system in the first stage");
    kb::tests::Require(trace.events[1].stageIndex == 0U, "ECS scheduler serialized compatible read-only systems");
    kb::tests::Require(trace.events[0].blockedDependencies.empty(), "ECS scheduler added a dependency to a read-only system");
    kb::tests::Require(trace.events[1].blockedDependencies.empty(), "ECS scheduler added a dependency between compatible read-only systems");

    scheduler.Shutdown(world);
}

void RunReadOnlySystemsExecuteInParallelTest() {
    kb::ecs::World world;
    std::atomic<int> running{ 0 };
    std::atomic<int> maxRunning{ 0 };

    kb::ecs::WorkerPoolConfig workerConfig;
    workerConfig.workerCount = 2;
    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{
        .mode = kb::ecs::SystemSchedulingMode::Automatic,
        .debugTraceEnabled = true,
        .parallelExecutionEnabled = true,
        .workerPool = workerConfig,
    } };
    scheduler.Add(std::make_unique<ConcurrentProbeSystem>("ReadA", ReadPosition(world), running, maxRunning), world);
    scheduler.Add(std::make_unique<ConcurrentProbeSystem>("ReadB", ReadPosition(world), running, maxRunning), world);

    scheduler.Update(world, 0.0F);

    kb::tests::Require(maxRunning.load(std::memory_order_acquire) > 1, "ECS scheduler did not execute compatible read-only systems in parallel");
    kb::tests::Require(scheduler.LastDebugTrace().workers.size() >= 2U, "ECS scheduler trace did not report parallel worker occupancy");

    scheduler.Shutdown(world);
}

void RunReadWriteConflictCreatesStageBoundaryTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .debugTraceEnabled = true } };
    scheduler.Add(std::make_unique<RecordingSystem>("ReadPosition", ReadPosition(world), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("WritePosition", WritePosition(world), executionOrder), world);

    scheduler.Update(world, 0.0F);

    const kb::ecs::SystemSchedulerTrace& trace = scheduler.LastDebugTrace();
    kb::tests::Require(trace.events.size() == 2U, "ECS scheduler trace omitted read/write conflict systems");
    kb::tests::Require(trace.events[0].systemName == "ReadPosition", "ECS scheduler reordered the read side of a registration-order conflict");
    kb::tests::Require(trace.events[1].systemName == "WritePosition", "ECS scheduler reordered the write side of a registration-order conflict");
    kb::tests::Require(trace.events[0].stageIndex == 0U, "ECS scheduler did not place the read side in the first conflict stage");
    kb::tests::Require(trace.events[1].stageIndex == 1U, "ECS scheduler allowed read/write conflict systems to share a stage");
    kb::tests::Require(trace.events[1].blockedDependencies == std::vector<std::string>{ "ReadPosition" }, "ECS scheduler did not record the read/write dependency");

    scheduler.Shutdown(world);
}

void RunWriteWriteConflictCreatesStageBoundaryTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .debugTraceEnabled = true } };
    scheduler.Add(std::make_unique<RecordingSystem>("WritePositionA", WritePosition(world), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("WritePositionB", WritePosition(world), executionOrder), world);

    scheduler.Update(world, 0.0F);

    const kb::ecs::SystemSchedulerTrace& trace = scheduler.LastDebugTrace();
    kb::tests::Require(trace.events.size() == 2U, "ECS scheduler trace omitted write/write conflict systems");
    kb::tests::Require(trace.events[0].stageIndex == 0U, "ECS scheduler did not place the first writer in the first conflict stage");
    kb::tests::Require(trace.events[1].stageIndex == 1U, "ECS scheduler allowed write/write conflict systems to share a stage");
    kb::tests::Require(trace.events[1].blockedDependencies == std::vector<std::string>{ "WritePositionA" }, "ECS scheduler did not record the write/write dependency");

    scheduler.Shutdown(world);
}

void RunDebugTraceCapturesSystemExecutionTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::SystemAccess dependentAccess;
    dependentAccess.After("TraceA");

    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .debugTraceEnabled = true } };
    scheduler.Add(std::make_unique<RecordingSystem>("TraceA", kb::ecs::SystemAccess{}, executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("TraceB", std::move(dependentAccess), executionOrder), world);

    scheduler.Update(world, 0.0F);

    const kb::ecs::SystemSchedulerTrace& trace = scheduler.LastDebugTrace();
    kb::tests::Require(trace.frameIndex == 0U, "ECS scheduler debug trace did not start at frame zero");
    kb::tests::Require(trace.events.size() == 2U, "ECS scheduler debug trace omitted system events");
    kb::tests::Require(trace.workers.size() == 1U, "ECS scheduler debug trace did not report the caller worker");
    kb::tests::Require(trace.events[0].systemName == "TraceA", "ECS scheduler debug trace recorded an invalid first system");
    kb::tests::Require(trace.events[1].systemName == "TraceB", "ECS scheduler debug trace recorded an invalid second system");
    kb::tests::Require(trace.events[0].workerIndex == 0U && trace.events[1].workerIndex == 0U, "ECS scheduler debug trace reported an invalid worker index");
    kb::tests::Require(trace.events[0].endTimeNanoseconds >= trace.events[0].startTimeNanoseconds, "ECS scheduler debug trace recorded an invalid first system time range");
    kb::tests::Require(trace.events[1].endTimeNanoseconds >= trace.events[1].startTimeNanoseconds, "ECS scheduler debug trace recorded an invalid second system time range");
    kb::tests::Require(trace.events[0].durationNanoseconds == trace.events[0].endTimeNanoseconds - trace.events[0].startTimeNanoseconds, "ECS scheduler debug trace recorded an invalid first system duration");
    kb::tests::Require(trace.events[1].durationNanoseconds == trace.events[1].endTimeNanoseconds - trace.events[1].startTimeNanoseconds, "ECS scheduler debug trace recorded an invalid second system duration");
    kb::tests::Require(trace.events[0].waitTimeNanoseconds == 0U, "ECS scheduler debug trace recorded dependency wait time for an independent system");
    kb::tests::Require(trace.events[1].blockedDependencies == std::vector<std::string>{ "TraceA" }, "ECS scheduler debug trace did not record blocked dependencies");
    kb::tests::Require(trace.workers[0].workerIndex == 0U, "ECS scheduler debug trace recorded an invalid worker occupancy index");
    kb::tests::Require(trace.workers[0].busyTimeNanoseconds == trace.events[0].durationNanoseconds + trace.events[1].durationNanoseconds, "ECS scheduler debug trace worker busy time did not match system durations");
    kb::tests::Require(trace.frameDurationNanoseconds >= trace.workers[0].busyTimeNanoseconds, "ECS scheduler debug trace frame duration is shorter than worker busy time");

    scheduler.Update(world, 0.0F);
    kb::tests::Require(scheduler.LastDebugTrace().frameIndex == 1U, "ECS scheduler debug trace did not advance frame indexes");

    scheduler.Shutdown(world);
}

} // namespace

namespace kb::tests {

void RunEcsSystemSchedulerTests() {
    RunExplicitOrderingReordersSystemsTest();
    RunReadWriteDependencyCycleTest();
    RunDeterministicModeOrdersIndependentSystemsByNameTest();
    RunDeterministicModeOrdersConflictingSystemsByNameTest();
    RunSyncPointCreatesRegistrationOrderBarrierTest();
    RunSyncPointRequiresRuntimeBoundaryReasonTest();
    RunSchedulerRejectsNullSystemTest();
    RunReadOnlySystemsShareExecutionStageTest();
    RunReadOnlySystemsExecuteInParallelTest();
    RunReadWriteConflictCreatesStageBoundaryTest();
    RunWriteWriteConflictCreatesStageBoundaryTest();
    RunDebugTraceCapturesSystemExecutionTest();
}

} // namespace kb::tests
