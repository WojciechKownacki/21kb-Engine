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
#include "engine/modules/IEngineModule.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

// Shared sink the probe module and its scene system write into so a test can observe
// exactly what the host did: which modules loaded, in what order, and whether the
// module's scene system actually reached the scene runtime and ticked.
struct ModuleProbe {
    std::vector<std::string> loadOrder;
    int sceneSystemUpdates = 0;
};

class ProbeSystem final : public kb::ecs::System {
public:
    explicit ProbeSystem(ModuleProbe& probe) noexcept
        : probe_(probe) {}

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
        ModuleProbe& probe)
        : name_(std::move(name))
        , phase_(phase)
        , dependencies_(std::move(dependencies))
        , probe_(probe) {}

    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override {
        return kb::modules::EngineModuleMetadata{ name_, 1U, dependencies_, phase_ };
    }

    void OnLoad(kb::modules::EngineModuleContext&) override {
        probe_.loadOrder.push_back(name_);
    }

    void OnSceneAttach(kb::scene::Scene& scene) override {
        scene.Runtime().AddSystem(std::make_unique<ProbeSystem>(probe_));
    }

private:
    std::string name_;
    kb::modules::EngineModuleLoadingPhase phase_;
    std::vector<std::string> dependencies_;
    ModuleProbe& probe_;
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

} // namespace

namespace kb::tests {

void RunEngineModuleTests() {
    RunActiveByDefaultTest();
    RunDisabledByProjectTest();
    RunOptInDefaultTest();
    RunDependencyOrderTest();
    RunPhaseOrderTest();
    RunSceneInputToggleTest();
}

} // namespace kb::tests
