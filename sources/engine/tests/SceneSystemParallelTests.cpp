#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/scene/ParallelSceneSystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "scene/systems/SceneSystemParallel.hpp"
#include "scene/systems/SceneSystemScheduler.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using kb::scene::IParallelSceneSystem;
using kb::scene::SceneSystem;
using kb::scene::SceneSystemAccess;
using kb::scene::SceneSystemContext;

// Shared by the systems of one test to observe what ran when. Tickets come from one counter, so
// "A ended before B started" is a plain comparison and does not depend on clocks.
struct Observation {
    std::atomic<std::uint64_t> ticket{ 0U };
    std::atomic<int> running{ 0 };
    std::atomic<int> maxRunning{ 0 };
};

// A system whose work is a pure function of its own state and the step count: any interleaving
// that respects the declared access gives the same final value.
class WorkSystem final : public SceneSystem, public IParallelSceneSystem {
public:
    WorkSystem(std::string name, SceneSystemAccess access, std::uint32_t rounds, Observation* observation = nullptr)
        : name_(std::move(name)), access_(std::move(access)), rounds_(rounds), observation_(observation) {}

    void OnUpdate(SceneSystemContext& context) override { Run(context.DeltaSeconds()); }
    void OnFixedUpdate(SceneSystemContext& context) override { Run(context.DeltaSeconds() * 2.0F); }
    [[nodiscard]] SceneSystemAccess DeclareAccess() const override { return access_; }

    [[nodiscard]] std::uint64_t Result() const noexcept { return result_; }
    [[nodiscard]] std::uint64_t Calls() const noexcept { return calls_; }
    [[nodiscard]] std::uint64_t StartTicket() const noexcept { return startTicket_; }
    [[nodiscard]] std::uint64_t EndTicket() const noexcept { return endTicket_; }
    void SetThrow(bool value) noexcept { throws_ = value; }
    void SetRendezvous(std::atomic<int>* arrived, int expected) noexcept {
        arrived_ = arrived;
        expected_ = expected;
    }
    [[nodiscard]] bool RendezvousSucceeded() const noexcept { return rendezvousOk_; }

private:
    void Run(float deltaSeconds) {
        if (observation_ != nullptr) {
            startTicket_ = observation_->ticket.fetch_add(1U) + 1U;
            const int now = observation_->running.fetch_add(1) + 1;
            int seen = observation_->maxRunning.load();
            while (now > seen && !observation_->maxRunning.compare_exchange_weak(seen, now)) {
            }
        }
        if (arrived_ != nullptr) {
            // Both systems must be inside OnUpdate at the same moment for this to succeed.
            arrived_->fetch_add(1);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (arrived_->load() < expected_ && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            rendezvousOk_ = arrived_->load() >= expected_;
        }
        std::uint64_t value = result_ ^ (static_cast<std::uint64_t>(deltaSeconds * 1000.0F) + calls_);
        for (std::uint32_t round = 0U; round < rounds_; ++round) {
            value = value * 6364136223846793005ULL + 1442695040888963407ULL;
            value ^= value >> 29U;
        }
        result_ = value;
        ++calls_;
        if (observation_ != nullptr) {
            observation_->running.fetch_sub(1);
            endTicket_ = observation_->ticket.fetch_add(1U) + 1U;
        }
        if (throws_) {
            throw std::runtime_error(name_ + " failed");
        }
    }

    std::string name_;
    SceneSystemAccess access_;
    std::uint32_t rounds_;
    Observation* observation_;
    std::atomic<int>* arrived_ = nullptr;
    int expected_ = 0;
    bool rendezvousOk_ = false;
    bool throws_ = false;
    std::uint64_t result_ = 0x9E3779B97F4A7C15ULL;
    std::uint64_t calls_ = 0U;
    std::uint64_t startTicket_ = 0U;
    std::uint64_t endTicket_ = 0U;
};

// Not parallel-capable: a barrier.
class BarrierSystem final : public SceneSystem {
public:
    explicit BarrierSystem(Observation* observation) : observation_(observation) {}
    void OnUpdate(SceneSystemContext&) override {
        startTicket_ = observation_->ticket.fetch_add(1U) + 1U;
        const int others = observation_->running.load();
        sawOthersRunning_ = others != 0;
        endTicket_ = observation_->ticket.fetch_add(1U) + 1U;
    }
    [[nodiscard]] std::uint64_t StartTicket() const noexcept { return startTicket_; }
    [[nodiscard]] std::uint64_t EndTicket() const noexcept { return endTicket_; }
    [[nodiscard]] bool SawOthersRunning() const noexcept { return sawOthersRunning_; }

private:
    Observation* observation_;
    std::uint64_t startTicket_ = 0U;
    std::uint64_t endTicket_ = 0U;
    bool sawOthersRunning_ = false;
};

SceneSystemAccess Writes(std::string resource) {
    return SceneSystemAccess{ {}, { std::move(resource) } };
}

struct Fleet {
    std::vector<WorkSystem*> systems;
    std::vector<std::uint64_t> Results() const {
        std::vector<std::uint64_t> results;
        for (const WorkSystem* system : systems) {
            results.push_back(system->Result());
        }
        return results;
    }
};

Fleet AddDisjointFleet(kb::scene::SceneSystemScheduler& scheduler, kb::scene::Scene& scene, std::size_t count, std::uint32_t rounds) {
    Fleet fleet;
    for (std::size_t index = 0U; index < count; ++index) {
        auto system = std::make_unique<WorkSystem>("Work" + std::to_string(index), Writes("res" + std::to_string(index)), rounds + static_cast<std::uint32_t>(index) * 1000U);
        fleet.systems.push_back(system.get());
        static_cast<void>(scheduler.Add(std::move(system), scene));
    }
    return fleet;
}

void RunParallelEqualsSerialTest() {
    std::vector<std::uint64_t> serial;
    std::vector<std::uint64_t> parallel;
    for (const bool parallelEnabled : { false, true }) {
        kb::scene::Scene scene;
        kb::scene::SceneSystemScheduler scheduler;
        const Fleet fleet = AddDisjointFleet(scheduler, scene, 6U, 20'000U);
        kb::scene::SetSceneSystemParallelExecution(scheduler, parallelEnabled);
        for (int frame = 0; frame < 25; ++frame) {
            scheduler.Update(scene, 1.0F / 60.0F, kb::scene::SceneUpdatePhase::PreFixed);
            scheduler.FixedUpdate(scene, 1.0F / 120.0F, kb::scene::SceneFixedUpdatePhase::Simulation);
        }
        for (const WorkSystem* system : fleet.systems) {
            kb::tests::Require(system->Calls() == 50U, "a system did not run once per update and once per fixed update");
        }
        (parallelEnabled ? parallel : serial) = fleet.Results();
        const kb::scene::SceneSystemParallelStats stats = kb::scene::GetSceneSystemParallelStats(scheduler);
        if (!parallelEnabled) {
            kb::tests::Require(stats.parallelGroups == 0U, "serial mode formed a parallel group");
        } else if (kb::scene::SceneSystemParallelWorkerCount(scheduler) >= 2U) {
            kb::tests::Require(stats.parallelGroups == 50U && stats.parallelSystemRuns == 300U,
                "parallel mode did not group the independent systems");
        }
    }
    kb::tests::Require(serial == parallel, "parallel execution of independent systems changed their results");
}

void RunSystemsReallyOverlapTest() {
    kb::scene::Scene scene;
    kb::scene::SceneSystemScheduler scheduler;
    std::atomic<int> arrived{ 0 };
    auto first = std::make_unique<WorkSystem>("First", Writes("a"), 10U);
    auto second = std::make_unique<WorkSystem>("Second", Writes("b"), 10U);
    first->SetRendezvous(&arrived, 2);
    second->SetRendezvous(&arrived, 2);
    const WorkSystem* firstPtr = first.get();
    const WorkSystem* secondPtr = second.get();
    static_cast<void>(scheduler.Add(std::move(first), scene));
    static_cast<void>(scheduler.Add(std::move(second), scene));
    scheduler.Update(scene, 0.016F, kb::scene::SceneUpdatePhase::PreFixed);
    if (kb::scene::SceneSystemParallelWorkerCount(scheduler) < 2U) {
        std::printf("scene-system-parallel: skipped the overlap check, the pool has fewer than two workers\n");
        return;
    }
    kb::tests::Require(firstPtr->RendezvousSucceeded() && secondPtr->RendezvousSucceeded(),
        "two systems with disjoint access were not inside OnUpdate at the same time");
}

void RunConflictsAndBarriersKeepOrderTest() {
    kb::scene::Scene scene;
    kb::scene::SceneSystemScheduler scheduler;
    Observation observation;
    // Registration order: A(writes x)  B(reads x)  C(writes y)  barrier  D(writes z)  E(writes w).
    auto a = std::make_unique<WorkSystem>("A", Writes("x"), 200'000U, &observation);
    auto b = std::make_unique<WorkSystem>("B", SceneSystemAccess{ { "x" }, {} }, 200'000U, &observation);
    auto c = std::make_unique<WorkSystem>("C", Writes("y"), 200'000U, &observation);
    auto barrier = std::make_unique<BarrierSystem>(&observation);
    auto d = std::make_unique<WorkSystem>("D", Writes("z"), 200'000U, &observation);
    auto e = std::make_unique<WorkSystem>("E", Writes("w"), 200'000U, &observation);
    const WorkSystem *pa = a.get(), *pb = b.get(), *pc = c.get(), *pd = d.get(), *pe = e.get();
    const BarrierSystem* pbarrier = barrier.get();
    static_cast<void>(scheduler.Add(std::move(a), scene));
    static_cast<void>(scheduler.Add(std::move(b), scene));
    static_cast<void>(scheduler.Add(std::move(c), scene));
    static_cast<void>(scheduler.Add(std::move(barrier), scene));
    static_cast<void>(scheduler.Add(std::move(d), scene));
    static_cast<void>(scheduler.Add(std::move(e), scene));

    for (int frame = 0; frame < 5; ++frame) {
        scheduler.Update(scene, 0.016F, kb::scene::SceneUpdatePhase::PreFixed);
        // B reads what A writes, so B (and everything after it) starts after A has finished.
        kb::tests::Require(pa->EndTicket() < pb->StartTicket(), "a reader started before the writer it follows had finished");
        kb::tests::Require(pa->EndTicket() < pc->StartTicket(), "a system registered after a conflict started before it ended");
        // The barrier waits for B and C, and D and E wait for the barrier.
        kb::tests::Require(pbarrier->StartTicket() > pb->EndTicket() && pbarrier->StartTicket() > pc->EndTicket(),
            "the barrier started before the systems registered before it had finished");
        kb::tests::Require(!pbarrier->SawOthersRunning(), "a system was running while the barrier ran");
        kb::tests::Require(pd->StartTicket() > pbarrier->EndTicket() && pe->StartTicket() > pbarrier->EndTicket(),
            "a system registered after the barrier started before the barrier ended");
    }
    const kb::scene::SceneSystemParallelStats stats = kb::scene::GetSceneSystemParallelStats(scheduler);
    kb::tests::Require(stats.conflictBarriers == 5U, "the read after write was not counted as a conflict");
    kb::tests::Require(observation.maxRunning.load() <= 2, "more systems ran at once than the groups allow");
}

void RunConflictingWritersNeverOverlapTest() {
    kb::scene::Scene scene;
    kb::scene::SceneSystemScheduler scheduler;
    Observation observation;
    for (int index = 0; index < 4; ++index) {
        static_cast<void>(scheduler.Add(std::make_unique<WorkSystem>("W" + std::to_string(index), Writes("shared"), 100'000U, &observation), scene));
    }
    for (int frame = 0; frame < 10; ++frame) {
        scheduler.Update(scene, 0.016F, kb::scene::SceneUpdatePhase::PreFixed);
    }
    kb::tests::Require(observation.maxRunning.load() == 1, "systems that write the same resource ran at the same time");
    kb::tests::Require(kb::scene::GetSceneSystemParallelStats(scheduler).parallelGroups == 0U, "conflicting systems formed a parallel group");
}

void RunErrorsKeepRegistrationOrderTest() {
    kb::scene::Scene scene;
    kb::scene::SceneSystemScheduler scheduler;
    auto first = std::make_unique<WorkSystem>("First", Writes("a"), 50'000U);
    auto second = std::make_unique<WorkSystem>("Second", Writes("b"), 10U);
    auto third = std::make_unique<WorkSystem>("Third", Writes("c"), 20'000U);
    auto fourth = std::make_unique<WorkSystem>("Fourth", Writes("d"), 10U);
    first->SetThrow(true);
    third->SetThrow(true);
    const WorkSystem* ok = second.get();
    const WorkSystem* last = fourth.get();
    static_cast<void>(scheduler.Add(std::move(first), scene));
    static_cast<void>(scheduler.Add(std::move(second), scene));
    static_cast<void>(scheduler.Add(std::move(third), scene));
    static_cast<void>(scheduler.Add(std::move(fourth), scene));
    scheduler.Update(scene, 0.016F, kb::scene::SceneUpdatePhase::PreFixed);
    kb::tests::Require(ok->Calls() == 1U && last->Calls() == 1U, "a failing system stopped the others");
    const std::vector<std::string> errors = scheduler.DrainSystemErrors();
    kb::tests::Require(errors.size() == 2U && errors[0].find("First failed") != std::string::npos && errors[1].find("Third failed") != std::string::npos,
        "errors from a parallel group are not recorded in registration order");
}

void RunRemoveAndShutdownTest() {
    kb::scene::Scene scene;
    kb::scene::SceneSystemScheduler scheduler;
    auto first = std::make_unique<WorkSystem>("First", Writes("a"), 10U);
    auto second = std::make_unique<WorkSystem>("Second", Writes("b"), 10U);
    const WorkSystem* secondPtr = second.get();
    const kb::scene::SceneSystemHandle firstHandle = scheduler.Add(std::move(first), scene);
    static_cast<void>(scheduler.Add(std::move(second), scene));
    scheduler.Update(scene, 0.016F, kb::scene::SceneUpdatePhase::PreFixed);
    kb::tests::Require(scheduler.Remove(firstHandle, scene), "a parallel system could not be removed");
    scheduler.Update(scene, 0.016F, kb::scene::SceneUpdatePhase::PreFixed);
    kb::tests::Require(secondPtr->Calls() == 2U, "the remaining system stopped after a removal");
    scheduler.Shutdown(scene);
    kb::tests::Require(kb::scene::GetSceneSystemParallelStats(scheduler).parallelGroups == 0U, "state survived shutdown");
    // A scheduler can be used again after shutdown.
    static_cast<void>(scheduler.Add(std::make_unique<WorkSystem>("Third", Writes("c"), 10U), scene));
    static_cast<void>(scheduler.Add(std::make_unique<WorkSystem>("Fourth", Writes("d"), 10U), scene));
    scheduler.Update(scene, 0.016F, kb::scene::SceneUpdatePhase::PreFixed);
}

void RunPlainSchedulerIsUnchangedTest() {
    kb::scene::Scene scene;
    kb::scene::SceneSystemScheduler scheduler;
    class Plain final : public SceneSystem {
    public:
        void OnUpdate(SceneSystemContext&) override { ++calls; }
        int calls = 0;
    };
    auto plain = std::make_unique<Plain>();
    const Plain* plainPtr = plain.get();
    static_cast<void>(scheduler.Add(std::move(plain), scene));
    scheduler.Update(scene, 0.016F, kb::scene::SceneUpdatePhase::PreFixed);
    kb::tests::Require(plainPtr->calls == 1, "a plain system did not run");
    const kb::scene::SceneSystemParallelStats stats = kb::scene::GetSceneSystemParallelStats(scheduler);
    kb::tests::Require(stats.parallelGroups == 0U && stats.serialSystemRuns == 0U && kb::scene::SceneSystemParallelWorkerCount(scheduler) == 0U,
        "a scheduler without parallel systems created parallel state");
}

} // namespace

namespace kb::tests {

void RunSceneSystemParallelTests() {
    RunPlainSchedulerIsUnchangedTest();
    RunParallelEqualsSerialTest();
    RunSystemsReallyOverlapTest();
    RunConflictsAndBarriersKeepOrderTest();
    RunConflictingWritersNeverOverlapTest();
    RunErrorsKeepRegistrationOrderTest();
    RunRemoveAndShutdownTest();
}

// Serial against parallel time for independent, CPU heavy systems. Prints the numbers; not run by default.
void RunSceneSystemParallelBenchmark() {
    constexpr std::size_t kSystems = 4U;
    constexpr std::uint32_t kRounds = 3'000'000U;
    constexpr int kFrames = 15;
    double medians[2]{};
    for (int mode = 0; mode < 2; ++mode) {
        kb::scene::Scene scene;
        kb::scene::SceneSystemScheduler scheduler;
        const Fleet fleet = AddDisjointFleet(scheduler, scene, kSystems, kRounds);
        kb::scene::SetSceneSystemParallelExecution(scheduler, mode == 1);
        scheduler.Update(scene, 0.016F, kb::scene::SceneUpdatePhase::PreFixed); // warm up, creates the pool
        std::vector<double> samples;
        for (int frame = 0; frame < kFrames; ++frame) {
            const auto start = std::chrono::steady_clock::now();
            scheduler.Update(scene, 0.016F, kb::scene::SceneUpdatePhase::PreFixed);
            samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        std::sort(samples.begin(), samples.end());
        medians[mode] = samples[samples.size() / 2U];
        std::printf("scene_system_parallel mode=%s systems=%zu workers=%u median_ms=%.3f p95_ms=%.3f\n", mode == 0 ? "serial" : "parallel", kSystems,
            kb::scene::SceneSystemParallelWorkerCount(scheduler), medians[mode], samples[samples.size() * 95U / 100U]);
        static_cast<void>(fleet);
    }
    std::printf("scene_system_parallel speedup=%.2f\n", medians[0] / medians[1]);
}

} // namespace kb::tests
