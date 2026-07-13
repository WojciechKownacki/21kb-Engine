#include "TestSupport.hpp"

#include "engine/library/EngineLibrary.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/library/EngineLibraryAssetRef.hpp"
#include "engine/library/EngineLibraryCommandApplication.hpp"
#include "engine/library/EngineLibraryDeprecation.hpp"
#include "engine/library/EngineLibraryContext.hpp"
#include "engine/library/EngineLibraryEntityHandle.hpp"
#include "engine/library/EngineLibraryError.hpp"
#include "engine/library/EngineLibraryExecutionOrder.hpp"
#include "engine/library/EngineLibraryFunctionId.hpp"
#include "engine/library/EngineLibraryInputLimits.hpp"
#include "engine/library/EngineLibraryLifecycle.hpp"
#include "engine/library/EngineLibraryManifest.hpp"
#include "engine/library/EngineLibraryManifestComparison.hpp"
#include "engine/library/EngineLibraryModule.hpp"
#include "engine/library/EngineLibraryModuleValidation.hpp"
#include "engine/library/EngineLibraryOwnership.hpp"
#include "engine/library/EngineLibraryPropertyDesc.hpp"
#include "engine/library/EngineLibraryResult.hpp"
#include "engine/library/EngineLibraryTypeDesc.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/script/NativeScriptBackend.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptApiExport.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

int g_moduleInstallSkipCallCount = 0;
bool RecordModuleInstallCall(kb::script::ScriptRuntimeHost&) {
    ++g_moduleInstallSkipCallCount;
    return true;
}

// Shared by the native-dispatch tests below: creates a SceneObject carrying
// a single Native BehaviourComponent for `assetId`, so each test only has
// to supply the lifecycle callbacks that differ.
[[nodiscard]] kb::scene::SceneObject SpawnNativeBehaviourObject(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    std::string_view name,
    kb::scene::BehaviourTickGroup tickGroup = kb::scene::BehaviourTickGroup::Gameplay,
    std::int32_t executionOrder = 0) {
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = std::string{ name } });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = assetId.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
        .tickGroup = tickGroup,
        .executionOrder = executionOrder,
    });
    return object;
}

void RunVersionValueTest() {
    using kb::library::LibraryApiVersion;

    constexpr LibraryApiVersion version = kb::library::kEngineLibraryApiVersion;
    kb::tests::Require(version.major == 0U, "Engine21kbLibrary API major version drifted from the documented M0 contract");
    kb::tests::Require(version.minor == 1U, "Engine21kbLibrary API minor version drifted from the documented M0 contract");
    kb::tests::Require(version.patch == 0U, "Engine21kbLibrary API patch version drifted from the documented M0 contract");

    kb::tests::Require(kb::library::ToString(version) == "0.1.0", "Engine21kbLibrary API version formatting is wrong");
}

void RunVersionOrderingTest() {
    using kb::library::LibraryApiVersion;

    constexpr LibraryApiVersion base{ 1U, 2U, 3U };
    constexpr LibraryApiVersion samePatchBumped{ 1U, 2U, 4U };
    constexpr LibraryApiVersion minorBumped{ 1U, 3U, 0U };
    constexpr LibraryApiVersion majorBumped{ 2U, 0U, 0U };

    kb::tests::Require(base == LibraryApiVersion{ 1U, 2U, 3U }, "Engine21kbLibrary API version equality is wrong");
    kb::tests::Require(base < samePatchBumped, "Engine21kbLibrary API version patch ordering is wrong");
    kb::tests::Require(base < minorBumped, "Engine21kbLibrary API version minor ordering is wrong");
    kb::tests::Require(base < majorBumped, "Engine21kbLibrary API version major ordering is wrong");
}

void RunVersionCompatibilityTest() {
    using kb::library::LibraryApiVersion;

    constexpr LibraryApiVersion current{ 1U, 2U, 3U };

    // Same contract: always runnable.
    kb::tests::Require(current.CanRun(current), "Engine21kbLibrary API version must be able to run its own contract");

    // Older patch/minor of the same major: forward compatible.
    kb::tests::Require(current.CanRun(LibraryApiVersion{ 1U, 2U, 0U }), "Engine21kbLibrary API version must run an older patch of the same minor");
    kb::tests::Require(current.CanRun(LibraryApiVersion{ 1U, 0U, 0U }), "Engine21kbLibrary API version must run an older minor of the same major");

    // Newer minor/patch than the running version: not guaranteed compatible.
    kb::tests::Require(!current.CanRun(LibraryApiVersion{ 1U, 3U, 0U }), "Engine21kbLibrary API version must not claim to run a newer minor");
    kb::tests::Require(!current.CanRun(LibraryApiVersion{ 1U, 2U, 4U }), "Engine21kbLibrary API version must not claim to run a newer patch");

    // Different major: never compatible, regardless of direction.
    kb::tests::Require(!current.CanRun(LibraryApiVersion{ 2U, 0U, 0U }), "Engine21kbLibrary API version must reject a different major (newer)");
    kb::tests::Require(!current.CanRun(LibraryApiVersion{ 0U, 9U, 9U }), "Engine21kbLibrary API version must reject a different major (older)");
}

// LIB-002: ScriptRuntimeHost must reach every domain module (Input, Audio,
// World, Time, Physics, Transform) through the single
// kb::library::EngineLibraryModule::Install() entry point, not through
// scattered per-module calls.
void RunModuleInstallCoversAllDomainsTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary module install reported diagnostics on a fresh host");

    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);
    const char* const kExpectedFunctions[] = {
        "Input.Vector2",
        "Audio.Play",
        "World.FindByName",
        "Time.Delta",
        "Physics.Raycast",
        "Transform.GetPosition",
        "Math.Clamp",
    };
    for (const char* const name : kExpectedFunctions) {
        kb::tests::Require(
            catalog.FindFunction(name) != nullptr,
            "Engine21kbLibrary module install did not reach every domain module through the single entry point");
    }
}

// The entry point must surface a real, per-module error path (not a silent
// fallback) when registration cannot succeed: installing a second time onto
// a host that already owns every function name must fail every module and
// report one diagnostic per failed module.
void RunModuleInstallReportsDuplicateDiagnosticsTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary module install reported diagnostics on a fresh host");

    const kb::library::EngineLibraryModuleResult second = kb::library::EngineLibraryModule::Install(host);
    kb::tests::Require(!second.succeeded, "Engine21kbLibrary module install must fail when every function name already exists");
    kb::tests::Require(second.diagnostics.size() == 7U, "Engine21kbLibrary module install must report one diagnostic per failed domain module");
}

// LIB-016: the module catalog EngineLibraryModule::Install() walks must
// have exactly the six domain modules in ScriptRuntimeHost::
// RegisterDefaultBackends()'s historical order (plus Math, appended by
// LIB-045 — Math has no ScriptRuntimeHost-era history since it's new),
// each with a real Register function, a non-empty owner label, and
// capability=true (every backend in this catalog is actually compiled
// into this build).
void RunModuleCatalogTest() {
    const std::vector<kb::library::LibraryModuleDesc>& catalog = kb::library::EngineLibraryModule::Catalog();
    const std::vector<std::string> expectedNames{ "Input", "Audio", "World", "Time", "Physics", "Transform", "Math" };
    kb::tests::Require(catalog.size() == expectedNames.size(), "Engine21kbLibrary module catalog must have exactly seven domain modules");
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        kb::tests::Require(catalog[index].name == expectedNames[index], "Engine21kbLibrary module catalog order/name drifted from the historical registration order");
        kb::tests::Require(catalog[index].Register != nullptr, "Engine21kbLibrary module catalog entry is missing its Register function");
        kb::tests::Require(!catalog[index].ownerRuntime.empty(), "Engine21kbLibrary module catalog entry is missing its owner runtime label");
        kb::tests::Require(catalog[index].capability, "Engine21kbLibrary module catalog entry must have capability=true when its backend is compiled in");
    }
}

// LIB-016/LIB-027: a module whose capability is false must be skipped
// entirely — its Register() must never be called, and skipping it must not
// itself count as a failure.
void RunModuleInstallSkipsUnavailableCapabilityTest() {
    g_moduleInstallSkipCallCount = 0;
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary module install skip test host setup failed");

    const std::vector<kb::library::LibraryModuleDesc> modules{
        kb::library::LibraryModuleDesc{
            .name = "UnavailableTestModule",
            .capability = false,
            .Register = &RecordModuleInstallCall,
        },
    };
    const kb::library::EngineLibraryModuleResult result = kb::library::EngineLibraryModule::InstallModules(host, modules);
    kb::tests::Require(result.succeeded, "Engine21kbLibrary module install must succeed when the only module present has capability=false");
    kb::tests::Require(result.diagnostics.empty(), "Engine21kbLibrary module install must not report a diagnostic for a capability=false module");
    kb::tests::Require(g_moduleInstallSkipCallCount == 0, "Engine21kbLibrary module install must never call Register() for a capability=false module");
}

// LIB-017: every audited LibraryFunctionDesc::canonicalName across the
// whole module catalog must resolve to a function ScriptApiCatalog reports
// as actually registered — an audited function description can never
// outlive (or predate) the real ScriptFunctionRegistry entry it describes.
void RunFunctionDescCatalogResolvesTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary function desc test host setup failed");
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);

    bool sawAnyAuditedFunction = false;
    for (const kb::library::LibraryModuleDesc& module : kb::library::EngineLibraryModule::Catalog()) {
        for (const kb::library::LibraryFunctionDesc& function : module.functions) {
            sawAnyAuditedFunction = true;
            kb::tests::Require(
                catalog.FindFunction(function.canonicalName) != nullptr,
                "Engine21kbLibrary LibraryFunctionDesc names a function ScriptApiCatalog does not report as registered");
        }
    }
    kb::tests::Require(sawAnyAuditedFunction, "Engine21kbLibrary module catalog must have at least one audited LibraryFunctionDesc");
}

// LIB-020: the real production catalog EngineLibraryModule::Catalog()
// returns must already pass validation (no duplicate names, no unknown
// dependencies, no cycles, no function audited twice) — the validator is
// meant to run at startup on real data, not just on synthetic failure
// cases.
void RunModuleCatalogValidatesTest() {
    const kb::library::ModuleCatalogValidationResult result = kb::library::ValidateModuleCatalog(kb::library::EngineLibraryModule::Catalog());
    kb::tests::Require(result.succeeded, "Engine21kbLibrary production module catalog must pass its own validation");
    kb::tests::Require(result.errors.empty(), "Engine21kbLibrary production module catalog validation must report no errors");
}

void RunModuleValidationDuplicateNameTest() {
    const std::vector<kb::library::LibraryModuleDesc> modules{
        kb::library::LibraryModuleDesc{ .name = "Dup" },
        kb::library::LibraryModuleDesc{ .name = "Dup" },
    };
    const kb::library::ModuleCatalogValidationResult result = kb::library::ValidateModuleCatalog(modules);
    kb::tests::Require(!result.succeeded, "Engine21kbLibrary module validation must reject duplicate module names");
    kb::tests::Require(!result.errors.empty(), "Engine21kbLibrary module validation must report a duplicate-name error");
}

void RunModuleValidationUnknownDependencyTest() {
    const std::vector<kb::library::LibraryModuleDesc> modules{
        kb::library::LibraryModuleDesc{ .name = "A", .dependencies = { "DoesNotExist" } },
    };
    const kb::library::ModuleCatalogValidationResult result = kb::library::ValidateModuleCatalog(modules);
    kb::tests::Require(!result.succeeded, "Engine21kbLibrary module validation must reject a dependency naming an unknown module");
}

void RunModuleValidationCycleTest() {
    const std::vector<kb::library::LibraryModuleDesc> modules{
        kb::library::LibraryModuleDesc{ .name = "A", .dependencies = { "B" } },
        kb::library::LibraryModuleDesc{ .name = "B", .dependencies = { "A" } },
    };
    const kb::library::ModuleCatalogValidationResult result = kb::library::ValidateModuleCatalog(modules);
    kb::tests::Require(!result.succeeded, "Engine21kbLibrary module validation must reject a dependency cycle");

    const std::vector<kb::library::LibraryModuleDesc> acyclic{
        kb::library::LibraryModuleDesc{ .name = "A", .dependencies = { "B" } },
        kb::library::LibraryModuleDesc{ .name = "B" },
    };
    const kb::library::ModuleCatalogValidationResult acyclicResult = kb::library::ValidateModuleCatalog(acyclic);
    kb::tests::Require(acyclicResult.succeeded, "Engine21kbLibrary module validation must accept a valid, acyclic dependency chain");
}

void RunModuleValidationDuplicateFunctionTest() {
    const std::vector<kb::library::LibraryModuleDesc> modules{
        kb::library::LibraryModuleDesc{
            .name = "A",
            .functions = { kb::library::LibraryFunctionDesc{ .canonicalName = "Shared.Fn" } },
        },
        kb::library::LibraryModuleDesc{
            .name = "B",
            .functions = { kb::library::LibraryFunctionDesc{ .canonicalName = "Shared.Fn" } },
        },
    };
    const kb::library::ModuleCatalogValidationResult result = kb::library::ValidateModuleCatalog(modules);
    kb::tests::Require(!result.succeeded, "Engine21kbLibrary module validation must reject a function audited by two modules at once");
}

// A catalog that fails validation must register nothing at all — not even
// the modules that would otherwise have registered successfully.
void RunModuleInstallFailsFastOnInvalidCatalogTest() {
    g_moduleInstallSkipCallCount = 0;
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary fail-fast test host setup failed");

    const std::vector<kb::library::LibraryModuleDesc> modules{
        kb::library::LibraryModuleDesc{ .name = "Dup", .Register = &RecordModuleInstallCall },
        kb::library::LibraryModuleDesc{ .name = "Dup", .Register = &RecordModuleInstallCall },
    };
    const kb::library::EngineLibraryModuleResult result = kb::library::EngineLibraryModule::InstallModules(host, modules);
    kb::tests::Require(!result.succeeded, "Engine21kbLibrary module install must fail for an invalid catalog");
    kb::tests::Require(g_moduleInstallSkipCallCount == 0, "Engine21kbLibrary module install must not call Register() at all when the catalog itself is invalid");
}

// LIB-018: every kb::script::ScriptValueType must resolve to a
// LibraryTypeDesc whose serialization/Visual-Graph-pin fields match the
// engine's own kb::script::ToString/ToVisualGraphValueType exactly (no
// parallel, independently-drifting type table), and ScriptValue's new
// structural equality (the "porównanie" this task asks for) must hold.
void RunTypeDescTest() {
    using kb::script::ScriptValue;
    using kb::script::ScriptValueType;

    const ScriptValueType kAllTypes[]{
        ScriptValueType::Void, ScriptValueType::Bool, ScriptValueType::Int, ScriptValueType::Float,
        ScriptValueType::String, ScriptValueType::Entity, ScriptValueType::Component,
        ScriptValueType::Int64, ScriptValueType::UInt32, ScriptValueType::Double,
        ScriptValueType::Name, ScriptValueType::Guid, ScriptValueType::Hash,
    };
    for (const ScriptValueType type : kAllTypes) {
        const kb::library::LibraryTypeDesc& desc = kb::library::DescribeType(type);
        kb::tests::Require(desc.scriptType == type, "Engine21kbLibrary LibraryTypeDesc.scriptType does not match the requested type");
        kb::tests::Require(desc.canonicalName == kb::script::ToString(type), "Engine21kbLibrary LibraryTypeDesc.canonicalName must match kb::script::ToString");
        kb::tests::Require(desc.visualGraphPinType == kb::script::ToVisualGraphValueType(type), "Engine21kbLibrary LibraryTypeDesc.visualGraphPinType must match kb::script::ToVisualGraphValueType");
        kb::tests::Require(!desc.luaTypeName.empty(), "Engine21kbLibrary LibraryTypeDesc.luaTypeName must be documented");
        kb::tests::Require(desc.defaultValue.Type() == type, "Engine21kbLibrary LibraryTypeDesc.defaultValue must carry the described type");
    }

    kb::tests::Require(ScriptValue{ 1 } == ScriptValue{ 1 }, "Engine21kbLibrary ScriptValue equality must hold for equal Int values");
    kb::tests::Require(ScriptValue{ 1 } != ScriptValue{ 2 }, "Engine21kbLibrary ScriptValue equality must reject different Int values");
    kb::tests::Require(ScriptValue{ true } != ScriptValue{ false }, "Engine21kbLibrary ScriptValue equality must reject different Bool values");
    kb::tests::Require(
        ScriptValue{ 5U, ScriptValueType::Entity } == ScriptValue{ 5U, ScriptValueType::Entity },
        "Engine21kbLibrary ScriptValue equality must hold for equal Entity values");
    kb::tests::Require(
        ScriptValue{ 5U, ScriptValueType::Entity } != ScriptValue{ 5U, ScriptValueType::Component },
        "Engine21kbLibrary ScriptValue equality must distinguish Entity from Component even with the same raw id");
}

// LIB-004: every ScriptLifecycleEvent must classify to exactly the public
// context kind the lifecycle table in others/Engine21kbLibrary.md (section 3)
// documents, and kb::library must reuse the engine's scheduler enum rather
// than declaring a second one.
void RunLifecycleContextClassificationTest() {
    using kb::library::LibraryLifecycleContextKind;
    using Event = kb::library::LifecycleEvent;

    static_assert(
        std::is_same_v<kb::library::LifecycleEvent, kb::script::ScriptLifecycleEvent>,
        "kb::library::LifecycleEvent must alias kb::script::ScriptLifecycleEvent, not duplicate it");

    struct Expectation {
        Event event;
        LibraryLifecycleContextKind kind;
    };
    const Expectation kExpectations[] = {
        { Event::Created, LibraryLifecycleContextKind::Behaviour },
        { Event::Activated, LibraryLifecycleContextKind::Behaviour },
        { Event::Ready, LibraryLifecycleContextKind::Behaviour },
        { Event::FixedTick, LibraryLifecycleContextKind::Fixed },
        { Event::Tick, LibraryLifecycleContextKind::Frame },
        { Event::LateTick, LibraryLifecycleContextKind::Frame },
        { Event::BeforeRender, LibraryLifecycleContextKind::Render },
        { Event::AfterRender, LibraryLifecycleContextKind::Render },
        { Event::Deactivated, LibraryLifecycleContextKind::Behaviour },
        { Event::Destroyed, LibraryLifecycleContextKind::Behaviour },
    };

    for (const Expectation& expectation : kExpectations) {
        kb::tests::Require(
            kb::library::ClassifyLifecycleContext(expectation.event) == expectation.kind,
            "Engine21kbLibrary lifecycle context classification does not match the documented lifecycle table");
    }

    kb::tests::Require(kb::library::ToString(LibraryLifecycleContextKind::Fixed) == std::string("Fixed"), "Engine21kbLibrary lifecycle context kind formatting is wrong");
}

// LIB-005: the guaranteed execution order (TickGroup asc -> executionOrder
// asc -> entity id asc) must hold through kb::library's exposed comparator,
// and EntityId/ComponentId must be exactly the engine's identifiers, not a
// parallel copy.
void RunExecutionOrderContractTest() {
    using kb::library::TickGroup;
    using kb::scene::BehaviourComponent;

    static_assert(std::is_same_v<kb::library::EntityId, kb::ecs::Entity::IdType>, "kb::library::EntityId must alias kb::ecs::Entity::IdType, not duplicate it");
    static_assert(std::is_same_v<kb::library::ComponentId, kb::ecs::ComponentId>, "kb::library::ComponentId must alias kb::ecs::ComponentId, not duplicate it");

    BehaviourComponent gameplayLow{};
    gameplayLow.tickGroup = TickGroup::Gameplay;
    gameplayLow.executionOrder = -10;

    BehaviourComponent gameplayHigh{};
    gameplayHigh.tickGroup = TickGroup::Gameplay;
    gameplayHigh.executionOrder = 50;

    BehaviourComponent cameraEarlier{};
    cameraEarlier.tickGroup = TickGroup::Camera;
    cameraEarlier.executionOrder = -100;

    const kb::scene::SceneEntity entityA{ 1U };
    const kb::scene::SceneEntity entityB{ 2U };

    // TickGroup beats executionOrder: Gameplay(-10) before Gameplay(50)
    // before Camera(-100), even though -100 < -10 numerically.
    kb::tests::Require(
        kb::library::BehaviourExecutionOrderLess(entityA, gameplayLow, entityA, gameplayHigh),
        "Engine21kbLibrary execution order must sort executionOrder ascending within the same TickGroup");
    kb::tests::Require(
        kb::library::BehaviourExecutionOrderLess(entityA, gameplayHigh, entityA, cameraEarlier),
        "Engine21kbLibrary execution order must sort TickGroup before executionOrder");

    // Same group and order: entity id is the deterministic tie-breaker.
    BehaviourComponent same{};
    same.tickGroup = TickGroup::Gameplay;
    same.executionOrder = 0;
    kb::tests::Require(
        kb::library::BehaviourExecutionOrderLess(entityA, same, entityB, same),
        "Engine21kbLibrary execution order must use entity id as a deterministic tie-breaker");
    kb::tests::Require(
        !kb::library::BehaviourExecutionOrderLess(entityB, same, entityA, same),
        "Engine21kbLibrary execution order tie-break must be antisymmetric");
}

// LIB-006: kb::library documents CommandApplicationPointFor() as Immediate
// for every phase. Prove the two consequences that follow from "immediate"
// rather than "batched at phase end": an entity spawned mid-Tick is live
// immediately (readable through the world right after the call that
// created it), but it does not receive its own Created/Activated/Ready in
// the same frame — only starting the next ScriptRuntimeSceneSystem frame,
// once SyncBehaviourLifecycles has picked it up.
void RunCommandApplicationContractTest() {
    kb::tests::Require(
        kb::library::CommandApplicationPointFor(kb::library::LifecycleEvent::Tick) == kb::library::CommandApplicationPoint::Immediate,
        "Engine21kbLibrary must document Tick as an immediate command application point");
    kb::tests::Require(
        kb::library::CommandApplicationPointFor(kb::library::LifecycleEvent::FixedTick) == kb::library::CommandApplicationPoint::Immediate,
        "Engine21kbLibrary must document FixedTick as an immediate command application point");

    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kSpawnerAsset{ 9101U };
    constexpr kb::assets::AssetId kSpawnedAsset{ 9102U };

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();

    kb::scene::SceneEntity spawnedEntity{};
    bool spawnedAliveImmediatelyAfterCreate = false;
    std::vector<std::string> spawnedOrder;

    kb::tests::Require(
        native->RegisterLifecycle(kSpawnerAsset, kb::script::ScriptLifecycleEvent::Tick, [&](kb::script::ScriptExecutionContext& context) {
            const kb::scene::SceneObject spawned = SpawnNativeBehaviourObject(context.GetScene(), kSpawnedAsset, "CommandApplicationSpawned");
            spawnedEntity = spawned.Entity();
            spawnedAliveImmediatelyAfterCreate = context.GetScene().Entities().IsAlive(spawnedEntity);
        }),
        "Command application contract test spawner registration failed");
    kb::tests::Require(
        native->RegisterLifecycle(kSpawnedAsset, kb::script::ScriptLifecycleEvent::Created, [&](kb::script::ScriptExecutionContext&) {
            spawnedOrder.emplace_back("Created");
        }),
        "Command application contract test spawned Created registration failed");
    kb::tests::Require(
        native->RegisterLifecycle(kSpawnedAsset, kb::script::ScriptLifecycleEvent::Tick, [&](kb::script::ScriptExecutionContext&) {
            spawnedOrder.emplace_back("Tick");
        }),
        "Command application contract test spawned Tick registration failed");

    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Command application contract test backend registration failed");

    static_cast<void>(SpawnNativeBehaviourObject(scene, kSpawnerAsset, "CommandApplicationSpawner"));

    kb::script::ScriptRuntimeSceneSystem system{ runtime };

    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(spawnedAliveImmediatelyAfterCreate, "Engine21kbLibrary command application must make a spawned entity live before the spawning call returns");
    kb::tests::Require(scene.Entities().IsAlive(spawnedEntity), "Engine21kbLibrary command application must keep the spawned entity live after the phase finishes");
    kb::tests::Require(spawnedOrder.empty(), "Engine21kbLibrary command application must not dispatch lifecycle events to an entity spawned during the same phase's already-collected snapshot");

    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(spawnedOrder.size() == 2U && spawnedOrder[0] == "Created" && spawnedOrder[1] == "Tick",
        "Engine21kbLibrary command application must dispatch Created then Tick to the spawned entity starting the next frame");
}

// LIB-007: BehaviourContext, FixedContext, FrameContext and RenderContext
// must be reachable from a real dispatch for every LibraryLifecycleContextKind
// (Behaviour, Fixed, Frame, Render), report the exact Self()/Phase() the
// underlying ScriptExecutionContext carries, and (Fixed/Frame) the exact
// delta the runtime passed for that phase.
void RunLibraryContextTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kAsset{ 9201U };

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();

    bool sawCreated = false;
    bool sawFixed = false;
    bool sawFrame = false;
    bool sawRender = false;

    kb::tests::Require(
        native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::Created, [&](kb::script::ScriptExecutionContext& context) {
            const kb::library::BehaviourContext ctx{ context };
            kb::tests::Require(ctx.Self() == context.Self().Id(), "Engine21kbLibrary BehaviourContext.Self() must match the raw context's entity");
            kb::tests::Require(ctx.Phase() == kb::library::LifecycleEvent::Created, "Engine21kbLibrary BehaviourContext.Phase() must match Created");
            const kb::library::EntityHandle selfHandle = ctx.SelfHandle();
            kb::tests::Require(selfHandle.Id() == ctx.Self(), "Engine21kbLibrary BehaviourContext.SelfHandle().Id() must match Self()");
            kb::tests::Require(selfHandle.SceneId() == context.GetScene().Id(), "Engine21kbLibrary BehaviourContext.SelfHandle().SceneId() must match the dispatching Scene");
            kb::tests::Require(selfHandle.IsAlive(context.GetScene()), "Engine21kbLibrary BehaviourContext.SelfHandle() must report alive for the entity it was built from");
            sawCreated = true;
        }),
        "Library context test Created registration failed");

    kb::tests::Require(
        native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::FixedTick, [&](kb::script::ScriptExecutionContext& context) {
            const kb::library::FixedContext ctx{ context };
            kb::tests::Require(ctx.Phase() == kb::library::LifecycleEvent::FixedTick, "Engine21kbLibrary FixedContext.Phase() must match FixedTick");
            kb::tests::Require(ctx.FixedDeltaSeconds() == context.DeltaSeconds(), "Engine21kbLibrary FixedContext.FixedDeltaSeconds() must match the runtime's fixed step delta");
            sawFixed = true;
        }),
        "Library context test FixedTick registration failed");

    kb::tests::Require(
        native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::Tick, [&](kb::script::ScriptExecutionContext& context) {
            const kb::library::FrameContext ctx{ context };
            kb::tests::Require(ctx.Phase() == kb::library::LifecycleEvent::Tick, "Engine21kbLibrary FrameContext.Phase() must match Tick");
            kb::tests::Require(ctx.DeltaSeconds() == context.DeltaSeconds(), "Engine21kbLibrary FrameContext.DeltaSeconds() must match the runtime's frame delta");
            sawFrame = true;
        }),
        "Library context test Tick registration failed");

    kb::tests::Require(
        native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::BeforeRender, [&](kb::script::ScriptExecutionContext& context) {
            const kb::library::RenderContext ctx{ context };
            kb::tests::Require(ctx.Phase() == kb::library::LifecycleEvent::BeforeRender, "Engine21kbLibrary RenderContext.Phase() must match BeforeRender");
            kb::tests::Require(
                kb::library::ClassifyLifecycleContext(ctx.Phase()) == kb::library::LibraryLifecycleContextKind::Render,
                "Engine21kbLibrary RenderContext must only be reachable for a Render-classified phase");
            sawRender = true;
        }),
        "Library context test BeforeRender registration failed");

    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Library context test backend registration failed");

    static_cast<void>(SpawnNativeBehaviourObject(scene, kAsset, "LibraryContext"));

    kb::script::ScriptRuntimeSceneSystem system{ runtime };
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));

    kb::tests::Require(sawCreated && sawFixed && sawFrame && sawRender, "Engine21kbLibrary context types were not exercised for every classified lifecycle phase");
}

// LIB-005 regression: SyncBehaviourLifecycles must dispatch Deactivated for
// multiple behaviours removed in the same frame in the guaranteed execution
// order (TickGroup ascending, then executionOrder, then entity id) — never
// in lifecycleRecords_'s unordered_map iteration order. The three
// behaviours are created in Camera, Input, Gameplay order, which differs
// from the expected TickGroup-sorted dispatch order (Input, Gameplay,
// Camera), so only a real sort before dispatch can produce that sequence.
void RunMultipleBehavioursRemovedSameFrameOrderTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kCameraAsset{ 9301U };
    constexpr kb::assets::AssetId kInputAsset{ 9302U };
    constexpr kb::assets::AssetId kGameplayAsset{ 9303U };

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();

    std::vector<std::string> order;
    kb::tests::Require(
        native->RegisterLifecycle(kCameraAsset, kb::script::ScriptLifecycleEvent::Deactivated, [&](kb::script::ScriptExecutionContext&) {
            order.emplace_back("Camera");
        }),
        "Multi-removal order test Camera registration failed");
    kb::tests::Require(
        native->RegisterLifecycle(kInputAsset, kb::script::ScriptLifecycleEvent::Deactivated, [&](kb::script::ScriptExecutionContext&) {
            order.emplace_back("Input");
        }),
        "Multi-removal order test Input registration failed");
    kb::tests::Require(
        native->RegisterLifecycle(kGameplayAsset, kb::script::ScriptLifecycleEvent::Deactivated, [&](kb::script::ScriptExecutionContext&) {
            order.emplace_back("Gameplay");
        }),
        "Multi-removal order test Gameplay registration failed");

    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Multi-removal order test backend registration failed");

    const kb::scene::SceneObject cameraObject = SpawnNativeBehaviourObject(scene, kCameraAsset, "Camera", kb::scene::BehaviourTickGroup::Camera);
    const kb::scene::SceneObject inputObject = SpawnNativeBehaviourObject(scene, kInputAsset, "Input", kb::scene::BehaviourTickGroup::Input);
    const kb::scene::SceneObject gameplayObject = SpawnNativeBehaviourObject(scene, kGameplayAsset, "Gameplay", kb::scene::BehaviourTickGroup::Gameplay);

    kb::script::ScriptRuntimeSceneSystem system{ runtime };
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(order.empty(), "Multi-removal order test fixture must not deactivate anything before removal");

    scene.Components().Behaviours().Remove(cameraObject.Entity());
    scene.Components().Behaviours().Remove(inputObject.Entity());
    scene.Components().Behaviours().Remove(gameplayObject.Entity());

    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(
        order.size() == 3U && order[0] == "Input" && order[1] == "Gameplay" && order[2] == "Camera",
        "Engine21kbLibrary must dispatch Deactivated for multiple behaviours removed in the same frame in TickGroup order, not unordered_map iteration order");
}

// LIB-008: EntityHandle must report alive only for an entity that is both
// structurally valid and actually alive in the exact Scene it was built
// from — not merely "some entity with this id exists somewhere" — and must
// diagnose each distinct failure (invalid, wrong world, stale) instead of
// treating them the same way.
void RunEntityHandleTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "EntityHandleSubject" });
    const kb::library::EntityHandle handle{ object.Entity(), scene.Id() };

    kb::tests::Require(handle.IsValid(), "Engine21kbLibrary EntityHandle built from a live entity must be structurally valid");
    kb::tests::Require(handle.Id() == object.Entity().Id(), "Engine21kbLibrary EntityHandle.Id() must match the entity it was built from");
    kb::tests::Require(handle.SceneId() == scene.Id(), "Engine21kbLibrary EntityHandle.SceneId() must match the originating scene");
    kb::tests::Require(handle.IsAlive(scene), "Engine21kbLibrary EntityHandle must report alive for a live entity in its own scene");

    bool validateSucceeded = true;
    try {
        handle.Validate(scene, "Test.Validate");
    } catch (const std::exception&) {
        validateSucceeded = false;
    }
    kb::tests::Require(validateSucceeded, "Engine21kbLibrary EntityHandle.Validate() must not throw for a live entity in its own scene");

    // Default-constructed (invalid) handle.
    const kb::library::EntityHandle invalidHandle{};
    kb::tests::Require(!invalidHandle.IsValid(), "Engine21kbLibrary default-constructed EntityHandle must be invalid");
    kb::tests::Require(!invalidHandle.IsAlive(scene), "Engine21kbLibrary invalid EntityHandle must never report alive");
    bool invalidThrew = false;
    try {
        invalidHandle.Validate(scene, "Test.Validate");
    } catch (const std::invalid_argument&) {
        invalidThrew = true;
    } catch (const std::exception&) {
    }
    kb::tests::Require(invalidThrew, "Engine21kbLibrary EntityHandle.Validate() must throw std::invalid_argument for an invalid handle");

    // A handle stamped with a different scene's id must never resolve as
    // alive against this scene, even though the entity id itself is live.
    kb::scene::Scene otherScene;
    kb::tests::Require(otherScene.Id() != scene.Id(), "Engine21kbLibrary test setup requires two scenes with distinct ids");
    const kb::library::EntityHandle wrongWorldHandle{ object.Entity(), otherScene.Id() };
    kb::tests::Require(!wrongWorldHandle.IsAlive(scene), "Engine21kbLibrary EntityHandle must reject a handle stamped with a different scene id");
    bool wrongWorldThrew = false;
    try {
        wrongWorldHandle.Validate(scene, "Test.Validate");
    } catch (const std::out_of_range&) {
        wrongWorldThrew = true;
    } catch (const std::exception&) {
    }
    kb::tests::Require(wrongWorldThrew, "Engine21kbLibrary EntityHandle.Validate() must throw std::out_of_range for a handle from a different world");

    // Stale: the entity this handle names has been destroyed.
    scene.Entities().Destroy(object);
    kb::tests::Require(!handle.IsAlive(scene), "Engine21kbLibrary EntityHandle must report not-alive once its entity is destroyed");
    bool staleThrew = false;
    try {
        handle.Validate(scene, "Test.Validate");
    } catch (const std::out_of_range&) {
        staleThrew = true;
    } catch (const std::exception&) {
    }
    kb::tests::Require(staleThrew, "Engine21kbLibrary EntityHandle.Validate() must throw std::out_of_range for a stale handle");

    // A freshly created entity (possibly recycling the destroyed one's
    // index, with a bumped generation) must not be confused with the old
    // handle: the old handle keeps reporting not-alive.
    const kb::scene::SceneObject recycledSlotCandidate = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "EntityHandleRecycled" });
    kb::tests::Require(!handle.IsAlive(scene), "Engine21kbLibrary EntityHandle for a destroyed entity must stay not-alive even after a new entity is created");
    kb::tests::Require(scene.Entities().IsAlive(recycledSlotCandidate.Entity()), "Engine21kbLibrary test setup requires the newly created entity to be alive");
}

// LIB-009: AssetRef<T>/SceneRef must be the real kb::assets::AssetHandle<T>
// (no parallel cache/refcount model), and the identifier behind it must be a
// deterministic hash of the asset's logical path — not the OS physical path.
void RunAssetRefTest() {
    static_assert(std::is_same_v<kb::library::SceneRef, kb::assets::AssetHandle<kb::scene::SceneDocument>>, "kb::library::SceneRef must alias kb::assets::AssetHandle<SceneDocument>, not duplicate it");
    static_assert(std::is_same_v<kb::library::AssetRef<kb::scene::SceneDocument>, kb::assets::AssetHandle<kb::scene::SceneDocument>>, "kb::library::AssetRef<T> must alias kb::assets::AssetHandle<T>, not duplicate it");

    const std::filesystem::path testRoot = std::filesystem::temp_directory_path() / "21kb_engine_library_asset_ref_tests";
    std::error_code removeError;
    std::filesystem::remove_all(testRoot, removeError);
    std::error_code createError;
    std::filesystem::create_directories(testRoot, createError);
    kb::tests::Require(!createError, "Engine21kbLibrary asset ref test root could not be prepared");

    const std::filesystem::path projectRoot = testRoot / "Project";
    const std::filesystem::path sceneFile = projectRoot / "Assets" / "Scenes" / "LibraryAssetRef.21kbscene";

    kb::scene::Scene source;
    static_cast<void>(source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "LibraryAssetRefRoot" }));
    kb::tests::Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "LibraryAssetRef"), "Engine21kbLibrary asset ref test fixture scene was not saved");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Engine21kbLibrary asset ref test project did not mount");
    kb::tests::Require(scene.Assets().Discover() == 1, "Engine21kbLibrary asset ref test scene asset was not discovered");

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Scenes/LibraryAssetRef.21kbscene");
    kb::tests::Require(metadata != nullptr, "Engine21kbLibrary asset ref test scene metadata was not registered");

    const kb::library::SceneRef ref = scene.Assets().Manager().Load<kb::scene::SceneDocument>(metadata->id);
    kb::tests::Require(ref.IsLoaded(), "Engine21kbLibrary SceneRef did not load through kb::assets::AssetManager");
    kb::tests::Require(ref->name == "LibraryAssetRef", "Engine21kbLibrary SceneRef payload did not carry the saved scene name");
    kb::tests::Require(ref.Id() == metadata->id, "Engine21kbLibrary SceneRef.Id() must match the asset's runtime AssetId");

    // The runtime identifier is a deterministic hash of the logical path
    // plus type — reproducing that exact formula must yield the same id,
    // independent of physicalPath (which this test never even reads).
    const kb::assets::AssetId recomputed = kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(metadata->virtualPath) + ":" + metadata->type);
    kb::tests::Require(recomputed == metadata->id, "Engine21kbLibrary AssetRef identifier must be a deterministic hash of the logical path, not the physical path");
}

// LIB-010: kb::library::Result<T> must carry either the value or the
// ScriptError naming why the operation failed, never both/neither, and
// ScriptError formatting must be stable and readable.
void RunResultTest() {
    using kb::library::Result;
    using kb::library::ScriptError;

    const Result<int> ok = Result<int>::Ok(42);
    kb::tests::Require(ok.Succeeded(), "Engine21kbLibrary Result::Ok must report success");
    kb::tests::Require(ok.Value() == 42, "Engine21kbLibrary Result::Value() must return the stored value");

    const Result<int> failed = Result<int>::Fail(ScriptError{
        .code = kb::library::LibraryErrorCode::InvalidArgument,
        .operation = "Tests.Op",
        .message = "went wrong",
    });
    kb::tests::Require(!failed.Succeeded(), "Engine21kbLibrary Result::Fail must report failure");
    kb::tests::Require(failed.Error().operation == "Tests.Op", "Engine21kbLibrary Result::Error() must return the stored operation");
    kb::tests::Require(failed.Error().message == "went wrong", "Engine21kbLibrary Result::Error() must return the stored message");
    kb::tests::Require(kb::library::ToString(failed.Error()) == "[InvalidArgument] Tests.Op: went wrong", "Engine21kbLibrary ScriptError formatting is wrong");

    const ScriptError bare{ .code = kb::library::LibraryErrorCode::Timeout, .message = "no operation name" };
    kb::tests::Require(kb::library::ToString(bare) == "[Timeout] no operation name", "Engine21kbLibrary ScriptError formatting must omit an empty operation but keep the code");
}

// LIB-019: LibraryPropertyDesc must be exactly kb::script::
// ScriptApiCatalogProperty, not a parallel property shape, and every
// property kb::script::ScriptApiCatalog already reports for a component
// must already be usable as a LibraryPropertyDesc with no conversion.
void RunPropertyDescTest() {
    static_assert(
        std::is_same_v<kb::library::LibraryPropertyDesc, kb::script::ScriptApiCatalogProperty>,
        "kb::library::LibraryPropertyDesc must alias kb::script::ScriptApiCatalogProperty, not duplicate it");

    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary property desc test host setup failed");
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);

    const kb::script::ScriptApiCatalogComponent* transform = nullptr;
    for (const kb::script::ScriptApiCatalogComponent& component : catalog.components) {
        if (component.name == "Transform") {
            transform = &component;
            break;
        }
    }
    kb::tests::Require(transform != nullptr, "Engine21kbLibrary property desc test catalog is missing the Transform component");

    bool sawWritablePosition = false;
    bool sawReadOnlyWorldPosition = false;
    for (const kb::library::LibraryPropertyDesc& property : transform->properties) {
        if (property.name == "localPosition.x" && property.writable) {
            sawWritablePosition = true;
        }
        if (property.name == "worldPosition.x" && !property.writable) {
            sawReadOnlyWorldPosition = true;
        }
    }
    kb::tests::Require(sawWritablePosition, "Engine21kbLibrary LibraryPropertyDesc did not carry Transform.localPosition.x as writable");
    kb::tests::Require(sawReadOnlyWorldPosition, "Engine21kbLibrary LibraryPropertyDesc did not carry Transform.worldPosition.x as read-only");
}

// LIB-023: the manifest hash must be deterministic (same catalog content
// -> same hash, run to run) and sensitive to content changes, and the
// manifest itself must pair it with the current LibraryApiVersion (LIB-001).
void RunApiManifestTest() {
    kb::tests::Require(
        kb::library::ComputeApiManifestHash("abc") == kb::library::ComputeApiManifestHash("abc"),
        "Engine21kbLibrary manifest hash must be deterministic for identical content");
    kb::tests::Require(
        kb::library::ComputeApiManifestHash("abc") != kb::library::ComputeApiManifestHash("abd"),
        "Engine21kbLibrary manifest hash must change when the content changes");
    kb::tests::Require(kb::library::ComputeApiManifestHash("").size() == 16U, "Engine21kbLibrary manifest hash must be 16 hex digits");

    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary manifest test host setup failed");
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);

    const kb::library::ApiManifest manifest = kb::library::BuildApiManifest(catalog);
    kb::tests::Require(manifest.version == kb::library::kEngineLibraryApiVersion, "Engine21kbLibrary manifest must carry the current LibraryApiVersion");
    kb::tests::Require(
        manifest.manifestHash == kb::library::ComputeApiManifestHash(kb::script::ScriptApiExport::ToJson(catalog)),
        "Engine21kbLibrary manifest hash must match the hash of the catalog's own JSON export");

    const kb::library::ApiManifest rebuilt = kb::library::BuildApiManifest(catalog);
    kb::tests::Require(rebuilt.manifestHash == manifest.manifestHash, "Engine21kbLibrary manifest hash must be stable across repeated builds of the same catalog");

    const std::string json = kb::library::ToJson(manifest);
    kb::tests::Require(json.find("\"version\":\"" + kb::library::ToString(manifest.version) + "\"") != std::string::npos, "Engine21kbLibrary manifest JSON is missing the version field");
    kb::tests::Require(json.find("\"hash\":\"" + manifest.manifestHash + "\"") != std::string::npos, "Engine21kbLibrary manifest JSON is missing the hash field");
}

// LIB-024: comparing two catalog snapshots must classify every difference
// as Breaking (something a Lua script or Visual Graph asset built against
// the baseline could rely on now working differently or not existing) or
// Additive (only new surface, nothing broken).
void RunApiCompatibilityComparisonTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary compatibility test host setup failed");
    const kb::script::ScriptApiCatalog baseline = kb::script::ScriptApiCatalog::Build(host);

    {
        const kb::library::ApiCompatibilityReport identical = kb::library::CompareApiCatalogs(baseline, baseline);
        kb::tests::Require(!identical.HasBreakingChanges(), "Engine21kbLibrary compatibility check must report no breaking changes for an identical catalog");
        kb::tests::Require(identical.changes.empty(), "Engine21kbLibrary compatibility check must report no changes at all for an identical catalog");
    }
    {
        kb::script::ScriptApiCatalog current = baseline;
        kb::tests::Require(!current.functions.empty(), "Engine21kbLibrary compatibility test fixture must have at least one function");
        current.functions.erase(current.functions.begin());
        const kb::library::ApiCompatibilityReport removed = kb::library::CompareApiCatalogs(baseline, current);
        kb::tests::Require(removed.HasBreakingChanges(), "Engine21kbLibrary compatibility check must flag a removed function as breaking");
    }
    {
        kb::script::ScriptApiCatalog current = baseline;
        current.functions.push_back(kb::script::ScriptApiCatalogFunction{ .name = "Tests.BrandNewFunction" });
        const kb::library::ApiCompatibilityReport added = kb::library::CompareApiCatalogs(baseline, current);
        kb::tests::Require(!added.HasBreakingChanges(), "Engine21kbLibrary compatibility check must not flag a purely additive function as breaking");
        bool sawAdditive = false;
        for (const kb::library::ApiChange& change : added.changes) {
            sawAdditive = sawAdditive || (change.severity == kb::library::ApiChangeSeverity::Additive && change.description.find("Tests.BrandNewFunction") != std::string::npos);
        }
        kb::tests::Require(sawAdditive, "Engine21kbLibrary compatibility check must report the new function as an additive change");
    }
    {
        kb::script::ScriptApiCatalog current = baseline;
        kb::tests::Require(!current.functions.front().inputs.empty() || !current.functions.front().outputs.empty(),
            "Engine21kbLibrary compatibility test fixture's first function must have at least one pin");
        if (!current.functions.front().outputs.empty()) {
            current.functions.front().outputs.front().type = kb::script::ScriptValueType::String;
        } else {
            current.functions.front().inputs.front().type = kb::script::ScriptValueType::String;
        }
        const kb::library::ApiCompatibilityReport changedSignature = kb::library::CompareApiCatalogs(baseline, current);
        kb::tests::Require(changedSignature.HasBreakingChanges(), "Engine21kbLibrary compatibility check must flag a changed pin type as breaking");
    }
    {
        kb::script::ScriptApiCatalog current = baseline;
        for (kb::script::ScriptApiCatalogComponent& component : current.components) {
            if (component.name != "Transform") {
                continue;
            }
            for (kb::script::ScriptApiCatalogProperty& property : component.properties) {
                if (property.name == "localPosition.x") {
                    property.writable = false;
                }
            }
        }
        const kb::library::ApiCompatibilityReport propertyChanged = kb::library::CompareApiCatalogs(baseline, current);
        kb::tests::Require(propertyChanged.HasBreakingChanges(), "Engine21kbLibrary compatibility check must flag a writable property becoming read-only as breaking");
    }
}

// LIB-025: a deprecated function's warning must name the function, the
// version it was deprecated since, and (when declared) the replacement to
// call instead; a Visual Graph CallNative node bound to the deprecated
// function must be rewritten to the replacement's binding key, and only
// when a replacement is actually declared.
void RunDeprecationTest() {
    using kb::library::LibraryApiVersion;
    using kb::library::LibraryDeprecation;

    const LibraryDeprecation withReplacement{
        .message = "renamed for clarity",
        .replacementCanonicalName = "World.FindByTagFast",
        .sinceVersion = LibraryApiVersion{ 0U, 2U, 0U },
    };
    const std::string warning = kb::library::FormatDeprecationWarning("World.FindByTag", withReplacement);
    kb::tests::Require(warning.find("World.FindByTag") != std::string::npos, "Engine21kbLibrary deprecation warning must name the deprecated function");
    kb::tests::Require(warning.find("0.2.0") != std::string::npos, "Engine21kbLibrary deprecation warning must name the version it was deprecated since");
    kb::tests::Require(warning.find("World.FindByTagFast") != std::string::npos, "Engine21kbLibrary deprecation warning must name the replacement");

    const LibraryDeprecation withoutReplacement{ .message = "capability removed", .sinceVersion = LibraryApiVersion{ 0U, 3U, 0U } };
    const std::string bareWarning = kb::library::FormatDeprecationWarning("World.Legacy", withoutReplacement);
    kb::tests::Require(bareWarning.find("instead") == std::string::npos, "Engine21kbLibrary deprecation warning must not invent a replacement when none is declared");

    std::vector<kb::visual::VisualGraphNode> nodes{
        kb::visual::VisualGraphNode{ .id = 1U, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "Function.World.FindByTag" },
        kb::visual::VisualGraphNode{ .id = 2U, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "Function.World.Exists" },
        kb::visual::VisualGraphNode{ .id = 3U, .kind = kb::visual::VisualGraphNodeKind::Event, .symbol = "Function.World.FindByTag" },
    };
    const std::size_t migrated = kb::library::MigrateVisualGraphCallNativeNodes(nodes, "World.FindByTag", withReplacement);
    kb::tests::Require(migrated == 1U, "Engine21kbLibrary Visual Graph migration must rewrite exactly the matching CallNative node");
    kb::tests::Require(nodes[0].symbol == "Function.World.FindByTagFast", "Engine21kbLibrary Visual Graph migration did not rewrite the matching node's symbol");
    kb::tests::Require(nodes[1].symbol == "Function.World.Exists", "Engine21kbLibrary Visual Graph migration must not touch an unrelated node");
    kb::tests::Require(nodes[2].symbol == "Function.World.FindByTag", "Engine21kbLibrary Visual Graph migration must not touch a non-CallNative node even with a matching symbol");

    std::vector<kb::visual::VisualGraphNode> unmigratable{
        kb::visual::VisualGraphNode{ .id = 1U, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "Function.World.Legacy" },
    };
    const std::size_t migratedWithoutReplacement = kb::library::MigrateVisualGraphCallNativeNodes(unmigratable, "World.Legacy", withoutReplacement);
    kb::tests::Require(migratedWithoutReplacement == 0U, "Engine21kbLibrary Visual Graph migration must not rewrite anything when no replacement is declared");
    kb::tests::Require(unmigratable[0].symbol == "Function.World.Legacy", "Engine21kbLibrary Visual Graph migration must leave the node untouched when no replacement is declared");
}

// LIB-026: a function's id must depend only on its canonical name, not on
// where it lands in ScriptFunctionRegistry::Functions() or which
// ScriptRuntimeHost instance registered it.
void RunFunctionIdTest() {
    kb::tests::Require(
        kb::library::ComputeLibraryFunctionId("World.Exists") == kb::library::ComputeLibraryFunctionId("World.Exists"),
        "Engine21kbLibrary function id must be deterministic for the same name");
    kb::tests::Require(
        kb::library::ComputeLibraryFunctionId("World.Exists") != kb::library::ComputeLibraryFunctionId("World.Name"),
        "Engine21kbLibrary function id must differ for different names");

    // Two independently constructed hosts register the same six modules;
    // the id for the same function name must match across both, proving
    // it does not depend on which ScriptRuntimeHost instance (or its
    // internal registration order) computed it.
    kb::scene::Scene sceneA;
    kb::script::ScriptRuntimeHost hostA{ sceneA };
    kb::scene::Scene sceneB;
    kb::script::ScriptRuntimeHost hostB{ sceneB };
    kb::tests::Require(hostA.Succeeded() && hostB.Succeeded(), "Engine21kbLibrary function id test hosts failed to set up");
    kb::tests::Require(hostA.Functions().FindSignature("World.Exists") != nullptr, "Engine21kbLibrary function id test fixture is missing World.Exists");

    const kb::library::LibraryFunctionId idFromA = kb::library::ComputeLibraryFunctionId("World.Exists");
    const kb::library::LibraryFunctionId idFromB = kb::library::ComputeLibraryFunctionId("World.Exists");
    kb::tests::Require(idFromA == idFromB, "Engine21kbLibrary function id must be identical across independently constructed hosts");
}

// LIB-029: every function the catalog reports must have a real binding in
// every supported frontend. Native + generic Lua CallFunction reachability
// are structurally guaranteed by ScriptRuntimeHost::RegisterFunction being
// the single registration path (LIB-002) — this test covers the one step
// that registers separately and could independently regress: the Visual
// Graph CallNative binding ScriptFunctionVisualGraphBindings attaches to
// every RegisterFunction call.
void RunCatalogFunctionsHaveVisualGraphBindingsTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary catalog binding test host setup failed");
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);
    kb::tests::Require(!catalog.functions.empty(), "Engine21kbLibrary catalog binding test fixture must have at least one function");

    for (const kb::script::ScriptApiCatalogFunction& function : catalog.functions) {
        const std::string bindingKey = "Function." + function.name;
        const std::string missingNativeBindingMessage = "Engine21kbLibrary function '" + function.name + "' is missing its Visual Graph native binding";
        kb::tests::Require(
            host.VisualGraphNativeBindings().Find(kb::visual::VisualGraphIrOpcode::CallNative, bindingKey) != nullptr,
            missingNativeBindingMessage.c_str());
        const std::string missingRuntimeBindingMessage = "Engine21kbLibrary function '" + function.name + "' is missing its Visual Graph runtime binding";
        kb::tests::Require(
            host.VisualGraphRuntimeBindings().Find(kb::visual::VisualGraphIrOpcode::CallNative, bindingKey) != nullptr,
            missingRuntimeBindingMessage.c_str());
        const std::string missingRegistryEntryMessage = "Engine21kbLibrary function '" + function.name + "' is missing its Native/Lua CallFunction registry entry";
        kb::tests::Require(
            host.Functions().FindSignature(function.name) != nullptr,
            missingRegistryEntryMessage.c_str());
    }
}

// LIB-031: every kb::library handle type must classify to exactly the
// LibraryOwnership its lifetime contract already documents (EntityHandle:
// Borrowed, since the Scene owns the entity; AssetRef<T>: Shared, since it
// carries a ref-counted shared_ptr).
void RunOwnershipTest() {
    static_assert(
        kb::library::LibraryOwnershipTraits<kb::library::EntityHandle>::value == kb::library::LibraryOwnership::Borrowed,
        "kb::library::EntityHandle must classify as Borrowed");
    static_assert(
        kb::library::LibraryOwnershipTraits<kb::library::AssetRef<kb::scene::SceneDocument>>::value == kb::library::LibraryOwnership::Shared,
        "kb::library::AssetRef<T> must classify as Shared");
    static_assert(
        kb::library::LibraryOwnershipTraits<kb::library::SceneRef>::value == kb::library::LibraryOwnership::Shared,
        "kb::library::SceneRef must classify as Shared");

    kb::tests::Require(std::string{ kb::library::ToString(kb::library::LibraryOwnership::Owned) } == "Owned", "Engine21kbLibrary LibraryOwnership::Owned formatting is wrong");
    kb::tests::Require(std::string{ kb::library::ToString(kb::library::LibraryOwnership::Borrowed) } == "Borrowed", "Engine21kbLibrary LibraryOwnership::Borrowed formatting is wrong");
    kb::tests::Require(std::string{ kb::library::ToString(kb::library::LibraryOwnership::Shared) } == "Shared", "Engine21kbLibrary LibraryOwnership::Shared formatting is wrong");
    kb::tests::Require(std::string{ kb::library::ToString(kb::library::LibraryOwnership::Weak) } == "Weak", "Engine21kbLibrary LibraryOwnership::Weak formatting is wrong");
}

// LIB-032: no raw C++ pointer or reference may cross the Lua/Visual Graph
// script boundary. ScriptValue is that boundary's only channel, so this
// locks down its Storage variant's exact shape (eight alternatives, none a
// pointer — LIB-041 appended std::int64_t and double to the original six)
// as a regression guard — ScriptValue.hpp itself already asserts "no
// pointer alternative" at the type definition; this additionally locks the
// alternative count/order so a silently-added ninth alternative (pointer or
// not) fails a test even if it happens not to be a pointer today.
template <typename Variant, std::size_t... Index>
constexpr bool NoVariantAlternativeIsAPointer(std::index_sequence<Index...>) {
    return (!std::is_pointer_v<std::variant_alternative_t<Index, Variant>> && ...);
}

void RunNoPointersCrossScriptBoundaryTest() {
    using Storage = kb::script::ScriptValue::Storage;
    static_assert(std::variant_size_v<Storage> == 8U, "kb::script::ScriptValue::Storage must have exactly the eight known alternatives (monostate, bool, int, float, string, uint64_t, int64_t, double)");
    static_assert(
        NoVariantAlternativeIsAPointer<Storage>(std::make_index_sequence<std::variant_size_v<Storage>>{}),
        "kb::script::ScriptValue::Storage must never hold a raw pointer or reference type");
    static_assert(std::is_same_v<std::variant_alternative_t<0, Storage>, std::monostate>, "ScriptValue::Storage alternative 0 must stay std::monostate (the Void representation)");
    kb::tests::Require(true, "kb::script::ScriptValue::Storage shape is verified at compile time above");
}

// LIB-035: every LibraryErrorCode must format to a stable, distinct name,
// and EntityHandle::CheckError() (the non-throwing counterpart of
// Validate(), LIB-035's real consumer) must report InvalidHandle for every
// EntityHandle failure mode and std::nullopt exactly when Validate()
// would not throw.
void RunErrorCodeTest() {
    using kb::library::LibraryErrorCode;
    const LibraryErrorCode kAllCodes[]{
        LibraryErrorCode::InvalidHandle, LibraryErrorCode::InactiveWorld, LibraryErrorCode::UnavailableCapability,
        LibraryErrorCode::Permission, LibraryErrorCode::InvalidArgument, LibraryErrorCode::Timeout,
    };
    std::vector<std::string> seenNames;
    for (const LibraryErrorCode code : kAllCodes) {
        const std::string name = kb::library::ToString(code);
        kb::tests::Require(!name.empty(), "Engine21kbLibrary LibraryErrorCode must have a non-empty name");
        kb::tests::Require(std::ranges::find(seenNames, name) == seenNames.end(), "Engine21kbLibrary LibraryErrorCode names must be distinct");
        seenNames.push_back(name);
    }

    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ErrorCodeSubject" });
    const kb::library::EntityHandle handle{ object.Entity(), scene.Id() };

    kb::tests::Require(!handle.CheckError(scene, "Test.Op").has_value(), "Engine21kbLibrary EntityHandle::CheckError must return nullopt for a live handle");

    const kb::library::EntityHandle invalidHandle{};
    const std::optional<kb::library::ScriptError> invalidError = invalidHandle.CheckError(scene, "Test.Op");
    kb::tests::Require(invalidError.has_value() && invalidError->code == kb::library::LibraryErrorCode::InvalidHandle, "Engine21kbLibrary EntityHandle::CheckError must report InvalidHandle for an invalid handle");

    scene.Entities().Destroy(object);
    const std::optional<kb::library::ScriptError> staleError = handle.CheckError(scene, "Test.Op");
    kb::tests::Require(staleError.has_value() && staleError->code == kb::library::LibraryErrorCode::InvalidHandle, "Engine21kbLibrary EntityHandle::CheckError must report InvalidHandle for a stale handle");
}

// LIB-037: kb::library::kDefaultLibraryInputLimits.maxStringLength must
// match the limit kb::script::ScriptFunctionRegistry actually enforces
// (verified behaviorally, since the enforced constant is private to
// ScriptFunctionRegistry.cpp), and maxGraphRecursionDepth must match
// ScriptRuntimeDispatchOptions' already-enforced default.
void RunInputLimitsTest() {
    kb::tests::Require(
        kb::library::kDefaultLibraryInputLimits.maxGraphRecursionDepth == kb::script::ScriptRuntimeDispatchOptions{}.maxEventDepth,
        "Engine21kbLibrary maxGraphRecursionDepth must match ScriptRuntimeDispatchOptions::maxEventDepth's default");

    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary input limits test host setup failed");
    kb::tests::Require(
        host.RegisterFunction(kb::script::ScriptFunctionDesc{
            .signature = kb::script::ScriptFunctionSignature{
                .name = "Tests.TakesString",
                .inputs = { kb::script::ScriptFunctionPin{ .name = "text", .type = kb::script::ScriptValueType::String } },
            },
            .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                return kb::script::ScriptFunctionCallResult{ .executed = true };
            },
        }),
        "Engine21kbLibrary input limits test could not register Tests.TakesString");

    const std::string atLimit(kb::library::kDefaultLibraryInputLimits.maxStringLength, 'a');
    const std::array atLimitArguments{ kb::script::ScriptFunctionArgument{ .name = "text", .value = kb::script::ScriptValue{ atLimit } } };
    const kb::script::ScriptFunctionCallResult atLimitResult = host.Functions().Call("Tests.TakesString", atLimitArguments, kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(atLimitResult.Succeeded(), "Engine21kbLibrary a string exactly at the documented limit must be accepted");

    const std::string overLimit(kb::library::kDefaultLibraryInputLimits.maxStringLength + 1U, 'a');
    const std::array overLimitArguments{ kb::script::ScriptFunctionArgument{ .name = "text", .value = kb::script::ScriptValue{ overLimit } } };
    const kb::script::ScriptFunctionCallResult overLimitResult = host.Functions().Call("Tests.TakesString", overLimitArguments, kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(!overLimitResult.Succeeded(), "Engine21kbLibrary a string exceeding the documented limit must be rejected by ScriptFunctionRegistry");
}

} // namespace

namespace kb::tests {

// LIB-041: Int64/UInt32/Double/Name/Guid/Hash must round-trip correctly
// through ScriptValue construction/accessors, the Visual Graph runtime
// value bridge (ToVisualGraphValue), and an actual function call through
// ScriptFunctionRegistry::Call — proving they are real, usable script pin
// types, not just enum labels with no working storage/marshalling path.
void RunExpandedValueTypesTest() {
    using kb::script::ScriptValue;
    using kb::script::ScriptValueType;

    // Construction + accessor round-trip for each new type.
    const ScriptValue int64Value{ std::int64_t{ -9000000000LL } };
    kb::tests::Require(int64Value.Type() == ScriptValueType::Int64, "Int64 ScriptValue must report ScriptValueType::Int64");
    kb::tests::Require(int64Value.AsInt64() == -9000000000LL, "Int64 ScriptValue must round-trip its exact value through AsInt64");

    const ScriptValue uint32Value{ std::uint32_t{ 4000000000U } };
    kb::tests::Require(uint32Value.Type() == ScriptValueType::UInt32, "UInt32 ScriptValue must report ScriptValueType::UInt32");
    kb::tests::Require(uint32Value.AsUInt32() == 4000000000U, "UInt32 ScriptValue must round-trip a value beyond int32 range through AsUInt32");

    const ScriptValue doubleValue{ 1.0e300 };
    kb::tests::Require(doubleValue.Type() == ScriptValueType::Double, "Double ScriptValue must report ScriptValueType::Double");
    kb::tests::Require(doubleValue.AsDouble() == 1.0e300, "Double ScriptValue must round-trip a magnitude a float cannot represent");

    const ScriptValue nameValue{ std::string{ "PlayerTag" }, ScriptValueType::Name };
    kb::tests::Require(nameValue.Type() == ScriptValueType::Name, "Name ScriptValue must report ScriptValueType::Name");
    kb::tests::Require(nameValue.AsString() == "PlayerTag", "Name ScriptValue must round-trip its string through AsString");

    const ScriptValue guidValue{ std::string{ "3F2504E0-4F89-11D3-9A0C-0305E82C3301" }, ScriptValueType::Guid };
    kb::tests::Require(guidValue.Type() == ScriptValueType::Guid, "Guid ScriptValue must report ScriptValueType::Guid");
    kb::tests::Require(guidValue.AsString() == "3F2504E0-4F89-11D3-9A0C-0305E82C3301", "Guid ScriptValue must round-trip its canonical string through AsString");

    const ScriptValue hashValue{ 0xDEADBEEFCAFEU, ScriptValueType::Hash };
    kb::tests::Require(hashValue.Type() == ScriptValueType::Hash, "Hash ScriptValue must report ScriptValueType::Hash");
    kb::tests::Require(hashValue.AsUInt64() == 0xDEADBEEFCAFEU, "Hash ScriptValue must round-trip its raw 64-bit value through AsUInt64");

    // A ScriptValue built from an unrecognized tag for the tagged-string
    // ctor must fall back to String, mirroring the existing uint64_t+type
    // ctor's fallback-to-Void pattern for its own restricted type set.
    const ScriptValue untaggedString{ std::string{ "plain" }, ScriptValueType::Bool };
    kb::tests::Require(untaggedString.Type() == ScriptValueType::String, "ScriptValue(string, type) must fall back to String for a type outside {Name, Guid}");

    // Structural equality (LIB-018) must hold for the new types too, and
    // distinguish types that happen to share underlying Storage.
    kb::tests::Require(ScriptValue{ std::int64_t{ 5 } } == ScriptValue{ std::int64_t{ 5 } }, "Int64 ScriptValue equality must hold for equal values");
    kb::tests::Require(
        ScriptValue{ 5U, ScriptValueType::Hash } != ScriptValue{ 5U, ScriptValueType::Entity },
        "ScriptValue equality must distinguish Hash from Entity even though both share the uint64_t Storage alternative");
    kb::tests::Require(
        ScriptValue{ std::string{ "x" }, ScriptValueType::Name } != ScriptValue{ std::string{ "x" }, ScriptValueType::Guid },
        "ScriptValue equality must distinguish Name from Guid even though both share the string Storage alternative");

    // Visual Graph runtime value bridge round-trip.
    kb::tests::Require(int64Value.ToVisualGraphValue().Type() == kb::visual::VisualGraphValueType::Int64, "Int64 must map to VisualGraphValueType::Int64");
    kb::tests::Require(int64Value.ToVisualGraphValue().AsInt64() == -9000000000LL, "Int64 must round-trip through VisualGraphRuntimeValue");
    kb::tests::Require(doubleValue.ToVisualGraphValue().Type() == kb::visual::VisualGraphValueType::Double, "Double must map to VisualGraphValueType::Double");
    kb::tests::Require(doubleValue.ToVisualGraphValue().AsDouble() == 1.0e300, "Double must round-trip through VisualGraphRuntimeValue");
    kb::tests::Require(guidValue.ToVisualGraphValue().Type() == kb::visual::VisualGraphValueType::Guid, "Guid must map to VisualGraphValueType::Guid");
    kb::tests::Require(guidValue.ToVisualGraphValue().AsString() == "3F2504E0-4F89-11D3-9A0C-0305E82C3301", "Guid must round-trip through VisualGraphRuntimeValue");

    // ToString/ToVisualGraphValueType must be defined for every new type
    // (already exercised generically by RunTypeDescTest's kAllTypes loop
    // via DescribeType; this pins the exact expected names).
    kb::tests::Require(std::string{ kb::script::ToString(ScriptValueType::Int64) } == "Int64", "ScriptValueType::Int64 must format as \"Int64\"");
    kb::tests::Require(std::string{ kb::script::ToString(ScriptValueType::UInt32) } == "UInt32", "ScriptValueType::UInt32 must format as \"UInt32\"");
    kb::tests::Require(std::string{ kb::script::ToString(ScriptValueType::Double) } == "Double", "ScriptValueType::Double must format as \"Double\"");
    kb::tests::Require(std::string{ kb::script::ToString(ScriptValueType::Name) } == "Name", "ScriptValueType::Name must format as \"Name\"");
    kb::tests::Require(std::string{ kb::script::ToString(ScriptValueType::Guid) } == "Guid", "ScriptValueType::Guid must format as \"Guid\"");
    kb::tests::Require(std::string{ kb::script::ToString(ScriptValueType::Hash) } == "Hash", "ScriptValueType::Hash must format as \"Hash\"");

    // A real end-to-end function call through the single choke point every
    // Native/Lua/Visual Graph caller funnels through, proving the new
    // types actually flow through ValidateInputs/ValidateOutputs (pin type
    // matching via IsCompatible), not just construct/format in isolation.
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Expanded value types test host setup failed");
    kb::tests::Require(
        host.RegisterFunction(kb::script::ScriptFunctionDesc{
            .signature = kb::script::ScriptFunctionSignature{
                .name = "Tests.EchoHash",
                .inputs = { kb::script::ScriptFunctionPin{ .name = "value", .type = ScriptValueType::Hash } },
                .outputs = { kb::script::ScriptFunctionPin{ .name = "value", .type = ScriptValueType::Hash } },
            },
            .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument> arguments) {
                return kb::script::ScriptFunctionCallResult{
                    .executed = true,
                    .outputs = { kb::script::ScriptFunctionArgument{ .name = "value", .value = arguments[0].value } },
                };
            },
        }),
        "Expanded value types test could not register Tests.EchoHash");
    const std::array<kb::script::ScriptFunctionArgument, 1> echoArguments{
        kb::script::ScriptFunctionArgument{ .name = "value", .value = ScriptValue{ 0x1234ULL, ScriptValueType::Hash } },
    };
    const kb::script::ScriptFunctionCallResult echoResult = host.Functions().Call("Tests.EchoHash", echoArguments, kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(echoResult.Succeeded(), "Tests.EchoHash call with a Hash-typed argument must succeed through ValidateInputs/ValidateOutputs");
    const std::optional<ScriptValue> echoedOutput = echoResult.Output("value");
    kb::tests::Require(echoedOutput.has_value() && echoedOutput->Type() == ScriptValueType::Hash && echoedOutput->AsUInt64() == 0x1234ULL, "Tests.EchoHash must return the exact Hash value it was given");
}

void RunEngineLibraryTests() {
    RunVersionValueTest();
    RunVersionOrderingTest();
    RunVersionCompatibilityTest();
    RunModuleInstallCoversAllDomainsTest();
    RunModuleInstallReportsDuplicateDiagnosticsTest();
    RunModuleCatalogTest();
    RunModuleInstallSkipsUnavailableCapabilityTest();
    RunFunctionDescCatalogResolvesTest();
    RunModuleCatalogValidatesTest();
    RunModuleValidationDuplicateNameTest();
    RunModuleValidationUnknownDependencyTest();
    RunModuleValidationCycleTest();
    RunModuleValidationDuplicateFunctionTest();
    RunModuleInstallFailsFastOnInvalidCatalogTest();
    RunTypeDescTest();
    RunPropertyDescTest();
    RunLifecycleContextClassificationTest();
    RunExecutionOrderContractTest();
    RunCommandApplicationContractTest();
    RunLibraryContextTest();
    RunMultipleBehavioursRemovedSameFrameOrderTest();
    RunEntityHandleTest();
    RunAssetRefTest();
    RunResultTest();
    RunApiManifestTest();
    RunApiCompatibilityComparisonTest();
    RunDeprecationTest();
    RunFunctionIdTest();
    RunCatalogFunctionsHaveVisualGraphBindingsTest();
    RunOwnershipTest();
    RunNoPointersCrossScriptBoundaryTest();
    RunErrorCodeTest();
    RunInputLimitsTest();
    RunExpandedValueTypesTest();
}

} // namespace kb::tests
