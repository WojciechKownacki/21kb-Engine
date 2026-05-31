#pragma once

#include "engine/scene/SceneObject.hpp"

#include <span>

namespace kb::scene {

class Scene;

class SceneEntityBatchService {
public:
    SceneEntityBatchService() = delete;

    static void Destroy(Scene& scene, std::span<const SceneObject> objects) noexcept;
    [[nodiscard]] static bool SetParent(Scene& scene, std::span<const SceneObject> objects, SceneObject parent) noexcept;
};

} // namespace kb::scene
