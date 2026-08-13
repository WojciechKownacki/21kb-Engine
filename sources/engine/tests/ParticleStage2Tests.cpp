#include "engine/ecs/World.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/project/ParticleProjectPolicy.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/MotionSkeletonRuleComponent.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"
#include "engine/script/ScriptModule.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabOptionalComponentMask.hpp"
#include "ParticleModule.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#ifndef KB_21KB_PARTICLE_PLUGIN_PATH
#define KB_21KB_PARTICLE_PLUGIN_PATH ""
#endif

namespace {

void Require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{ std::string{ message } };
}

class Backend final : public kb::particles::IParticleSimulationBackend {
public:
    [[nodiscard]] kb::particles::ParticleRuntimeResult Create(kb::scene::Scene&, std::uint64_t, kb::scene::SceneEntity) override {
        ++calls;
        return { .status = kb::particles::ParticleRuntimeStatus::Success, .instanceId = 17U };
    }
    [[nodiscard]] kb::particles::ParticleRuntimeResult Release(kb::scene::Scene&, std::uint64_t id) noexcept override { return Control(id); }
    [[nodiscard]] kb::particles::ParticleRuntimeResult Play(kb::scene::Scene&, std::uint64_t id) noexcept override { return Control(id); }
    [[nodiscard]] kb::particles::ParticleRuntimeResult Pause(kb::scene::Scene&, std::uint64_t id) noexcept override { return Control(id); }
    [[nodiscard]] kb::particles::ParticleRuntimeResult Stop(kb::scene::Scene&, std::uint64_t id) noexcept override { return Control(id); }
    [[nodiscard]] kb::particles::ParticleRuntimeResult Restart(kb::scene::Scene&, std::uint64_t id) noexcept override { return Control(id); }
    [[nodiscard]] kb::particles::ParticleRuntimeResult SetSeed(kb::scene::Scene&, std::uint64_t id, std::uint64_t) noexcept override { return Control(id); }
    [[nodiscard]] kb::particles::ParticleRuntimeResult SetParameterScalar(kb::scene::Scene&, std::uint64_t id, std::string_view, float) noexcept override { return Control(id); }
    [[nodiscard]] kb::particles::ParticleRuntimeResult ClearParameter(kb::scene::Scene&, std::uint64_t id, std::string_view) noexcept override { return Control(id); }
    [[nodiscard]] kb::particles::ParticleRuntimeResult Emit(kb::scene::Scene&, std::uint64_t id, std::uint32_t) override { return Control(id); }
    [[nodiscard]] kb::particles::ParticleRuntimeQueryResult Query(const kb::scene::Scene&, std::uint64_t id) const noexcept override {
        return id == 17U ? kb::particles::ParticleRuntimeQueryResult{ .status = kb::particles::ParticleRuntimeStatus::Success, .state = true, .assetId = 3U, .materialAssetId = 4U, .liveParticleCount = 5U }
                         : kb::particles::ParticleRuntimeQueryResult{ .status = kb::particles::ParticleRuntimeStatus::InvalidInstance };
    }
    [[nodiscard]] std::size_t CopyLiveInstanceIds(const kb::scene::Scene&, std::span<std::uint64_t> output) const noexcept override {
        if (!output.empty()) output[0] = 17U;
        return 1U;
    }
    [[nodiscard]] std::size_t CopyLiveParticleStates(
        const kb::scene::Scene&,
        std::uint64_t id,
        std::span<kb::particles::ParticleRuntimeState> output) const noexcept override {
        if (id != 17U) return 0U;
        constexpr std::array<kb::particles::ParticleRuntimeState, 2> states{
            kb::particles::ParticleRuntimeState{ .position = { 1.0F, 2.0F, 3.0F }, .lifetime = 4.0F },
            kb::particles::ParticleRuntimeState{ .position = { 5.0F, 6.0F, 7.0F }, .age = 1.0F, .lifetime = 2.0F },
        };
        const std::size_t copied = std::min(output.size(), states.size());
        std::copy_n(states.begin(), copied, output.begin());
        return states.size();
    }

    int calls = 0;

private:
    [[nodiscard]] kb::particles::ParticleRuntimeResult Control(std::uint64_t id) noexcept {
        ++calls;
        return { .status = id == 17U ? kb::particles::ParticleRuntimeStatus::Success : kb::particles::ParticleRuntimeStatus::InvalidInstance, .instanceId = id };
    }
};

void TestComponentAndPresenceContract() {
    static_assert(std::is_trivially_copyable_v<kb::scene::ParticleEffectComponent>);
    kb::scene::ParticleEffectComponent valid{ .effectAssetId = 7U, .deterministicSeed = 9U, .rateMultiplier = 2.0F, .maxParticlesOverride = 128U,
        .ownerDeathPolicy = kb::scene::ParticleOwnerDeathPolicy::Clear, .enabled = true, .autoPlay = false, .followTransform = false, .restartOnActivate = false };
    Require(kb::scene::IsParticleEffectComponentValid(valid), "valid particle component rejected");
    valid.effectAssetId = 0U;
    Require(!kb::scene::IsParticleEffectComponentPersistable(valid), "enabled component without asset accepted");
    valid.enabled = false;
    Require(kb::scene::IsParticleEffectComponentPersistable(valid), "disabled empty authoring component rejected");

    kb::scene::Scene scene;
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Particle" });
    kb::scene::ScenePrefabNodeComponents expected;
    auto matches = [&] { return kb::scene::ScenePrefabOptionalComponentMaskMatches(kb::scene::SceneAccess::State(scene), entity, expected).matches; };
    Require(matches(), "empty optional component expectation failed");

    const auto exercise = [&](auto setter, auto remover, auto& slot, const auto& value, std::string_view label) {
        setter(value);
        Require(!matches(), label);
        slot = value;
        Require(matches(), label);
        remover();
        Require(!matches(), label);
        slot.reset();
        Require(matches(), label);
    };

    kb::scene::ParticleEffectComponent particle{ .effectAssetId = 1U };
    exercise([&](const auto& v) { scene.Components().ParticleEffects().Set(entity, v); }, [&] { scene.Components().ParticleEffects().Remove(entity); }, expected.particleEffect, particle, "particle presence mismatch was not detected");
    kb::scene::SkeletonBindingComponent skeleton{};
    exercise([&](const auto& v) { static_cast<void>(scene.Components().SkeletonBindings().Set(entity, v)); }, [&] { scene.Components().SkeletonBindings().Remove(entity); }, expected.skeletonBinding, skeleton, "skeleton presence mismatch was not detected");
    kb::scene::MotionSkeletonRuleComponent motion{};
    exercise([&](const auto& v) { static_cast<void>(scene.Components().MotionSkeletonRules().Set(entity, v)); }, [&] { scene.Components().MotionSkeletonRules().Remove(entity); }, expected.motionSkeletonRule, motion, "motion rule presence mismatch was not detected");
    kb::scene::DrawD3DeformedGeometryComponent deformed{};
    exercise([&](const auto& v) { static_cast<void>(scene.Components().DeformedGeometries().Set(entity, v)); }, [&] { scene.Components().DeformedGeometries().Remove(entity); }, expected.deformedGeometry, deformed, "deformed geometry presence mismatch was not detected");

    const kb::ecs::ComponentReflection* reflection = kb::scene::SceneAccess::State(scene).world.Reflection(kb::scene::ParticleEffectComponent::StableId);
    Require(reflection != nullptr && reflection->Fields().size() == 9U, "particle component reflection is missing or incomplete");
}

void TestPlaybackOwnershipAndBounds() {
    kb::scene::Scene scene;
    Backend first;
    Backend second;
    Require(kb::particles::ParticlePlayback::Create(scene, 1U, {}).status == kb::particles::ParticleRuntimeStatus::BackendUnavailable, "absent backend did not fail explicitly");
    for (int cycle = 0; cycle < 100; ++cycle) {
        Require(kb::particles::ParticlePlayback::RegisterBackend(scene, first).Succeeded(), "backend registration failed");
        Require(kb::particles::ParticlePlayback::RegisterBackend(scene, second).status == kb::particles::ParticleRuntimeStatus::BackendAlreadyRegistered, "conflicting backend was accepted");
        Require(kb::particles::ParticlePlayback::UnregisterBackend(scene, second).status == kb::particles::ParticleRuntimeStatus::InvalidRequest, "wrong backend unregister succeeded");
        Require(kb::particles::ParticlePlayback::HasBackend(scene), "wrong backend unregister cleared owner");
        Require(kb::particles::ParticlePlayback::Create(scene, 1U, {}).instanceId == 17U, "typed create was not forwarded");
        const std::span<const kb::scene::ParticleState> states = scene.Particles().Particles(17U);
        Require(states.size() == 2U && states[0].position.x == 1.0F && states[1].position.z == 7.0F,
            "particle states did not reach the scene facade");
        Require(kb::particles::ParticlePlayback::UnregisterBackend(scene, first).Succeeded(), "owner backend unregister failed");
        Require(!kb::particles::ParticlePlayback::HasBackend(scene), "backend survived unregister");
        Require(kb::particles::ParticlePlayback::DrainEvents(scene).empty(), "event queue survived unregister");
    }

    Require(kb::particles::ParticlePlayback::RegisterBackend(scene, first).Succeeded(), "queue test registration failed");
    for (std::uint32_t index = 0U; index < kb::scene::kParticleEffectMaxEventsPerStep; ++index) {
        Require(kb::particles::ParticlePlayback::QueueEvent(scene, {}).Succeeded(), "bounded event queue rejected a valid boundary event");
    }
    Require(kb::particles::ParticlePlayback::QueueEvent(scene, {}).status == kb::particles::ParticleRuntimeStatus::EventQueueFull, "bounded event queue accepted boundary plus one");
    Require(kb::particles::ParticlePlayback::UnregisterBackend(scene, first).Succeeded(), "queue test unregister failed");
}

void TestSceneAndPrefabRoundTrips() {
    const kb::scene::ParticleEffectComponent authored{
        .effectAssetId = 41U,
        .deterministicSeed = 0x123456789ABCDEF0ULL,
        .rateMultiplier = 1.75F,
        .maxParticlesOverride = 1024U,
        .ownerDeathPolicy = kb::scene::ParticleOwnerDeathPolicy::Clear,
        .enabled = true,
        .autoPlay = false,
        .followTransform = false,
        .restartOnActivate = false,
    };
    kb::scene::Scene source;
    const kb::scene::SceneObject root = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Particle Root" });
    source.Components().ParticleEffects().Set(root.Entity(), authored);

    const std::filesystem::path scenePath = std::filesystem::temp_directory_path() / "21kb_particle_stage2_scene.21kbscene";
    std::error_code error;
    std::filesystem::remove(scenePath, error);
    std::filesystem::remove(scenePath.string() + ".meta", error);
    Require(kb::scene::SceneDocumentService::Save(source, scenePath, "Particle Stage 2"), "v33 scene save failed");
    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(scenePath);
    Require(loaded.succeeded && loaded.document.fileVersion == 33U, "v33 scene load failed");
    Require(loaded.document.worldPrefab.NodeCount() == 1U, "v33 scene node count changed");
    const auto& loadedComponent = loaded.document.worldPrefab.Nodes()[0].components.particleEffect;
    Require(loadedComponent.has_value() && loadedComponent->effectAssetId == authored.effectAssetId &&
        loadedComponent->deterministicSeed == authored.deterministicSeed && loadedComponent->rateMultiplier == authored.rateMultiplier &&
        loadedComponent->maxParticlesOverride == authored.maxParticlesOverride && loadedComponent->ownerDeathPolicy == authored.ownerDeathPolicy &&
        loadedComponent->enabled == authored.enabled && loadedComponent->autoPlay == authored.autoPlay &&
        loadedComponent->followTransform == authored.followTransform && loadedComponent->restartOnActivate == authored.restartOnActivate,
        "v33 scene component fields did not roundtrip");

    const kb::scene::ScenePrefab captured = source.Prefabs().Capture(root);
    kb::scene::Scene target;
    const kb::scene::ScenePrefabHandle base = target.Prefabs().Register("Particle Base", captured);
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(base);
    Require(!instance.Empty(), "particle prefab did not instantiate");
    const kb::scene::SceneEntity entity = instance.RootObject().Entity();
    kb::scene::ParticleEffectComponent changed = authored;
    changed.rateMultiplier = 2.5F;
    target.Components().ParticleEffects().Set(entity, changed);
    const kb::scene::ScenePrefabOverrideReport changedReport = target.Prefabs().Overrides(instance.Handle());
    Require(std::ranges::any_of(changedReport.properties, [](const auto& property) { return property.propertyPath == "particleEffect.rateMultiplier"; }),
        "particle prefab change was not reported");
    Require(target.Prefabs().RevertOverride(instance.Handle(), 0U, "particleEffect.rateMultiplier"), "particle prefab property revert failed");
    Require(target.Components().ParticleEffects().TryGet(entity)->rateMultiplier == authored.rateMultiplier, "particle prefab property revert restored wrong value");

    target.Components().ParticleEffects().Set(entity, changed);
    Require(target.Prefabs().ApplyOverride(instance.Handle(), 0U, "particleEffect.rateMultiplier"), "particle prefab property apply failed");
    const kb::scene::ScenePrefabInstance applied = target.Prefabs().Instantiate(base);
    Require(target.Components().ParticleEffects().TryGet(applied.RootObject().Entity())->rateMultiplier == changed.rateMultiplier,
        "particle prefab property apply did not update template");

    target.Components().ParticleEffects().Remove(entity);
    const kb::scene::ScenePrefabOverrideReport removedReport = target.Prefabs().Overrides(instance.Handle());
    Require(std::ranges::any_of(removedReport.properties, [](const auto& property) { return property.propertyPath == "particleEffect"; }),
        "particle prefab missing presence was not reported");
    Require(target.Prefabs().RevertOverride(instance.Handle(), 0U, "particleEffect"), "particle prefab presence revert failed");
    Require(target.Components().ParticleEffects().Has(entity), "particle prefab presence revert did not restore component");

    kb::scene::ScenePrefabPropertyOverride variantOverride{
        .nodeIndex = 0U,
        .nodeId = captured.Nodes()[0].stableId,
        .propertyPath = "particleEffect.rateMultiplier",
        .value = "3.25",
        .flag = kb::scene::ScenePrefabOverrideFlag::ParticleEffect,
    };
    const kb::scene::ScenePrefabHandle variant = target.Prefabs().RegisterVariant("Particle Variant", base, { variantOverride });
    const kb::scene::ScenePrefabInstance variantInstance = target.Prefabs().Instantiate(variant);
    Require(!variantInstance.Empty() && target.Components().ParticleEffects().TryGet(variantInstance.RootObject().Entity())->rateMultiplier == 3.25F,
        "particle prefab variant override failed");
    const std::vector<kb::scene::ScenePrefabInstance> bulk = target.Prefabs().InstantiateMany(variant, 4U);
    Require(bulk.size() == 4U && std::ranges::all_of(bulk, [&](const auto& item) {
        const auto* component = target.Components().ParticleEffects().TryGet(item.RootObject().Entity());
        return component != nullptr && component->rateMultiplier == 3.25F;
    }), "particle prefab bulk instantiate lost variant component state");

    std::filesystem::remove(scenePath, error);
    std::filesystem::remove(scenePath.string() + ".meta", error);
}

void TestModuleMetadataAndLifecycle() {
    kb::particle_plugin::ParticleModule module;
    const kb::modules::EngineModuleMetadata metadata = module.Metadata();
    Require(metadata.name == "Rendering.21kbParticle", "plugin identifier mismatch");
    Require(metadata.loadingPhase == kb::modules::EngineModuleLoadingPhase::PreDefault, "plugin loading phase mismatch");
    kb::scene::Scene scene;
    const std::size_t baseline = scene.Runtime().SceneSystemCount();
    for (int cycle = 0; cycle < 100; ++cycle) {
        module.OnSceneAttach(scene);
        Require(scene.Runtime().SceneSystemCount() == baseline + 1U, "provider scene system was not attached exactly once");
        module.OnSceneAttach(scene);
        Require(scene.Runtime().SceneSystemCount() == baseline + 1U, "duplicate provider attach was not idempotent");
        module.OnSceneDetach(scene);
        Require(scene.Runtime().SceneSystemCount() == baseline, "provider scene system survived detach");
        Require(!kb::particles::ParticlePlayback::HasBackend(scene), "stage 2 provider registered a simulation backend");
    }
}

void TestDynamicModuleHostLifecycle() {
    const std::filesystem::path pluginPath = KB_21KB_PARTICLE_PLUGIN_PATH;
    if (pluginPath.empty()) return;

    const auto run = [&](bool withScript) {
        kb::project::ProjectDescriptor descriptor;
        descriptor.disableEnginePluginsByDefault = true;
        descriptor.plugins.push_back({ .name = "Rendering.21kbParticle", .binaryPath = pluginPath.string(), .enabled = true });
        std::vector<std::unique_ptr<kb::modules::IEngineModule>> modules;
        if (withScript) {
            descriptor.plugins.push_back({ .name = "Script", .enabled = true });
            modules.push_back(std::make_unique<kb::script::ScriptModule>());
        }
        kb::scene::Scene scene{ std::move(descriptor), std::move(modules) };
        Require(scene.IsModuleActive("Rendering.21kbParticle"), "produced provider module did not load through the module host");
        Require(scene.ModuleDiagnostics().empty(), "produced provider module emitted load diagnostics");
        const std::size_t stableSystemCount = scene.Runtime().SceneSystemCount();
        Require(stableSystemCount >= 1U, "produced provider module did not attach its scene system");
        for (int cycle = 0; cycle < 100; ++cycle) {
            scene.ReloadModules();
            Require(scene.IsModuleActive("Rendering.21kbParticle"), "provider module did not reactivate after reload");
            Require(scene.Runtime().SceneSystemCount() == stableSystemCount, "provider reload leaked or lost a scene system");
            Require(!kb::particles::ParticlePlayback::HasBackend(scene), "stage 2 dynamic provider registered a simulation backend");
            Require(kb::particles::ParticlePlayback::DrainEvents(scene).empty(), "provider reload left particle events queued");
        }
    };
    run(false);
    run(true);
}

[[nodiscard]] std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void TestProjectPolicy() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_particle_stage2_project_policy";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Assets", error);
    Require(!error, "project policy fixture could not be created");

    kb::project::ProjectDescriptor descriptor;
    Require(kb::project::ParticleProjectPolicy::Inspect(root, descriptor).requirement == kb::project::ParticleProjectRequirement::NotRequired,
        "project without effects unexpectedly required the provider");
    {
        std::ofstream effect{ root / "Assets" / "Effect.kbvfx", std::ios::binary };
        effect << "21kb ParticleEffect 2\n";
    }
    Require(kb::project::ParticleProjectPolicy::Inspect(root, descriptor).requirement == kb::project::ParticleProjectRequirement::Missing,
        "project with effects did not report a missing provider");
    descriptor.plugins.push_back({ .name = "Rendering.21kbParticle", .binaryPath = "kb_21kb_particle_plugin.dll", .enabled = false });
    Require(kb::project::ParticleProjectPolicy::Inspect(root, descriptor).requirement == kb::project::ParticleProjectRequirement::Disabled,
        "project with a disabled provider did not report disabled");
    Require(kb::project::ParticleProjectPolicy::Enable(descriptor, "kb_21kb_particle_plugin.dll"), "explicit provider Add changed nothing");
    Require(kb::project::ParticleProjectPolicy::Inspect(root, descriptor).requirement == kb::project::ParticleProjectRequirement::Enabled,
        "explicit provider Add did not make the project runnable");

    const std::filesystem::path projectFile = root / "Project.21kbproject";
    Require(kb::project::ProjectManager::SaveProject(projectFile, descriptor), "project policy descriptor could not be saved");
    const std::string before = ReadBytes(projectFile);
    const auto timestamp = std::filesystem::last_write_time(projectFile, error);
    static_cast<void>(kb::project::ParticleProjectPolicy::Inspect(root, descriptor));
    Require(ReadBytes(projectFile) == before && std::filesystem::last_write_time(projectFile, error) == timestamp,
        "project policy inspection silently changed the project descriptor");

    kb::project::ProjectDescriptor escape = descriptor;
    escape.contentRoot = "../";
    Require(kb::project::ParticleProjectPolicy::Inspect(root, escape).requirement == kb::project::ParticleProjectRequirement::InvalidContentRoot,
        "project policy accepted a content-root escape");
    std::filesystem::remove_all(root, error);
}

} // namespace

int main() {
    try {
        TestComponentAndPresenceContract();
        TestPlaybackOwnershipAndBounds();
        TestSceneAndPrefabRoundTrips();
        TestModuleMetadataAndLifecycle();
        TestDynamicModuleHostLifecycle();
        TestProjectPolicy();
        std::cout << "Particle stage 2 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
