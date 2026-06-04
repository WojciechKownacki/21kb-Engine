#include "engine/script/ScriptRuntimeHost.hpp"

#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"
#include "engine/script/ScriptSceneVisualGraphBindings.hpp"

#include <memory>
#include <utility>

namespace kb::script {
namespace {

class ScriptRuntimeHostSceneSystem final : public kb::scene::SceneSystem {
public:
    explicit ScriptRuntimeHostSceneSystem(std::shared_ptr<ScriptRuntimeHostState> state);

    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;

private:
    std::shared_ptr<ScriptRuntimeHostState> state_;
    ScriptRuntimeSceneSystem system_;
};

} // namespace

struct ScriptRuntimeHostState final {
    explicit ScriptRuntimeHostState(kb::scene::Scene& sceneRef)
        : scene(sceneRef)
        , assetPreparer(sceneRef.Assets().Manager(), luaRuntime, visualGraphs) {}

    kb::scene::Scene& scene;
    PucLuaScriptRuntime luaRuntime;
    kb::visual::VisualGraphRuntimeRegistry visualGraphs;
    kb::visual::VisualGraphRuntimeBindingRegistry visualGraphRuntimeBindings;
    kb::visual::VisualGraphNativeBindingRegistry visualGraphNativeBindings;
    kb::visual::VisualGraphBehaviourInstanceRegistry visualGraphInstances;
    ScriptRuntime runtime;
    ScriptRuntimeAssetPreparer assetPreparer;
    NativeScriptBackend* nativeBackend = nullptr;
    LuaScriptBackend* luaBackend = nullptr;
    VisualGraphScriptBackend* visualGraphBackend = nullptr;
};

namespace {

ScriptRuntimeHostSceneSystem::ScriptRuntimeHostSceneSystem(std::shared_ptr<ScriptRuntimeHostState> state)
    : state_(std::move(state))
    , system_(state_->runtime, state_->assetPreparer) {}

void ScriptRuntimeHostSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    system_.OnCreate(context);
}

void ScriptRuntimeHostSceneSystem::OnUpdate(kb::scene::SceneSystemContext& context) {
    system_.OnUpdate(context);
}

void ScriptRuntimeHostSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    system_.OnDestroy(context);
}

} // namespace

ScriptRuntimeHost::ScriptRuntimeHost(kb::scene::Scene& scene, ScriptRuntimeHostOptions options)
    : state_(std::make_shared<ScriptRuntimeHostState>(scene)) {
    if (options.visualGraphPrepareSettings.nativeBindings == nullptr) {
        options.visualGraphPrepareSettings.nativeBindings = &state_->visualGraphNativeBindings;
    }
    state_->assetPreparer.SetVisualGraphSettings(std::move(options.visualGraphPrepareSettings));
    RegisterDefaultBackends();
    if (options.installSceneSystem) {
        static_cast<void>(InstallSceneSystem());
    }
}

ScriptRuntimeHost::~ScriptRuntimeHost() = default;

bool ScriptRuntimeHost::Succeeded() const noexcept {
    return diagnostics_.empty();
}

const std::vector<std::string>& ScriptRuntimeHost::Diagnostics() const noexcept {
    return diagnostics_;
}

bool ScriptRuntimeHost::InstallSceneSystem() {
    if (sceneSystemInstalled_) {
        return true;
    }
    if (!Succeeded()) {
        return false;
    }
    state_->scene.Runtime().AddSceneSystem(std::make_unique<ScriptRuntimeHostSceneSystem>(state_));
    sceneSystemInstalled_ = true;
    return true;
}

kb::visual::VisualGraphNodeCatalog ScriptRuntimeHost::CreateVisualGraphNodeCatalog() const {
    kb::visual::VisualGraphNodeCatalog catalog = kb::visual::VisualGraphNodeCatalog::CreateDefault();
    catalog.RegisterNativeBindings(state_->visualGraphNativeBindings);
    catalog.RegisterRuntimeBindings(state_->visualGraphRuntimeBindings);
    return catalog;
}

ScriptRuntime& ScriptRuntimeHost::Runtime() noexcept {
    return state_->runtime;
}

const ScriptRuntime& ScriptRuntimeHost::Runtime() const noexcept {
    return state_->runtime;
}

ScriptRuntimeAssetPreparer& ScriptRuntimeHost::AssetPreparer() noexcept {
    return state_->assetPreparer;
}

const ScriptRuntimeAssetPreparer& ScriptRuntimeHost::AssetPreparer() const noexcept {
    return state_->assetPreparer;
}

PucLuaScriptRuntime& ScriptRuntimeHost::LuaRuntime() noexcept {
    return state_->luaRuntime;
}

const PucLuaScriptRuntime& ScriptRuntimeHost::LuaRuntime() const noexcept {
    return state_->luaRuntime;
}

NativeScriptBackend& ScriptRuntimeHost::NativeBackend() noexcept {
    return *state_->nativeBackend;
}

const NativeScriptBackend& ScriptRuntimeHost::NativeBackend() const noexcept {
    return *state_->nativeBackend;
}

VisualGraphScriptBackend& ScriptRuntimeHost::VisualGraphBackend() noexcept {
    return *state_->visualGraphBackend;
}

const VisualGraphScriptBackend& ScriptRuntimeHost::VisualGraphBackend() const noexcept {
    return *state_->visualGraphBackend;
}

kb::visual::VisualGraphRuntimeRegistry& ScriptRuntimeHost::VisualGraphs() noexcept {
    return state_->visualGraphs;
}

const kb::visual::VisualGraphRuntimeRegistry& ScriptRuntimeHost::VisualGraphs() const noexcept {
    return state_->visualGraphs;
}

kb::visual::VisualGraphRuntimeBindingRegistry& ScriptRuntimeHost::VisualGraphRuntimeBindings() noexcept {
    return state_->visualGraphRuntimeBindings;
}

const kb::visual::VisualGraphRuntimeBindingRegistry& ScriptRuntimeHost::VisualGraphRuntimeBindings() const noexcept {
    return state_->visualGraphRuntimeBindings;
}

kb::visual::VisualGraphNativeBindingRegistry& ScriptRuntimeHost::VisualGraphNativeBindings() noexcept {
    return state_->visualGraphNativeBindings;
}

const kb::visual::VisualGraphNativeBindingRegistry& ScriptRuntimeHost::VisualGraphNativeBindings() const noexcept {
    return state_->visualGraphNativeBindings;
}

kb::visual::VisualGraphBehaviourInstanceRegistry& ScriptRuntimeHost::VisualGraphInstances() noexcept {
    return state_->visualGraphInstances;
}

const kb::visual::VisualGraphBehaviourInstanceRegistry& ScriptRuntimeHost::VisualGraphInstances() const noexcept {
    return state_->visualGraphInstances;
}

void ScriptRuntimeHost::RegisterDefaultBackends() {
    auto nativeBackend = std::make_unique<NativeScriptBackend>();
    state_->nativeBackend = nativeBackend.get();
    if (!state_->runtime.RegisterBackend(std::move(nativeBackend))) {
        AddDiagnostic("native script backend could not be registered");
    } else {
        state_->assetPreparer.SetNativeBackend(*state_->nativeBackend);
    }

    auto luaBackend = std::make_unique<LuaScriptBackend>(state_->luaRuntime);
    state_->luaBackend = luaBackend.get();
    if (!state_->runtime.RegisterBackend(std::move(luaBackend))) {
        AddDiagnostic("Lua script backend could not be registered");
    }

    if (!ScriptSceneVisualGraphBindings::Register(state_->visualGraphRuntimeBindings, state_->scene)) {
        AddDiagnostic("VisualGraph scene component runtime bindings could not be registered");
    }

    auto visualGraphBackend = std::make_unique<VisualGraphScriptBackend>(state_->visualGraphs, state_->visualGraphRuntimeBindings, state_->visualGraphInstances);
    state_->visualGraphBackend = visualGraphBackend.get();
    if (!state_->runtime.RegisterBackend(std::move(visualGraphBackend))) {
        AddDiagnostic("VisualGraph script backend could not be registered");
    }
}

void ScriptRuntimeHost::AddDiagnostic(std::string message) {
    diagnostics_.push_back(std::move(message));
}

} // namespace kb::script
