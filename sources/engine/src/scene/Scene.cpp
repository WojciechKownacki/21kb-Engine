#include "engine/scene/Scene.hpp"

#include "engine/audio/AudioClipAssetLoader.hpp"
#include "engine/script/ScriptAssetLoader.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/input/InputAssetLoaders.hpp"
#include "engine/input/InputModule.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/visual/VisualGraphAssetLoader.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/assets/SceneAssetLoader.hpp"
#include "scene/assets/ScenePrefabAssetLoader.hpp"

#include <array>
#include <atomic>
#include <memory>

namespace kb::scene {
namespace {

std::atomic<std::uint64_t> g_nextSceneId{ 1U };

} // namespace

Scene::Scene()
    : Scene(kb::project::ProjectDescriptor{}) {}

Scene::Scene(kb::project::ProjectDescriptor descriptor)
    : Scene(std::move(descriptor), {}) {}

Scene::Scene(kb::project::ProjectDescriptor descriptor, std::vector<std::unique_ptr<kb::modules::IEngineModule>> staticModules)
    : state_(std::make_unique<SceneState>())
    , id_(g_nextSceneId.fetch_add(1U, std::memory_order_relaxed)) {
    const bool registeredPrefabLoader = state_->assets.RegisterLoader(std::make_unique<ScenePrefabAssetLoader>(*this));
    const bool registeredSceneLoader = state_->assets.RegisterLoader(std::make_unique<SceneAssetLoader>());
    const bool registeredLuaScriptLoader = state_->assets.RegisterLoader(std::make_unique<kb::script::LuaScriptAssetLoader>());
    const bool registeredNativeBehaviourLoader = state_->assets.RegisterLoader(std::make_unique<kb::script::NativeBehaviourDescriptorAssetLoader>());
    const bool registeredVisualGraphLoader = state_->assets.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>());
    const bool registeredInputActionLoader = state_->assets.RegisterLoader(std::make_unique<kb::input::InputActionAssetLoader>());
    const bool registeredInputContextLoader = state_->assets.RegisterLoader(std::make_unique<kb::input::InputMappingContextAssetLoader>());
    const bool registeredAudioClipLoader = state_->assets.RegisterLoader(std::make_unique<kb::audio::AudioClipAssetLoader>());
    const bool registeredImportedAssetLoader = state_->assets.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>());
    static_cast<void>(registeredPrefabLoader);
    static_cast<void>(registeredSceneLoader);
    static_cast<void>(registeredLuaScriptLoader);
    static_cast<void>(registeredNativeBehaviourLoader);
    static_cast<void>(registeredVisualGraphLoader);
    static_cast<void>(registeredInputActionLoader);
    static_cast<void>(registeredInputContextLoader);
    static_cast<void>(registeredAudioClipLoader);
    static_cast<void>(registeredImportedAssetLoader);

    // Subsystems are driven through the engine module host instead of being wired
    // by hand, so the project's descriptor can enable or disable them. Input is a
    // built-in module whose OnSceneAttach binds the input resolvers to this scene's
    // asset manager and installs the polling system. The default constructor passes a
    // default descriptor (every built-in enabled), so scenes built without project
    // context (tests, tools) behave exactly as before.
    moduleHost_ = std::make_unique<kb::modules::EngineModuleHost>(std::move(descriptor));
    moduleHost_->Add(std::make_unique<kb::input::InputModule>());
    for (std::unique_ptr<kb::modules::IEngineModule>& module : staticModules) {
        moduleHost_->Add(std::move(module));
    }
    moduleHost_->Load(state_->world);
    moduleHost_->AttachScene(*this);
}

Scene::~Scene() {
    if (moduleHost_ != nullptr) {
        moduleHost_->DetachScene(*this);
    }
    state_->sceneSystemScheduler.Shutdown(*this);
    state_->systemScheduler.Shutdown(state_->world);
    if (moduleHost_ != nullptr) {
        moduleHost_->Unload();
    }
}

kb::input::InputSubsystem& Scene::Input() noexcept {
    return state_->inputSubsystem;
}

const kb::input::InputSubsystem& Scene::Input() const noexcept {
    return state_->inputSubsystem;
}

void Scene::ReloadModules() {
    if (moduleHost_ == nullptr) {
        return;
    }
    std::array<Scene*, 1U> attachedScenes{ this };
    moduleHost_->Reload(state_->world, attachedScenes);
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

void SceneLightingAccess::SetBasicLightingEnabled(Scene& scene, bool enabled) noexcept {
    SceneAccess::State(scene).basicLightingEnabled = enabled;
}

bool SceneLightingAccess::BasicLightingEnabled(const Scene& scene) noexcept {
    return SceneAccess::State(scene).basicLightingEnabled;
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
