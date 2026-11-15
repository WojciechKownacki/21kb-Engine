#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/NativeScriptPluginManager.hpp"
#include "engine/script/ScriptRuntime.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

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

// A script written as a class: the hooks it defines are found by name.
int classDestroyed = 0;
struct ClassStyle {
    static void Ready(kb::script::ScriptExecutionContext* context) {
        auto transform = context->GetScene().Transforms().Get(context->Self());
        transform.localPosition.z = 2.0F;
        context->GetScene().Transforms().Set(context->Self(), transform);
    }
    static void Tick(kb::script::ScriptExecutionContext* context) {
        auto transform = context->GetScene().Transforms().Get(context->Self());
        transform.localPosition.z += 1.0F;
        context->GetScene().Transforms().Set(context->Self(), transform);
    }
    static void Destroyed(kb::script::ScriptExecutionContext*) { ++classDestroyed; }
};
struct NoHooks {
    static void Helper(kb::script::ScriptExecutionContext*) {}
};
bool RegisterClassStyle(kb::script::NativeScriptPluginApi* api) {
    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&
        kb::script::RegisterNativeScript<ClassStyle>(api, "tests.ClassStyle");
}

// A script class with state: one object for each scene object, kept between calls, carried over a reload.
int counterDestroyed = 0;
struct Counter {
    int ticks = 0;
    std::string label = "start";
    ~Counter() { ++counterDestroyed; }
    void Ready(kb::script::ScriptExecutionContext*) { label = "ready"; }
    void Tick(kb::script::ScriptExecutionContext* context) {
        ++ticks;
        auto transform = context->GetScene().Transforms().Get(context->Self());
        transform.localPosition.x = static_cast<float>(ticks);
        context->GetScene().Transforms().Set(context->Self(), transform);
    }
    void Serialize(kb::script::NativeScriptArchive& archive) {
        archive.Field("ticks", ticks);
        archive.Field("label", label);
    }
};
// The next build of the same script: another behaviour, a new member and one the old build did not list.
struct CounterNext {
    int ticks = 0;
    std::string label = "start";
    std::string note = "new";
    void Tick(kb::script::ScriptExecutionContext* context) {
        ++ticks;
        auto transform = context->GetScene().Transforms().Get(context->Self());
        transform.localPosition.x = static_cast<float>(ticks) * 10.0F;
        transform.localPosition.y = label == "ready" && note == "new" ? 1.0F : 0.0F;
        context->GetScene().Transforms().Set(context->Self(), transform);
    }
    void Serialize(kb::script::NativeScriptArchive& archive) {
        archive.Field("ticks", ticks);
        archive.Field("label", label);
        archive.Field("note", note);
    }
};
// Data members and no Serialize: a rebuilt module starts them over. A class with hooks only has nothing to lose.
struct Forgetful {
    int ticks = 0;
    void Tick(kb::script::ScriptExecutionContext*) { ++ticks; }
};
struct Stateless {
    void Tick(kb::script::ScriptExecutionContext*) {}
};
bool RegisterForgetfulAndStateless(kb::script::NativeScriptPluginApi* api) {
    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&
        kb::script::RegisterNativeScript<Forgetful>(api, "tests.Forgetful") &&
        kb::script::RegisterNativeScript<Stateless>(api, "tests.Stateless");
}
bool RegisterCounter(kb::script::NativeScriptPluginApi* api) {
    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&
        kb::script::RegisterNativeScript<Counter>(api, "tests.Counter");
}
bool RegisterCounterNext(kb::script::NativeScriptPluginApi* api) {
    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&
        kb::script::RegisterNativeScript<CounterNext>(api, "tests.Counter");
}
bool RegisterNoHooks(kb::script::NativeScriptPluginApi* api) {
    return api != nullptr && kb::script::RegisterNativeScript<NoHooks>(api, "tests.NoHooks");
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
        // The same through RegisterNativeScript: Ready, Tick and Destroyed are found on the class, and a class
        // that defines no hook is refused instead of registering nothing.
        const auto classObject = scene.Entities().CreateObject({.name = "ClassCharacter"});
        constexpr kb::assets::AssetId classAsset{1004U};
        scene.Components().Behaviours().Set(classObject.Entity(), {.behaviourAssetId=classAsset.value,
            .backend=kb::scene::BehaviourBackend::Native,.enabled=true});
        Require(!modules.LoadLinked("hookless", &RegisterNoHooks).Succeeded(), "A class without hooks was accepted");
        Require(modules.LoadLinked("class", &RegisterClassStyle).Succeeded(), "Class registration failed");
        Require(native->BindAssetSymbol(classAsset, "tests.ClassStyle"), "Class asset symbol binding failed");
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Ready,0.0F).Succeeded(), "Class Ready failed");
        Require(scene.Transforms().Get(classObject.Entity()).localPosition.z == 2.0F, "Class Ready was not found by name");
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Tick,1.0F).Succeeded(), "Class Tick failed");
        Require(scene.Transforms().Get(classObject.Entity()).localPosition.z == 3.0F, "Class Tick was not found by name");
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Destroyed,0.0F).Succeeded() && classDestroyed == 1, "Class Destroyed was not found by name");
        // A class with member hooks: an object for each scene object, with its own members.
        constexpr kb::assets::AssetId counterAsset{1005U};
        const auto first = scene.Entities().CreateObject({.name = "CounterA"});
        const auto second = scene.Entities().CreateObject({.name = "CounterB"});
        for (const auto entity : {first.Entity(), second.Entity()}) {
            scene.Components().Behaviours().Set(entity, {.behaviourAssetId=counterAsset.value,
                .backend=kb::scene::BehaviourBackend::Native,.enabled=true});
        }
        Require(modules.LoadLinked("counter", &RegisterCounter).Succeeded(), "Stateful class registration failed");
        Require(native->BindAssetSymbol(counterAsset, "tests.Counter"), "Stateful class asset symbol binding failed");
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Ready,0.0F).Succeeded(), "Stateful Ready failed");
        Require(native->InstanceCount("tests.Counter") == 2U, "An object was not made for each scene object");
        // Which classes would lose their members in a rebuilt module (the editor warns about them once).
        Require(modules.LoadLinked("forgetful", &RegisterForgetfulAndStateless).Succeeded(), "Warning fixture registration failed");
        Require(native->ClassLosesStateOnReload("tests.Forgetful"), "A class with members and no Serialize was not reported");
        Require(!native->ClassLosesStateOnReload("tests.Stateless"), "A class without members was reported");
        Require(!native->ClassLosesStateOnReload("tests.Counter"), "A class that lists its members in Serialize was reported");
        Require(!native->ClassLosesStateOnReload("tests.Missing"), "An unknown class was reported");
        for (int tick = 0; tick < 3; ++tick) {
            Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Tick,1.0F).Succeeded(), "Stateful Tick failed");
        }
        Require(scene.Transforms().Get(first.Entity()).localPosition.x == 3.0F && scene.Transforms().Get(second.Entity()).localPosition.x == 3.0F,
            "Member variables were not kept between calls");
        // Rebuilt module: the old objects are destroyed by the old code, the new class makes them again with the saved values.
        const int destroyedBefore = counterDestroyed;
        Require(modules.LoadLinked("counter", &RegisterCounterNext).Succeeded(), "Stateful class reload failed");
        Require(counterDestroyed - destroyedBefore == 2, "The objects of the old build were not destroyed");
        Require(native->InstanceCount("tests.Counter") == 2U, "The objects were not made again after the reload");
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Tick,1.0F).Succeeded(), "Tick after the reload failed");
        Require(scene.Transforms().Get(first.Entity()).localPosition.x == 40.0F, "The saved member was not restored into the new build");
        Require(scene.Transforms().Get(first.Entity()).localPosition.y == 1.0F, "A saved string or a new member's default was wrong after the reload");
        // A failed replacement leaves the objects as they are.
        Require(!modules.LoadLinked("counter", &RejectAfterPartialRegistration).Succeeded(), "A failed class replacement was accepted");
        Require(native->InstanceCount("tests.Counter") == 2U, "A failed replacement dropped the objects");
        // The object goes with the behaviour.
        Require(runtime.ExecuteLifecycle(scene,kb::script::ScriptLifecycleEvent::Destroyed,0.0F).Succeeded(), "Stateful Destroyed failed");
        Require(native->InstanceCount("tests.Counter") == 0U, "The objects outlived their behaviours");
        // What one call costs when thousands of objects run the same script: the lookups are per script, not per call.
        {
            kb::scene::Scene crowd;
            constexpr kb::assets::AssetId crowdAsset{1006U};
            constexpr int kObjects = 5000;
            constexpr int kFrames = 100;
            for (int index = 0; index < kObjects; ++index) {
                const auto member = crowd.Entities().CreateObject({.name = "Crowd"});
                crowd.Components().Behaviours().Set(member.Entity(), {.behaviourAssetId=crowdAsset.value,
                    .backend=kb::scene::BehaviourBackend::Native,.enabled=true});
            }
            kb::script::ScriptRuntime crowdRuntime;
            auto crowdBackend = std::make_unique<kb::script::NativeScriptBackend>();
            auto* crowdNative = crowdBackend.get();
            kb::script::NativeScriptPluginManager crowdModules{*crowdNative};
            Require(crowdModules.LoadLinked("counter", &RegisterCounter).Succeeded() && crowdNative->BindAssetSymbol(crowdAsset, "tests.Counter"),
                "Crowd script registration failed");
            Require(crowdRuntime.RegisterBackend(std::move(crowdBackend)), "Crowd backend installation failed");
            Require(crowdRuntime.ExecuteLifecycle(crowd,kb::script::ScriptLifecycleEvent::Ready,0.0F).Succeeded(), "Crowd Ready failed");
            const auto started = std::chrono::steady_clock::now();
            std::size_t calls = 0U;
            for (int frame = 0; frame < kFrames; ++frame) {
                calls += crowdRuntime.ExecuteLifecycle(crowd,kb::script::ScriptLifecycleEvent::Tick,0.016F).executedBehaviours;
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            Require(calls == static_cast<std::size_t>(kObjects) * kFrames, "Not every object ran its Tick");
            std::cout << "Linked native dispatch: " << kObjects << " objects, " << (seconds * 1.0e6 / static_cast<double>(calls)) << " us per Tick call\n";
        }
        std::cout << "Linked native Ready/Tick/event/Destroyed, replacement rollback and unload PASS\n";
        std::cout << "Linked native per-object state, reload with restored members and destruction PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
