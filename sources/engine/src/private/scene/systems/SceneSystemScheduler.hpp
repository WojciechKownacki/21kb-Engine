#pragma once

#include "engine/scene/SceneSystem.hpp"

#include <memory>
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
    void Shutdown(Scene& scene) noexcept;

private:
    std::vector<std::unique_ptr<SceneSystem>> systems_;
};

} // namespace kb::scene
