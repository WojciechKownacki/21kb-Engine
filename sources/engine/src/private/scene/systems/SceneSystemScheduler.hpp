#pragma once

#include "engine/scene/SceneSystem.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace kb::scene {

class Scene;

class SceneSystemScheduler {
public:
    SceneSystemScheduler() = default;
    ~SceneSystemScheduler();

    SceneSystemScheduler(const SceneSystemScheduler&) = delete;
    SceneSystemScheduler& operator=(const SceneSystemScheduler&) = delete;
    SceneSystemScheduler(SceneSystemScheduler&&) noexcept = default;
    SceneSystemScheduler& operator=(SceneSystemScheduler&&) noexcept = default;

    void Add(std::unique_ptr<SceneSystem> system, Scene& scene);
    void Update(Scene& scene, float deltaSeconds);
    void FixedUpdate(Scene& scene, float fixedDeltaSeconds);
    void Shutdown(Scene& scene) noexcept;

    // A throw from one scene system (e.g. a third-party plugin's system) must
    // not silently abort the whole per-frame update and starve every later
    // system — most importantly the script system, which is added last. Each
    // OnCreate/OnUpdate/OnFixedUpdate is isolated; a fault is recorded here
    // (de-duplicated) and drained by the host (editor Console) so it surfaces
    // instead of vanishing. Empty when everything ran cleanly.
    [[nodiscard]] std::vector<std::string> DrainSystemErrors();

private:
    void RecordSystemError(std::string phase, const char* what);

    std::vector<std::unique_ptr<SceneSystem>> systems_;
    std::vector<std::string> systemErrors_;
    std::unordered_set<std::string> reportedSystemErrors_;
};

} // namespace kb::scene
