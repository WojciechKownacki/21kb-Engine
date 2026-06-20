#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/System.hpp"
#include "engine/ecs/RuntimeAccessValidator.hpp"
#include "engine/ecs/SystemFusionPlanner.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/SystemSchedulerTraceExport.hpp"
#include "engine/ecs/World.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
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

class ProfilingProbeSystem final : public kb::ecs::System {
public:
    ProfilingProbeSystem(std::uint64_t entitiesProcessed, std::uint64_t bytesTouched)
        : entitiesProcessed_(entitiesProcessed)
        , bytesTouched_(bytesTouched) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "ProfilingProbe";
    }

    [[nodiscard]] kb::ecs::SystemAccess DeclareAccess(kb::ecs::World& world) const override {
        static_cast<void>(world);
        return kb::ecs::SystemAccess{};
    }

    void OnUpdate(kb::ecs::World& world, float deltaSeconds) override {
        static_cast<void>(world);
        static_cast<void>(deltaSeconds);
        ReportProfilerWork(entitiesProcessed_, bytesTouched_);
    }

private:
    std::uint64_t entitiesProcessed_ = 0;
    std::uint64_t bytesTouched_ = 0;
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

void RunAssetBoundarySyncPointCreatesRegistrationOrderBarrierTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::SystemAccess assetBoundaryAccess;
    assetBoundaryAccess.RequireSyncPoint(kb::ecs::SystemSyncPoint::AssetBoundary);

    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .mode = kb::ecs::SystemSchedulingMode::Deterministic } };
    scheduler.Add(std::make_unique<RecordingSystem>("BeforeAssetZ", ReadPosition(world), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("AssetBoundaryM", std::move(assetBoundaryAccess), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("AfterAssetA", ReadPosition(world), executionOrder), world);

    const std::vector<std::string> snapshot = scheduler.ExecutionOrderSnapshot();
    kb::tests::Require(snapshot == std::vector<std::string>{ "BeforeAssetZ", "AssetBoundaryM", "AfterAssetA" }, "ECS asset sync point did not preserve registration boundary order");

    scheduler.Update(world, 0.0F);
    kb::tests::Require(executionOrder == snapshot, "ECS asset sync point runtime order differed from its snapshot");

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
    workerConfig.collectDispatchTelemetry = true;
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
    const kb::ecs::SystemSchedulerTrace& trace = scheduler.LastDebugTrace();
    kb::tests::Require(trace.workers.size() >= 2U, "ECS scheduler trace did not report parallel worker occupancy");
    kb::tests::Require(trace.frameCounters.workerDispatchCount == 1U, "ECS scheduler trace did not report worker dispatch count");
    kb::tests::Require(trace.frameCounters.workerStaticStridedDispatchCount == 1U, "ECS scheduler trace did not report static worker dispatch");
    kb::tests::Require(trace.frameCounters.workerQueuedDispatchCount == 0U, "ECS scheduler trace misreported static worker dispatch as queued");
    kb::tests::Require(trace.frameCounters.lastWorkerDispatchMode == "batches_static_strided", "ECS scheduler trace reported an invalid worker dispatch mode");
    kb::tests::Require(trace.frameCounters.lastWorkerDispatchWorkItemCount == 2U, "ECS scheduler trace reported an invalid worker dispatch work item count");
    kb::tests::Require(trace.frameCounters.lastWorkerDispatchActiveWorkerCount == 2U, "ECS scheduler trace reported an invalid active worker count");
    kb::tests::Require(trace.frameCounters.lastWorkerStealCount == 0U, "ECS scheduler trace reported steals for static worker dispatch");
    kb::tests::Require(trace.frameCounters.workerStealCount == 0U, "ECS scheduler trace reported total steals for static worker dispatch");
    kb::tests::Require(trace.frameCounters.lastWorkerDispatchScheduleNanoseconds > 0U, "ECS scheduler trace did not report dispatch schedule time");
    kb::tests::Require(trace.frameCounters.workerDispatchScheduleNanoseconds >= trace.frameCounters.lastWorkerDispatchScheduleNanoseconds, "ECS scheduler trace reported invalid total dispatch schedule time");
    kb::tests::Require(trace.frameCounters.averageWorkerDispatchScheduleNanoseconds > 0U, "ECS scheduler trace did not report average dispatch schedule time");
    kb::tests::Require(trace.frameCounters.lastWorkerDispatchWallNanoseconds > 0U, "ECS scheduler trace did not report worker dispatch wall time");
    kb::tests::Require(trace.frameCounters.workerDispatchWallNanoseconds >= trace.frameCounters.lastWorkerDispatchWallNanoseconds, "ECS scheduler trace reported invalid total worker dispatch wall time");
    kb::tests::Require(trace.frameCounters.averageWorkerDispatchWallNanoseconds > 0U, "ECS scheduler trace did not report average worker dispatch wall time");
    kb::tests::Require(trace.frameCounters.lastWorkerActiveNanoseconds > 0U, "ECS scheduler trace did not report worker active time");
    kb::tests::Require(trace.frameCounters.workerActiveNanoseconds >= trace.frameCounters.lastWorkerActiveNanoseconds, "ECS scheduler trace reported invalid total worker active time");
    kb::tests::Require(trace.frameCounters.averageWorkerActiveNanoseconds > 0U, "ECS scheduler trace did not report average worker active time");
    kb::tests::Require(trace.frameCounters.lastWorkerCapacityNanoseconds >= trace.frameCounters.lastWorkerDispatchWallNanoseconds, "ECS scheduler trace reported invalid worker capacity time");
    kb::tests::Require(trace.frameCounters.workerCapacityNanoseconds >= trace.frameCounters.lastWorkerCapacityNanoseconds, "ECS scheduler trace reported invalid total worker capacity time");
    kb::tests::Require(trace.frameCounters.lastWorkerUtilizationPercent > 0.0, "ECS scheduler trace did not report worker utilization");
    kb::tests::Require(trace.frameCounters.averageWorkerUtilizationPercent > 0.0, "ECS scheduler trace did not report average worker utilization");

    scheduler.Shutdown(world);
}

void RunSingleThreadSchedulerRunsCompatibleSystemsOnCallerThreadTest() {
    kb::ecs::World world;
    std::vector<std::string> executionOrder;

    kb::ecs::WorkerPoolConfig workerConfig;
    workerConfig.singleThreaded = true;
    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{
        .debugTraceEnabled = true,
        .parallelExecutionEnabled = false,
        .workerPool = workerConfig,
    } };
    scheduler.Add(std::make_unique<RecordingSystem>("ReadA", ReadPosition(world), executionOrder), world);
    scheduler.Add(std::make_unique<RecordingSystem>("ReadB", ReadPosition(world), executionOrder), world);

    scheduler.Update(world, 0.0F);

    const kb::ecs::SystemSchedulerTrace& trace = scheduler.LastDebugTrace();
    kb::tests::Require(executionOrder == std::vector<std::string>{ "ReadA", "ReadB" }, "ECS single-thread scheduler produced invalid execution order");
    kb::tests::Require(trace.events.size() == 2U, "ECS single-thread scheduler trace omitted systems");
    kb::tests::Require(trace.workers.size() == 1U, "ECS single-thread scheduler reported extra workers");
    kb::tests::Require(trace.events[0].workerIndex == 0U && trace.events[1].workerIndex == 0U, "ECS single-thread scheduler did not run on caller worker");

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

void RunRuntimeAccessValidatorAllowsReadOnlyJobsTest() {
    kb::ecs::World world;
    kb::ecs::RuntimeAccessValidator validator;
    const kb::ecs::SystemAccess readAccess = ReadPosition(world);

    kb::ecs::RuntimeAccessValidator::Guard firstRead = validator.Acquire("ReadA", readAccess, 0);
    kb::ecs::RuntimeAccessValidator::Guard secondRead = validator.Acquire("ReadB", readAccess, 1);

    kb::tests::Require(firstRead.Active() && secondRead.Active(), "ECS runtime access validator did not track read-only jobs");
    kb::tests::Require(validator.ActiveAccessCount() == 2U, "ECS runtime access validator rejected compatible read-only jobs");
}

void RunRuntimeAccessValidatorDetectsReadWriteConflictTest() {
    kb::ecs::World world;
    kb::ecs::RuntimeAccessValidator validator;
    const kb::ecs::SystemAccess readAccess = ReadPosition(world);
    const kb::ecs::SystemAccess writeAccess = WritePosition(world);

    kb::ecs::RuntimeAccessValidator::Guard readGuard = validator.Acquire("ReadPosition", readAccess, 0);

    bool detectedConflict = false;
    try {
        kb::ecs::RuntimeAccessValidator::Guard writeGuard = validator.Acquire("WritePosition", writeAccess, 1);
        static_cast<void>(writeGuard);
    } catch (const std::logic_error& error) {
        detectedConflict = std::string_view{ error.what() }.find("ReadPosition") != std::string_view::npos &&
            std::string_view{ error.what() }.find("WritePosition") != std::string_view::npos;
    }

    kb::tests::Require(detectedConflict, "ECS runtime access validator did not report a read/write conflict between active jobs");
    kb::tests::Require(validator.ActiveAccessCount() == 1U, "ECS runtime access validator registered a rejected conflicting job");

    readGuard.Release();
    kb::ecs::RuntimeAccessValidator::Guard writeGuard = validator.Acquire("WritePosition", writeAccess, 1);
    kb::tests::Require(writeGuard.Active(), "ECS runtime access validator did not release completed job access");
}

void RunRuntimeAccessValidatorDetectsWriteWriteConflictTest() {
    kb::ecs::World world;
    kb::ecs::RuntimeAccessValidator validator;
    const kb::ecs::SystemAccess writeAccess = WritePosition(world);

    kb::ecs::RuntimeAccessValidator::Guard firstWrite = validator.Acquire("WriteA", writeAccess, 0);

    bool detectedConflict = false;
    try {
        kb::ecs::RuntimeAccessValidator::Guard secondWrite = validator.Acquire("WriteB", writeAccess, 1);
        static_cast<void>(secondWrite);
    } catch (const std::logic_error&) {
        detectedConflict = true;
    }

    kb::tests::Require(detectedConflict, "ECS runtime access validator did not report a write/write conflict between active jobs");
    kb::tests::Require(validator.ActiveAccessCount() == 1U, "ECS runtime access validator leaked a rejected writer");
    firstWrite.Release();
    kb::tests::Require(validator.ActiveAccessCount() == 0U, "ECS runtime access validator did not release writer access");
}

void RunProfilerCountersCaptureFrameAndSystemWorkTest() {
    kb::ecs::World world;
    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .profilerEnabled = true } };
    scheduler.Add(std::make_unique<ProfilingProbeSystem>(42, 336), world);

    scheduler.Update(world, 0.0F);

    const kb::ecs::SystemSchedulerTrace& trace = scheduler.LastProfilerTrace();
    kb::tests::Require(trace.frameCounters.frameIndex == 0U, "ECS profiler counters did not start at frame zero");
    kb::tests::Require(trace.frameCounters.systemCount == 1U, "ECS profiler counters recorded invalid system count");
    kb::tests::Require(trace.frameCounters.stageCount == 1U, "ECS profiler counters recorded invalid stage count");
    kb::tests::Require(trace.frameCounters.jobsCount == 1U, "ECS profiler counters recorded invalid job count");
    kb::tests::Require(trace.frameCounters.chunkJobsCount == 0U, "ECS profiler counters recorded chunk jobs for a plain virtual system");
    kb::tests::Require(trace.frameCounters.entitiesProcessed == 42U, "ECS profiler counters omitted processed entities");
    kb::tests::Require(trace.frameCounters.bytesTouched == 336U, "ECS profiler counters omitted touched bytes");
    kb::tests::Require(trace.frameCounters.workerCount == 1U, "ECS profiler counters recorded invalid worker count");
    kb::tests::Require(trace.stageCounters.size() == 1U, "ECS profiler counters omitted per-stage counters");
    kb::tests::Require(trace.stageCounters[0].systemCount == 1U, "ECS profiler stage counters recorded invalid system count");
    kb::tests::Require(trace.stageCounters[0].jobsCount == 1U, "ECS profiler stage counters recorded invalid job count");
    kb::tests::Require(trace.stageCounters[0].chunkJobsCount == 0U, "ECS profiler stage counters recorded invalid chunk job count");
    kb::tests::Require(trace.stageCounters[0].workerBusyTimeNanoseconds > 0U, "ECS profiler stage counters omitted worker busy time");
    kb::tests::Require(trace.systemCounters.size() == 1U, "ECS profiler counters omitted per-system counters");
    kb::tests::Require(trace.systemCounters[0].systemName == "ProfilingProbe", "ECS profiler counters recorded invalid system name");
    kb::tests::Require(trace.systemCounters[0].executionPath == "virtual_callback", "ECS profiler counters did not report the virtual callback execution path");
    kb::tests::Require(trace.systemCounters[0].jobsCount == 1U, "ECS profiler counters recorded invalid per-system job count");
    kb::tests::Require(trace.systemCounters[0].chunkJobsCount == 0U, "ECS profiler counters recorded invalid per-system chunk job count");
    kb::tests::Require(trace.systemCounters[0].entitiesProcessed == 42U, "ECS profiler counters recorded invalid per-system entity count");
    kb::tests::Require(trace.systemCounters[0].bytesTouched == 336U, "ECS profiler counters recorded invalid per-system byte count");
    kb::tests::Require(trace.events.size() == 1U, "ECS profiler trace omitted system event");
    kb::tests::Require(trace.events[0].executionPath == "virtual_callback", "ECS profiler trace event did not report the virtual callback execution path");
    kb::tests::Require(trace.events[0].jobsCount == 1U, "ECS profiler trace recorded invalid event job count");
    kb::tests::Require(trace.events[0].chunkJobsCount == 0U, "ECS profiler trace recorded invalid event chunk job count");
    kb::tests::Require(trace.events[0].entitiesProcessed == 42U, "ECS profiler trace recorded invalid event entity count");
    kb::tests::Require(trace.events[0].bytesTouched == 336U, "ECS profiler trace recorded invalid event byte count");
    kb::tests::Require(trace.workers[0].utilizationPermille <= 1000U, "ECS profiler counters recorded invalid worker utilization");

    scheduler.Shutdown(world);
}

void RunProfilerTraceExportWritesExternalJsonFileTest() {
    kb::ecs::World world;
    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .profilerEnabled = true } };
    scheduler.Add(std::make_unique<ProfilingProbeSystem>(7, 56), world);
    scheduler.Update(world, 0.0F);

    const std::filesystem::path outputPath = std::filesystem::temp_directory_path() / "kb_ecs_scheduler_trace_test.json";
    kb::ecs::ExportSystemSchedulerTraceToJsonFile(scheduler.LastProfilerTrace(), outputPath);

    std::ifstream input(outputPath, std::ios::binary);
    kb::tests::Require(input.is_open(), "ECS profiler trace export did not create an output file");
    const std::string content{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    kb::tests::Require(content.find("\"schema\": \"kb.ecs.scheduler_trace.v1\"") != std::string::npos, "ECS profiler trace export omitted schema");
    kb::tests::Require(content.find("\"system_name\": \"ProfilingProbe\"") != std::string::npos, "ECS profiler trace export omitted system counters");
    kb::tests::Require(content.find("\"execution_path\": \"virtual_callback\"") != std::string::npos, "ECS profiler trace export omitted execution path");
    kb::tests::Require(content.find("\"stage_counters\"") != std::string::npos, "ECS profiler trace export omitted stage counters");
    kb::tests::Require(content.find("\"chunk_jobs_count\"") != std::string::npos, "ECS profiler trace export omitted chunk job counters");
    kb::tests::Require(content.find("\"worker_dispatch_count\"") != std::string::npos, "ECS profiler trace export omitted worker dispatch counters");
    kb::tests::Require(content.find("\"worker_steal_count\"") != std::string::npos, "ECS profiler trace export omitted worker steal counters");
    kb::tests::Require(content.find("\"last_worker_dispatch_schedule_ns\"") != std::string::npos, "ECS profiler trace export omitted last dispatch schedule time");
    kb::tests::Require(content.find("\"worker_dispatch_schedule_ns\"") != std::string::npos, "ECS profiler trace export omitted total dispatch schedule time");
    kb::tests::Require(content.find("\"average_worker_dispatch_schedule_ns\"") != std::string::npos, "ECS profiler trace export omitted average dispatch schedule time");
    kb::tests::Require(content.find("\"last_worker_dispatch_wall_ns\"") != std::string::npos, "ECS profiler trace export omitted dispatch wall time");
    kb::tests::Require(content.find("\"worker_active_ns\"") != std::string::npos, "ECS profiler trace export omitted worker active time");
    kb::tests::Require(content.find("\"worker_capacity_ns\"") != std::string::npos, "ECS profiler trace export omitted worker capacity time");
    kb::tests::Require(content.find("\"average_worker_utilization_percent\"") != std::string::npos, "ECS profiler trace export omitted worker utilization");
    kb::tests::Require(content.find("\"chrome_trace_events\"") != std::string::npos, "ECS profiler trace export omitted external trace events");
    kb::tests::Require(content.find("\"ph\": \"X\"") != std::string::npos, "ECS profiler trace export omitted duration events");
    kb::tests::Require(content.find("\"entities_processed\": 7") != std::string::npos, "ECS profiler trace export omitted profiler payload");

    input.close();
    std::filesystem::remove(outputPath);

    const std::filesystem::path chromePath = std::filesystem::temp_directory_path() / "kb_ecs_scheduler_trace_test.chrome.json";
    kb::ecs::ExportSystemSchedulerTraceToChromeTraceFile(scheduler.LastProfilerTrace(), chromePath);
    std::ifstream chromeInput(chromePath, std::ios::binary);
    kb::tests::Require(chromeInput.is_open(), "ECS profiler chrome trace export did not create an output file");
    const std::string chromeContent{ std::istreambuf_iterator<char>{ chromeInput }, std::istreambuf_iterator<char>{} };
    kb::tests::Require(chromeContent.find("\"traceEvents\"") != std::string::npos, "ECS chrome trace export omitted traceEvents array");
    kb::tests::Require(chromeContent.find("\"displayTimeUnit\": \"ms\"") != std::string::npos, "ECS chrome trace export omitted displayTimeUnit");
    kb::tests::Require(chromeContent.find("\"ph\": \"X\"") != std::string::npos, "ECS chrome trace export omitted duration events");
    chromeInput.close();
    std::filesystem::remove(chromePath);

    const std::filesystem::path csvPath = std::filesystem::temp_directory_path() / "kb_ecs_scheduler_trace_test.csv";
    kb::ecs::ExportSystemSchedulerTraceToCsvFile(scheduler.LastProfilerTrace(), csvPath);
    std::ifstream csvInput(csvPath, std::ios::binary);
    kb::tests::Require(csvInput.is_open(), "ECS profiler csv export did not create an output file");
    const std::string csvContent{ std::istreambuf_iterator<char>{ csvInput }, std::istreambuf_iterator<char>{} };
    kb::tests::Require(csvContent.find("system_index,system_name,execution_path") != std::string::npos, "ECS csv export omitted header row");
    kb::tests::Require(csvContent.find("ProfilingProbe") != std::string::npos, "ECS csv export omitted system row");
    csvInput.close();
    std::filesystem::remove(csvPath);

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
    kb::tests::Require(trace.events[1].waitReason == "dependencies", "ECS scheduler debug trace did not record dependency wait reason");
    kb::tests::Require(trace.workers[0].workerIndex == 0U, "ECS scheduler debug trace recorded an invalid worker occupancy index");
    kb::tests::Require(trace.workers[0].busyTimeNanoseconds == trace.events[0].durationNanoseconds + trace.events[1].durationNanoseconds, "ECS scheduler debug trace worker busy time did not match system durations");
    kb::tests::Require(trace.frameDurationNanoseconds >= trace.workers[0].busyTimeNanoseconds, "ECS scheduler debug trace frame duration is shorter than worker busy time");
    kb::tests::Require(trace.workers[0].utilizationPermille <= 1000U, "ECS scheduler debug trace recorded invalid worker utilization");

    scheduler.Update(world, 0.0F);
    kb::tests::Require(scheduler.LastDebugTrace().frameIndex == 1U, "ECS scheduler debug trace did not advance frame indexes");

    scheduler.Shutdown(world);
}

} // namespace

namespace kb::tests {

void RunSystemFusionPlannerReducesChunkPassesTest() {
    constexpr std::uint64_t kMovementArchetype = 0xA1ULL;
    constexpr std::uint64_t kRenderArchetype = 0xB2ULL;
    const std::array systems{
        kb::ecs::SystemFusionDescriptor{ .name = "ReadVelocity", .archetypeSignature = kMovementArchetype, .estimatedCostNanoseconds = 50'000, .entityCount = 100'000 },
        kb::ecs::SystemFusionDescriptor{ .name = "UpdatePosition", .archetypeSignature = kMovementArchetype, .estimatedCostNanoseconds = 60'000, .entityCount = 100'000 },
        kb::ecs::SystemFusionDescriptor{ .name = "UpdateBounds", .archetypeSignature = kMovementArchetype, .estimatedCostNanoseconds = 40'000, .entityCount = 100'000 },
        kb::ecs::SystemFusionDescriptor{ .name = "Cull", .archetypeSignature = kRenderArchetype, .estimatedCostNanoseconds = 30'000, .entityCount = 100'000 },
        kb::ecs::SystemFusionDescriptor{ .name = "Structural", .archetypeSignature = kMovementArchetype, .estimatedCostNanoseconds = 10'000, .entityCount = 100'000, .hasSyncPoint = true },
    };

    const kb::ecs::SystemFusionPlan plan = kb::ecs::PlanSystemFusion(
        std::span<const kb::ecs::SystemFusionDescriptor>{ systems },
        kb::ecs::SystemFusionPlannerConfig{ .workerCount = 8 });

    // Baseline = 5 systems each its own pass. Fused = movement family (1) +
    // render family (1) + structural sync system (1) = 3 passes.
    kb::tests::Require(plan.baselineChunkPasses == 5U, "fusion planner miscounted baseline passes");
    kb::tests::Require(plan.fusedChunkPasses == 3U, "fusion planner did not fuse compatible systems");
    kb::tests::Require(plan.ReducedChunkPasses() == 2U, "fusion planner reported the wrong pass reduction");
    kb::tests::Require(plan.fusionGroups.size() == 1U, "fusion planner produced an unexpected number of fusion groups");
    kb::tests::Require(plan.fusionGroups.front().archetypeSignature == kMovementArchetype, "fusion planner fused the wrong archetype family");
    kb::tests::Require(plan.fusionGroups.front().systemIndices.size() == 3U, "fusion planner left a fusible system out of the group");

    // A heavy system on a large archetype should be recommended for splitting.
    const std::array heavySystem{
        kb::ecs::SystemFusionDescriptor{ .name = "HeavyTransform", .archetypeSignature = 0xC3ULL, .estimatedCostNanoseconds = 4'000'000, .entityCount = 10'000'000 },
    };
    const kb::ecs::SystemFusionPlan heavyPlan = kb::ecs::PlanSystemFusion(
        std::span<const kb::ecs::SystemFusionDescriptor>{ heavySystem },
        kb::ecs::SystemFusionPlannerConfig{ .workerCount = 8 });
    kb::tests::Require(heavyPlan.splits.size() == 1U, "fusion planner did not recommend splitting a heavy system");
    kb::tests::Require(heavyPlan.splits.front().suggestedRangeCount >= 2U && heavyPlan.splits.front().suggestedRangeCount <= 8U, "fusion planner produced an invalid split range count");
}

void RunEcsSystemSchedulerTests() {
    RunSystemFusionPlannerReducesChunkPassesTest();
    RunExplicitOrderingReordersSystemsTest();
    RunReadWriteDependencyCycleTest();
    RunDeterministicModeOrdersIndependentSystemsByNameTest();
    RunDeterministicModeOrdersConflictingSystemsByNameTest();
    RunSyncPointCreatesRegistrationOrderBarrierTest();
    RunAssetBoundarySyncPointCreatesRegistrationOrderBarrierTest();
    RunSyncPointRequiresRuntimeBoundaryReasonTest();
    RunSchedulerRejectsNullSystemTest();
    RunReadOnlySystemsShareExecutionStageTest();
    RunReadOnlySystemsExecuteInParallelTest();
    RunSingleThreadSchedulerRunsCompatibleSystemsOnCallerThreadTest();
    RunReadWriteConflictCreatesStageBoundaryTest();
    RunWriteWriteConflictCreatesStageBoundaryTest();
    RunRuntimeAccessValidatorAllowsReadOnlyJobsTest();
    RunRuntimeAccessValidatorDetectsReadWriteConflictTest();
    RunRuntimeAccessValidatorDetectsWriteWriteConflictTest();
    RunProfilerCountersCaptureFrameAndSystemWorkTest();
    RunProfilerTraceExportWritesExternalJsonFileTest();
    RunDebugTraceCapturesSystemExecutionTest();
}

} // namespace kb::tests
