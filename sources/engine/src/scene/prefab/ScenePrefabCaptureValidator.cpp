#include "scene/prefab/ScenePrefabCaptureValidator.hpp"

#include "scene/SceneEntityService.hpp"

namespace kb::scene {

bool ScenePrefabCaptureValidator::CanCapture(const Scene& scene, SceneObject root) noexcept {
    return SceneEntityService::IsAlive(scene, root);
}

} // namespace kb::scene
