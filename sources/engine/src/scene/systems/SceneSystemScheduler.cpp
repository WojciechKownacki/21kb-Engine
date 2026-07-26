#include "scene/systems/SceneSystemScheduler.hpp"

#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"

#include <exception>
#include <utility>

namespace kb::scene {

SceneSystemScheduler::~SceneSystemScheduler() = default;

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

void SceneSystemScheduler::Add(std::unique_ptr<SceneSystem> system, Scene& scene) {
    if (system == nullptr) {
        return;
    }

    SceneSystemContext context{ scene, 0.0F };
    try {
        system->OnCreate(context);
    } catch (const std::exception& error) {
        RecordSystemError("OnCreate", error.what());
    } catch (...) {
        RecordSystemError("OnCreate", nullptr);
    }
    systems_.push_back(std::move(system));
}

void SceneSystemScheduler::BeginFrame(Scene& scene, float deltaSeconds) {
    SceneSystemContext context{ scene, deltaSeconds };
    for (const auto& system : systems_) {
        try {
            system->OnFrameStart(context);
        } catch (const std::exception& error) {
            RecordSystemError("OnFrameStart", error.what());
        } catch (...) {
            RecordSystemError("OnFrameStart", nullptr);
        }
    }
}

void SceneSystemScheduler::Update(Scene& scene, float deltaSeconds, SceneUpdatePhase phase) {
    SceneSystemContext context{ scene, deltaSeconds };
    for (const auto& system : systems_) {
        if (system->UpdatePhase() != phase) {
            continue;
        }
        try {
            system->OnUpdate(context);
        } catch (const std::exception& error) {
            RecordSystemError("OnUpdate", error.what());
        } catch (...) {
            RecordSystemError("OnUpdate", nullptr);
        }
    }
}

void SceneSystemScheduler::FixedUpdate(Scene& scene, float fixedDeltaSeconds, SceneFixedUpdatePhase phase) {
    SceneSystemContext context{ scene, fixedDeltaSeconds };
    for (const auto& system : systems_) {
        if (system->FixedUpdatePhase() != phase) {
            continue;
        }
        try {
            system->OnFixedUpdate(context);
        } catch (const std::exception& error) {
            RecordSystemError("OnFixedUpdate", error.what());
        } catch (...) {
            RecordSystemError("OnFixedUpdate", nullptr);
        }
    }
}

void SceneSystemScheduler::Shutdown(Scene& scene) noexcept {
    SceneSystemContext context{ scene, 0.0F };
    for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) {
        try {
            (*it)->OnDestroy(context);
        } catch (...) {
        }
    }
    systems_.clear();
}

} // namespace kb::scene
