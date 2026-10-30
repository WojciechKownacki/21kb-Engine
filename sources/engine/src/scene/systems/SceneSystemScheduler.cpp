#include "scene/systems/SceneSystemScheduler.hpp"

#include "engine/ecs/WorkerPool.hpp"
#include "engine/scene/ParallelSceneSystem.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/StatelessSceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "scene/systems/SceneSystemParallel.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <typeinfo>

namespace kb::scene {

SceneSystemScheduler::RuntimeStateOperation::RuntimeStateOperation(bool& active) : active_(active) {
    if (active_) throw std::logic_error("Scene system dispatch or checkpoint is already active");
    active_ = true;
}

SceneSystemScheduler::RuntimeStateOperation::~RuntimeStateOperation() { active_ = false; }

SceneSystemScheduler::RuntimeStateOperation SceneSystemScheduler::BeginRuntimeStateOperation() {
    return RuntimeStateOperation{dispatching_};
}

bool SceneSystemScheduler::CollectRuntimeStateProviders(std::vector<RuntimeStateBinding>& output, std::string& error) const {
    output.clear();
    if (dispatching_) { error = "Scene checkpoints require a completed system dispatch boundary"; return false; }
    std::vector<RuntimeStateBinding> bindings;
    bindings.reserve(systems_.size());
    for (const auto& entry : systems_) {
        auto* provider = dynamic_cast<kb::core::IRuntimeStateProvider*>(entry.system.get());
        if (provider == nullptr && dynamic_cast<const IStatelessSceneSystem*>(entry.system.get()) == nullptr) {
            error = "Scene system at index " + std::to_string(bindings.size()) + " (" + typeid(*entry.system).name()
                + ") does not declare runtime checkpoint state ownership";
            return false;
        }
        bindings.emplace_back(entry.handle, provider);
    }
    output = std::move(bindings);
    error.clear();
    return true;
}

namespace {

[[nodiscard]] std::uint64_t AllocateSchedulerLifetime() {
    static std::atomic<std::uint64_t> next{ 1U };
    std::uint64_t candidate = next.load(std::memory_order_relaxed);
    for (;;) {
        if (candidate == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("scene system scheduler lifetime space exhausted");
        }
        if (next.compare_exchange_weak(candidate, candidate + 1U, std::memory_order_relaxed)) {
            return candidate;
        }
    }
}

class DispatchScope final {
public:
    explicit DispatchScope(bool& dispatching)
        : dispatching_(dispatching) {
        if (dispatching_) {
            throw std::logic_error("scene system scheduler dispatch is already active");
        }
        dispatching_ = true;
    }

    ~DispatchScope() { dispatching_ = false; }

private:
    bool& dispatching_;
};

// ---- Parallel execution of systems that declare their access (IParallelSceneSystem).
//
// The state lives beside the scheduler, keyed by its address, so that SceneSystemScheduler.hpp (which
// every scene translation unit includes through SceneState.hpp) does not change. A scheduler that
// never receives a parallel system has no entry and takes the original serial loops.

enum class UpdateKind : std::uint8_t { Update, Fixed };

struct SystemAccess final {
    std::vector<std::string> reads;  // sorted, unique
    std::vector<std::string> writes; // sorted, unique
};

struct ParallelState final {
    bool enabled = true;
    std::unordered_map<const SceneSystem*, SystemAccess> access;
    std::unique_ptr<kb::ecs::WorkerPool> pool;
    SceneSystemParallelStats stats;
};

std::mutex g_parallelMutex;
std::unordered_map<const SceneSystemScheduler*, std::unique_ptr<ParallelState>> g_parallelStates;
std::atomic<std::size_t> g_parallelStateCount{ 0U };
// A system running on a worker thread may report an error through the scheduler while the main
// thread records another, so the error lists are guarded process-wide (uncontended in the serial case).
std::mutex g_errorMutex;

[[nodiscard]] ParallelState* FindState(const SceneSystemScheduler* scheduler) {
    if (g_parallelStateCount.load(std::memory_order_acquire) == 0U) {
        return nullptr;
    }
    const std::lock_guard lock{ g_parallelMutex };
    const auto found = g_parallelStates.find(scheduler);
    return found == g_parallelStates.end() ? nullptr : found->second.get();
}

void EraseState(const SceneSystemScheduler* scheduler) noexcept {
    if (g_parallelStateCount.load(std::memory_order_acquire) == 0U) {
        return;
    }
    std::unique_ptr<ParallelState> doomed;
    {
        const std::lock_guard lock{ g_parallelMutex };
        const auto found = g_parallelStates.find(scheduler);
        if (found == g_parallelStates.end()) {
            return;
        }
        doomed = std::move(found->second);
        g_parallelStates.erase(found);
        g_parallelStateCount.store(g_parallelStates.size(), std::memory_order_release);
    }
}

[[nodiscard]] std::vector<std::string> Normalised(std::vector<std::string> names) {
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

[[nodiscard]] bool Intersects(const std::vector<std::string>& left, const std::vector<std::string>& right) {
    auto a = left.begin();
    auto b = right.begin();
    while (a != left.end() && b != right.end()) {
        if (*a == *b) {
            return true;
        }
        if (*a < *b) {
            ++a;
        } else {
            ++b;
        }
    }
    return false;
}

[[nodiscard]] bool Conflicts(const SystemAccess& left, const SystemAccess& right) {
    return Intersects(left.writes, right.writes) || Intersects(left.writes, right.reads) || Intersects(left.reads, right.writes);
}

void RegisterParallelSystem(const SceneSystemScheduler* scheduler, SceneSystem& system) {
    const auto* parallel = dynamic_cast<const IParallelSceneSystem*>(&system);
    if (parallel == nullptr) {
        return;
    }
    SceneSystemAccess declared = parallel->DeclareAccess();
    SystemAccess access{ Normalised(std::move(declared.reads)), Normalised(std::move(declared.writes)) };
    const std::lock_guard lock{ g_parallelMutex };
    std::unique_ptr<ParallelState>& state = g_parallelStates[scheduler];
    if (state == nullptr) {
        state = std::make_unique<ParallelState>();
    }
    state->access[&system] = std::move(access);
    g_parallelStateCount.store(g_parallelStates.size(), std::memory_order_release);
}

void ForgetParallelSystem(const SceneSystemScheduler* scheduler, const SceneSystem& system) noexcept {
    if (g_parallelStateCount.load(std::memory_order_acquire) == 0U) {
        return;
    }
    const std::lock_guard lock{ g_parallelMutex };
    const auto found = g_parallelStates.find(scheduler);
    if (found != g_parallelStates.end()) {
        found->second->access.erase(&system);
    }
}

[[nodiscard]] const char* PhaseName(UpdateKind kind) noexcept {
    return kind == UpdateKind::Update ? "OnUpdate" : "OnFixedUpdate";
}

void InvokeSystem(SceneSystem& system, SceneSystemContext& context, UpdateKind kind) {
    if (kind == UpdateKind::Update) {
        system.OnUpdate(context);
    } else {
        system.OnFixedUpdate(context);
    }
}

// One system update on a worker thread. Failures are captured here and recorded by the main thread
// after the group has finished, so the error list keeps registration order.
struct ParallelSlot final {
    ParallelSlot(SceneSystem& target, Scene& scene, float deltaSeconds, UpdateKind updateKind)
        : system(&target), context(scene, deltaSeconds), kind(updateKind) {}

    SceneSystem* system;
    SceneSystemContext context;
    UpdateKind kind;
    bool failed = false;
    bool unknown = false;
    std::string error;
};

void RunParallelSlot(kb::ecs::WorkerContext, void* data) {
    ParallelSlot& slot = *static_cast<ParallelSlot*>(data);
    try {
        InvokeSystem(*slot.system, slot.context, slot.kind);
    } catch (const std::exception& error) {
        slot.failed = true;
        slot.error = error.what();
    } catch (...) {
        slot.failed = true;
        slot.unknown = true;
    }
}

// Runs `selected` (already in registration order) so that the result equals the serial result:
// consecutive systems that declared non-conflicting access run together, everything else in order.
template <typename ErrorSink>
void RunSelected(ParallelState& state, std::span<SceneSystem* const> selected, UpdateKind kind, Scene& scene, float deltaSeconds,
    ErrorSink&& recordError) {
    const auto runSerial = [&](SceneSystem& system) {
        SceneSystemContext context{ scene, deltaSeconds };
        try {
            InvokeSystem(system, context, kind);
        } catch (const std::exception& error) {
            recordError(PhaseName(kind), error.what());
        } catch (...) {
            recordError(PhaseName(kind), nullptr);
        }
        ++state.stats.serialSystemRuns;
    };
    const auto accessOf = [&](const SceneSystem* system) -> const SystemAccess* {
        const auto found = state.access.find(system);
        return found == state.access.end() ? nullptr : &found->second;
    };

    std::size_t next = 0U;
    while (next < selected.size()) {
        std::vector<SceneSystem*> group{ selected[next] };
        const SystemAccess* first = state.enabled ? accessOf(selected[next]) : nullptr;
        if (first != nullptr) {
            std::size_t cursor = next + 1U;
            for (; cursor < selected.size(); ++cursor) {
                const SystemAccess* candidate = accessOf(selected[cursor]);
                if (candidate == nullptr) {
                    break; // a system that did not declare its access is a barrier
                }
                bool conflicting = false;
                for (const SceneSystem* member : group) {
                    conflicting = conflicting || Conflicts(*candidate, *accessOf(member));
                }
                if (conflicting) {
                    ++state.stats.conflictBarriers;
                    break;
                }
                group.push_back(selected[cursor]);
            }
        }
        next += group.size();

        if (group.size() >= 2U && state.pool == nullptr) {
            state.pool = std::make_unique<kb::ecs::WorkerPool>(kb::ecs::WorkerPoolConfig{});
        }
        if (group.size() < 2U || state.pool->WorkerCount() < 2U) {
            for (SceneSystem* system : group) {
                runSerial(*system);
            }
            continue;
        }

        std::deque<ParallelSlot> slots; // a deque keeps every slot at a fixed address
        std::vector<kb::ecs::WorkerPoolJob> jobs;
        jobs.reserve(group.size());
        for (SceneSystem* system : group) {
            slots.emplace_back(*system, scene, deltaSeconds, kind);
            jobs.push_back(kb::ecs::WorkerPoolJob{ &RunParallelSlot, &slots.back() });
        }
        state.pool->Run(jobs);
        for (const ParallelSlot& slot : slots) {
            if (slot.failed) {
                recordError(PhaseName(kind), slot.unknown ? nullptr : slot.error.c_str());
            }
        }
        ++state.stats.parallelGroups;
        state.stats.parallelSystemRuns += group.size();
    }
}

} // namespace

void SetSceneSystemParallelExecution(SceneSystemScheduler& scheduler, bool enabled) {
    if (ParallelState* state = FindState(&scheduler); state != nullptr) {
        state->enabled = enabled;
    }
}

SceneSystemParallelStats GetSceneSystemParallelStats(const SceneSystemScheduler& scheduler) {
    const ParallelState* state = FindState(&scheduler);
    return state == nullptr ? SceneSystemParallelStats{} : state->stats;
}

std::uint32_t SceneSystemParallelWorkerCount(const SceneSystemScheduler& scheduler) {
    const ParallelState* state = FindState(&scheduler);
    return state == nullptr || state->pool == nullptr ? 0U : static_cast<std::uint32_t>(state->pool->WorkerCount());
}

SceneSystemScheduler::~SceneSystemScheduler() {
    EraseState(this);
}

SceneSystemScheduler::SceneSystemScheduler()
    : schedulerLifetime_(AllocateSchedulerLifetime()) {}

void SceneSystemScheduler::RecordSystemError(std::string phase, const char* what) {
    const std::lock_guard lock{ g_errorMutex };
    std::string line = "scene system threw in " + std::move(phase) + ": " + (what != nullptr ? what : "unknown error");
    if (reportedSystemErrors_.insert(line).second) {
        systemErrors_.push_back(std::move(line));
    }
}

void SceneSystemScheduler::ReportSystemError(std::string message) {
    const std::lock_guard lock{ g_errorMutex };
    if (reportedSystemErrors_.insert(message).second) {
        systemErrors_.push_back(std::move(message));
    }
}

std::vector<std::string> SceneSystemScheduler::DrainSystemErrors() {
    const std::lock_guard lock{ g_errorMutex };
    std::vector<std::string> drained;
    drained.swap(systemErrors_);
    return drained;
}

SceneSystemHandle SceneSystemScheduler::Add(std::unique_ptr<SceneSystem> system, Scene& scene) {
    if (system == nullptr) {
        return {};
    }
    if (nextHandle_ == 0U) {
        throw std::overflow_error("scene system handle space exhausted");
    }
    if (dispatching_) {
        throw std::logic_error("scene systems cannot be added during scheduler dispatch");
    }

    const SceneSystemHandle handle{ schedulerLifetime_, nextHandle_++ };
    bool requiresFixedStep = false;
    try {
        requiresFixedStep = system->RequiresFixedStep();
    } catch (const std::exception& error) {
        RecordSystemError("RequiresFixedStep", error.what());
    } catch (...) {
        RecordSystemError("RequiresFixedStep", nullptr);
    }

    systems_.push_back(Entry{ .handle = handle, .system = std::move(system), .requiresFixedStep = requiresFixedStep });
    try {
        RegisterParallelSystem(this, *systems_.back().system);
    } catch (const std::exception& error) {
        RecordSystemError("DeclareAccess", error.what()); // the system then simply runs serially
    } catch (...) {
        RecordSystemError("DeclareAccess", nullptr);
    }
    SceneSystemContext context{ scene, 0.0F };
    try {
        systems_.back().system->OnCreate(context);
    } catch (const std::exception& error) {
        RecordSystemError("OnCreate", error.what());
    } catch (...) {
        RecordSystemError("OnCreate", nullptr);
    }
    return handle;
}

bool SceneSystemScheduler::Remove(SceneSystemHandle handle, Scene& scene) noexcept {
    if (!handle.IsValid() || handle.schedulerLifetime_ != schedulerLifetime_ || dispatching_) {
        return false;
    }
    const auto iterator = std::find_if(systems_.begin(), systems_.end(), [handle](const Entry& entry) {
        return entry.handle == handle;
    });
    if (iterator == systems_.end()) {
        return false;
    }

    SceneSystemContext context{ scene, 0.0F };
    try {
        iterator->system->OnDestroy(context);
    } catch (...) {
    }
    ForgetParallelSystem(this, *iterator->system);
    systems_.erase(iterator);
    return true;
}

bool SceneSystemScheduler::Contains(SceneSystemHandle handle) const noexcept {
    if (!handle.IsValid() || handle.schedulerLifetime_ != schedulerLifetime_) {
        return false;
    }
    return std::any_of(systems_.begin(), systems_.end(), [handle](const Entry& entry) {
        return entry.handle == handle;
    });
}

bool SceneSystemScheduler::RequiresFixedStep() const noexcept {
    return std::any_of(systems_.begin(), systems_.end(), [](const Entry& entry) {
        return entry.requiresFixedStep;
    });
}

void SceneSystemScheduler::BeginFrame(Scene& scene, float deltaSeconds) {
    DispatchScope dispatchScope{ dispatching_ };
    SceneSystemContext context{ scene, deltaSeconds };
    for (const Entry& entry : systems_) {
        try {
            entry.system->OnFrameStart(context);
        } catch (const std::exception& error) {
            RecordSystemError("OnFrameStart", error.what());
        } catch (...) {
            RecordSystemError("OnFrameStart", nullptr);
        }
    }
}

void SceneSystemScheduler::EndUpdate(Scene& scene, float deltaSeconds) {
    DispatchScope dispatchScope{ dispatching_ };
    SceneSystemContext context{ scene, deltaSeconds };
    for (const Entry& entry : systems_) {
        try {
            entry.system->OnUpdateEnd(context);
        } catch (const std::exception& error) {
            RecordSystemError("OnUpdateEnd", error.what());
        } catch (...) {
            RecordSystemError("OnUpdateEnd", nullptr);
        }
    }
}

void SceneSystemScheduler::Update(Scene& scene, float deltaSeconds, SceneUpdatePhase phase) {
    DispatchScope dispatchScope{ dispatching_ };
    if (ParallelState* state = FindState(this); state != nullptr) {
        std::vector<SceneSystem*> selected;
        for (const Entry& entry : systems_) {
            if (entry.system->UpdatePhase() == phase) {
                selected.push_back(entry.system.get());
            }
        }
        RunSelected(*state, selected, UpdateKind::Update, scene, deltaSeconds,
            [this](const char* name, const char* what) { RecordSystemError(name, what); });
        return;
    }
    SceneSystemContext context{ scene, deltaSeconds };
    for (const Entry& entry : systems_) {
        if (entry.system->UpdatePhase() != phase) {
            continue;
        }
        try {
            entry.system->OnUpdate(context);
        } catch (const std::exception& error) {
            RecordSystemError("OnUpdate", error.what());
        } catch (...) {
            RecordSystemError("OnUpdate", nullptr);
        }
    }
}

void SceneSystemScheduler::FixedStepBegin(Scene& scene, float fixedDeltaSeconds) {
    DispatchScope dispatchScope{ dispatching_ };
    SceneSystemContext context{ scene, fixedDeltaSeconds };
    for (const Entry& entry : systems_) {
        try {
            entry.system->OnFixedStepBegin(context);
        } catch (const std::exception& error) {
            RecordSystemError("OnFixedStepBegin", error.what());
        } catch (...) {
            RecordSystemError("OnFixedStepBegin", nullptr);
        }
    }
}

void SceneSystemScheduler::FixedUpdate(Scene& scene, float fixedDeltaSeconds, SceneFixedUpdatePhase phase) {
    DispatchScope dispatchScope{ dispatching_ };
    if (ParallelState* state = FindState(this); state != nullptr) {
        std::vector<SceneSystem*> selected;
        for (const Entry& entry : systems_) {
            if (entry.system->FixedUpdatePhase() == phase) {
                selected.push_back(entry.system.get());
            }
        }
        RunSelected(*state, selected, UpdateKind::Fixed, scene, fixedDeltaSeconds,
            [this](const char* name, const char* what) { RecordSystemError(name, what); });
        return;
    }
    SceneSystemContext context{ scene, fixedDeltaSeconds };
    for (const Entry& entry : systems_) {
        if (entry.system->FixedUpdatePhase() != phase) {
            continue;
        }
        try {
            entry.system->OnFixedUpdate(context);
        } catch (const std::exception& error) {
            RecordSystemError("OnFixedUpdate", error.what());
        } catch (...) {
            RecordSystemError("OnFixedUpdate", nullptr);
        }
    }
}

void SceneSystemScheduler::Shutdown(Scene& scene) noexcept {
    if (dispatching_) {
        assert(false && "scene system scheduler shutdown requires quiescent owner-thread dispatch");
        std::terminate();
    }
    dispatching_ = true;
    SceneSystemContext context{ scene, 0.0F };
    for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) {
        try {
            it->system->OnDestroy(context);
        } catch (...) {
        }
    }
    systems_.clear();
    EraseState(this);
    dispatching_ = false;
}

} // namespace kb::scene
