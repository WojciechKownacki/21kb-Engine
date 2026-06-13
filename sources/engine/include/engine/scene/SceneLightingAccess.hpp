#pragma once

namespace kb::scene {

class Scene;

class SceneLightingAccess {
public:
    SceneLightingAccess() = delete;

    static void SetBasicLightingEnabled(Scene& scene, bool enabled) noexcept;
    [[nodiscard]] static bool BasicLightingEnabled(const Scene& scene) noexcept;
};

} // namespace kb::scene
