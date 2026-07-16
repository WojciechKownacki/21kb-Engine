#include "engine/scene/Scene.hpp"

#include "engine/audio/AudioClipAssetLoader.hpp"
#include "engine/audio/AudioMixerAssetLoader.hpp"
#include "engine/script/ScriptAssetLoader.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputAssetLoaders.hpp"
#include "engine/input/InputLocalUser.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputModule.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/ParticleEffectAssetLoader.hpp"
#include "engine/scene/PhysicsLayersAssetLoader.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/ScenePostProcessAccess.hpp"
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

Scene::Scene(kb::ecs::WorldConfig worldConfig)
    : Scene(kb::project::ProjectDescriptor{}, {}, worldConfig) {}

Scene::Scene(SceneMode mode)
    : Scene(kb::project::ProjectDescriptor{}, mode) {}

Scene::Scene(kb::project::ProjectDescriptor descriptor)
    : Scene(std::move(descriptor), std::vector<std::unique_ptr<kb::modules::IEngineModule>>{}) {}

Scene::Scene(kb::project::ProjectDescriptor descriptor, SceneMode mode)
    : Scene(std::move(descriptor), {}, mode) {}

Scene::Scene(
    kb::project::ProjectDescriptor descriptor,
    std::vector<std::unique_ptr<kb::modules::IEngineModule>> staticModules,
    SceneMode mode)
    : Scene(std::move(descriptor), std::move(staticModules), kb::ecs::WorldConfig{}, mode) {}

Scene::Scene(
    kb::project::ProjectDescriptor descriptor,
    std::vector<std::unique_ptr<kb::modules::IEngineModule>> staticModules,
    kb::ecs::WorldConfig worldConfig,
    SceneMode mode)
    : state_(std::make_unique<SceneState>(worldConfig))
    , id_(g_nextSceneId.fetch_add(1U, std::memory_order_relaxed)) {
    state_->mode = mode;

    const bool registeredPrefabLoader = state_->assets.RegisterLoader(std::make_unique<ScenePrefabAssetLoader>(*this));
    const bool registeredSceneLoader = state_->assets.RegisterLoader(std::make_unique<SceneAssetLoader>());
    const bool registeredLuaScriptLoader = state_->assets.RegisterLoader(std::make_unique<kb::script::LuaScriptAssetLoader>());
    const bool registeredNativeBehaviourLoader = state_->assets.RegisterLoader(std::make_unique<kb::script::NativeBehaviourDescriptorAssetLoader>());
    const bool registeredVisualGraphLoader = state_->assets.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>());
    const bool registeredInputActionLoader = state_->assets.RegisterLoader(std::make_unique<kb::input::InputActionAssetLoader>());
    const bool registeredInputContextLoader = state_->assets.RegisterLoader(std::make_unique<kb::input::InputMappingContextAssetLoader>());
    const bool registeredAudioClipLoader = state_->assets.RegisterLoader(std::make_unique<kb::audio::AudioClipAssetLoader>());
    const bool registeredAudioMixerLoader = state_->assets.RegisterLoader(std::make_unique<kb::audio::AudioMixerAssetLoader>());
    const bool registeredImportedAssetLoader = state_->assets.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>());
    const bool registeredPhysicsLayersLoader = state_->assets.RegisterLoader(std::make_unique<kb::scene::PhysicsLayersAssetLoader>());
    const bool registeredParticleEffectLoader = state_->assets.RegisterLoader(std::make_unique<kb::scene::ParticleEffectAssetLoader>());
    static_cast<void>(registeredPrefabLoader);
    static_cast<void>(registeredSceneLoader);
    static_cast<void>(registeredLuaScriptLoader);
    static_cast<void>(registeredNativeBehaviourLoader);
    static_cast<void>(registeredVisualGraphLoader);
    static_cast<void>(registeredInputActionLoader);
    static_cast<void>(registeredInputContextLoader);
    static_cast<void>(registeredAudioClipLoader);
    static_cast<void>(registeredAudioMixerLoader);
    static_cast<void>(registeredImportedAssetLoader);
    static_cast<void>(registeredPhysicsLayersLoader);
    static_cast<void>(registeredParticleEffectLoader);

    if (mode == SceneMode::PrefabPrivate) {
        return;
    }

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

kb::input::InputSubsystem& Scene::Input(kb::input::LocalUserId user) noexcept {
    if (user == kb::input::kPrimaryLocalUser) {
        return state_->inputSubsystem;
    }
    const auto [iterator, inserted] = state_->secondaryInputSubsystems.try_emplace(user.value);
    if (inserted) {
        // Mirrors InputModule::OnSceneAttach's resolver wiring for the primary
        // subsystem - a fresh secondary subsystem needs the same asset resolvers
        // before any InputComponent targeting it can push a mapping context.
        kb::assets::AssetManager& assetManager = state_->assets;
        iterator->second.SetResolvers(
            [&assetManager](std::uint64_t id) {
                return assetManager.Load<kb::input::InputActionAsset>(kb::assets::AssetId{ id }).Shared();
            },
            [&assetManager](std::uint64_t id) {
                return assetManager.Load<kb::input::InputMappingContextAsset>(kb::assets::AssetId{ id }).Shared();
            });
    }
    return iterator->second;
}

const kb::input::InputSubsystem* Scene::TryGetInput(kb::input::LocalUserId user) const noexcept {
    if (user == kb::input::kPrimaryLocalUser) {
        return &state_->inputSubsystem;
    }
    const auto iterator = state_->secondaryInputSubsystems.find(user.value);
    return iterator != state_->secondaryInputSubsystems.end() ? &iterator->second : nullptr;
}

void Scene::EvaluateAllLocalUserInput(float deltaSeconds) {
    state_->inputSubsystem.Evaluate(deltaSeconds);
    const kb::input::InputDeviceState& sharedDevice = state_->inputSubsystem.DeviceState();
    for (auto& [id, subsystem] : state_->secondaryInputSubsystems) {
        subsystem.EvaluateWithDeviceState(sharedDevice, deltaSeconds);
    }
}

void Scene::ClearAllLocalUserInputMappingContexts() noexcept {
    state_->inputSubsystem.ClearMappingContexts();
    for (auto& [id, subsystem] : state_->secondaryInputSubsystems) {
        subsystem.ClearMappingContexts();
    }
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

SceneMode Scene::Mode() const noexcept {
    return state_->mode;
}

bool Scene::IsPrefabPrivate() const noexcept {
    return Mode() == SceneMode::PrefabPrivate;
}

void SceneLightingAccess::SetBasicLightingEnabled(Scene& scene, bool enabled) noexcept {
    SceneAccess::State(scene).basicLightingEnabled = enabled;
}

bool SceneLightingAccess::BasicLightingEnabled(const Scene& scene) noexcept {
    return SceneAccess::State(scene).basicLightingEnabled;
}

void ScenePostProcessAccess::SetActiveProfile(Scene& scene, std::uint64_t profileAssetId) noexcept {
    SceneAccess::State(scene).postProcessProfileAssetId = profileAssetId;
}

std::uint64_t ScenePostProcessAccess::ActiveProfile(const Scene& scene) noexcept {
    return SceneAccess::State(scene).postProcessProfileAssetId;
}

void SceneAudioMixerAccess::SetActiveMixer(Scene& scene, std::uint64_t mixerAssetId) noexcept {
    SceneAccess::State(scene).audioMixerAssetId = mixerAssetId;
}

std::uint64_t SceneAudioMixerAccess::ActiveMixer(const Scene& scene) noexcept {
    return SceneAccess::State(scene).audioMixerAssetId;
}

void SceneAudioMixerAccess::SetActiveSnapshot(Scene& scene, std::string_view snapshotName) {
    SceneAccess::State(scene).audioMixerSnapshotName.assign(snapshotName);
}

const std::string& SceneAudioMixerAccess::ActiveSnapshot(const Scene& scene) noexcept {
    return SceneAccess::State(scene).audioMixerSnapshotName;
}

void SceneAudioMixerAccess::SetBusVolumeOverride(Scene& scene, std::string_view busName, float volume) {
    SceneState& state = SceneAccess::State(scene);
    for (AudioMixerBusVolumeOverride& override_ : state.audioMixerBusVolumeOverrides) {
        if (override_.bus == busName) {
            override_.volume = volume;
            return;
        }
    }
    state.audioMixerBusVolumeOverrides.push_back(AudioMixerBusVolumeOverride{ .bus = std::string{ busName }, .volume = volume });
}

bool SceneAudioMixerAccess::ClearBusVolumeOverride(Scene& scene, std::string_view busName) noexcept {
    SceneState& state = SceneAccess::State(scene);
    for (auto iterator = state.audioMixerBusVolumeOverrides.begin(); iterator != state.audioMixerBusVolumeOverrides.end(); ++iterator) {
        if (iterator->bus == busName) {
            state.audioMixerBusVolumeOverrides.erase(iterator);
            return true;
        }
    }
    return false;
}

std::span<const AudioMixerBusVolumeOverride> SceneAudioMixerAccess::BusVolumeOverrides(const Scene& scene) noexcept {
    return SceneAccess::State(scene).audioMixerBusVolumeOverrides;
}

void SceneAudioMixerAccess::ResetRuntimeMixerState(Scene& scene) noexcept {
    SceneState& state = SceneAccess::State(scene);
    state.audioMixerBusVolumeOverrides.clear();
    state.audioMixerSnapshotTransition = {};
}

void SceneAudioMixerAccess::BeginSnapshotTransition(Scene& scene, std::string_view toSnapshot, float durationSeconds) {
    SceneState& state = SceneAccess::State(scene);
    // A running transition completes instantly before retargeting - the new blend always
    // starts from a well-defined snapshot state, never from an unrepresentable mid-blend.
    if (state.audioMixerSnapshotTransition.IsActive()) {
        state.audioMixerSnapshotName = state.audioMixerSnapshotTransition.toSnapshot;
        state.audioMixerSnapshotTransition = {};
    }
    if (durationSeconds <= 0.0F) {
        state.audioMixerSnapshotName.assign(toSnapshot);
        return;
    }
    state.audioMixerSnapshotTransition = AudioMixerSnapshotTransition{
        .toSnapshot = std::string{ toSnapshot },
        .elapsedSeconds = 0.0F,
        .durationSeconds = durationSeconds,
    };
}

const AudioMixerSnapshotTransition& SceneAudioMixerAccess::SnapshotTransition(const Scene& scene) noexcept {
    return SceneAccess::State(scene).audioMixerSnapshotTransition;
}

bool SceneAudioMixerAccess::AdvanceSnapshotTransition(Scene& scene, float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene);
    if (!state.audioMixerSnapshotTransition.IsActive()) {
        return false;
    }
    state.audioMixerSnapshotTransition.elapsedSeconds += deltaSeconds < 0.0F ? 0.0F : deltaSeconds;
    if (state.audioMixerSnapshotTransition.elapsedSeconds < state.audioMixerSnapshotTransition.durationSeconds) {
        return false;
    }
    state.audioMixerSnapshotName = state.audioMixerSnapshotTransition.toSnapshot;
    state.audioMixerSnapshotTransition = {};
    return true;
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
