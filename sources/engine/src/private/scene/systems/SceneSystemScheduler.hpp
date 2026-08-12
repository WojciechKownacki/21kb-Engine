#pragma once

#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemHandle.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace kb::scene {

class Scene;

class SceneSystemScheduler {
public:
    SceneSystemScheduler();
    ~SceneSystemScheduler();

    SceneSystemScheduler(const SceneSystemScheduler&) = delete;
    SceneSystemScheduler& operator=(const SceneSystemScheduler&) = delete;
    SceneSystemScheduler(SceneSystemScheduler&&) = delete;
    SceneSystemScheduler& operator=(SceneSystemScheduler&&) = delete;

    SceneSystemHandle Add(std::unique_ptr<SceneSystem> system, Scene& scene);
    [[nodiscard]] bool Remove(SceneSystemHandle handle, Scene& scene) noexcept;
    [[nodiscard]] bool Contains(SceneSystemHandle handle) const noexcept;
    [[nodiscard]] bool RequiresFixedStep() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept { return systems_.size(); }
    void BeginFrame(Scene& scene, float deltaSeconds);
    void Update(Scene& scene, float deltaSeconds, SceneUpdatePhase phase);
    void FixedUpdate(Scene& scene, float fixedDeltaSeconds, SceneFixedUpdatePhase phase);
    void Shutdown(Scene& scene) noexcept;

    // A throw from one scene system (e.g. a third-party plugin's system) must
    // not silently abort the whole per-frame update and starve every later
    // system — most importantly the script system, which is added last. Each
    // OnCreate/OnUpdate/OnFixedUpdate is isolated; a fault is recorded here
    // (de-duplicated) and drained by the host (editor Console) so it surfaces
    // instead of vanishing. Empty when everything ran cleanly.
    [[nodiscard]] std::vector<std::string> DrainSystemErrors();

private:
    struct Entry final {
        SceneSystemHandle handle{};
        std::unique_ptr<SceneSystem> system;
        bool requiresFixedStep = false;
    };

    void RecordSystemError(std::string phase, const char* what);

    std::vector<Entry> systems_;
    std::uint64_t schedulerLifetime_ = 0U;
    std::uint64_t nextHandle_ = 1U;
    bool dispatching_ = false;
    std::vector<std::string> systemErrors_;
    std::unordered_set<std::string> reportedSystemErrors_;
};

} // namespace kb::scene
