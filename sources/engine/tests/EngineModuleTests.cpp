#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/ecs/System.hpp"
#include "engine/ecs/World.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/modules/EngineModuleContext.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/modules/EngineModuleLoader.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace {

#ifndef KB_NATIVE_SCRIPT_TEST_PLUGIN_PATH
#define KB_NATIVE_SCRIPT_TEST_PLUGIN_PATH ""
#endif

#ifndef KB_BASIC_LIGHTING_PLUGIN_PATH
#define KB_BASIC_LIGHTING_PLUGIN_PATH ""
#endif

[[nodiscard]] std::filesystem::path ModuleTestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_module_tests";
}

void ResetModuleTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(ModuleTestRoot(), error);
    std::filesystem::create_directories(ModuleTestRoot(), error);
    kb::tests::Require(!error, "engine module test root could not be created");
}

// Shared sink the probe module and its scene system write into so a test can observe
// exactly what the host did: which modules loaded, in what order, and whether the
// module's scene system actually reached the scene runtime and ticked.
struct ModuleProbe {
    std::vector<std::string> loadOrder;
    int sceneSystemUpdates = 0;
    int enableCount = 0;
    int disableCount = 0;
    int unloadCount = 0;
    int attachCount = 0;
    int detachCount = 0;
};

class ProbeSystem final : public kb::ecs::System {
public:
    explicit ProbeSystem(ModuleProbe& probe) noexcept
        : probe_(probe) {}

    [[nodiscard]] kb::ecs::SystemAccess DeclareAccess(kb::ecs::World& world) const override {
        static_cast<void>(world);
        return {};
    }

    void OnUpdate(kb::ecs::World& world, float deltaSeconds) override {
        static_cast<void>(world);
        static_cast<void>(deltaSeconds);
        ++probe_.sceneSystemUpdates;
    }

private:
    ModuleProbe& probe_;
};

class ProbeModule final : public kb::modules::IEngineModule {
public:
    ProbeModule(
        std::string name,
        kb::modules::EngineModuleLoadingPhase phase,
        std::vector<std::string> dependencies,
        ModuleProbe& probe,
        bool installSystem = true)
        : name_(std::move(name))
        , phase_(phase)
        , dependencies_(std::move(dependencies))
        , probe_(probe)
        , installSystem_(installSystem) {}

    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override {
        return kb::modules::EngineModuleMetadata{ name_, 1U, dependencies_, phase_ };
    }

    void OnLoad(kb::modules::EngineModuleContext&) override {
        probe_.loadOrder.push_back(name_);
    }

    void OnEnable() override {
        ++probe_.enableCount;
    }

    void OnSceneAttach(kb::scene::Scene& scene) override {
        ++probe_.attachCount;
        if (installSystem_) {
            scene.Runtime().AddSystem(std::make_unique<ProbeSystem>(probe_));
        }
    }

    void OnSceneDetach(kb::scene::Scene&) override {
        ++probe_.detachCount;
    }

    void OnDisable() override {
        ++probe_.disableCount;
    }

    void OnUnload() override {
        ++probe_.unloadCount;
    }

private:
    std::string name_;
    kb::modules::EngineModuleLoadingPhase phase_;
    std::vector<std::string> dependencies_;
    ModuleProbe& probe_;
    bool installSystem_ = true;
};

using kb::modules::EngineModuleHost;
using kb::modules::EngineModuleLoadingPhase;
using Phase = EngineModuleLoadingPhase;

// A module enabled by default reaches both OnLoad and the scene runtime.
void RunActiveByDefaultTest() {
    ModuleProbe probe;
    kb::scene::Scene scene;

    EngineModuleHost host{ kb::project::ProjectDescriptor{} };
    host.Add(std::make_unique<ProbeModule>("Physics", Phase::Default, std::vector<std::string>{}, probe));
    host.Load(scene.Runtime().EcsWorld());

    kb::tests::Require(host.ActiveCount() == 1, "module enabled by default should be active");
    kb::tests::Require(host.IsActive("Physics"), "IsActive should report the default-enabled module");
    kb::tests::Require(probe.loadOrder.size() == 1, "default-enabled module should receive OnLoad");

    host.AttachScene(scene);
    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(probe.sceneSystemUpdates == 1, "active module's scene system should tick");
}

// An explicit disabled plugin reference removes the module from the runtime entirely.
void RunDisabledByProjectTest() {
    ModuleProbe probe;
    kb::scene::Scene scene;

    kb::project::ProjectDescriptor project;
    project.plugins.push_back(kb::project::ProjectPluginReference{ .name = "Physics", .enabled = false });

    EngineModuleHost host{ project };
    host.Add(std::make_unique<ProbeModule>("Physics", Phase::Default, std::vector<std::string>{}, probe));
    host.Load(scene.Runtime().EcsWorld());

    kb::tests::Require(host.ActiveCount() == 0, "disabled module should not be active");
    kb::tests::Require(!host.IsActive("Physics"), "IsActive should be false for a disabled module");
    kb::tests::Require(probe.loadOrder.empty(), "disabled module should not receive OnLoad");

    host.AttachScene(scene);
    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(probe.sceneSystemUpdates == 0, "disabled module must not wire any scene system");
}

// disableEnginePluginsByDefault flips the default: only explicitly enabled plugins run.
void RunOptInDefaultTest() {
    {
        ModuleProbe probe;
        kb::scene::Scene scene;
        kb::project::ProjectDescriptor project;
        project.disableEnginePluginsByDefault = true;

        EngineModuleHost host{ project };
        host.Add(std::make_unique<ProbeModule>("Audio", Phase::Default, std::vector<std::string>{}, probe));
        host.Load(scene.Runtime().EcsWorld());
        kb::tests::Require(host.ActiveCount() == 0, "opt-in default should leave an unlisted module inactive");
    }
    {
        ModuleProbe probe;
        kb::scene::Scene scene;
        kb::project::ProjectDescriptor project;
        project.disableEnginePluginsByDefault = true;
        project.plugins.push_back(kb::project::ProjectPluginReference{ .name = "Audio", .enabled = true });

        EngineModuleHost host{ project };
        host.Add(std::make_unique<ProbeModule>("Audio", Phase::Default, std::vector<std::string>{}, probe));
        host.Load(scene.Runtime().EcsWorld());
        kb::tests::Require(host.IsActive("Audio"), "opt-in default should activate an explicitly enabled module");
    }
}

void RunSceneStaticModuleInjectionTest() {
    ModuleProbe probe;
    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back(kb::project::ProjectPluginReference{ .name = "Static.Audio", .enabled = true });

    std::vector<std::unique_ptr<kb::modules::IEngineModule>> modules;
    modules.push_back(std::make_unique<ProbeModule>("Static.Audio", Phase::Default, std::vector<std::string>{}, probe));
    kb::scene::Scene scene{ std::move(project), std::move(modules) };

    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(probe.loadOrder.size() == 1 && probe.loadOrder[0] == "Static.Audio", "Scene did not load the injected static module");
    kb::tests::Require(probe.sceneSystemUpdates == 1, "Scene did not attach the injected static module system");
}

void RunSceneModuleReloadLifecycleTest() {
    ModuleProbe probe;
    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back(kb::project::ProjectPluginReference{ .name = "Static.Reload", .enabled = true });

    std::vector<std::unique_ptr<kb::modules::IEngineModule>> modules;
    modules.push_back(std::make_unique<ProbeModule>("Static.Reload", Phase::Default, std::vector<std::string>{}, probe, false));
    kb::scene::Scene scene{ std::move(project), std::move(modules) };

    kb::tests::Require(probe.loadOrder.size() == 1, "Scene should load the reload probe during construction");
    kb::tests::Require(probe.enableCount == 1, "Scene should enable the reload probe during construction");
    kb::tests::Require(probe.attachCount == 1, "Scene should attach the reload probe during construction");

    scene.ReloadModules();

    kb::tests::Require(probe.detachCount == 1, "Reload should detach active modules before unload");
    kb::tests::Require(probe.disableCount == 1, "Reload should disable active modules before reload");
    kb::tests::Require(probe.unloadCount == 1, "Reload should unload active modules before reload");
    kb::tests::Require(probe.loadOrder.size() == 2, "Reload should load active modules again");
    kb::tests::Require(probe.enableCount == 2, "Reload should enable active modules again");
    kb::tests::Require(probe.attachCount == 2, "Reload should attach active modules again");
}

// Dependencies load before dependents regardless of registration order.
void RunDependencyOrderTest() {
    ModuleProbe probe;
    kb::scene::Scene scene;

    EngineModuleHost host{ kb::project::ProjectDescriptor{} };
    host.Add(std::make_unique<ProbeModule>("Render", Phase::Default, std::vector<std::string>{ "Core" }, probe));
    host.Add(std::make_unique<ProbeModule>("Core", Phase::Default, std::vector<std::string>{}, probe));
    host.Load(scene.Runtime().EcsWorld());

    kb::tests::Require(probe.loadOrder.size() == 2, "both modules should load");
    kb::tests::Require(probe.loadOrder[0] == "Core", "dependency must load before its dependent");
    kb::tests::Require(probe.loadOrder[1] == "Render", "dependent must load after its dependency");
}

// Loading phase orders modules ahead of dependency-free registration order.
void RunPhaseOrderTest() {
    ModuleProbe probe;
    kb::scene::Scene scene;

    EngineModuleHost host{ kb::project::ProjectDescriptor{} };
    host.Add(std::make_unique<ProbeModule>("Late", Phase::PostDefault, std::vector<std::string>{}, probe));
    host.Add(std::make_unique<ProbeModule>("Early", Phase::PreDefault, std::vector<std::string>{}, probe));
    host.Load(scene.Runtime().EcsWorld());

    kb::tests::Require(probe.loadOrder.size() == 2, "both modules should load");
    kb::tests::Require(probe.loadOrder[0] == "Early", "earlier loading phase must load first");
    kb::tests::Require(probe.loadOrder[1] == "Late", "later loading phase must load last");
}

void RunEngineModuleLoaderShadowCopyTest() {
    ResetModuleTestRoot();
    const std::filesystem::path pluginPath = KB_NATIVE_SCRIPT_TEST_PLUGIN_PATH;
    kb::tests::Require(!pluginPath.empty() && std::filesystem::is_regular_file(pluginPath), "module loader test DLL is missing");

    kb::modules::EngineModuleLoader loader;
    kb::modules::EngineModuleLoadResult first = loader.Load(kb::modules::EngineModuleLoadDesc{
        .key = "module-loader-test",
        .modulePath = pluginPath,
        .shadowCopy = true,
        .shadowCopyDirectory = ModuleTestRoot() / "EngineModuleLoaderShadow",
        .diagnosticLabel = "module loader test",
    });
    kb::tests::Require(first.Succeeded(), "EngineModuleLoader did not load a shadow-copied DLL");
    kb::tests::Require(first.reloadSerial == 1U, "EngineModuleLoader did not assign the first reload serial");
    kb::tests::Require(first.loadedPath != first.originalPath, "EngineModuleLoader did not shadow-copy the DLL");
    kb::tests::Require(std::filesystem::is_regular_file(first.loadedPath), "EngineModuleLoader shadow copy file is missing");

    std::vector<std::string> symbolErrors;
    kb::tests::Require(first.library.FindSymbol("kb_register_native_scripts", symbolErrors, "module loader test") != nullptr, "EngineModuleLoader did not resolve an exported symbol");
    kb::tests::Require(symbolErrors.empty(), "EngineModuleLoader reported errors for a valid symbol");
    const std::filesystem::path firstLoadedPath = first.loadedPath;
    first.library.Reset();

    kb::modules::EngineModuleLoadResult second = loader.Load(kb::modules::EngineModuleLoadDesc{
        .key = "module-loader-test",
        .modulePath = pluginPath,
        .shadowCopy = true,
        .shadowCopyDirectory = ModuleTestRoot() / "EngineModuleLoaderShadow",
        .diagnosticLabel = "module loader test",
    });
    kb::tests::Require(second.Succeeded(), "EngineModuleLoader did not load the second shadow-copied DLL");
    kb::tests::Require(second.reloadSerial == 2U, "EngineModuleLoader did not advance the reload serial");
    kb::tests::Require(second.loadedPath != firstLoadedPath, "EngineModuleLoader reused a shadow-copy path across reloads");
}

// End-to-end through the production Scene(ProjectDescriptor) path: the built-in
// Input module is what installs the polling system, so disabling it in the project
// descriptor must stop the scene tick from evaluating input. Proves 1.4b: the
// descriptor passed to a Scene actually drives which subsystems run.
void RunSceneInputToggleTest() {
    auto move = std::make_shared<kb::input::InputActionAsset>();
    move->name = "Move";
    move->valueType = kb::input::InputActionValueType::Axis1D;
    move->consumeInput = true;
    auto context = std::make_shared<kb::input::InputMappingContextAsset>();
    context->mappings.push_back(kb::input::InputKeyMapping{ .actionId = 1U, .key = kb::input::InputKey::W, .scale = 1.0F });

    const auto wire = [&move, &context](kb::scene::Scene& scene) {
        scene.Input().SetResolvers(
            [move](std::uint64_t id) -> std::shared_ptr<const kb::input::InputActionAsset> {
                return id == 1U ? move : nullptr;
            },
            [context](std::uint64_t id) -> std::shared_ptr<const kb::input::InputMappingContextAsset> {
                return id == 10U ? context : nullptr;
            });
        kb::tests::Require(scene.Input().AddMappingContext(10U, 0), "mapping context should resolve in the toggle test");
        scene.Input().MutableDeviceState().SetKeyDown(kb::input::InputKey::W, true);
    };

    // Input enabled (default descriptor): OnSceneAttach installs the polling system,
    // so Update evaluates input and Move reads +1 while W is held.
    {
        kb::scene::Scene scene;
        wire(scene);
        static_cast<void>(scene.Runtime().Update(0.016F));
        kb::tests::Require(
            kb::tests::NearlyEqual(scene.Input().GetActionValue("Move").AsAxis1D(), 1.0F),
            "with Input enabled the scene tick should evaluate input");
    }

    // Input disabled via the project descriptor: no polling system is installed, so
    // the scene tick never evaluates input and Move stays at rest despite W held.
    {
        kb::project::ProjectDescriptor project;
        project.plugins.push_back(kb::project::ProjectPluginReference{ .name = "Input", .enabled = false });
        kb::scene::Scene scene{ project };
        wire(scene);
        static_cast<void>(scene.Runtime().Update(0.016F));
        kb::tests::Require(
            kb::tests::NearlyEqual(scene.Input().GetActionValue("Move").AsAxis1D(), 0.0F),
            "with Input disabled the scene tick must not evaluate input");
    }
}

void RunBasicLightingPluginTogglesSceneLightingTest() {
    const std::filesystem::path pluginPath = KB_BASIC_LIGHTING_PLUGIN_PATH;
    kb::tests::Require(!pluginPath.empty() && std::filesystem::is_regular_file(pluginPath), "Basic Lighting plugin DLL is missing");

    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Rendering.BasicLighting",
        .binaryPath = pluginPath.string(),
        .enabled = true,
    });

    kb::scene::Scene scene;
    EngineModuleHost host{ project };
    host.Load(scene.Runtime().EcsWorld());
    kb::tests::Require(host.IsActive("Rendering.BasicLighting"), "Basic Lighting plugin was not active after host load");
    host.AttachScene(scene);
    kb::tests::Require(
        kb::scene::SceneLightingAccess::BasicLightingEnabled(scene),
        "Basic Lighting plugin did not enable scene lighting on attach");

    host.DetachScene(scene);
    kb::tests::Require(
        !kb::scene::SceneLightingAccess::BasicLightingEnabled(scene),
        "Basic Lighting plugin did not disable scene lighting on detach");
}

} // namespace

namespace kb::tests {

void RunEngineModuleTests() {
    RunActiveByDefaultTest();
    RunDisabledByProjectTest();
    RunOptInDefaultTest();
    RunSceneStaticModuleInjectionTest();
    RunSceneModuleReloadLifecycleTest();
    RunDependencyOrderTest();
    RunPhaseOrderTest();
    RunEngineModuleLoaderShadowCopyTest();
    RunSceneInputToggleTest();
    RunBasicLightingPluginTogglesSceneLightingTest();
}

} // namespace kb::tests
