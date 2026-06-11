#include "scene/systems/SceneSystemScheduler.hpp"

#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"

#include <utility>

namespace kb::scene {

SceneSystemScheduler::~SceneSystemScheduler() = default;

void SceneSystemScheduler::Add(std::unique_ptr<SceneSystem> system, Scene& scene) {
    if (system == nullptr) {
        return;
    }

    SceneSystemContext context{ scene, 0.0F };
    system->OnCreate(context);
    systems_.push_back(std::move(system));
}

void SceneSystemScheduler::Update(Scene& scene, float deltaSeconds) {
    SceneSystemContext context{ scene, deltaSeconds };
    for (const auto& system : systems_) {
        system->OnUpdate(context);
    }
}

void SceneSystemScheduler::FixedUpdate(Scene& scene, float fixedDeltaSeconds) {
    SceneSystemContext context{ scene, fixedDeltaSeconds };
    for (const auto& system : systems_) {
        system->OnFixedUpdate(context);
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
