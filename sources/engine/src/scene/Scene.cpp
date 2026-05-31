#include "engine/scene/Scene.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/assets/ScenePrefabAssetLoader.hpp"

#include <memory>

namespace kb::scene {

Scene::Scene()
    : state_(std::make_unique<SceneState>()) {
    const bool registeredPrefabLoader = state_->assets.RegisterLoader(std::make_unique<ScenePrefabAssetLoader>(*this));
    static_cast<void>(registeredPrefabLoader);
}

Scene::~Scene() {
    state_->sceneSystemScheduler.Shutdown(*this);
    state_->systemScheduler.Shutdown(state_->world);
}

SceneState& SceneAccess::State(Scene& scene) noexcept {
    return *scene.state_;
}

const SceneState& SceneAccess::State(const Scene& scene) noexcept {
    return *scene.state_;
}

SceneObject SceneAccess::MakeObject(Scene& scene, SceneEntity entity) noexcept {
    return SceneObject{ scene, entity };
}

bool SceneAccess::BelongsTo(Scene& scene, SceneObject object) noexcept {
    return BelongsTo(static_cast<const Scene&>(scene), object);
}

bool SceneAccess::BelongsTo(const Scene& scene, SceneObject object) noexcept {
    return object.scene_ == &scene && object.EntityHandle().IsValid();
}

} // namespace kb::scene
