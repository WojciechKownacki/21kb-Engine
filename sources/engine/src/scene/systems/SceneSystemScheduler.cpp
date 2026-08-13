#include "scene/systems/SceneSystemScheduler.hpp"

#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace kb::scene {
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

} // namespace

SceneSystemScheduler::~SceneSystemScheduler() = default;

SceneSystemScheduler::SceneSystemScheduler()
    : schedulerLifetime_(AllocateSchedulerLifetime()) {}

void SceneSystemScheduler::RecordSystemError(std::string phase, const char* what) {
    std::string line = "scene system threw in " + std::move(phase) + ": " + (what != nullptr ? what : "unknown error");
    if (reportedSystemErrors_.insert(line).second) {
        systemErrors_.push_back(std::move(line));
    }
}

std::vector<std::string> SceneSystemScheduler::DrainSystemErrors() {
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

void SceneSystemScheduler::Update(Scene& scene, float deltaSeconds, SceneUpdatePhase phase) {
    DispatchScope dispatchScope{ dispatching_ };
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

void SceneSystemScheduler::FixedUpdate(Scene& scene, float fixedDeltaSeconds, SceneFixedUpdatePhase phase) {
    DispatchScope dispatchScope{ dispatching_ };
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
    dispatching_ = false;
}

} // namespace kb::scene
