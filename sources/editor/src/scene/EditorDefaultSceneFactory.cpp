#include "scene/EditorDefaultSceneFactory.hpp"

#include "engine/scene/SceneEntity.hpp"

namespace kb::editor {

kb::scene::SceneEntity EditorDefaultSceneFactory::Seed(kb::scene::Scene& scene) {
    // A new/empty project starts with a completely empty scene -- no camera,
    // light, floor or demo objects. The user adds whatever they need.
    static_cast<void>(scene);
    return kb::scene::SceneEntity{};
}

} // namespace kb::editor
