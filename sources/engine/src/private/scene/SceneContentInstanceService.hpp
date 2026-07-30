#pragma once

namespace kb::scene {

class Scene;

class SceneContentInstanceService final {
public:
    SceneContentInstanceService() = delete;
    static void Synchronize(Scene& scene);
    static void Shutdown(Scene& scene) noexcept;
};

} // namespace kb::scene
