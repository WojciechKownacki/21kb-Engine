#include "engine/scene/Scene.hpp"

#include "engine/script/ScriptAssetLoader.hpp"
#include "engine/visual/VisualGraphAssetLoader.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/assets/ScenePrefabAssetLoader.hpp"

#include <atomic>
#include <memory>

namespace kb::scene {
namespace {

std::atomic<std::uint64_t> g_nextSceneId{ 1U };

} // namespace

Scene::Scene()
    : state_(std::make_unique<SceneState>())
    , id_(g_nextSceneId.fetch_add(1U, std::memory_order_relaxed)) {
    const bool registeredPrefabLoader = state_->assets.RegisterLoader(std::make_unique<ScenePrefabAssetLoader>(*this));
    const bool registeredLuaScriptLoader = state_->assets.RegisterLoader(std::make_unique<kb::script::LuaScriptAssetLoader>());
    const bool registeredNativeBehaviourLoader = state_->assets.RegisterLoader(std::make_unique<kb::script::NativeBehaviourDescriptorAssetLoader>());
    const bool registeredVisualGraphLoader = state_->assets.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>());
    static_cast<void>(registeredPrefabLoader);
    static_cast<void>(registeredLuaScriptLoader);
    static_cast<void>(registeredNativeBehaviourLoader);
    static_cast<void>(registeredVisualGraphLoader);
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

std::uint64_t Scene::Id() const noexcept {
    return id_;
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
