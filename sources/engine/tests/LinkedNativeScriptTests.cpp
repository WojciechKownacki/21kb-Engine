#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/NativeScriptPluginManager.hpp"
#include "engine/script/ScriptRuntime.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
constexpr const char* kSymbol = "tests.LinkedCharacter";
int destroyed = 0;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Ready(kb::script::ScriptExecutionContext* context) {
    auto transform = context->GetScene().Transforms().Get(context->Self());
    transform.localPosition.x = 3.0F;
    context->GetScene().Transforms().Set(context->Self(), transform);
}
void Tick(kb::script::ScriptExecutionContext* context) {
    auto transform = context->GetScene().Transforms().Get(context->Self());
    transform.localPosition.x += context->DeltaSeconds();
    context->GetScene().Transforms().Set(context->Self(), transform);
}
void Destroyed(kb::script::ScriptExecutionContext*) { ++destroyed; }
void Ping(kb::script::ScriptExecutionContext* context, const kb::script::ScriptEvent*) {
    auto transform = context->GetScene().Transforms().Get(context->Self());
    transform.localPosition.y = 7.0F;
    context->GetScene().Transforms().Set(context->Self(), transform);
}
bool Register(kb::script::NativeScriptPluginApi* api) {
    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&
        api->registerLifecycle(api->user, kSymbol, kb::script::ScriptLifecycleEvent::Ready, &Ready) &&
        api->registerLifecycle(api->user, kSymbol, kb::script::ScriptLifecycleEvent::Tick, &Tick) &&
        api->registerLifecycle(api->user, kSymbol, kb::script::ScriptLifecycleEvent::Destroyed, &Destroyed) &&
        api->registerEvent(api->user, kSymbol, "LinkedPing", &Ping);
}
bool RejectAfterPartialRegistration(kb::script::NativeScriptPluginApi* api) {
    static_cast<void>(api->registerLifecycle(api->user, kSymbol, kb::script::ScriptLifecycleEvent::Tick, &Ready));
    return false;
}
}

int main() {
    try {
        kb::scene::Scene scene;
        const auto object = scene.Entities().CreateObject({.name = "LinkedCharacter"});
        constexpr kb::assets::AssetId asset{1003U};
        scene.Components().Behaviours().Set(object.Entity(), {.behaviourAssetId=asset.value,
            .backend=kb::scene::BehaviourBackend::Native,.enabled=true});
        kb::script::ScriptRuntime runtime;
        auto backend = std::make_unique<kb::script::NativeScriptBackend>();
        auto* native = backend.get();
        kb::script::NativeScriptPluginManager modules{*native};
        Require(!modules.LoadLinked("", &Register).Succeeded(), "Empty linked module key accepted");
        Require(!modules.LoadLinked("invalid", nullptr).Succeeded(), "Missing entry point accepted");
        const auto loaded = modules.LoadLinked("project", &Register);
        Require(loaded.Succeeded() && loaded.loadedPath.empty() && modules.IsLoaded("project"), "Linked module registration failed");
        Require(native->BindAssetSymbol(asset, kSymbol), "Linked asset symbol binding failed");
        Require(runtime.RegisterBackend(std::move(backend)), "Native backend installation failed");
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Ready,0.0F).Succeeded(), "Ready failed");
        Require(scene.Transforms().Get(object.Entity()).localPosition.x == 3.0F, "Linked Ready did not change the scene");
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Tick,1.0F).executedBehaviours == 1U, "Tick was not dispatched");
        Require(scene.Transforms().Get(object.Entity()).localPosition.x == 4.0F, "Linked Tick did not change the scene");
        Require(runtime.DispatchEvent(scene,{.name="LinkedPing"},0.0F).executedBehaviours == 1U, "Linked event not dispatched");
        Require(scene.Transforms().Get(object.Entity()).localPosition.y == 7.0F, "Linked event did not change the scene");
        Require(!modules.LoadLinked("project", &RejectAfterPartialRegistration).Succeeded(), "Partial failed registration accepted");
        Require(modules.IsLoaded("project"), "Failed replacement lost the linked module");
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Tick,1.0F).executedBehaviours == 1U, "Rollback lost Tick");
        Require(scene.Transforms().Get(object.Entity()).localPosition.x == 5.0F, "Rollback did not restore original callback");
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Destroyed,0.0F).Succeeded() && destroyed == 1, "Destroyed not dispatched");
        modules.Unload("project");
        Require(!modules.IsLoaded("project"), "Linked module was not unloaded");
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Tick,1.0F).executedBehaviours == 0U, "Callback survived unload");
        std::cout << "Linked native Ready/Tick/event/Destroyed, replacement rollback and unload PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
