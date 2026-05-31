#include "engine/scene/Scene.hpp"

#include "engine/scene/SceneAssets.hpp"

namespace kb::scene {

SceneAssets Scene::Assets() noexcept {
    return SceneAssets{ *this };
}

SceneAssets Scene::Assets() const noexcept {
    return SceneAssets{ const_cast<Scene&>(*this) };
}

} // namespace kb::scene
