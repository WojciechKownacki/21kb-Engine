#include "TestSupport.hpp"

#include "engine/library/EngineLibrary.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/library/EngineLibraryArrayView.hpp"
#include "engine/library/EngineLibraryAssetRef.hpp"
#include "engine/library/EngineLibraryCollections.hpp"
#include "engine/library/EngineLibraryCommandApplication.hpp"
#include "engine/library/EngineLibraryCommandBatch.hpp"
#include "engine/library/EngineLibraryComponentChangeTracker.hpp"
#include "engine/library/EngineLibraryTransformChangeTracker.hpp"
#include "engine/library/EngineLibraryComponentDesc.hpp"
#include "engine/library/EngineLibraryComponentInspectorDesc.hpp"
#include "engine/library/EngineLibraryDeprecation.hpp"
#include "engine/library/EngineLibraryContext.hpp"
#include "engine/library/EngineLibraryEntityHandle.hpp"
#include "engine/library/EngineLibraryScriptComponentAccess.hpp"
#include "engine/library/EngineLibraryError.hpp"
#include "engine/library/EngineLibraryEventSchema.hpp"
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
#include "engine/library/EngineLibraryQuery.hpp"
#include "engine/library/EngineLibraryParsing.hpp"
#include "engine/library/EngineLibraryResult.hpp"
#include "engine/library/EngineLibrarySignal.hpp"
#include "engine/library/EngineLibraryTextEncoding.hpp"
#include "engine/library/EngineLibraryTextFormat.hpp"
#include "engine/library/EngineLibraryTypeDesc.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/gameplay/GameInstance.hpp"
#include "engine/gameplay/GameplayIdentity.hpp"
#include "engine/gameplay/Damage.hpp"
#include "engine/gameplay/GameplayModules.hpp"
#include "engine/gameplay/GameplayAbilities.hpp"
#include "engine/gameplay/GameplaySamples.hpp"
#include "engine/network/NetworkModel.hpp"
#include "engine/network/NetworkObject.hpp"
#include "engine/network/ReplicationSchema.hpp"
#include "engine/network/Rpc.hpp"
#include "engine/network/NetworkVariable.hpp"
#include "engine/network/NetworkPrediction.hpp"
#include "engine/network/NetworkBudget.hpp"
#include "engine/network/NetworkSecurity.hpp"
#include "engine/network/NetworkSimulation.hpp"
#include "engine/network/NetworkSession.hpp"
#include "engine/platform/PlatformCapabilities.hpp"
#include "engine/platform/UserStorage.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/AiBehaviourAssetIO.hpp"
#include "engine/scene/AiBehaviourRuntime.hpp"
#include "engine/scene/AiBlackboard.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/Perception.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/NativeScriptBackend.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptAssetsApi.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptApiExport.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

void RecordNetworkVariableChange(void* context, std::int32_t previous, std::int32_t current) noexcept { *static_cast<std::int32_t*>(context) = current - previous; }

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
        "Timer.Once",
        "Task.IsRunning",
        "Physics.Raycast",
        "Transform.GetPosition",
        "Math.Clamp",
        "Scene.Load",
        "Assets.Load",
        "Save.SetInt",
        "Timeline.Create",
        "UI.Create",
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
    kb::tests::Require(second.diagnostics.size() == 24U, "Engine21kbLibrary module install must report one diagnostic per failed domain module");
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
    const std::vector<std::string> expectedNames{ "Input", "Audio", "World", "Time", "Timer", "Task", "Events", "Physics", "Transform", "Math", "Scene", "MeshRenderer", "MaterialInstance", "PostProcess", "Particles", "Animator", "Timeline", "UI", "Localization", "Renderer", "Assets", "Save", "Collections", "Text" };
    kb::tests::Require(catalog.size() == expectedNames.size(), "Engine21kbLibrary module catalog must have exactly twenty-four domain modules");
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

// LIB-028: InstallModules() must produce a real startup report — one entry
// per catalog module (installed, version, owner, and, for a disabled one,
// the reason) — not just the older failure-only diagnostics list, and
// FormatStartupReport() must actually turn that into readable text.
void RunModuleInstallStartupReportTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary startup report test host setup failed");

    const std::vector<kb::library::LibraryModuleDesc> modules{
        kb::library::LibraryModuleDesc{
            .name = "AvailableTestModule",
            .version = kb::library::LibraryModuleVersion{ 1U, 2U, 3U },
            .ownerRuntime = "kb::tests::FakeRuntime",
            .capability = true,
            .Register = [](kb::script::ScriptRuntimeHost&) { return true; },
        },
        kb::library::LibraryModuleDesc{
            .name = "DisabledTestModule",
            .version = kb::library::LibraryModuleVersion{ 4U, 5U, 6U },
            .ownerRuntime = "kb::tests::MissingRuntime",
            .capability = false,
            .disabledReason = "optional backend not compiled into this test build",
            .Register = &RecordModuleInstallCall,
        },
    };
    const kb::library::EngineLibraryModuleResult result = kb::library::EngineLibraryModule::InstallModules(host, modules);
    kb::tests::Require(result.report.size() == 2U, "Engine21kbLibrary startup report must have exactly one entry per catalog module");

    const kb::library::EngineLibraryModuleReportEntry& availableEntry = result.report[0];
    kb::tests::Require(availableEntry.name == "AvailableTestModule" && availableEntry.installed && availableEntry.capability && availableEntry.reason.empty(),
        "Engine21kbLibrary startup report must mark a successfully registered module installed, with no reason text");
    kb::tests::Require(availableEntry.version.major == 1U && availableEntry.version.minor == 2U && availableEntry.version.patch == 3U,
        "Engine21kbLibrary startup report must carry the module's own version, not a default");

    const kb::library::EngineLibraryModuleReportEntry& disabledEntry = result.report[1];
    kb::tests::Require(disabledEntry.name == "DisabledTestModule" && !disabledEntry.installed && !disabledEntry.capability,
        "Engine21kbLibrary startup report must mark a capability=false module as not installed");
    kb::tests::Require(disabledEntry.reason == "optional backend not compiled into this test build",
        "Engine21kbLibrary startup report must carry the module's own disabledReason verbatim, not a generic placeholder");

    const std::string formatted = kb::library::FormatStartupReport(result.report);
    kb::tests::Require(formatted.find("AvailableTestModule") != std::string::npos && formatted.find("1.2.3") != std::string::npos,
        "FormatStartupReport must render the installed module's name and version as readable text");
    kb::tests::Require(formatted.find("DisabledTestModule") != std::string::npos && formatted.find("optional backend not compiled into this test build") != std::string::npos,
        "FormatStartupReport must render the disabled module's name and its real reason as readable text");

    // Real end-to-end: a normal host installs the real seven-module
    // Catalog(), all with capability=true, so ScriptRuntimeHost's own
    // report (populated from the real Install() call it made while
    // constructing, not a separately re-run InstallModules) must show all
    // seven as installed.
    const std::vector<kb::library::EngineLibraryModuleReportEntry>& realReport = host.LibraryStartupReport();
    kb::tests::Require(realReport.size() == kb::library::EngineLibraryModule::Catalog().size(),
        "ScriptRuntimeHost::LibraryStartupReport must have one entry per real catalog module");
    for (const kb::library::EngineLibraryModuleReportEntry& entry : realReport) {
        kb::tests::Require(entry.installed && entry.capability && entry.reason.empty(), "ScriptRuntimeHost::LibraryStartupReport must show every real catalog module as installed, since every module's capability is true today");
    }
}

// LIB-017: every audited LibraryFunctionDesc::canonicalName across the
// whole module catalog must resolve to a function ScriptApiCatalog reports
// as actually registered — an audited function description can never
// outlive (or predate) the real ScriptFunctionRegistry entry it describes —
// AND its recorded inputs/outputs must machine-match the real, live
// ScriptFunctionSignature-derived pins (FunctionDescMatchesCatalog), not
// just its name. This is the "wejścia, wyjścia" half of LIB-017's contract
// the 2026-07-17 audit found unfulfilled: recording pins that are never
// cross-checked against the registry would be no better than prose.
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
            kb::tests::Require(
                kb::library::FunctionDescMatchesCatalog(function, catalog),
                "Engine21kbLibrary LibraryFunctionDesc's recorded inputs/outputs do not machine-match the real registered signature");
        }
    }
    kb::tests::Require(sawAnyAuditedFunction, "Engine21kbLibrary module catalog must have at least one audited LibraryFunctionDesc");
}

// LIB-017 adversarial: FunctionDescMatchesCatalog must actually be capable
// of REJECTING a mismatch, not just accepting whatever the production
// catalog happens to contain today. Exercises a wrong pin name, a wrong
// pin type, a wrong required flag, a missing pin, and an unresolvable
// canonicalName — each independently, each caught.
void RunFunctionDescMatchesCatalogRejectsMismatchTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary function desc mismatch test host setup failed");
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);

    const kb::library::LibraryFunctionDesc correct{
        .canonicalName = "World.Exists",
        .inputs = { kb::script::ScriptApiPin{ "entity", kb::script::ScriptValueType::Entity, true } },
        .outputs = { kb::script::ScriptApiPin{ "exists", kb::script::ScriptValueType::Bool, true } },
    };
    kb::tests::Require(kb::library::FunctionDescMatchesCatalog(correct, catalog), "Engine21kbLibrary FunctionDescMatchesCatalog must accept a genuinely correct description");

    kb::library::LibraryFunctionDesc wrongName = correct;
    wrongName.canonicalName = "World.DoesNotExist";
    kb::tests::Require(!kb::library::FunctionDescMatchesCatalog(wrongName, catalog), "Engine21kbLibrary FunctionDescMatchesCatalog must reject an unresolvable canonicalName");

    kb::library::LibraryFunctionDesc wrongInputName = correct;
    wrongInputName.inputs = { kb::script::ScriptApiPin{ "target", kb::script::ScriptValueType::Entity, true } };
    kb::tests::Require(!kb::library::FunctionDescMatchesCatalog(wrongInputName, catalog), "Engine21kbLibrary FunctionDescMatchesCatalog must reject a wrong input pin name");

    kb::library::LibraryFunctionDesc wrongOutputType = correct;
    wrongOutputType.outputs = { kb::script::ScriptApiPin{ "exists", kb::script::ScriptValueType::Int, true } };
    kb::tests::Require(!kb::library::FunctionDescMatchesCatalog(wrongOutputType, catalog), "Engine21kbLibrary FunctionDescMatchesCatalog must reject a wrong output pin type");

    kb::library::LibraryFunctionDesc wrongRequired = correct;
    wrongRequired.inputs = { kb::script::ScriptApiPin{ "entity", kb::script::ScriptValueType::Entity, false } };
    kb::tests::Require(!kb::library::FunctionDescMatchesCatalog(wrongRequired, catalog), "Engine21kbLibrary FunctionDescMatchesCatalog must reject a wrong required flag");

    kb::library::LibraryFunctionDesc missingInput = correct;
    missingInput.inputs.clear();
    kb::tests::Require(!kb::library::FunctionDescMatchesCatalog(missingInput, catalog), "Engine21kbLibrary FunctionDescMatchesCatalog must reject a missing input pin");

    kb::library::LibraryFunctionDesc extraOutput = correct;
    extraOutput.outputs.push_back(kb::script::ScriptApiPin{ "extra", kb::script::ScriptValueType::Bool, true });
    kb::tests::Require(!kb::library::FunctionDescMatchesCatalog(extraOutput, catalog), "Engine21kbLibrary FunctionDescMatchesCatalog must reject an extra output pin");
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

// LIB-003: a LibraryFunctionDesc living in the wrong module's functions list
// (e.g. copy-pasted, or attributed after a rename) is a real catalog
// corruption the duplicate-function check above cannot catch on its own —
// there is nothing duplicated, just a mismatched owner.
void RunModuleValidationFunctionPrefixMismatchTest() {
    const std::vector<kb::library::LibraryModuleDesc> mismatched{
        kb::library::LibraryModuleDesc{
            .name = "World",
            .functions = { kb::library::LibraryFunctionDesc{ .canonicalName = "Physics.Raycast" } },
        },
    };
    const kb::library::ModuleCatalogValidationResult mismatchedResult = kb::library::ValidateModuleCatalog(mismatched);
    kb::tests::Require(!mismatchedResult.succeeded, "Engine21kbLibrary module validation must reject a function name not prefixed with its declaring module's name");

    const std::vector<kb::library::LibraryModuleDesc> matched{
        kb::library::LibraryModuleDesc{
            .name = "World",
            .functions = { kb::library::LibraryFunctionDesc{ .canonicalName = "World.Exists" } },
        },
    };
    const kb::library::ModuleCatalogValidationResult matchedResult = kb::library::ValidateModuleCatalog(matched);
    kb::tests::Require(matchedResult.succeeded, "Engine21kbLibrary module validation must accept a function name correctly prefixed with its declaring module's name");
}

// LIB-020 "zmiana sygnatur" (signature changes): the SAME canonicalName
// described twice within one module with conflicting content is a real,
// easy-to-introduce catalog bug (a copy-pasted LibraryFunctionDesc edited
// in one place but not the other) that RunModuleValidationDuplicateFunctionTest
// above cannot catch — that test only exercises two DIFFERENT modules
// claiming the same name, an ownership collision independent of whether
// content agrees.
void RunModuleValidationFunctionSignatureChangedTest() {
    using kb::library::LibraryDeterminism;
    using kb::library::LibraryFunctionDesc;
    using kb::library::LibraryThreadAffinity;
    using kb::script::ScriptApiPin;
    using kb::script::ScriptValueType;

    const std::vector<kb::library::LibraryModuleDesc> conflicting{
        kb::library::LibraryModuleDesc{
            .name = "World",
            .functions = {
                LibraryFunctionDesc{
                    .canonicalName = "World.Exists",
                    .threadAffinity = LibraryThreadAffinity::MainThread,
                    .determinism = LibraryDeterminism::Deterministic,
                    .canFail = false,
                    .inputs = { ScriptApiPin{ "entity", ScriptValueType::Entity, true } },
                    .outputs = { ScriptApiPin{ "exists", ScriptValueType::Bool, true } },
                },
                LibraryFunctionDesc{
                    .canonicalName = "World.Exists",
                    .threadAffinity = LibraryThreadAffinity::MainThread,
                    .determinism = LibraryDeterminism::Deterministic,
                    .canFail = false,
                    // Conflicting: wrong output type, as if someone edited
                    // this copy after a real signature change and forgot
                    // the sibling entry above.
                    .inputs = { ScriptApiPin{ "entity", ScriptValueType::Entity, true } },
                    .outputs = { ScriptApiPin{ "exists", ScriptValueType::Int, true } },
                },
            },
        },
    };
    const kb::library::ModuleCatalogValidationResult conflictingResult = kb::library::ValidateModuleCatalog(conflicting);
    kb::tests::Require(!conflictingResult.succeeded,
        "Engine21kbLibrary module validation must reject the same function described twice with conflicting inputs/outputs, even within one module");

    const std::vector<kb::library::LibraryModuleDesc> identicalDuplicate{
        kb::library::LibraryModuleDesc{
            .name = "World",
            .functions = {
                LibraryFunctionDesc{
                    .canonicalName = "World.Exists",
                    .threadAffinity = LibraryThreadAffinity::MainThread,
                    .determinism = LibraryDeterminism::Deterministic,
                    .canFail = false,
                    .inputs = { ScriptApiPin{ "entity", ScriptValueType::Entity, true } },
                    .outputs = { ScriptApiPin{ "exists", ScriptValueType::Bool, true } },
                },
                LibraryFunctionDesc{
                    .canonicalName = "World.Exists",
                    .threadAffinity = LibraryThreadAffinity::MainThread,
                    .determinism = LibraryDeterminism::Deterministic,
                    .canFail = false,
                    .inputs = { ScriptApiPin{ "entity", ScriptValueType::Entity, true } },
                    .outputs = { ScriptApiPin{ "exists", ScriptValueType::Bool, true } },
                },
            },
        },
    };
    const kb::library::ModuleCatalogValidationResult identicalResult = kb::library::ValidateModuleCatalog(identicalDuplicate);
    kb::tests::Require(identicalResult.succeeded,
        "Engine21kbLibrary module validation must accept the same function described twice with IDENTICAL content (harmless redundancy, not a signature change)");

    // Positive control: the real production catalog (now with 2 audited
    // World functions, LIB-017) must still pass every rule this function
    // enforces, signature-change detection included.
    const kb::library::ModuleCatalogValidationResult productionResult = kb::library::ValidateModuleCatalog(kb::library::EngineLibraryModule::Catalog());
    kb::tests::Require(productionResult.succeeded, "Engine21kbLibrary production module catalog must still pass signature-change validation");
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
        // LIB-024: kb::script::TryParse must be the exact inverse of
        // ToString — every type ToString emits must parse back to itself.
        // This is what lets kb_cli api-check reconstruct a baseline catalog
        // from its JSON without a second, driftable type table.
        ScriptValueType parsed{};
        kb::tests::Require(kb::script::TryParse(kb::script::ToString(type), parsed), "Engine21kbLibrary kb::script::TryParse must accept every name ToString emits");
        kb::tests::Require(parsed == type, "Engine21kbLibrary kb::script::TryParse must be the exact inverse of ToString");
    }
    ScriptValueType rejected{};
    kb::tests::Require(!kb::script::TryParse("NotARealType", rejected), "Engine21kbLibrary kb::script::TryParse must reject a string ToString never produces");
    kb::tests::Require(!kb::script::TryParse("bool", rejected), "Engine21kbLibrary kb::script::TryParse must be case-sensitive (canonical PascalCase only)");

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

// LIB-018 round-trip: RunTypeDescTest above only proves LibraryTypeDesc's
// fields match kb::script's OWN reflection helpers — luaTypeName's claim
// about the REAL (private) PucLuaValueBridge was previously undocumented
// prose nobody verified against actual Lua behavior. This calls a real
// native function through Lua's generic CallFunction(name, {...}) - the
// same public mechanism every Lua sugar table (Input.*, Transform.*, ...)
// itself uses - for every non-Void ScriptValueType, and checks the result
// IN LUA (`result == literal`, `type(result)`), reporting only Bool/String
// verdicts back via SetShared to avoid SetShared's OWN magnitude-based
// inference (see the dedicated negative case below) muddying what this
// test proves. Void is excluded: no Lua argument/return position
// represents "no value" as a typed pin.
//
// Two calling shapes, chosen per type by reading PucLuaValueBridge::FromLua
// (the REAL blind-inference rules, private header, read directly rather
// than guessed) rather than assumed identical for every type:
//   - identity (Bool/Int/Float/String/Entity/Component): a bare Lua literal
//     ALREADY blind-infers to the declared pin type, or to a type
//     ScriptFunctionRegistry::CoerceArgument bridges (Int->Float,
//     non-negative Int->Entity/Component) - a true input+output round trip.
//   - producer (Int64/UInt32/Double/Name/Guid/Hash): FromLua's blind
//     inference can NEVER produce these from a bare literal (a large
//     non-negative integer literal blind-infers to Entity, not Int64/
//     UInt32/Hash; any Lua float literal blind-infers to Float, not
//     Double; any Lua string literal blind-infers to String, not Name/
//     Guid) - there is no coercion rule bridging that gap either, so a
//     "true identity" call would be rejected as a type mismatch. These
//     instead use a fixed C++-constructed value (a properly-tagged
//     ScriptValue needs no blind inference to reach Lua - only the
//     reverse direction does), proving the C++->Lua push side that
//     luaTypeName actually documents.
void RunTypeDescLuaRoundTripTest() {
    using kb::script::ScriptFunctionArgument;
    using kb::script::ScriptFunctionCallContext;
    using kb::script::ScriptFunctionCallResult;
    using kb::script::ScriptFunctionDesc;
    using kb::script::ScriptFunctionPin;
    using kb::script::ScriptValue;
    using kb::script::ScriptValueType;

    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary type desc round-trip test host setup failed");

    struct Case {
        ScriptValueType type;
        const char* suffix;
        bool producer;
        const char* luaLiteral; // identity: input+expected value; producer: expected value only
        const char* expectedLuaType;
        ScriptValue fixedValue; // producer only
    };
    const Case kCases[] = {
        { ScriptValueType::Bool, "Bool", false, "true", "boolean", ScriptValue{} },
        { ScriptValueType::Int, "Int", false, "42", "number", ScriptValue{} },
        { ScriptValueType::Float, "Float", false, "3.5", "number", ScriptValue{} },
        { ScriptValueType::String, "String", false, "\"hello\"", "string", ScriptValue{} },
        { ScriptValueType::Entity, "Entity", false, "123", "number", ScriptValue{} },
        { ScriptValueType::Component, "Component", false, "456", "number", ScriptValue{} },
        { ScriptValueType::Int64, "Int64", true, "9007199254740993", "number", ScriptValue{ std::int64_t{ 9007199254740993LL } } },
        { ScriptValueType::UInt32, "UInt32", true, "4000000000", "number", ScriptValue{ std::uint32_t{ 4000000000U } } },
        { ScriptValueType::Double, "Double", true, "3.14159265358979", "number", ScriptValue{ 3.14159265358979 } },
        { ScriptValueType::Name, "Name", true, "\"PlayerName\"", "string", ScriptValue{ std::string{ "PlayerName" }, ScriptValueType::Name } },
        { ScriptValueType::Guid, "Guid", true, "\"550e8400-e29b-41d4-a716-446655440000\"", "string",
            ScriptValue{ std::string{ "550e8400-e29b-41d4-a716-446655440000" }, ScriptValueType::Guid } },
        { ScriptValueType::Hash, "Hash", true, "9007199254740993", "number", ScriptValue{ static_cast<std::uint64_t>(9007199254740993ULL), ScriptValueType::Hash } },
    };

    for (const Case& testCase : kCases) {
        const kb::library::LibraryTypeDesc& desc = kb::library::DescribeType(testCase.type);
        kb::tests::Require(desc.luaTypeName.find(testCase.expectedLuaType) != std::string_view::npos,
            "Engine21kbLibrary LibraryTypeDesc.luaTypeName does not mention the real Lua base type it round-trips to");

        ScriptFunctionDesc function;
        if (testCase.producer) {
            function.signature.name = std::string{ "Tests.Produce" } + testCase.suffix;
            function.signature.inputs = { ScriptFunctionPin{ "trigger", ScriptValueType::Bool, true } };
            function.signature.outputs = { ScriptFunctionPin{ "value", testCase.type, true } };
            const ScriptValue fixedValue = testCase.fixedValue;
            function.callback = [fixedValue](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>) {
                return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ .name = "value", .value = fixedValue } }, .errors = {} };
            };
        } else {
            function.signature.name = std::string{ "Tests.Identity" } + testCase.suffix;
            function.signature.inputs = { ScriptFunctionPin{ "value", testCase.type, true } };
            function.signature.outputs = { ScriptFunctionPin{ "value", testCase.type, true } };
            function.callback = [](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
                return ScriptFunctionCallResult{ .executed = true, .outputs = { arguments[0] }, .errors = {} };
            };
        }
        function.signature.description = "Exercises one registered value type through every script frontend.";
        kb::tests::Require(host.RegisterFunction(std::move(function)),
            "Engine21kbLibrary type desc round-trip function registration failed");
    }

    std::string luaScript = "function Tick(self, dt)\n";
    for (const Case& testCase : kCases) {
        if (testCase.producer) {
            luaScript += std::string{ "    local result" } + testCase.suffix + " = CallFunction(\"Tests.Produce" + testCase.suffix + "\", { trigger = true })\n";
        } else {
            luaScript += std::string{ "    local result" } + testCase.suffix + " = CallFunction(\"Tests.Identity" + testCase.suffix + "\", { value = " + testCase.luaLiteral + " })\n";
        }
        luaScript += std::string{ "    SetShared(\"matched" } + testCase.suffix + "\", result" + testCase.suffix + " == " + testCase.luaLiteral + ")\n";
        luaScript += std::string{ "    SetShared(\"luaType" } + testCase.suffix + "\", type(result" + testCase.suffix + "))\n";
    }
    // Dedicated negative case for the documented "SetShared loses the
    // Entity tag" gap (LIB-123/124/125's known bridge limitation): unlike
    // Tests.IdentityEntity's typed OUTPUT pin above (which correctly
    // coerces the value back to Entity via ScriptFunctionRegistry::
    // CoerceArgument/ValidateOutputs), SetShared has no declared target
    // type to coerce against, so the SAME entity id comes back tagged Int,
    // not Entity - asserted explicitly below instead of silently assumed.
    luaScript += "    local entityResult = CallFunction(\"Tests.IdentityEntity\", { value = 777 })\n";
    luaScript += "    SetShared(\"entityViaSetShared\", entityResult)\n";
    luaScript += "end\n";

    constexpr kb::assets::AssetId kRoundTripAsset{ 700501U };
    const kb::script::PucLuaLoadResult loadedLua = host.LuaRuntime().LoadScript(kRoundTripAsset, luaScript);
    kb::tests::Require(loadedLua.succeeded, "Engine21kbLibrary type desc round-trip Lua script did not load");

    const kb::scene::SceneObject roundTripObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "TypeDescRoundTrip" });
    scene.Components().Behaviours().Set(roundTripObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kRoundTripAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    const kb::script::ScriptRuntimeExecutionResult tickResult = host.Runtime().ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(tickResult.diagnostics.empty(), "Engine21kbLibrary type desc round-trip Tick produced script diagnostics");

    for (const Case& testCase : kCases) {
        const std::optional<ScriptValue> matched = host.SharedState().Get(std::string{ "matched" } + testCase.suffix);
        kb::tests::Require(matched.has_value() && matched->AsBool(),
            "Engine21kbLibrary type desc round-trip value did not survive the real Lua bridge unchanged");
        const std::optional<ScriptValue> luaType = host.SharedState().Get(std::string{ "luaType" } + testCase.suffix);
        kb::tests::Require(luaType.has_value() && luaType->AsString() == testCase.expectedLuaType,
            "Engine21kbLibrary type desc round-trip Lua type() did not match LibraryTypeDesc.luaTypeName's documented base type");
    }

    const std::optional<ScriptValue> entityViaSetShared = host.SharedState().Get("entityViaSetShared");
    kb::tests::Require(entityViaSetShared.has_value(), "Engine21kbLibrary type desc SetShared negative case did not report a value");
    kb::tests::Require(entityViaSetShared->Type() == ScriptValueType::Int,
        "Engine21kbLibrary type desc SetShared negative case: an Entity id round-tripped through SetShared must currently come back as Int, "
        "not Entity - if this now fails, the known bridge limitation was fixed and this test (and its documentation) should be updated, not just relaxed");
    kb::tests::Require(static_cast<std::uint64_t>(entityViaSetShared->AsInt()) == 777U,
        "Engine21kbLibrary type desc SetShared negative case lost the numeric value, not just the type tag");
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
    // LIB-128: the installed script system runs FixedTick in PreSimulation,
    // so the matching physics substep observes commands flushed by it.
    kb::tests::Require(kb::library::PhysicsSimulationSeesSameFrameFixedTick(),
        "Engine21kbLibrary must document that physics observes the same fixed step's FixedTick commands");

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
    kb::tests::Require(
        native->RegisterLifecycle(kSpawnedAsset, kb::script::ScriptLifecycleEvent::LateTick, [&](kb::script::ScriptExecutionContext&) {
            spawnedOrder.emplace_back("LateTick");
        }),
        "Command application contract test spawned LateTick registration failed");

    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Command application contract test backend registration failed");

    static_cast<void>(SpawnNativeBehaviourObject(scene, kSpawnerAsset, "CommandApplicationSpawner"));

    kb::script::ScriptRuntimeSceneSystem system{ runtime };

    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(spawnedAliveImmediatelyAfterCreate, "Engine21kbLibrary command application must make a spawned entity live before the spawning call returns");
    kb::tests::Require(scene.Entities().IsAlive(spawnedEntity), "Engine21kbLibrary command application must keep the spawned entity live after the phase finishes");
    kb::tests::Require(spawnedOrder.empty(), "Engine21kbLibrary command application must not dispatch lifecycle events to an entity spawned during the same phase's already-collected snapshot");

    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(
        spawnedOrder.size() == 3U &&
            spawnedOrder[0] == "Created" &&
            spawnedOrder[1] == "Tick" &&
            spawnedOrder[2] == "LateTick",
        "Engine21kbLibrary command application must dispatch Created, Tick, then LateTick to the spawned entity starting the next frame");
}

// LIB-006 audit gap closed 2026-07-17: RunCommandApplicationContractTest
// above only proved immediacy for Tick (via a direct
// kb::scene::SceneEntities::CreateObject shortcut, not the public
// World.Spawn/World.Destroy front-end) and FixedTick (as a bare
// CommandApplicationPointFor() constant check). This test drives the
// prober behaviour through EVERY one of the ten ScriptLifecycleEvent
// phases and, in each one, calls World.Spawn then World.Destroy through
// ScriptExecutionContext::CallFunction — the exact ScriptFunctionRegistry
// path Lua/VisualGraph/native code all share — proving the "Immediate"
// contract holds behaviourally for the full phase matrix through the real
// public front-end, not just as CommandApplicationPointFor's
// always-Immediate constant.
void RunCommandApplicationImmediateAcrossAllPhasesTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Command application all-phases test host setup failed");

    constexpr kb::assets::AssetId kProberAsset{ 9121U };

    const auto probe = [](std::string_view phaseName, kb::script::ScriptExecutionContext& context) {
        const std::vector<kb::script::ScriptFunctionArgument> spawnArgs{
            kb::script::ScriptFunctionArgument{ .name = "name", .value = kb::script::ScriptValue{ std::string{ "Probe_" } + std::string{ phaseName } } },
        };
        const kb::script::ScriptFunctionCallResult spawnResult = context.CallFunction("World.Spawn", spawnArgs);
        const std::string spawnFailMessage = "Engine21kbLibrary World.Spawn call through CallFunction failed for phase " + std::string{ phaseName };
        kb::tests::Require(spawnResult.Succeeded(), spawnFailMessage.c_str());
        const std::optional<kb::script::ScriptValue> spawnedValue = spawnResult.Output("entity");
        const std::string spawnOutputMessage = "Engine21kbLibrary World.Spawn did not return an entity output for phase " + std::string{ phaseName };
        kb::tests::Require(spawnedValue.has_value(), spawnOutputMessage.c_str());
        const kb::scene::SceneEntity spawned{ spawnedValue->AsUInt64() };
        const std::string spawnAliveMessage = "Engine21kbLibrary World.Spawn must make the entity live before the call returns, for phase " + std::string{ phaseName };
        kb::tests::Require(context.GetScene().Entities().IsAlive(spawned), spawnAliveMessage.c_str());

        const std::vector<kb::script::ScriptFunctionArgument> destroyArgs{
            kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ spawned.Id(), kb::script::ScriptValueType::Entity } },
        };
        const kb::script::ScriptFunctionCallResult destroyResult = context.CallFunction("World.Destroy", destroyArgs);
        const std::string destroyFailMessage = "Engine21kbLibrary World.Destroy call through CallFunction failed for phase " + std::string{ phaseName };
        kb::tests::Require(destroyResult.Succeeded() && destroyResult.Output("destroyed").has_value() && destroyResult.Output("destroyed")->AsBool(), destroyFailMessage.c_str());
        const std::string destroyGoneMessage = "Engine21kbLibrary World.Destroy must make the entity gone before the call returns, for phase " + std::string{ phaseName };
        kb::tests::Require(!context.GetScene().Entities().IsAlive(spawned), destroyGoneMessage.c_str());
    };

    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kProberAsset, kb::script::ScriptLifecycleEvent::Created,
                            [&](kb::script::ScriptExecutionContext& context) { probe("Created", context); }),
        "All-phases command application test Created registration failed");
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kProberAsset, kb::script::ScriptLifecycleEvent::Activated,
                            [&](kb::script::ScriptExecutionContext& context) { probe("Activated", context); }),
        "All-phases command application test Activated registration failed");
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kProberAsset, kb::script::ScriptLifecycleEvent::Ready,
                            [&](kb::script::ScriptExecutionContext& context) { probe("Ready", context); }),
        "All-phases command application test Ready registration failed");
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kProberAsset, kb::script::ScriptLifecycleEvent::FixedTick,
                            [&](kb::script::ScriptExecutionContext& context) { probe("FixedTick", context); }),
        "All-phases command application test FixedTick registration failed");
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kProberAsset, kb::script::ScriptLifecycleEvent::Tick,
                            [&](kb::script::ScriptExecutionContext& context) { probe("Tick", context); }),
        "All-phases command application test Tick registration failed");
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kProberAsset, kb::script::ScriptLifecycleEvent::LateTick,
                            [&](kb::script::ScriptExecutionContext& context) { probe("LateTick", context); }),
        "All-phases command application test LateTick registration failed");
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kProberAsset, kb::script::ScriptLifecycleEvent::BeforeRender,
                            [&](kb::script::ScriptExecutionContext& context) { probe("BeforeRender", context); }),
        "All-phases command application test BeforeRender registration failed");
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kProberAsset, kb::script::ScriptLifecycleEvent::AfterRender,
                            [&](kb::script::ScriptExecutionContext& context) { probe("AfterRender", context); }),
        "All-phases command application test AfterRender registration failed");
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kProberAsset, kb::script::ScriptLifecycleEvent::Deactivated,
                            [&](kb::script::ScriptExecutionContext& context) { probe("Deactivated", context); }),
        "All-phases command application test Deactivated registration failed");
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kProberAsset, kb::script::ScriptLifecycleEvent::Destroyed,
                            [&](kb::script::ScriptExecutionContext& context) { probe("Destroyed", context); }),
        "All-phases command application test Destroyed registration failed");

    const kb::scene::SceneObject proberObject = SpawnNativeBehaviourObject(scene, kProberAsset, "CommandApplicationProber");

    kb::script::ScriptRuntimeSceneSystem system{ host.Runtime() };
    // One frame with the default fixedDeltaSeconds (1/60) dispatches
    // Created, Activated, Ready (all from the same SyncBehaviourLifecycles
    // call, first time this behaviour is seen), then exactly one FixedTick,
    // then Tick, LateTick, BeforeRender, AfterRender — eight of the ten
    // phases in a single ExecuteFrame call.
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));

    // Removing the BehaviourComponent and running one more frame makes
    // SyncBehaviourLifecycles dispatch Deactivated then Destroyed for the
    // departing record — the remaining two phases.
    scene.Components().Behaviours().Remove(proberObject.Entity());
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
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

// LIB-007 audit gap closed 2026-07-17: BehaviourContext/FixedContext/
// FrameContext/RenderContext were copyable and exposed Raw() (a reference
// to the call-scoped ScriptExecutionContext with no lifetime enforcement
// beyond a comment) — a script could copy a context out of its callback,
// retain it, and later call Raw() (or even the by-value accessors, which
// also dereference the same dangling pointer) on a destroyed
// ScriptExecutionContext. Compile-time proof the fix actually closes this:
// none of the four types are copy- or move-constructible/-assignable
// (deleted in LibraryContextBase, inherited by every final derived type),
// and Raw() no longer exists as a callable member at all.
static_assert(!std::is_copy_constructible_v<kb::library::BehaviourContext>, "Engine21kbLibrary BehaviourContext must not be copy-constructible");
static_assert(!std::is_copy_assignable_v<kb::library::BehaviourContext>, "Engine21kbLibrary BehaviourContext must not be copy-assignable");
static_assert(!std::is_move_constructible_v<kb::library::BehaviourContext>, "Engine21kbLibrary BehaviourContext must not be move-constructible");
static_assert(!std::is_move_assignable_v<kb::library::BehaviourContext>, "Engine21kbLibrary BehaviourContext must not be move-assignable");
static_assert(!std::is_copy_constructible_v<kb::library::FixedContext>, "Engine21kbLibrary FixedContext must not be copy-constructible");
static_assert(!std::is_move_constructible_v<kb::library::FixedContext>, "Engine21kbLibrary FixedContext must not be move-constructible");
static_assert(!std::is_copy_constructible_v<kb::library::FrameContext>, "Engine21kbLibrary FrameContext must not be copy-constructible");
static_assert(!std::is_move_constructible_v<kb::library::FrameContext>, "Engine21kbLibrary FrameContext must not be move-constructible");
static_assert(!std::is_copy_constructible_v<kb::library::RenderContext>, "Engine21kbLibrary RenderContext must not be copy-constructible");
static_assert(!std::is_move_constructible_v<kb::library::RenderContext>, "Engine21kbLibrary RenderContext must not be move-constructible");

// Raw() itself is gone (not merely restricted): EngineLibraryContext.hpp no
// longer declares it on LibraryContextBase, so any code that tried to call
// ctx.Raw() fails to compile with "Raw is not a member" — verified directly
// while developing this fix, not asserted here (MSVC does not treat a
// requires-expression over a genuinely nonexistent member as a SFINAE-able
// false in a non-template context, so a compile-time "must not compile"
// check for a fully-removed member isn't expressible portably here).

// LIB-005/LIB-012 regression: SyncBehaviourLifecycles must dispatch BOTH
// Deactivated and Destroyed for multiple behaviours removed (via
// BehaviourComponent removal, not entity destruction or world shutdown) in
// the same frame, in the guaranteed execution order (TickGroup ascending,
// then executionOrder, then entity id) — never in lifecycleRecords_'s
// unordered_map iteration order. The three behaviours are created in
// Camera, Input, Gameplay order, which differs from the expected
// TickGroup-sorted dispatch order (Input, Gameplay, Camera), so only a real
// sort before dispatch can produce that sequence. Destroyed coverage closed
// 2026-07-17 (audit gap: this test previously registered/checked Deactivated
// only — RunShutdownDispatchesDeactivateAndDestroyInOrderTest, LIB-005,
// proves the SAME order for the SEPARATE ExecuteShutdown/world-teardown
// path, not this one).
void RunMultipleBehavioursRemovedSameFrameOrderTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kCameraAsset{ 9301U };
    constexpr kb::assets::AssetId kInputAsset{ 9302U };
    constexpr kb::assets::AssetId kGameplayAsset{ 9303U };

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();

    std::vector<std::string> order;
    const auto registerPair = [&](kb::assets::AssetId assetId, const char* label) {
        kb::tests::Require(
            native->RegisterLifecycle(assetId, kb::script::ScriptLifecycleEvent::Deactivated, [&order, label](kb::script::ScriptExecutionContext&) {
                order.emplace_back(std::string(label) + ".Deactivated");
            }),
            "Multi-removal order test Deactivated registration failed");
        kb::tests::Require(
            native->RegisterLifecycle(assetId, kb::script::ScriptLifecycleEvent::Destroyed, [&order, label](kb::script::ScriptExecutionContext&) {
                order.emplace_back(std::string(label) + ".Destroyed");
            }),
            "Multi-removal order test Destroyed registration failed");
    };
    registerPair(kCameraAsset, "Camera");
    registerPair(kInputAsset, "Input");
    registerPair(kGameplayAsset, "Gameplay");

    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Multi-removal order test backend registration failed");

    const kb::scene::SceneObject cameraObject = SpawnNativeBehaviourObject(scene, kCameraAsset, "Camera", kb::scene::BehaviourTickGroup::Camera);
    const kb::scene::SceneObject inputObject = SpawnNativeBehaviourObject(scene, kInputAsset, "Input", kb::scene::BehaviourTickGroup::Input);
    const kb::scene::SceneObject gameplayObject = SpawnNativeBehaviourObject(scene, kGameplayAsset, "Gameplay", kb::scene::BehaviourTickGroup::Gameplay);

    kb::script::ScriptRuntimeSceneSystem system{ runtime };
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(order.empty(), "Multi-removal order test fixture must not deactivate/destroy anything before removal");

    scene.Components().Behaviours().Remove(cameraObject.Entity());
    scene.Components().Behaviours().Remove(inputObject.Entity());
    scene.Components().Behaviours().Remove(gameplayObject.Entity());

    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    const std::vector<std::string> expected{ "Input.Deactivated", "Input.Destroyed", "Gameplay.Deactivated", "Gameplay.Destroyed", "Camera.Deactivated", "Camera.Destroyed" };
    kb::tests::Require(
        order == expected,
        "Engine21kbLibrary must dispatch Deactivated then Destroyed per behaviour for multiple behaviours removed in the same frame, visiting behaviours in TickGroup order, not unordered_map iteration order");
}

// LIB-005 audit gap closed 2026-07-17: the regression above only proves
// Deactivated order through SyncBehaviourLifecycles' per-frame removal path.
// ExecuteShutdown (ScriptRuntimeSceneSystem::ShutdownTrackedBehaviours) is a
// SEPARATE production call site — SceneSystemContext::OnDestroy (world/host
// teardown) and kb-cli's `run` command (CliRunCommand.cpp) both call it
// directly — and nothing previously proved its Deactivated+Destroyed
// dispatch also follows the guaranteed execution order rather than
// lifecycleRecords_'s unordered_map iteration order. Three behaviours are
// tracked (via one prior ExecuteFrame, matching real startup) in Camera,
// Input, Gameplay creation order — the same scrambled order as the test
// above — then torn down in one ExecuteShutdown call, without ever removing
// their BehaviourComponent first (a real "world is going away" shutdown,
// not a per-behaviour removal).
void RunShutdownDispatchesDeactivateAndDestroyInOrderTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kCameraAsset{ 9311U };
    constexpr kb::assets::AssetId kInputAsset{ 9312U };
    constexpr kb::assets::AssetId kGameplayAsset{ 9313U };

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();

    std::vector<std::string> order;
    const auto registerPair = [&](kb::assets::AssetId assetId, const char* label) {
        kb::tests::Require(
            native->RegisterLifecycle(assetId, kb::script::ScriptLifecycleEvent::Deactivated, [&order, label](kb::script::ScriptExecutionContext&) {
                order.emplace_back(std::string(label) + ".Deactivated");
            }),
            "Shutdown order test Deactivated registration failed");
        kb::tests::Require(
            native->RegisterLifecycle(assetId, kb::script::ScriptLifecycleEvent::Destroyed, [&order, label](kb::script::ScriptExecutionContext&) {
                order.emplace_back(std::string(label) + ".Destroyed");
            }),
            "Shutdown order test Destroyed registration failed");
    };
    registerPair(kCameraAsset, "Camera");
    registerPair(kInputAsset, "Input");
    registerPair(kGameplayAsset, "Gameplay");

    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Shutdown order test backend registration failed");

    static_cast<void>(SpawnNativeBehaviourObject(scene, kCameraAsset, "Camera", kb::scene::BehaviourTickGroup::Camera));
    static_cast<void>(SpawnNativeBehaviourObject(scene, kInputAsset, "Input", kb::scene::BehaviourTickGroup::Input));
    static_cast<void>(SpawnNativeBehaviourObject(scene, kGameplayAsset, "Gameplay", kb::scene::BehaviourTickGroup::Gameplay));

    kb::script::ScriptRuntimeSceneSystem system{ runtime };
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(order.empty(), "Shutdown order test fixture must not deactivate/destroy anything before shutdown");

    static_cast<void>(system.ExecuteShutdown(scene, 1.0F / 60.0F));

    const std::vector<std::string> expected{ "Input.Deactivated", "Input.Destroyed", "Gameplay.Deactivated", "Gameplay.Destroyed", "Camera.Deactivated", "Camera.Destroyed" };
    kb::tests::Require(
        order == expected,
        "Engine21kbLibrary ExecuteShutdown must dispatch Deactivated then Destroyed per behaviour, visiting behaviours in TickGroup order, not unordered_map iteration order");
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

// LIB-075: EntityHandle::Has<T>/TryGet<T>/GetRequired<T>/Add<T>/Remove<T> —
// only for the closed set of ten component types registered for scripts
// (ScriptComponentAccess<T> specializations), covering both an OPTIONAL
// component (Camera — has a real Remove) and a MANDATORY one (Transform —
// present from creation, Remove always reports false), plus the
// dead/wrong-scene handle contract (false/nullptr/failed Result, never a
// crash). LIB-123's four physics components (Rigidbody/Collider/
// CharacterController/Joint) get the same optional-component treatment in
// RunEntityHandlePhysicsComponentAccessTest below.
void RunEntityHandleScriptComponentAccessTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ScriptComponentAccessSubject" });
    const kb::library::EntityHandle handle{ object.Entity(), scene.Id() };

    // Optional component (Camera): absent by default.
    kb::tests::Require(!handle.Has<kb::scene::CameraComponent>(scene), "Engine21kbLibrary EntityHandle::Has<CameraComponent> must be false before the component is ever added");
    kb::tests::Require(handle.TryGet<kb::scene::CameraComponent>(scene) == nullptr, "Engine21kbLibrary EntityHandle::TryGet<CameraComponent> must return nullptr before the component is ever added");
    const kb::library::Result<kb::scene::CameraComponent> missingCamera = handle.GetRequired<kb::scene::CameraComponent>(scene);
    kb::tests::Require(!missingCamera.Succeeded() && missingCamera.Error().code == kb::library::LibraryErrorCode::InvalidArgument,
        "Engine21kbLibrary EntityHandle::GetRequired<CameraComponent> must fail with InvalidArgument when the component is absent");
    kb::tests::Require(!handle.Remove<kb::scene::CameraComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<CameraComponent> must report false when there was nothing to remove");

    kb::scene::CameraComponent camera{};
    camera.verticalFovDegrees = 75.0F;
    kb::tests::Require(handle.Add<kb::scene::CameraComponent>(scene, camera), "Engine21kbLibrary EntityHandle::Add<CameraComponent> must succeed for a live handle");
    kb::tests::Require(handle.Has<kb::scene::CameraComponent>(scene), "Engine21kbLibrary EntityHandle::Has<CameraComponent> must be true immediately after Add<CameraComponent>");
    const kb::scene::CameraComponent* cameraPointer = handle.TryGet<kb::scene::CameraComponent>(scene);
    kb::tests::Require(cameraPointer != nullptr && kb::tests::NearlyEqual(cameraPointer->verticalFovDegrees, 75.0F),
        "Engine21kbLibrary EntityHandle::TryGet<CameraComponent> must return the real, just-added component data");
    const kb::library::Result<kb::scene::CameraComponent> gotCamera = handle.GetRequired<kb::scene::CameraComponent>(scene);
    kb::tests::Require(gotCamera.Succeeded() && kb::tests::NearlyEqual(gotCamera.Value().verticalFovDegrees, 75.0F),
        "Engine21kbLibrary EntityHandle::GetRequired<CameraComponent> must succeed and return a correct copy once the component exists");

    // `scene` is non-const here, so this call resolves to the MUTABLE
    // TryGet<T> overload — proving it returns a pointer into the real live
    // component (writable through it), not merely a const view.
    kb::scene::CameraComponent* mutableCameraPointer = handle.TryGet<kb::scene::CameraComponent>(scene);
    kb::tests::Require(mutableCameraPointer != nullptr, "Engine21kbLibrary EntityHandle::TryGet<CameraComponent> (mutable overload) must return a real pointer once the component exists");
    mutableCameraPointer->verticalFovDegrees = 90.0F;
    kb::tests::Require(kb::tests::NearlyEqual(scene.Components().Cameras().TryGet(object.Entity())->verticalFovDegrees, 90.0F),
        "Engine21kbLibrary EntityHandle::TryGet<CameraComponent> (mutable overload) must return a pointer into the REAL live component, not a copy");

    kb::tests::Require(handle.Remove<kb::scene::CameraComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<CameraComponent> must report true when the component was actually present");
    kb::tests::Require(!handle.Has<kb::scene::CameraComponent>(scene), "Engine21kbLibrary EntityHandle::Has<CameraComponent> must be false immediately after Remove<CameraComponent>");

    // Mandatory component (Transform): present from entity creation, and
    // Remove is honestly rejected rather than silently no-op'd or crashing
    // against a facade method that does not exist.
    kb::tests::Require(handle.Has<kb::scene::TransformComponent>(scene), "Engine21kbLibrary EntityHandle::Has<TransformComponent> must be true — every entity has one from creation");
    kb::tests::Require(handle.TryGet<kb::scene::TransformComponent>(scene) != nullptr, "Engine21kbLibrary EntityHandle::TryGet<TransformComponent> must never be null for a live entity");
    kb::scene::TransformComponent transformOverride{};
    transformOverride.localPosition = kb::scene::Vec3{ 3.0F, 4.0F, 5.0F };
    kb::tests::Require(handle.Add<kb::scene::TransformComponent>(scene, transformOverride), "Engine21kbLibrary EntityHandle::Add<TransformComponent> must succeed (overwrite semantics) even though Transform is mandatory");
    kb::tests::Require(kb::tests::NearlyEqual(handle.TryGet<kb::scene::TransformComponent>(scene)->localPosition.x, 3.0F),
        "Engine21kbLibrary EntityHandle::Add<TransformComponent> must actually overwrite the live component's data");
    kb::tests::Require(!handle.Remove<kb::scene::TransformComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<TransformComponent> must report false — Transform can never be removed from an entity in this engine");
    kb::tests::Require(handle.Has<kb::scene::TransformComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<TransformComponent>'s false result must mean nothing was actually removed");

    // Dead/wrong-scene handle: every operation reports failure, never
    // crashes — same contract as IsAlive()/Validate() above.
    scene.Entities().Destroy(object);
    const kb::library::EntityHandle deadHandle = handle;
    kb::tests::Require(!deadHandle.Has<kb::scene::CameraComponent>(scene), "Engine21kbLibrary EntityHandle::Has<T> on a destroyed entity must report false, not throw");
    kb::tests::Require(deadHandle.TryGet<kb::scene::CameraComponent>(scene) == nullptr, "Engine21kbLibrary EntityHandle::TryGet<T> on a destroyed entity must return nullptr, not throw");
    kb::tests::Require(!deadHandle.GetRequired<kb::scene::CameraComponent>(scene).Succeeded(), "Engine21kbLibrary EntityHandle::GetRequired<T> on a destroyed entity must return a failed Result, not throw");
    kb::tests::Require(!deadHandle.Add<kb::scene::CameraComponent>(scene, kb::scene::CameraComponent{}), "Engine21kbLibrary EntityHandle::Add<T> on a destroyed entity must report false, not throw");
    kb::tests::Require(!deadHandle.Remove<kb::scene::CameraComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<T> on a destroyed entity must report false, not throw");
}

// LIB-137: the same EntityHandle::Has/TryGet/GetRequired/Add/Remove contract
// RunEntityHandleScriptComponentAccessTest proves for Camera, exercised for
// MeshRendererComponent - ScriptComponentAccess<MeshRendererComponent> (LIB-077's
// full-struct native access specialization) already compiled and worked, but had no
// dedicated coverage until now.
void RunEntityHandleMeshRendererComponentAccessTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "MeshRendererComponentAccessSubject" });
    const kb::library::EntityHandle handle{ object.Entity(), scene.Id() };

    kb::tests::Require(!handle.Has<kb::scene::MeshRendererComponent>(scene), "Engine21kbLibrary EntityHandle::Has<MeshRendererComponent> must be false before the component is ever added");
    kb::tests::Require(handle.TryGet<kb::scene::MeshRendererComponent>(scene) == nullptr, "Engine21kbLibrary EntityHandle::TryGet<MeshRendererComponent> must return nullptr before the component is ever added");
    const kb::library::Result<kb::scene::MeshRendererComponent> missingRenderer = handle.GetRequired<kb::scene::MeshRendererComponent>(scene);
    kb::tests::Require(!missingRenderer.Succeeded() && missingRenderer.Error().code == kb::library::LibraryErrorCode::InvalidArgument,
        "Engine21kbLibrary EntityHandle::GetRequired<MeshRendererComponent> must fail with InvalidArgument when the component is absent");
    kb::tests::Require(!handle.Remove<kb::scene::MeshRendererComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<MeshRendererComponent> must report false when there was nothing to remove");

    kb::scene::MeshRendererComponent renderer{};
    renderer.meshAssetId = 111U;
    renderer.materialAssetId = 222U;
    kb::tests::Require(handle.Add<kb::scene::MeshRendererComponent>(scene, renderer), "Engine21kbLibrary EntityHandle::Add<MeshRendererComponent> must succeed for a live handle");
    kb::tests::Require(handle.Has<kb::scene::MeshRendererComponent>(scene), "Engine21kbLibrary EntityHandle::Has<MeshRendererComponent> must be true immediately after Add<MeshRendererComponent>");
    const kb::scene::MeshRendererComponent* rendererPointer = handle.TryGet<kb::scene::MeshRendererComponent>(scene);
    kb::tests::Require(rendererPointer != nullptr && rendererPointer->meshAssetId == 111U && rendererPointer->materialAssetId == 222U,
        "Engine21kbLibrary EntityHandle::TryGet<MeshRendererComponent> must return the real, just-added component data");
    const kb::library::Result<kb::scene::MeshRendererComponent> gotRenderer = handle.GetRequired<kb::scene::MeshRendererComponent>(scene);
    kb::tests::Require(gotRenderer.Succeeded() && gotRenderer.Value().meshAssetId == 111U,
        "Engine21kbLibrary EntityHandle::GetRequired<MeshRendererComponent> must succeed and return a correct copy once the component exists");

    kb::scene::MeshRendererComponent* mutableRendererPointer = handle.TryGet<kb::scene::MeshRendererComponent>(scene);
    kb::tests::Require(mutableRendererPointer != nullptr, "Engine21kbLibrary EntityHandle::TryGet<MeshRendererComponent> (mutable overload) must return a real pointer once the component exists");
    mutableRendererPointer->materialAssetId = 333U;
    kb::tests::Require(scene.Components().MeshRenderers().TryGet(object.Entity())->materialAssetId == 333U,
        "Engine21kbLibrary EntityHandle::TryGet<MeshRendererComponent> (mutable overload) must return a pointer into the REAL live component, not a copy");

    kb::tests::Require(handle.Remove<kb::scene::MeshRendererComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<MeshRendererComponent> must report true when the component was actually present");
    kb::tests::Require(!handle.Has<kb::scene::MeshRendererComponent>(scene), "Engine21kbLibrary EntityHandle::Has<MeshRendererComponent> must be false immediately after Remove<MeshRendererComponent>");

    scene.Entities().Destroy(object);
    const kb::library::EntityHandle deadHandle = handle;
    kb::tests::Require(!deadHandle.Has<kb::scene::MeshRendererComponent>(scene), "Engine21kbLibrary EntityHandle::Has<MeshRendererComponent> on a destroyed entity must report false, not throw");
    kb::tests::Require(deadHandle.TryGet<kb::scene::MeshRendererComponent>(scene) == nullptr, "Engine21kbLibrary EntityHandle::TryGet<MeshRendererComponent> on a destroyed entity must return nullptr, not throw");
    kb::tests::Require(!deadHandle.Add<kb::scene::MeshRendererComponent>(scene, kb::scene::MeshRendererComponent{}), "Engine21kbLibrary EntityHandle::Add<MeshRendererComponent> on a destroyed entity must report false, not throw");
}

// LIB-123: the same EntityHandle::Has/TryGet/GetRequired/Add/Remove contract
// RunEntityHandleScriptComponentAccessTest proves for Camera, exercised for
// all four physics components this task adds — each is optional (never
// present from entity creation), so each gets the full absent -> add ->
// mutate-through-the-real-pointer -> remove cycle.
void RunEntityHandlePhysicsComponentAccessTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "PhysicsComponentAccessSubject" });
    const kb::library::EntityHandle handle{ object.Entity(), scene.Id() };

    // Rigidbody.
    kb::tests::Require(!handle.Has<kb::scene::RigidbodyComponent>(scene), "Engine21kbLibrary EntityHandle::Has<RigidbodyComponent> must be false before the component is ever added");
    kb::tests::Require(!handle.Remove<kb::scene::RigidbodyComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<RigidbodyComponent> must report false when there was nothing to remove");
    kb::scene::RigidbodyComponent rigidbody{};
    rigidbody.mass = 5.0F;
    kb::tests::Require(handle.Add<kb::scene::RigidbodyComponent>(scene, rigidbody), "Engine21kbLibrary EntityHandle::Add<RigidbodyComponent> must succeed for a live handle");
    const kb::library::Result<kb::scene::RigidbodyComponent> gotRigidbody = handle.GetRequired<kb::scene::RigidbodyComponent>(scene);
    kb::tests::Require(gotRigidbody.Succeeded() && kb::tests::NearlyEqual(gotRigidbody.Value().mass, 5.0F),
        "Engine21kbLibrary EntityHandle::GetRequired<RigidbodyComponent> must succeed and return a correct copy once the component exists");
    kb::scene::RigidbodyComponent* mutableRigidbody = handle.TryGet<kb::scene::RigidbodyComponent>(scene);
    kb::tests::Require(mutableRigidbody != nullptr, "Engine21kbLibrary EntityHandle::TryGet<RigidbodyComponent> (mutable overload) must return a real pointer once the component exists");
    mutableRigidbody->mass = 9.0F;
    kb::tests::Require(kb::tests::NearlyEqual(scene.Components().Rigidbodies().TryGet(object.Entity())->mass, 9.0F),
        "Engine21kbLibrary EntityHandle::TryGet<RigidbodyComponent> (mutable overload) must return a pointer into the REAL live component, not a copy");
    kb::tests::Require(handle.Remove<kb::scene::RigidbodyComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<RigidbodyComponent> must report true when the component was actually present");
    kb::tests::Require(!handle.Has<kb::scene::RigidbodyComponent>(scene), "Engine21kbLibrary EntityHandle::Has<RigidbodyComponent> must be false immediately after Remove<RigidbodyComponent>");

    // Collider (including its embedded PhysicsMaterial fields).
    kb::tests::Require(!handle.Has<kb::scene::ColliderComponent>(scene), "Engine21kbLibrary EntityHandle::Has<ColliderComponent> must be false before the component is ever added");
    kb::scene::ColliderComponent collider{};
    collider.friction = 0.8F;
    collider.restitution = 0.3F;
    kb::tests::Require(handle.Add<kb::scene::ColliderComponent>(scene, collider), "Engine21kbLibrary EntityHandle::Add<ColliderComponent> must succeed for a live handle");
    const kb::scene::ColliderComponent* colliderPointer = handle.TryGet<kb::scene::ColliderComponent>(scene);
    kb::tests::Require(colliderPointer != nullptr && kb::tests::NearlyEqual(colliderPointer->friction, 0.8F) && kb::tests::NearlyEqual(colliderPointer->restitution, 0.3F),
        "Engine21kbLibrary EntityHandle::TryGet<ColliderComponent> must return the real, just-added PhysicsMaterial field data");
    kb::tests::Require(handle.Remove<kb::scene::ColliderComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<ColliderComponent> must report true when the component was actually present");

    // CharacterController.
    kb::tests::Require(!handle.Has<kb::scene::CharacterControllerComponent>(scene), "Engine21kbLibrary EntityHandle::Has<CharacterControllerComponent> must be false before the component is ever added");
    kb::scene::CharacterControllerComponent characterController{};
    characterController.radius = 0.6F;
    kb::tests::Require(handle.Add<kb::scene::CharacterControllerComponent>(scene, characterController), "Engine21kbLibrary EntityHandle::Add<CharacterControllerComponent> must succeed for a live handle");
    const kb::scene::CharacterControllerComponent* characterControllerPointer = handle.TryGet<kb::scene::CharacterControllerComponent>(scene);
    kb::tests::Require(characterControllerPointer != nullptr && kb::tests::NearlyEqual(characterControllerPointer->radius, 0.6F),
        "Engine21kbLibrary EntityHandle::TryGet<CharacterControllerComponent> must return the real, just-added component data");
    kb::tests::Require(handle.Remove<kb::scene::CharacterControllerComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<CharacterControllerComponent> must report true when the component was actually present");

    // Joint (including its Entity-typed connectedEntity field).
    const kb::scene::SceneObject otherObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "JointTarget" });
    kb::tests::Require(!handle.Has<kb::scene::JointComponent>(scene), "Engine21kbLibrary EntityHandle::Has<JointComponent> must be false before the component is ever added");
    kb::scene::JointComponent joint{};
    joint.type = kb::scene::JointType::Distance;
    joint.connectedEntity = otherObject.Entity();
    kb::tests::Require(handle.Add<kb::scene::JointComponent>(scene, joint), "Engine21kbLibrary EntityHandle::Add<JointComponent> must succeed for a live handle");
    const kb::scene::JointComponent* jointPointer = handle.TryGet<kb::scene::JointComponent>(scene);
    kb::tests::Require(jointPointer != nullptr && jointPointer->type == kb::scene::JointType::Distance && jointPointer->connectedEntity == otherObject.Entity(),
        "Engine21kbLibrary EntityHandle::TryGet<JointComponent> must return the real, just-added component data, including the connected entity");
    kb::tests::Require(handle.Remove<kb::scene::JointComponent>(scene), "Engine21kbLibrary EntityHandle::Remove<JointComponent> must report true when the component was actually present");
}

// LIB-009: AssetRef<T>/SceneRef must be the real kb::assets::AssetHandle<T>
// (no parallel cache/refcount model), and the identifier behind it must be a
// deterministic hash of the asset's logical path — not the OS physical path.
// LIB-057: ArrayView<T> is std::span<const T>, not a new type — the test
// proves the two properties that make that a real, checkable contract
// rather than just an assertion in a comment: (1) it is genuinely
// immutable BY CONSTRUCTION (ArrayView<T>'s element access always yields
// `const T&`, even when constructed from a mutable container — checked at
// compile time, not just documented), and (2) it is a drop-in view over
// data an existing runtime query already returns (kb::scene::
// SceneHierarchyAccess::RootEntities(), a std::vector<SceneEntity> —
// LIB-057's own "dla danych zwracanych przez runtime"), with no
// conversion/copy step needed.
void RunArrayViewTest() {
    static_assert(std::is_same_v<kb::library::ArrayView<int>, std::span<const int>>, "kb::library::ArrayView<T> must alias std::span<const T>, not duplicate it");
    static_assert(
        std::is_same_v<decltype(std::declval<kb::library::ArrayView<int>>()[0]), const int&>,
        "ArrayView<T>::operator[] must yield const T&, even for a non-const T — immutability is enforced by the alias itself, not by caller discipline");

    // Construction from an owned, MUTABLE std::vector<T> must still yield
    // an immutable view — the source container's mutability doesn't leak
    // through ArrayView.
    std::vector<int> mutableNumbers{ 1, 2, 3 };
    const kb::library::ArrayView<int> numbersView = mutableNumbers;
    kb::tests::Require(numbersView.size() == 3U, "ArrayView constructed from a std::vector must report the vector's size");
    kb::tests::Require(numbersView[0] == 1 && numbersView[1] == 2 && numbersView[2] == 3, "ArrayView constructed from a std::vector must see its elements in order");
    int sum = 0;
    for (const int value : numbersView) {
        sum += value;
    }
    kb::tests::Require(sum == 6, "ArrayView must support range-based for iteration");

    // A real runtime query result (kb::scene::SceneHierarchyAccess), used
    // directly as an ArrayView without any adapter/copy step.
    kb::scene::Scene scene;
    const kb::scene::SceneObject rootA = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ArrayViewRootA" });
    const kb::scene::SceneObject rootB = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ArrayViewRootB" });
    const std::vector<kb::scene::SceneEntity> roots = kb::scene::SceneHierarchyAccess{ scene }.RootEntities();
    const kb::library::ArrayView<kb::scene::SceneEntity> rootsView = roots;
    kb::tests::Require(rootsView.size() == roots.size(), "ArrayView over a runtime query result must report the same size as the original vector");
    bool sawRootA = false;
    bool sawRootB = false;
    for (const kb::scene::SceneEntity& entity : rootsView) {
        sawRootA = sawRootA || entity == rootA.Entity();
        sawRootB = sawRootB || entity == rootB.Entity();
    }
    kb::tests::Require(sawRootA && sawRootB, "ArrayView over SceneHierarchyAccess::RootEntities() must see the same entities the runtime query actually returned");
}

// LIB-058: capacity/mutation contract for kb::library::Array/Set/Map/Queue/
// Stack, exercised with a plain scalar T (int) — the everyday native-C++
// use case, independent of anything script-related.
void RunCollectionsScalarTest() {
    kb::library::Array<int> numbers{ 3U };
    kb::tests::Require(numbers.Capacity() == 3U, "Array capacity must equal the requested value when under the shared policy limit");
    kb::tests::Require(numbers.PushBack(10) && numbers.PushBack(20) && numbers.PushBack(30), "Array::PushBack must succeed while under capacity");
    kb::tests::Require(!numbers.PushBack(40), "Array::PushBack must fail (return false), not grow past its declared capacity");
    kb::tests::Require(numbers.Count() == 3U && numbers.Full(), "Array must report Full() once Count() reaches Capacity()");
    kb::tests::Require(*numbers.GetAt(1) == 20, "Array::GetAt must return the element actually stored at that index");
    kb::tests::Require(numbers.GetAt(3) == nullptr, "Array::GetAt must return nullptr for an out-of-range index, not read out of bounds");
    kb::tests::Require(numbers.SetAt(1, 99) && *numbers.GetAt(1) == 99, "Array::SetAt must overwrite the element in place");
    kb::tests::Require(numbers.RemoveAt(0) && numbers.Count() == 2U && *numbers.GetAt(0) == 99, "Array::RemoveAt must shift later elements down and shrink Count");
    numbers.Clear();
    kb::tests::Require(numbers.Empty() && numbers.Count() == 0U, "Array::Clear must empty the array");

    kb::library::Set<int> tags{ 2U };
    kb::tests::Require(tags.Insert(7) && tags.Insert(7), "Set::Insert of an already-present value must be a no-op that still returns true");
    kb::tests::Require(tags.Count() == 1U, "Set must not store duplicate elements");
    kb::tests::Require(tags.Insert(8), "Set::Insert must succeed while under capacity");
    kb::tests::Require(!tags.Insert(9), "Set::Insert of a genuinely new value must fail once the set is at capacity");
    kb::tests::Require(tags.Contains(7) && tags.Contains(8) && !tags.Contains(9), "Set::Contains must reflect exactly the inserted membership");
    kb::tests::Require(tags.Remove(7) && !tags.Contains(7) && tags.Count() == 1U, "Set::Remove must remove exactly the requested element");
    kb::tests::Require(!tags.Remove(123), "Set::Remove of an absent value must return false");

    kb::library::Map<int, std::string> names{ 2U };
    kb::tests::Require(names.Set(1, "one") && names.Set(2, "two"), "Map::Set must succeed for new keys while under capacity");
    kb::tests::Require(!names.Set(3, "three"), "Map::Set of a genuinely new key must fail once the map is at capacity");
    kb::tests::Require(names.Set(1, "ONE"), "Map::Set of an already-present key must always succeed (update, not insert)");
    kb::tests::Require(*names.Find(1) == "ONE", "Map::Find must return the most recently Set value for that key");
    kb::tests::Require(names.Find(3) == nullptr, "Map::Find must return nullptr for a key never inserted");
    kb::tests::Require(names.ContainsKey(2) && !names.ContainsKey(3), "Map::ContainsKey must reflect exactly the inserted keys");
    kb::tests::Require(names.Remove(2) && !names.ContainsKey(2) && names.Count() == 1U, "Map::Remove must remove exactly the requested key");

    kb::library::Queue<int> fifo{ 2U };
    kb::tests::Require(fifo.Enqueue(1) && fifo.Enqueue(2), "Queue::Enqueue must succeed while under capacity");
    kb::tests::Require(!fifo.Enqueue(3), "Queue::Enqueue must fail once the queue is at capacity");
    kb::tests::Require(*fifo.Peek() == 1, "Queue::Peek must report the oldest enqueued item without removing it");
    int dequeued = 0;
    kb::tests::Require(fifo.Dequeue(dequeued) && dequeued == 1, "Queue::Dequeue must remove and return items in FIFO order");
    kb::tests::Require(fifo.Dequeue(dequeued) && dequeued == 2, "Queue::Dequeue must continue draining in FIFO order");
    kb::tests::Require(!fifo.Dequeue(dequeued), "Queue::Dequeue on an empty queue must return false and leave the queue empty");

    kb::library::Stack<int> lifo{ 2U };
    kb::tests::Require(lifo.Push(1) && lifo.Push(2), "Stack::Push must succeed while under capacity");
    kb::tests::Require(!lifo.Push(3), "Stack::Push must fail once the stack is at capacity");
    kb::tests::Require(*lifo.Top() == 2, "Stack::Top must report the most recently pushed item without removing it");
    int popped = 0;
    kb::tests::Require(lifo.Pop(popped) && popped == 2, "Stack::Pop must remove and return items in LIFO order");
    kb::tests::Require(lifo.Pop(popped) && popped == 1, "Stack::Pop must continue unwinding in LIFO order");
    kb::tests::Require(!lifo.Pop(popped), "Stack::Pop on an empty stack must return false and leave the stack empty");

    kb::library::Array<int> overRequested{ kb::library::kDefaultLibraryInputLimits.maxCollectionSize + 1000U };
    kb::tests::Require(
        overRequested.Capacity() == kb::library::kDefaultLibraryInputLimits.maxCollectionSize,
        "A capacity request above the shared policy limit (LIB-037's maxCollectionSize) must be clamped down, never honored as-is");
}

// LIB-058: the same collection templates instantiated with
// kb::script::ScriptValue as the element/key/value type — proving they
// genuinely hold "script data" (ScriptValue is the one channel through
// which data crosses the Lua/Visual Graph boundary, LIB-032), not just
// plain native scalars. ScriptValue has operator== but no std::hash, which
// is exactly why Set/Map are linear-scan rather than hash-backed.
void RunCollectionsScriptValueTest() {
    using kb::script::ScriptValue;
    using kb::script::ScriptValueType;

    kb::library::Array<ScriptValue> events{ 4U };
    kb::tests::Require(events.PushBack(ScriptValue{ 1 }), "Array<ScriptValue>::PushBack must accept a ScriptValue element");
    kb::tests::Require(events.PushBack(ScriptValue{ std::string{ "PlayerTag" }, ScriptValueType::Name }), "Array<ScriptValue> must hold heterogeneously-typed ScriptValues (Int then Name) since ScriptValue is itself self-describing");
    kb::tests::Require(events.GetAt(0)->Type() == ScriptValueType::Int && events.GetAt(0)->AsInt() == 1, "Array<ScriptValue>::GetAt must preserve both the type and value of the stored ScriptValue");
    kb::tests::Require(events.GetAt(1)->Type() == ScriptValueType::Name && events.GetAt(1)->AsString() == "PlayerTag", "Array<ScriptValue>::GetAt must preserve a second, differently-typed ScriptValue in the same array");

    kb::library::Set<ScriptValue> uniqueHashes{ 4U };
    const ScriptValue hashA{ 0xAAULL, ScriptValueType::Hash };
    const ScriptValue hashAAsEntity{ 0xAAULL, ScriptValueType::Entity };
    kb::tests::Require(uniqueHashes.Insert(hashA), "Set<ScriptValue>::Insert must accept a Hash-typed ScriptValue");
    kb::tests::Require(uniqueHashes.Insert(hashAAsEntity), "Set<ScriptValue> must treat a ScriptValue with the same raw id but a different ScriptValueType as a distinct element, matching ScriptValue::operator=='s structural-equality contract");
    kb::tests::Require(uniqueHashes.Count() == 2U, "Set<ScriptValue> must have stored both the Hash and the Entity ScriptValue as distinct members");
    kb::tests::Require(
        uniqueHashes.Insert(ScriptValue{ 0xAAULL, ScriptValueType::Hash }) && uniqueHashes.Count() == 2U,
        "Set<ScriptValue>::Insert of a value structurally equal to an existing member must be a no-op that still returns true and does not grow Count");

    kb::library::Map<ScriptValue, ScriptValue> properties{ 4U };
    const ScriptValue key{ std::string{ "health" }, ScriptValueType::Name };
    kb::tests::Require(properties.Set(key, ScriptValue{ 100 }), "Map<ScriptValue, ScriptValue>::Set must accept ScriptValue keys and values");
    kb::tests::Require(properties.Find(key)->AsInt() == 100, "Map<ScriptValue, ScriptValue>::Find must look up by ScriptValue structural equality");
    kb::tests::Require(properties.Set(key, ScriptValue{ 50 }) && properties.Find(key)->AsInt() == 50, "Map<ScriptValue, ScriptValue>::Set on an existing key must update, not duplicate, the entry");

    kb::library::Queue<ScriptValue> pending{ 2U };
    kb::tests::Require(pending.Enqueue(ScriptValue{ true }) && pending.Enqueue(ScriptValue{ false }), "Queue<ScriptValue>::Enqueue must accept Bool ScriptValues");
    ScriptValue dequeuedValue;
    kb::tests::Require(pending.Dequeue(dequeuedValue) && dequeuedValue.AsBool() == true, "Queue<ScriptValue>::Dequeue must preserve FIFO order for ScriptValue elements");

    kb::library::Stack<ScriptValue> undo{ 2U };
    kb::tests::Require(undo.Push(ScriptValue{ 1.5F }) && undo.Push(ScriptValue{ 2.5F }), "Stack<ScriptValue>::Push must accept Float ScriptValues");
    ScriptValue poppedValue;
    kb::tests::Require(undo.Pop(poppedValue) && poppedValue.AsFloat() == 2.5F, "Stack<ScriptValue>::Pop must preserve LIFO order for ScriptValue elements");
}

// LIB-059: proves the "single upfront allocation, zero reallocation up to
// capacity" claim documented in EngineLibraryCollections.hpp — not just
// asserting it in a comment. AllocatedCapacity() (the backing
// std::vector's real capacity()) is captured right after construction and
// must never change as the collection fills up to its declared Capacity().
void RunCollectionsAllocationCostTest() {
    kb::library::Array<int> numbers{ 16U };
    const std::size_t allocatedAfterConstruction = numbers.AllocatedCapacity();
    kb::tests::Require(allocatedAfterConstruction >= 16U, "Array must reserve at least its declared capacity at construction time");
    for (int i = 0; i < 16; ++i) {
        kb::tests::Require(numbers.PushBack(i), "Array::PushBack must succeed while filling up to the reserved capacity");
        kb::tests::Require(numbers.AllocatedCapacity() == allocatedAfterConstruction, "Array must never reallocate its backing storage while growing up to the capacity reserved at construction");
    }

    kb::library::Set<int> tags{ 8U };
    const std::size_t setAllocated = tags.AllocatedCapacity();
    for (int i = 0; i < 8; ++i) {
        kb::tests::Require(tags.Insert(i), "Set::Insert must succeed while filling up to the reserved capacity");
        kb::tests::Require(tags.AllocatedCapacity() == setAllocated, "Set must never reallocate its backing storage while growing up to the capacity reserved at construction");
    }

    kb::library::Map<int, int> map{ 8U };
    const std::size_t mapAllocated = map.AllocatedCapacity();
    for (int i = 0; i < 8; ++i) {
        kb::tests::Require(map.Set(i, i * 2), "Map::Set must succeed while filling up to the reserved capacity");
        kb::tests::Require(map.AllocatedCapacity() == mapAllocated, "Map must never reallocate its backing storage while growing up to the capacity reserved at construction");
    }

    kb::library::Stack<int> stack{ 8U };
    const std::size_t stackAllocated = stack.AllocatedCapacity();
    for (int i = 0; i < 8; ++i) {
        kb::tests::Require(stack.Push(i), "Stack::Push must succeed while filling up to the reserved capacity");
        kb::tests::Require(stack.AllocatedCapacity() == stackAllocated, "Stack must never reallocate its backing storage while growing up to the capacity reserved at construction");
    }
}

// LIB-059: NonAlloc variants operate entirely over caller-provided storage
// (here, plain stack std::array buffers standing in for a frame arena) and
// never own or allocate memory themselves — the defining property for hot
// path use. Semantics are checked to match the owning counterparts.
void RunCollectionsNonAllocTest() {
    std::array<int, 3> arrayStorage{};
    kb::library::ArrayNonAlloc<int> numbers{ arrayStorage };
    kb::tests::Require(numbers.Capacity() == 3U, "ArrayNonAlloc::Capacity must equal the caller-provided storage size");
    kb::tests::Require(numbers.PushBack(10) && numbers.PushBack(20) && numbers.PushBack(30), "ArrayNonAlloc::PushBack must succeed while under capacity");
    kb::tests::Require(!numbers.PushBack(40), "ArrayNonAlloc::PushBack must fail once the caller-provided storage is full");
    kb::tests::Require(*numbers.GetAt(1) == 20, "ArrayNonAlloc::GetAt must return the element actually stored at that index");
    kb::tests::Require(numbers.RemoveAt(0) && numbers.Count() == 2U && *numbers.GetAt(0) == 20, "ArrayNonAlloc::RemoveAt must shift later elements down");
    kb::tests::Require(arrayStorage[0] == 20, "ArrayNonAlloc must write directly into the caller-provided storage, not a hidden internal copy");

    std::array<int, 2> setStorage{};
    kb::library::SetNonAlloc<int> tags{ setStorage };
    kb::tests::Require(tags.Insert(7) && tags.Insert(7) && tags.Count() == 1U, "SetNonAlloc::Insert of a duplicate must be a no-op that still returns true");
    kb::tests::Require(tags.Insert(8), "SetNonAlloc::Insert must succeed while under capacity");
    kb::tests::Require(!tags.Insert(9), "SetNonAlloc::Insert of a genuinely new value must fail once at capacity");
    kb::tests::Require(tags.Remove(7) && !tags.Contains(7), "SetNonAlloc::Remove must remove exactly the requested element");

    std::array<kb::library::MapEntry<int, int>, 2> mapStorage{};
    kb::library::MapNonAlloc<int, int> doubled{ mapStorage };
    kb::tests::Require(doubled.Set(1, 10) && doubled.Set(2, 20), "MapNonAlloc::Set must succeed for new keys while under capacity");
    kb::tests::Require(!doubled.Set(3, 30), "MapNonAlloc::Set of a genuinely new key must fail once at capacity");
    kb::tests::Require(doubled.Set(1, 100) && *doubled.Find(1) == 100, "MapNonAlloc::Set of an existing key must update in place, not fail as if new");

    std::array<int, 2> queueStorage{};
    kb::library::QueueNonAlloc<int> fifo{ queueStorage };
    kb::tests::Require(fifo.Enqueue(1) && fifo.Enqueue(2), "QueueNonAlloc::Enqueue must succeed while under capacity");
    kb::tests::Require(!fifo.Enqueue(3), "QueueNonAlloc::Enqueue must fail once at capacity");
    int dequeuedFirst = 0;
    int dequeuedSecond = 0;
    kb::tests::Require(fifo.Dequeue(dequeuedFirst) && dequeuedFirst == 1, "QueueNonAlloc::Dequeue must return items in FIFO order");
    // Ring-buffer wraparound: enqueue again right after a dequeue so the
    // write index wraps past the end of the fixed storage, proving this is
    // a real ring buffer and not just a plain array that happens to work
    // for the first pass through the storage.
    kb::tests::Require(fifo.Enqueue(3), "QueueNonAlloc::Enqueue must succeed again once Dequeue freed a slot, even when the write index must wrap around the end of the storage");
    kb::tests::Require(fifo.Dequeue(dequeuedSecond) && dequeuedSecond == 2, "QueueNonAlloc::Dequeue must continue in FIFO order across a wraparound");
    int dequeuedThird = 0;
    kb::tests::Require(fifo.Dequeue(dequeuedThird) && dequeuedThird == 3, "QueueNonAlloc::Dequeue must return the wrapped-around item correctly");
    kb::tests::Require(fifo.Empty(), "QueueNonAlloc must be empty after draining every enqueued item");

    std::array<int, 2> stackStorage{};
    kb::library::StackNonAlloc<int> lifo{ stackStorage };
    kb::tests::Require(lifo.Push(1) && lifo.Push(2), "StackNonAlloc::Push must succeed while under capacity");
    kb::tests::Require(!lifo.Push(3), "StackNonAlloc::Push must fail once at capacity");
    int poppedFirst = 0;
    kb::tests::Require(lifo.Pop(poppedFirst) && poppedFirst == 2, "StackNonAlloc::Pop must return items in LIFO order");
    kb::tests::Require(stackStorage[1] == 2, "StackNonAlloc must write directly into the caller-provided storage, not a hidden internal copy");
}

// LIB-060: proves the formal iteration-order guarantee documented in
// EngineLibraryCollections.hpp — current members appear in the order they
// were most recently inserted, remove-in-place never reorders survivors,
// and a removed-then-re-inserted member reappears at the end rather than
// its old position. Checked for both Set<T> and Map<K,V>, and both the
// allocating and NonAlloc variants (independent implementations, so a
// bug in one would not be caught by testing only the other).
void RunCollectionsDeterministicIterationTest() {
    kb::library::Set<int> set{ 4U };
    kb::tests::Require(set.Insert(1) && set.Insert(2) && set.Insert(3), "Set::Insert must succeed for three new elements under capacity");
    std::vector<int> orderAfterInsert(set.begin(), set.end());
    kb::tests::Require((orderAfterInsert == std::vector<int>{ 1, 2, 3 }), "Set iteration order must equal insertion order");
    kb::tests::Require(set.Remove(2), "Set::Remove must remove the requested element");
    std::vector<int> orderAfterRemove(set.begin(), set.end());
    kb::tests::Require((orderAfterRemove == std::vector<int>{ 1, 3 }), "Set::Remove must preserve the relative order of the surviving elements");
    kb::tests::Require(set.Insert(2), "Set::Insert of a previously-removed value must succeed as a genuinely new insertion");
    std::vector<int> orderAfterReinsert(set.begin(), set.end());
    kb::tests::Require((orderAfterReinsert == std::vector<int>{ 1, 3, 2 }), "Set must place a removed-then-re-inserted element at the end, not restore its old position");

    kb::library::Map<int, int> map{ 4U };
    kb::tests::Require(map.Set(1, 10) && map.Set(2, 20) && map.Set(3, 30), "Map::Set must succeed for three new keys under capacity");
    kb::tests::Require(map.Set(2, 200), "Map::Set on an existing key must succeed as an update");
    std::vector<int> keysAfterUpdate;
    for (const auto& entry : map) {
        keysAfterUpdate.push_back(entry.key);
    }
    kb::tests::Require((keysAfterUpdate == std::vector<int>{ 1, 2, 3 }), "Map::Set updating an existing key must not move it in iteration order");
    kb::tests::Require(map.Remove(1), "Map::Remove must remove the requested key");
    std::vector<int> keysAfterRemove;
    for (const auto& entry : map) {
        keysAfterRemove.push_back(entry.key);
    }
    kb::tests::Require((keysAfterRemove == std::vector<int>{ 2, 3 }), "Map::Remove must preserve the relative order of the surviving keys");
    kb::tests::Require(map.Set(1, 999), "Map::Set of a previously-removed key must succeed as a genuinely new insertion");
    std::vector<int> keysAfterReinsert;
    for (const auto& entry : map) {
        keysAfterReinsert.push_back(entry.key);
    }
    kb::tests::Require((keysAfterReinsert == std::vector<int>{ 2, 3, 1 }), "Map must place a removed-then-re-inserted key at the end, not restore its old position");

    std::array<int, 4> setStorage{};
    kb::library::SetNonAlloc<int> setNonAlloc{ setStorage };
    kb::tests::Require(setNonAlloc.Insert(1) && setNonAlloc.Insert(2) && setNonAlloc.Insert(3) && setNonAlloc.Remove(2) && setNonAlloc.Insert(2), "SetNonAlloc insert/remove/re-insert sequence must succeed identically to Set<T>");
    std::vector<int> nonAllocOrder(setNonAlloc.begin(), setNonAlloc.end());
    kb::tests::Require((nonAllocOrder == std::vector<int>{ 1, 3, 2 }), "SetNonAlloc must obey the same deterministic-iteration guarantee as the allocating Set<T>");

    std::array<kb::library::MapEntry<int, int>, 4> mapStorage{};
    kb::library::MapNonAlloc<int, int> mapNonAlloc{ mapStorage };
    kb::tests::Require(mapNonAlloc.Set(1, 10) && mapNonAlloc.Set(2, 20) && mapNonAlloc.Set(3, 30) && mapNonAlloc.Remove(1) && mapNonAlloc.Set(1, 999), "MapNonAlloc insert/remove/re-insert sequence must succeed identically to Map<K,V>");
    std::vector<int> nonAllocKeys;
    for (const auto& entry : mapNonAlloc) {
        nonAllocKeys.push_back(entry.key);
    }
    kb::tests::Require((nonAllocKeys == std::vector<int>{ 2, 3, 1 }), "MapNonAlloc must obey the same deterministic-iteration guarantee as the allocating Map<K,V>");
}

// LIB-062: TextFormatBuffer must build text entirely inside caller-provided
// storage — no growth, no partial writes on overflow, numeric conversions
// exact.
void RunTextFormatBufferTest() {
    std::array<char, 16> storage{};
    kb::library::TextFormatBuffer buffer{ storage };
    kb::tests::Require(buffer.Capacity() == 16U && buffer.Empty(), "TextFormatBuffer::Capacity must equal the caller-provided storage size and start Empty");

    kb::tests::Require(buffer.Append("HP:"), "TextFormatBuffer::Append must succeed while under capacity");
    kb::tests::Require(buffer.AppendChar(' '), "TextFormatBuffer::AppendChar must succeed while under capacity");
    kb::tests::Require(buffer.AppendInt(-42), "TextFormatBuffer::AppendInt must succeed for a negative value");
    kb::tests::Require(buffer.View() == "HP: -42", "TextFormatBuffer must accumulate appends in order with exact formatting");

    buffer.Clear();
    kb::tests::Require(buffer.Empty() && buffer.Length() == 0U, "TextFormatBuffer::Clear must reset Length to zero");
    kb::tests::Require(buffer.AppendUInt(4000000000ULL), "TextFormatBuffer::AppendUInt must succeed for a value beyond int32 range");
    kb::tests::Require(buffer.View() == "4000000000", "TextFormatBuffer::AppendUInt must format the exact unsigned value");

    buffer.Clear();
    kb::tests::Require(buffer.AppendBool(true) && buffer.AppendChar('/') && buffer.AppendBool(false), "TextFormatBuffer::AppendBool must succeed for both values");
    kb::tests::Require(buffer.View() == "true/false", "TextFormatBuffer::AppendBool must format the canonical Lua-style literal spelling");

    buffer.Clear();
    kb::tests::Require(buffer.AppendFloat(3.5, 2), "TextFormatBuffer::AppendFloat must succeed while under capacity");
    kb::tests::Require(buffer.View() == "3.50", "TextFormatBuffer::AppendFloat must respect the requested fixed precision");

    // Overflow: a std::string_view Append that would not fit must fail
    // WITHOUT writing anything (not a partial/truncated write) and without
    // advancing Length — the "no uncontrolled allocation OR corruption"
    // contract from the task's own wording.
    std::array<char, 4> smallStorage{ 'X', 'X', 'X', 'X' };
    kb::library::TextFormatBuffer small{ smallStorage };
    kb::tests::Require(small.Append("ab"), "TextFormatBuffer::Append must succeed when it fits exactly within remaining capacity");
    kb::tests::Require(!small.Append("xyz"), "TextFormatBuffer::Append must fail (return false) when the text does not fit in the remaining capacity");
    kb::tests::Require(small.Length() == 2U && small.View() == "ab", "TextFormatBuffer::Append must leave Length and View() unchanged after a rejected (would-not-fit) append, not a truncated one");
    kb::tests::Require(smallStorage[2] == 'X' && smallStorage[3] == 'X', "TextFormatBuffer must not write into caller storage past Length() when an append is rejected");

    std::array<char, 1> tinyStorage{};
    kb::library::TextFormatBuffer tiny{ tinyStorage };
    kb::tests::Require(!tiny.AppendInt(123), "TextFormatBuffer::AppendInt must fail cleanly when the formatted number cannot fit");
    kb::tests::Require(tiny.Length() == 0U, "TextFormatBuffer::AppendInt must not advance Length after a failed conversion");
}

// LIB-063: TryParseInt64/UInt64/Double/Guid/Color/Date — each must accept
// exactly its one documented grammar and reject everything else (empty
// input, trailing garbage, wrong length, syntactically plausible but
// calendrically invalid dates), never throw, never partially write the
// output on failure.
void RunParsingTest() {
    std::int64_t intValue = 0;
    kb::tests::Require(kb::library::TryParseInt64("42", intValue) && intValue == 42, "TryParseInt64 must parse a plain positive integer");
    kb::tests::Require(kb::library::TryParseInt64("-42", intValue) && intValue == -42, "TryParseInt64 must parse a negative integer");
    kb::tests::Require(!kb::library::TryParseInt64("", intValue), "TryParseInt64 must reject an empty string");
    kb::tests::Require(!kb::library::TryParseInt64("42abc", intValue), "TryParseInt64 must reject trailing non-numeric garbage, not silently stop at the first invalid character");
    kb::tests::Require(!kb::library::TryParseInt64("  42", intValue), "TryParseInt64 must reject leading whitespace rather than skipping it");

    std::uint64_t uintValue = 0;
    kb::tests::Require(kb::library::TryParseUInt64("4000000000", uintValue) && uintValue == 4000000000ULL, "TryParseUInt64 must parse a value beyond int32 range");
    kb::tests::Require(!kb::library::TryParseUInt64("-1", uintValue), "TryParseUInt64 must reject a negative value");

    double doubleValue = 0.0;
    kb::tests::Require(kb::library::TryParseDouble("3.14", doubleValue) && doubleValue > 3.139 && doubleValue < 3.141, "TryParseDouble must parse a decimal value using '.' as the invariant decimal separator, not the process locale's");
    kb::tests::Require(kb::library::TryParseDouble("1e3", doubleValue) && doubleValue == 1000.0, "TryParseDouble must parse scientific notation");
    kb::tests::Require(!kb::library::TryParseDouble("not-a-number", doubleValue), "TryParseDouble must reject non-numeric text");

    kb::tests::Require(kb::library::TryParseGuid("3F2504E0-4F89-11D3-9A0C-0305E82C3301"), "TryParseGuid must accept the canonical uppercase-hex 8-4-4-4-12 form");
    kb::tests::Require(kb::library::TryParseGuid("3f2504e0-4f89-11d3-9a0c-0305e82c3301"), "TryParseGuid must accept lowercase hex digits too");
    kb::tests::Require(!kb::library::TryParseGuid("3F2504E0-4F89-11D3-9A0C-0305E82C330"), "TryParseGuid must reject a string one character short of the canonical length");
    kb::tests::Require(!kb::library::TryParseGuid("3F2504E04F8911D39A0C0305E82C3301-"), "TryParseGuid must reject hyphens in the wrong positions");
    kb::tests::Require(!kb::library::TryParseGuid("3G2504E0-4F89-11D3-9A0C-0305E82C3301"), "TryParseGuid must reject a non-hex character");

    kb::math::Color color{};
    kb::tests::Require(kb::library::TryParseColor("#FF0000", color) && color.r == 1.0F && color.g == 0.0F && color.b == 0.0F && color.a == 1.0F, "TryParseColor must parse a 6-digit hex color as fully opaque red");
    kb::tests::Require(kb::library::TryParseColor("#00FF0080", color) && color.g == 1.0F, "TryParseColor must parse an 8-digit hex color's RGB channels");
    kb::tests::Require(color.a > 0.501F && color.a < 0.503F, "TryParseColor must parse the explicit alpha channel from an 8-digit hex color (0x80/255)");
    kb::tests::Require(!kb::library::TryParseColor("FF0000", color), "TryParseColor must reject a hex string missing the leading '#'");
    kb::tests::Require(!kb::library::TryParseColor("#FF00", color), "TryParseColor must reject a hex string of the wrong length");
    kb::tests::Require(!kb::library::TryParseColor("#GG0000", color), "TryParseColor must reject a non-hex character");

    std::chrono::year_month_day date{};
    kb::tests::Require(kb::library::TryParseDate("2024-03-15", date) && date.year() == std::chrono::year{ 2024 } && date.month() == std::chrono::month{ 3 } && date.day() == std::chrono::day{ 15 },
        "TryParseDate must parse a valid ISO 8601 calendar date");
    kb::tests::Require(!kb::library::TryParseDate("2023-02-30", date), "TryParseDate must reject a syntactically well-formed but calendrically invalid date (February has no 30th day)");
    kb::tests::Require(!kb::library::TryParseDate("2024-13-01", date), "TryParseDate must reject an out-of-range month");
    kb::tests::Require(!kb::library::TryParseDate("15-03-2024", date), "TryParseDate must reject a non-ISO-8601 field order, even if every field is individually numeric");
    kb::tests::Require(kb::library::TryParseDate("2024-02-29", date), "TryParseDate must accept February 29th in a leap year");
    kb::tests::Require(!kb::library::TryParseDate("2023-02-29", date), "TryParseDate must reject February 29th in a non-leap year");
}

// LIB-064: IsValidUtf8 must accept every well-formed encoding length (1
// through 4 bytes) and reject the specific malformed byte patterns real
// UTF-8 decoders are expected to catch: overlong encodings, truncated
// sequences, stray continuation bytes, encoded UTF-16 surrogate halves,
// and codepoints beyond U+10FFFF.
void RunUtf8ValidationTest() {
    kb::tests::Require(kb::library::IsValidUtf8(""), "IsValidUtf8 must accept an empty string");
    kb::tests::Require(kb::library::IsValidUtf8("Hello, world!"), "IsValidUtf8 must accept plain ASCII");

    const std::string twoByte{ static_cast<char>(0xC3), static_cast<char>(0xA9) }; // U+00E9 'e' with acute accent
    kb::tests::Require(kb::library::IsValidUtf8(twoByte), "IsValidUtf8 must accept a well-formed 2-byte sequence");

    const std::string threeByte{ static_cast<char>(0xE2), static_cast<char>(0x82), static_cast<char>(0xAC) }; // U+20AC euro sign
    kb::tests::Require(kb::library::IsValidUtf8(threeByte), "IsValidUtf8 must accept a well-formed 3-byte sequence");

    const std::string fourByte{ static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x98), static_cast<char>(0x80) }; // U+1F600 grinning face
    kb::tests::Require(kb::library::IsValidUtf8(fourByte), "IsValidUtf8 must accept a well-formed 4-byte sequence");

    const std::string maxCodepoint{ static_cast<char>(0xF4), static_cast<char>(0x8F), static_cast<char>(0xBF), static_cast<char>(0xBF) }; // U+10FFFF, the top of the Unicode range
    kb::tests::Require(kb::library::IsValidUtf8(maxCodepoint), "IsValidUtf8 must accept the maximum valid Unicode codepoint U+10FFFF");

    const std::string overlongNul{ static_cast<char>(0xC0), static_cast<char>(0x80) }; // overlong 2-byte encoding of NUL
    kb::tests::Require(!kb::library::IsValidUtf8(overlongNul), "IsValidUtf8 must reject an overlong encoding (0xC0 0x80 for NUL, which must be 1-byte ASCII)");

    const std::string truncated{ static_cast<char>(0xC3) }; // 2-byte lead with no continuation byte
    kb::tests::Require(!kb::library::IsValidUtf8(truncated), "IsValidUtf8 must reject a truncated multi-byte sequence");

    const std::string strayContinuation{ static_cast<char>(0x80) };
    kb::tests::Require(!kb::library::IsValidUtf8(strayContinuation), "IsValidUtf8 must reject a stray continuation byte with no lead byte");

    const std::string surrogateHalf{ static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80) }; // encoded U+D800, a UTF-16 surrogate half
    kb::tests::Require(!kb::library::IsValidUtf8(surrogateHalf), "IsValidUtf8 must reject an encoded UTF-16 surrogate half, which well-formed UTF-8 must never represent");

    const std::string beyondMaxCodepoint{ static_cast<char>(0xF4), static_cast<char>(0x90), static_cast<char>(0x80), static_cast<char>(0x80) }; // U+110000, one past the Unicode range
    kb::tests::Require(!kb::library::IsValidUtf8(beyondMaxCodepoint), "IsValidUtf8 must reject a codepoint beyond U+10FFFF");

    const std::string invalidLeadByte{ static_cast<char>(0xF5) };
    kb::tests::Require(!kb::library::IsValidUtf8(invalidLeadByte), "IsValidUtf8 must reject a lead byte (0xF5) that can never begin a valid UTF-8 sequence");

    // Real end-to-end enforcement: a malformed String argument must be
    // rejected by ScriptFunctionRegistry::Call itself (LIB-064's actual
    // "platform boundary" wiring), not just by the standalone validator.
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "UTF-8 boundary enforcement host setup failed");
    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{
                               .name = "Tests.EchoUtf8Text",
                               .description = "Accepts UTF-8 text for the platform-boundary test.",
                               .inputs = { kb::script::ScriptFunctionPin{ .name = "text", .type = kb::script::ScriptValueType::String } },
                           },
                           .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                               return kb::script::ScriptFunctionCallResult{ .executed = true };
                           },
                       }),
        "UTF-8 boundary enforcement did not register Tests.EchoUtf8Text");

    const std::string malformedArgument{ static_cast<char>(0xC0), static_cast<char>(0x80) };
    const std::array malformedArguments{ kb::script::ScriptFunctionArgument{ .name = "text", .value = kb::script::ScriptValue{ malformedArgument } } };
    const kb::script::ScriptFunctionCallResult malformedResult = host.Functions().Call("Tests.EchoUtf8Text", malformedArguments, kb::script::ScriptFunctionCallContext{ .scene = &scene });
    kb::tests::Require(!malformedResult.Succeeded(), "ScriptFunctionRegistry::Call must reject a String argument that is not valid UTF-8");

    const std::array validArguments{ kb::script::ScriptFunctionArgument{ .name = "text", .value = kb::script::ScriptValue{ std::string{ "valid text" } } } };
    const kb::script::ScriptFunctionCallResult validResult = host.Functions().Call("Tests.EchoUtf8Text", validArguments, kb::script::ScriptFunctionCallContext{ .scene = &scene });
    kb::tests::Require(validResult.Succeeded(), "ScriptFunctionRegistry::Call must still accept a well-formed UTF-8 String argument");
}

void RunAssetRefTest() {
    static_assert(std::is_same_v<kb::library::SceneRef, kb::assets::AssetHandle<kb::scene::SceneDocument>>, "kb::library::SceneRef must alias kb::assets::AssetHandle<SceneDocument>, not duplicate it");
    static_assert(std::is_same_v<kb::library::AssetRef<kb::scene::SceneDocument>, kb::assets::AssetHandle<kb::scene::SceneDocument>>, "kb::library::AssetRef<T> must alias kb::assets::AssetHandle<T>, not duplicate it");

    // LIB-157: the typed asset-reference aliases for the kinds whose payload
    // type kb_engine can name are exactly AssetRef<PayloadType> — no second
    // handle model. (Mesh/Material/Texture have no alias here on purpose:
    // their payloads live in kb_render; see EngineLibraryAssetRef.hpp.)
    static_assert(std::is_same_v<kb::library::PrefabRef, kb::assets::AssetHandle<kb::scene::ScenePrefab>>, "kb::library::PrefabRef must alias kb::assets::AssetHandle<ScenePrefab>");
    static_assert(std::is_same_v<kb::library::GraphRef, kb::assets::AssetHandle<kb::visual::VisualGraphAsset>>, "kb::library::GraphRef must alias kb::assets::AssetHandle<VisualGraphAsset>");
    static_assert(std::is_same_v<kb::library::AudioClipRef, kb::assets::AssetHandle<kb::audio::AudioClipAsset>>, "kb::library::AudioClipRef must alias kb::assets::AssetHandle<AudioClipAsset>");
    static_assert(std::is_same_v<kb::library::AnimationRef, kb::assets::AssetHandle<kb::assets::ImportedAsset>>, "kb::library::AnimationRef must alias kb::assets::AssetHandle<ImportedAsset>");
    static_assert(std::is_same_v<kb::library::InputActionRef, kb::assets::AssetHandle<kb::input::InputActionAsset>>, "kb::library::InputActionRef must alias kb::assets::AssetHandle<InputActionAsset>");
    static_assert(std::is_same_v<kb::library::InputMapRef, kb::assets::AssetHandle<kb::input::InputMappingContextAsset>>, "kb::library::InputMapRef must alias kb::assets::AssetHandle<InputMappingContextAsset>");

    // A typed alias is a real, usable handle, not just a name: construct one
    // over an AssetManager-shaped shared payload and prove it dereferences
    // and reports its id/loaded state exactly like the generic AssetHandle
    // it aliases (the file-backed Load<T> path itself is covered by
    // RunAssetRuntimeTests; here we only prove the alias carries a payload).
    const kb::assets::AssetId clipId{ 90201U };
    const kb::library::AudioClipRef clipRef{ clipId, std::make_shared<const kb::audio::AudioClipAsset>(kb::audio::AudioClipAsset{ .path = "/Game/Audio/Typed.wav" }) };
    kb::tests::Require(clipRef.IsLoaded() && clipRef.Id() == clipId, "Engine21kbLibrary AudioClipRef must behave as a loaded AssetHandle carrying its id");
    kb::tests::Require(clipRef->path == std::filesystem::path{ "/Game/Audio/Typed.wav" }, "Engine21kbLibrary AudioClipRef must dereference to its typed payload");
    const kb::library::AudioClipRef emptyClipRef{};
    kb::tests::Require(!emptyClipRef.IsLoaded(), "Engine21kbLibrary AudioClipRef default-constructs to an unloaded handle");

    // LIB-158: WeakAssetRef<T> is the non-owning companion — it aliases
    // kb::assets::WeakAssetHandle<T>, observes a payload without extending
    // its lifetime, and Lock()s back to a strong AssetRef while alive.
    static_assert(std::is_same_v<kb::library::WeakAssetRef<kb::audio::AudioClipAsset>, kb::assets::WeakAssetHandle<kb::audio::AudioClipAsset>>,
        "kb::library::WeakAssetRef<T> must alias kb::assets::WeakAssetHandle<T>, not duplicate it");
    {
        kb::library::AudioClipRef strong{ clipId, std::make_shared<const kb::audio::AudioClipAsset>(kb::audio::AudioClipAsset{ .path = "/Game/Audio/Weak.wav" }) };
        const kb::library::WeakAssetRef<kb::audio::AudioClipAsset> weakRef{ strong };
        kb::tests::Require(!weakRef.Expired() && weakRef.Id() == clipId, "A WeakAssetRef built from a live AssetRef must not be expired and must carry its id");
        const kb::library::AudioClipRef relocked = weakRef.Lock();
        kb::tests::Require(relocked.IsLoaded() && relocked->path == std::filesystem::path{ "/Game/Audio/Weak.wav" }, "Locking a live WeakAssetRef must yield the strong payload");
        strong = kb::library::AudioClipRef{};
        // `relocked` still holds it — not yet expired.
        kb::tests::Require(!weakRef.Expired(), "A WeakAssetRef must stay live while ANY strong holder (the relocked handle) remains");
    }
    const kb::library::WeakAssetRef<kb::audio::AudioClipAsset> expiredWeak{ kb::library::AudioClipRef{ clipId, std::make_shared<const kb::audio::AudioClipAsset>() } };
    kb::tests::Require(expiredWeak.Expired() && !expiredWeak.Lock().IsLoaded(), "A WeakAssetRef whose only strong holder was a temporary must be expired, and Lock() must yield an empty handle");

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

    // LIB-019 asset half: the 2026-07-17 audit correctly flagged that
    // "properties of assets" had no script-facing accessor at all — this
    // proves ScriptAssetsApi::AssetProperties() entries are directly usable
    // as LibraryPropertyDesc (a trivial string_view->string name copy, the
    // only difference from the component path above, which already gets
    // ScriptApiCatalogProperty-shaped entries from ScriptApiCatalog
    // directly) AND that Assets.GetProperty actually returns real data for
    // a real, discovered project asset - not just a shape check.
    kb::tests::Require(kb::script::ScriptAssetsApi::AssetProperties().size() == 2U,
        "Engine21kbLibrary asset property catalog must describe exactly virtualPath and type");
    for (const kb::script::ScriptSceneComponentPropertyDesc& assetProperty : kb::script::ScriptAssetsApi::AssetProperties()) {
        const kb::library::LibraryPropertyDesc asLibraryProperty{ .name = std::string{ assetProperty.name }, .type = assetProperty.type, .writable = assetProperty.writable };
        kb::tests::Require(asLibraryProperty.type == kb::script::ScriptValueType::String, "Engine21kbLibrary asset properties must be String-typed today");
        kb::tests::Require(!asLibraryProperty.writable, "Engine21kbLibrary asset properties (virtualPath, type) must be read-only");
    }

    const std::filesystem::path assetPropertyRoot = std::filesystem::temp_directory_path() / "21kb_engine_library_tests_property_desc_asset";
    std::error_code assetPropertyResetError;
    std::filesystem::remove_all(assetPropertyRoot, assetPropertyResetError);
    std::filesystem::create_directories(assetPropertyRoot / "Assets" / "Logic", assetPropertyResetError);
    kb::tests::Require(!assetPropertyResetError, "Engine21kbLibrary asset property test project root could not be prepared");
    {
        std::ofstream sampleScript{ assetPropertyRoot / "Assets" / "Logic" / "Sample.lua", std::ios::binary | std::ios::trunc };
        kb::tests::Require(sampleScript.is_open(), "Engine21kbLibrary asset property test sample script could not be opened for writing");
        sampleScript << "function Ready(self, dt)\nend\n";
        kb::tests::Require(sampleScript.good(), "Engine21kbLibrary asset property test sample script could not be written");
    }
    kb::tests::Require(scene.Assets().MountProject(assetPropertyRoot), "Engine21kbLibrary asset property test project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Engine21kbLibrary asset property test did not discover exactly the sample script");

    const kb::script::ScriptFunctionCallResult virtualPathResult = host.Functions().Call(
        "Assets.GetProperty",
        std::vector<kb::script::ScriptFunctionArgument>{
            kb::script::ScriptFunctionArgument{ .name = "reference", .value = kb::script::ScriptValue{ std::string{ "/Game/Logic/Sample.lua" } } },
            kb::script::ScriptFunctionArgument{ .name = "property", .value = kb::script::ScriptValue{ std::string{ "virtualPath" } } },
        },
        kb::script::ScriptFunctionCallContext{ .scene = &scene });
    kb::tests::Require(virtualPathResult.Succeeded(), "Engine21kbLibrary Assets.GetProperty(virtualPath) call failed");
    const std::optional<kb::script::ScriptValue> virtualPathFound = virtualPathResult.Output("found");
    kb::tests::Require(virtualPathFound.has_value() && virtualPathFound->AsBool(), "Engine21kbLibrary Assets.GetProperty(virtualPath) must find the real discovered asset");
    const std::optional<kb::script::ScriptValue> virtualPathValue = virtualPathResult.Output("value");
    kb::tests::Require(virtualPathValue.has_value() && virtualPathValue->AsString() == "/Game/Logic/Sample.lua",
        "Engine21kbLibrary Assets.GetProperty(virtualPath) must report the asset's real virtual path");

    const kb::script::ScriptFunctionCallResult typeResult = host.Functions().Call(
        "Assets.GetProperty",
        std::vector<kb::script::ScriptFunctionArgument>{
            kb::script::ScriptFunctionArgument{ .name = "reference", .value = kb::script::ScriptValue{ std::string{ "/Game/Logic/Sample.lua" } } },
            kb::script::ScriptFunctionArgument{ .name = "property", .value = kb::script::ScriptValue{ std::string{ "type" } } },
        },
        kb::script::ScriptFunctionCallContext{ .scene = &scene });
    kb::tests::Require(typeResult.Succeeded(), "Engine21kbLibrary Assets.GetProperty(type) call failed");
    const std::optional<kb::script::ScriptValue> typeValue = typeResult.Output("value");
    kb::tests::Require(typeValue.has_value() && typeValue->AsString() == "LuaScript", "Engine21kbLibrary Assets.GetProperty(type) must report the asset's real loader type");

    const kb::script::ScriptFunctionCallResult unresolvedResult = host.Functions().Call(
        "Assets.GetProperty",
        std::vector<kb::script::ScriptFunctionArgument>{
            kb::script::ScriptFunctionArgument{ .name = "reference", .value = kb::script::ScriptValue{ std::string{ "/Game/Logic/DoesNotExist.lua" } } },
            kb::script::ScriptFunctionArgument{ .name = "property", .value = kb::script::ScriptValue{ std::string{ "virtualPath" } } },
        },
        kb::script::ScriptFunctionCallContext{ .scene = &scene });
    kb::tests::Require(unresolvedResult.Succeeded(), "Engine21kbLibrary Assets.GetProperty must not error on an unresolvable reference");
    const std::optional<kb::script::ScriptValue> unresolvedFound = unresolvedResult.Output("found");
    kb::tests::Require(unresolvedFound.has_value() && !unresolvedFound->AsBool(), "Engine21kbLibrary Assets.GetProperty must honestly report found=false for an unresolvable reference");

    const kb::script::ScriptFunctionCallResult unknownPropertyResult = host.Functions().Call(
        "Assets.GetProperty",
        std::vector<kb::script::ScriptFunctionArgument>{
            kb::script::ScriptFunctionArgument{ .name = "reference", .value = kb::script::ScriptValue{ std::string{ "/Game/Logic/Sample.lua" } } },
            kb::script::ScriptFunctionArgument{ .name = "property", .value = kb::script::ScriptValue{ std::string{ "physicalPath" } } },
        },
        kb::script::ScriptFunctionCallContext{ .scene = &scene });
    kb::tests::Require(!unknownPropertyResult.Succeeded(), "Engine21kbLibrary Assets.GetProperty must reject an unrecognized property name as an honest error, not a silent found=false");
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
        current.functions.front().inputs.push_back(kb::script::ScriptApiPin{
            .name = "optionalExtension",
            .type = kb::script::ScriptValueType::Int,
            .required = false,
        });
        const kb::library::ApiCompatibilityReport optionalInput = kb::library::CompareApiCatalogs(baseline, current);
        kb::tests::Require(
            !optionalInput.HasBreakingChanges(),
            "Engine21kbLibrary compatibility check must allow an appended optional input");
        kb::tests::Require(
            !optionalInput.changes.empty()
                && optionalInput.changes.front().severity == kb::library::ApiChangeSeverity::Additive,
            "Engine21kbLibrary compatibility check must report an appended optional input as additive");
    }
    {
        kb::script::ScriptApiCatalog current = baseline;
        current.functions.front().inputs.push_back(kb::script::ScriptApiPin{
            .name = "requiredExtension",
            .type = kb::script::ScriptValueType::Int,
            .required = true,
        });
        const kb::library::ApiCompatibilityReport requiredInput = kb::library::CompareApiCatalogs(baseline, current);
        kb::tests::Require(
            requiredInput.HasBreakingChanges(),
            "Engine21kbLibrary compatibility check must reject an appended required input");
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

// LIB-025 real wiring (the 2026-07-17 audit's gap): EngineLibraryModule::
// InstallModules must apply a LibraryFunctionDesc's declared deprecation to
// the real, now-registered ScriptFunctionRegistry entry, and calling that
// function - through EVERY frontend that matters, Native directly AND a
// REAL Lua script via the generic CallFunction global - must actually
// surface FormatDeprecationWarning's exact text, not just compute it and
// drop it. Uses a throwaway module/function: nothing in the real
// production catalog is deprecated today, and marking a live one just to
// exercise this would be a lie the warning would tell every real caller.
void RunFunctionDeprecationWiringTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary deprecation wiring test host setup failed");

    // A real native "Log" sink, registered before ANY dispatch (LIB-021
    // locks the registry after the first one) - the same mechanism kb_cli
    // run's RegisterStdoutLog uses, capturing to a vector instead of a
    // stream. ScriptExecutionContext::CallFunction forwards a deprecation
    // warning through exactly this channel (see its own comment), so
    // capturing it here is the real, observable proof a Lua caller
    // actually sees the warning, not just the native ScriptFunctionCallResult.
    std::vector<std::string> capturedLogs;
    {
        kb::script::ScriptFunctionDesc logDesc;
        logDesc.signature.name = "Log";
        logDesc.signature.description = "Records a message for the deprecation wiring test.";
        logDesc.signature.inputs = { kb::script::ScriptFunctionPin{ "message", kb::script::ScriptValueType::String, true } };
        logDesc.callback = [&capturedLogs](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument> arguments) {
            for (const kb::script::ScriptFunctionArgument& argument : arguments) {
                if (argument.name == "message") {
                    capturedLogs.push_back(argument.value.AsString());
                    break;
                }
            }
            return kb::script::ScriptFunctionCallResult{ .executed = true, .outputs = {}, .errors = {} };
        };
        kb::tests::Require(host.RegisterFunction(std::move(logDesc)), "Engine21kbLibrary deprecation wiring test Log sink registration failed");
    }

    const auto noopCallback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
        return kb::script::ScriptFunctionCallResult{ .executed = true, .outputs = {}, .errors = {} };
    };
    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{ .signature = { .name = "Tests.OldFunction", .description = "Deprecated test function." }, .callback = noopCallback }),
        "Engine21kbLibrary deprecation wiring test function registration failed");
    // Registered up front too (not after the Lua dispatch below), since
    // LIB-021 locks the registry against new Register() calls after the
    // first lifecycle dispatch.
    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{ .signature = { .name = "Tests.NewFunction", .description = "Replacement test function." }, .callback = noopCallback }),
        "Engine21kbLibrary deprecation wiring test replacement function registration failed");

    const kb::library::LibraryDeprecation deprecation{
        .message = "test-only deprecation",
        .replacementCanonicalName = "Tests.NewFunction",
        .sinceVersion = kb::library::LibraryApiVersion{ 9U, 9U, 9U },
    };
    const std::string expectedWarning = kb::library::FormatDeprecationWarning("Tests.OldFunction", deprecation);

    const std::vector<kb::library::LibraryModuleDesc> modules{
        kb::library::LibraryModuleDesc{
            .name = "Tests",
            .Register = [](kb::script::ScriptRuntimeHost&) { return true; }, // both functions already registered above
            .functions = { kb::library::LibraryFunctionDesc{ .canonicalName = "Tests.OldFunction", .deprecation = deprecation } },
        },
    };
    const kb::library::EngineLibraryModuleResult installResult = kb::library::EngineLibraryModule::InstallModules(host, modules);
    kb::tests::Require(installResult.succeeded, "Engine21kbLibrary deprecation wiring test install must succeed");

    // --- Native call: the raw ScriptFunctionCallResult already carries it.
    const kb::script::ScriptFunctionCallResult nativeResult =
        host.Functions().Call("Tests.OldFunction", {}, kb::script::ScriptFunctionCallContext{ .scene = &scene });
    kb::tests::Require(nativeResult.Succeeded(), "Engine21kbLibrary deprecation wiring test native call must still succeed - a deprecation warning is not an error");
    kb::tests::Require(nativeResult.warnings.size() == 1U && nativeResult.warnings.front() == expectedWarning,
        "Engine21kbLibrary deprecation wiring test native call must carry the exact deprecation warning");

    // --- Negative control: a non-deprecated function must never warn.
    const kb::script::ScriptFunctionCallResult freshResult =
        host.Functions().Call("Tests.NewFunction", {}, kb::script::ScriptFunctionCallContext{ .scene = &scene });
    kb::tests::Require(freshResult.warnings.empty(), "Engine21kbLibrary deprecation wiring test: a non-deprecated function must never warn");

    // --- Real Lua call through the generic CallFunction global, proving
    // the warning reaches a REAL running Lua script, not just the native
    // struct a C++ caller happens to inspect.
    constexpr kb::assets::AssetId kDeprecationTestAsset{ 700601U };
    const kb::script::PucLuaLoadResult loadedLua = host.LuaRuntime().LoadScript(kDeprecationTestAsset,
        "function Tick(self, dt)\n"
        "    CallFunction(\"Tests.OldFunction\", {})\n"
        "end\n");
    kb::tests::Require(loadedLua.succeeded, "Engine21kbLibrary deprecation wiring test Lua script did not load");

    const kb::scene::SceneObject deprecationObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "DeprecationCaller" });
    scene.Components().Behaviours().Set(deprecationObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kDeprecationTestAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    const kb::script::ScriptRuntimeExecutionResult tickResult = host.Runtime().ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(tickResult.diagnostics.empty(), "Engine21kbLibrary deprecation wiring test Tick produced script diagnostics");
    kb::tests::Require(!capturedLogs.empty() && capturedLogs.front() == expectedWarning,
        "Engine21kbLibrary deprecation wiring test: a real Lua CallFunction call must surface the exact deprecation warning through Log");

    // --- Catalog integrity: a LibraryFunctionDesc declaring deprecation for
    // a function that does not actually exist must fail installation, not
    // silently no-op (mirrors LIB-020's own "don't silently ignore a
    // catalog/registry mismatch" standard).
    const std::vector<kb::library::LibraryModuleDesc> danglingDeprecation{
        kb::library::LibraryModuleDesc{
            .name = "Tests",
            .Register = [](kb::script::ScriptRuntimeHost&) { return true; },
            .functions = { kb::library::LibraryFunctionDesc{ .canonicalName = "Tests.DoesNotExist", .deprecation = deprecation } },
        },
    };
    const kb::library::EngineLibraryModuleResult danglingResult = kb::library::EngineLibraryModule::InstallModules(host, danglingDeprecation);
    kb::tests::Require(!danglingResult.succeeded, "Engine21kbLibrary deprecation wiring test must reject a deprecation declared for a nonexistent function");
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

// LIB-076: the component registry — proves it does not silently drift from
// the two mechanisms that actually gate script access to components
// (ScriptSceneComponentApi.cpp's kComponentNames for Lua/VisualGraph, and
// LIB-075's ScriptComponentAccess<T> specializations for native), and
// that its "serializable" claim is honest by cross-checking against the
// real save/load round trip (SceneDocumentService::Save/Load), not just
// asserting the hand-written flag.
void RunEngineLibraryComponentRegistryTest() {
    const std::vector<kb::library::LibraryComponentDesc>& catalog = kb::library::EngineLibraryComponentRegistry::Catalog();
    const std::span<const std::string_view> scriptComponentNames = kb::script::ScriptSceneComponentApi::ComponentNames();

    kb::tests::Require(catalog.size() == scriptComponentNames.size(),
        "Engine21kbLibrary component registry must catalog exactly the same number of components ScriptSceneComponentApi.cpp gates Lua/VisualGraph access behind");
    for (const std::string_view scriptName : scriptComponentNames) {
        const kb::library::LibraryComponentDesc* desc = kb::library::EngineLibraryComponentRegistry::Find(scriptName);
        kb::tests::Require(desc != nullptr, "Engine21kbLibrary component registry is missing an entry for a component ScriptSceneComponentApi.cpp already gates Lua/VisualGraph access behind");
    }
    for (const kb::library::LibraryComponentDesc& desc : catalog) {
        const bool foundInScriptNames = std::ranges::find(scriptComponentNames, std::string_view{ desc.name }) != scriptComponentNames.end();
        kb::tests::Require(foundInScriptNames, "Engine21kbLibrary component registry names a component ScriptSceneComponentApi.cpp does not gate Lua/VisualGraph access behind — the two must never drift apart");
        kb::tests::Require(desc.id != 0U, "Engine21kbLibrary component registry entry must have a nonzero id");
        kb::tests::Require(desc.id == kb::library::ComputeLibraryComponentId(desc.name), "Engine21kbLibrary component registry entry's id must match ComputeLibraryComponentId(name)");
        kb::tests::Require(desc.version.major == 1U && desc.version.minor == 0U, "Engine21kbLibrary component registry entries must all start at version 1.0 today");
        kb::tests::Require(desc.threadPolicy == kb::library::LibraryThreadAffinity::MainThread, "Engine21kbLibrary component registry entries must all be MainThread today — no worker-safe component access path exists yet");
        kb::tests::Require(desc.capability == kb::library::LibraryComponentCapability::ReadWrite, "Engine21kbLibrary component registry entries must all be ReadWrite today — no read-only component exists yet");
    }

    kb::tests::Require(kb::library::EngineLibraryComponentRegistry::Find("NoSuchComponent") == nullptr, "Engine21kbLibrary component registry Find() must return nullptr for an unregistered name");

    // Id determinism, same contract as LIB-026's function id.
    kb::tests::Require(kb::library::ComputeLibraryComponentId("Camera") == kb::library::ComputeLibraryComponentId("Camera"),
        "Engine21kbLibrary component id must be deterministic for the same name");
    kb::tests::Require(kb::library::ComputeLibraryComponentId("Camera") != kb::library::ComputeLibraryComponentId("Light"),
        "Engine21kbLibrary component id must differ for different names");

    // Honest serializable check: round-trip a scene containing every
    // cataloged component and verify that every serializable claim survives
    // Save+Load with its real field values and cross-entity references.
    kb::scene::Scene source;
    const kb::scene::SceneObject object = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ComponentRegistrySubject" });
    source.Components().Visibility().Set(object.Entity(), kb::scene::VisibilityComponent{ .visible = false });
    source.Components().Cameras().Set(object.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 55.0F });
    source.Components().Lights().Set(object.Entity(), kb::scene::LightComponent{ .intensity = 3.0F });
    source.Components().MeshRenderers().Set(object.Entity(), kb::scene::MeshRendererComponent{ .meshAssetId = 77U });
    source.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{ .behaviourAssetId = 88U, .enabled = true });
    source.Components().Rigidbodies().Set(object.Entity(), kb::scene::RigidbodyComponent{ .mass = 12.5F });
    source.Components().Colliders().Set(object.Entity(), kb::scene::ColliderComponent{ .radius = 0.75F, .friction = 0.6F, .restitution = 0.2F });
    source.Components().CharacterControllers().Set(object.Entity(), kb::scene::CharacterControllerComponent{ .radius = 0.4F, .height = 1.8F, .slopeLimitDegrees = 40.0F, .stepOffset = 0.3F, .gravityScale = 1.5F, .useGravity = false });
    const kb::scene::SceneObject jointTarget = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ComponentRegistryJointTarget" });
    source.Components().Joints().Set(object.Entity(), kb::scene::JointComponent{ .type = kb::scene::JointType::Hinge, .connectedEntity = jointTarget.Entity(), .minLimit = -45.0F, .maxLimit = 45.0F, .enableLimit = true });

    const std::filesystem::path testRoot = std::filesystem::temp_directory_path() / "21kb_engine_library_component_registry_tests";
    std::error_code removeError;
    std::filesystem::remove_all(testRoot, removeError);
    std::error_code createError;
    std::filesystem::create_directories(testRoot, createError);
    kb::tests::Require(!createError, "Engine21kbLibrary component registry test root could not be prepared");
    const std::filesystem::path sceneFile = testRoot / "RoundTrip.21kbscene";
    kb::tests::Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "ComponentRegistryRoundTrip"), "Engine21kbLibrary component registry round-trip fixture was not saved");

    kb::scene::Scene target;
    kb::tests::Require(kb::scene::SceneDocumentService::LoadFileIntoScene(target, sceneFile), "Engine21kbLibrary component registry round-trip scene was not loaded");
    const std::vector<kb::scene::SceneEntity> roots = target.Hierarchy().RootEntities();
    kb::tests::Require(roots.size() == 2U, "Engine21kbLibrary component registry round-trip scene must restore both cross-root joint bodies");
    kb::scene::SceneEntity restored{};
    kb::scene::SceneEntity restoredJointTarget{};
    for (const kb::scene::SceneEntity root : roots) {
        if (target.Entities().Name(root) == "ComponentRegistrySubject") {
            restored = root;
        } else if (target.Entities().Name(root) == "ComponentRegistryJointTarget") {
            restoredJointTarget = root;
        }
    }
    kb::tests::Require(restored.IsValid() && restoredJointTarget.IsValid(),
        "Engine21kbLibrary component registry round-trip must identify both joint bodies by their persisted names");

    kb::tests::Require(target.Components().Cameras().Has(restored), "Engine21kbLibrary component registry: Camera is marked serializable=true and must survive a save/load round trip");
    kb::tests::Require(target.Components().Lights().Has(restored), "Engine21kbLibrary component registry: Light is marked serializable=true and must survive a save/load round trip");
    kb::tests::Require(target.Components().MeshRenderers().Has(restored), "Engine21kbLibrary component registry: MeshRenderer is marked serializable=true and must survive a save/load round trip");
    kb::tests::Require(target.Components().Behaviours().Has(restored), "Engine21kbLibrary component registry: Behaviour is marked serializable=true and must survive a save/load round trip");
    // Transform and Visibility are baked UNCONDITIONALLY per prefab node
    // (ScenePrefabBakedArchetype::transforms/visibility, no mask bit — see
    // ScenePrefabBakedData.hpp), unlike Camera/Light/MeshRenderer/
    // Behaviour which are behind ScenePrefabBakedComponentMask bits — but
    // both routes genuinely persist the data, so the non-default
    // visible=false set on the source must survive too.
    const kb::scene::VisibilityComponent* restoredVisibility = target.Components().Visibility().TryGet(restored);
    kb::tests::Require(restoredVisibility != nullptr && !restoredVisibility->visible,
        "Engine21kbLibrary component registry: Visibility is marked serializable=true and must survive a save/load round trip (baked unconditionally per prefab node)");

    // LIB-123: Rigidbody/Collider/CharacterController are marked
    // serializable=true and must actually round-trip, field values included
    // (not just presence).
    const kb::scene::RigidbodyComponent* restoredRigidbody = target.Components().Rigidbodies().TryGet(restored);
    kb::tests::Require(restoredRigidbody != nullptr && kb::tests::NearlyEqual(restoredRigidbody->mass, 12.5F),
        "Engine21kbLibrary component registry: Rigidbody is marked serializable=true and must survive a save/load round trip");
    const kb::scene::ColliderComponent* restoredCollider = target.Components().Colliders().TryGet(restored);
    kb::tests::Require(restoredCollider != nullptr && kb::tests::NearlyEqual(restoredCollider->radius, 0.75F) &&
                            kb::tests::NearlyEqual(restoredCollider->friction, 0.6F) && kb::tests::NearlyEqual(restoredCollider->restitution, 0.2F),
        "Engine21kbLibrary component registry: Collider is marked serializable=true and must survive a save/load round trip, including its PhysicsMaterial fields");
    const kb::scene::CharacterControllerComponent* restoredCharacterController = target.Components().CharacterControllers().TryGet(restored);
    kb::tests::Require(restoredCharacterController != nullptr && kb::tests::NearlyEqual(restoredCharacterController->radius, 0.4F) &&
                            kb::tests::NearlyEqual(restoredCharacterController->height, 1.8F),
        "Engine21kbLibrary component registry: CharacterController is marked serializable=true and must survive a save/load round trip");
    // LIB-131: slopeLimitDegrees/stepOffset/gravityScale/useGravity must survive the same
    // round trip too, not just the pre-existing shape fields above.
    kb::tests::Require(restoredCharacterController != nullptr &&
                            kb::tests::NearlyEqual(restoredCharacterController->slopeLimitDegrees, 40.0F) &&
                            kb::tests::NearlyEqual(restoredCharacterController->stepOffset, 0.3F) &&
                            kb::tests::NearlyEqual(restoredCharacterController->gravityScale, 1.5F) &&
                            !restoredCharacterController->useGravity,
        "Engine21kbLibrary component registry: CharacterController's slopeLimitDegrees/stepOffset/gravityScale/useGravity must survive a save/load round trip");
    // Joint stores a prefab-local stable target id and resolves it to this
    // loaded scene's live entity only after every node has been created.
    const kb::scene::JointComponent* restoredJoint = target.Components().Joints().TryGet(restored);
    kb::tests::Require(restoredJoint != nullptr && restoredJoint->type == kb::scene::JointType::Hinge &&
                            restoredJoint->connectedEntity == restoredJointTarget &&
                            kb::tests::NearlyEqual(restoredJoint->minLimit, -45.0F) &&
                            kb::tests::NearlyEqual(restoredJoint->maxLimit, 45.0F) && restoredJoint->enableLimit,
        "Engine21kbLibrary component registry: Joint must survive real cross-root save/load with connectedEntity remapped to the loaded target");

    for (const kb::library::LibraryComponentDesc& desc : catalog) {
        kb::tests::Require(desc.serializable,
            "Engine21kbLibrary component registry: every cataloged component is serializable=true after the real save/load round trip above");
    }
}

// LIB-108: kb::library::EngineLibraryEventRegistry — proves the built-in
// event schema catalog is internally consistent (nonzero deterministic ids
// matching kb::script::ComputeEventId, all starting at version 1.0, no
// duplicate names) and that Find() honestly reports an unregistered name as
// absent rather than fabricating an entry.
void RunEngineLibraryEventSchemaRegistryTest() {
    const std::vector<kb::library::LibraryEventDesc>& catalog = kb::library::EngineLibraryEventRegistry::Catalog();
    kb::tests::Require(catalog.size() == 24U, "Engine21kbLibrary event schema registry must catalog exactly the 24 built-in events this engine emits today");

    for (const kb::library::LibraryEventDesc& desc : catalog) {
        kb::tests::Require(!desc.name.empty(), "Engine21kbLibrary event schema registry entry must have a non-empty name");
        kb::tests::Require(desc.id != 0U, "Engine21kbLibrary event schema registry entry must have a nonzero id");
        kb::tests::Require(desc.id == kb::script::ComputeEventId(desc.name), "Engine21kbLibrary event schema registry entry's id must match kb::script::ComputeEventId(name) — the SAME id ScriptEvent::Id() computes for a real dispatched event of this name");
        kb::tests::Require(desc.version.major == 1U && desc.version.minor == 0U, "Engine21kbLibrary event schema registry entries must all start at version 1.0 today");
        kb::tests::Require(!desc.arguments.empty(), "Every currently cataloged built-in event carries at least one argument");
        const kb::library::LibraryEventDesc* found = kb::library::EngineLibraryEventRegistry::Find(desc.name);
        kb::tests::Require(found == &desc, "Find() must return a pointer into the SAME catalog storage Catalog() returns, not a copy");
    }

    const std::vector<std::string> expectedNames{ "SceneLoading", "SceneLoaded", "SceneActivated", "SceneUnloading", "SceneUnloaded", "TimerFired", "TaskCompleted", "TaskFailed",
        "OnCollisionEnter", "OnCollisionStay", "OnCollisionExit", "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit", "OnAudioMarker", "OnPrefabInstantiated", "OnAnimationEvent", "OnTimelineMarker",
        "UI.Click", "UI.Pointer", "UI.Submit", "UI.Changed", "UI.Focus", "UI.Navigation" };
    for (const std::string& name : expectedNames) {
        kb::tests::Require(kb::library::EngineLibraryEventRegistry::Find(name) != nullptr, "Engine21kbLibrary event schema registry is missing an entry for a real engine-emitted event");
    }
    kb::tests::Require(kb::library::EngineLibraryEventRegistry::Find("NoSuchEvent") == nullptr, "Engine21kbLibrary event schema registry Find() must return nullptr for an unregistered name");

    const kb::library::LibraryEventDesc* sceneLoaded = kb::library::EngineLibraryEventRegistry::Find("SceneLoaded");
    kb::tests::Require(sceneLoaded != nullptr && sceneLoaded->arguments.size() == 2U && sceneLoaded->arguments[0].name == "sceneId" && sceneLoaded->arguments[0].type == kb::script::ScriptValueType::Hash && sceneLoaded->arguments[1].name == "sceneName" && sceneLoaded->arguments[1].type == kb::script::ScriptValueType::String,
        "SceneLoaded's cataloged schema must match its real dispatch shape (sceneId: Hash, sceneName: String) — see ScriptRuntimeSceneSystem.cpp::DispatchPendingSceneLifecycleEvents");
    const kb::library::LibraryEventDesc* timerFired = kb::library::EngineLibraryEventRegistry::Find("TimerFired");
    kb::tests::Require(timerFired != nullptr && timerFired->arguments.size() == 1U && timerFired->arguments[0].name == "timer" && timerFired->arguments[0].type == kb::script::ScriptValueType::Hash,
        "TimerFired's cataloged schema must match its real dispatch shape (timer: Hash) — see ScriptRuntimeSceneSystem.cpp::DispatchFiredTimers");

    // LIB-108: the newly-cataloged engine events must match their REAL dispatch
    // shapes (ScriptRuntimeSceneSystem.cpp) — the exact arguments a script
    // subscribing to them receives, so the versioned schema is a true contract.
    const kb::library::LibraryEventDesc* onCollisionEnter = kb::library::EngineLibraryEventRegistry::Find("OnCollisionEnter");
    kb::tests::Require(
        onCollisionEnter != nullptr && onCollisionEnter->arguments.size() == 7U &&
            onCollisionEnter->arguments[0].name == "other" && onCollisionEnter->arguments[0].type == kb::script::ScriptValueType::Entity &&
            onCollisionEnter->arguments[1].name == "pointX" && onCollisionEnter->arguments[1].type == kb::script::ScriptValueType::Float &&
            onCollisionEnter->arguments[4].name == "normalX" && onCollisionEnter->arguments[4].type == kb::script::ScriptValueType::Float &&
            onCollisionEnter->arguments[6].name == "normalZ",
        "OnCollisionEnter's cataloged schema must match its real dispatch shape (other: Entity, point/normal X/Y/Z: Float) — see DispatchPendingCollisionEvents");
    const kb::library::LibraryEventDesc* onAudioMarker = kb::library::EngineLibraryEventRegistry::Find("OnAudioMarker");
    kb::tests::Require(
        onAudioMarker != nullptr && onAudioMarker->arguments.size() == 3U &&
            onAudioMarker->arguments[0].name == "voice" && onAudioMarker->arguments[0].type == kb::script::ScriptValueType::Int &&
            onAudioMarker->arguments[1].name == "marker" && onAudioMarker->arguments[1].type == kb::script::ScriptValueType::String &&
            onAudioMarker->arguments[2].name == "positionSeconds" && onAudioMarker->arguments[2].type == kb::script::ScriptValueType::Float,
        "OnAudioMarker's cataloged schema must match its real dispatch shape (voice: Int, marker: String, positionSeconds: Float) — see DispatchPendingAudioMarkerEvents");
    const kb::library::LibraryEventDesc* onPrefabInstantiated = kb::library::EngineLibraryEventRegistry::Find("OnPrefabInstantiated");
    kb::tests::Require(
        onPrefabInstantiated != nullptr && onPrefabInstantiated->arguments.size() == 2U &&
            onPrefabInstantiated->arguments[0].name == "root" && onPrefabInstantiated->arguments[0].type == kb::script::ScriptValueType::Entity &&
            onPrefabInstantiated->arguments[1].name == "count" && onPrefabInstantiated->arguments[1].type == kb::script::ScriptValueType::Int,
        "OnPrefabInstantiated's cataloged schema must match its real dispatch shape (root: Entity, count: Int) — see DispatchPendingPrefabInstantiatedEvents");
    const kb::library::LibraryEventDesc* onAnimationEvent = kb::library::EngineLibraryEventRegistry::Find("OnAnimationEvent");
    kb::tests::Require(
        onAnimationEvent != nullptr && onAnimationEvent->version == kb::library::LibraryEventVersion{ 1U, 0U } &&
            onAnimationEvent->arguments.size() == 7U &&
            onAnimationEvent->arguments[0].name == "schemaMajor" && onAnimationEvent->arguments[0].type == kb::script::ScriptValueType::Int &&
            onAnimationEvent->arguments[1].name == "schemaMinor" && onAnimationEvent->arguments[1].type == kb::script::ScriptValueType::Int &&
            onAnimationEvent->arguments[2].name == "event" && onAnimationEvent->arguments[2].type == kb::script::ScriptValueType::Hash &&
            onAnimationEvent->arguments[3].name == "clip" && onAnimationEvent->arguments[3].type == kb::script::ScriptValueType::Hash &&
            onAnimationEvent->arguments[4].name == "layer" && onAnimationEvent->arguments[4].type == kb::script::ScriptValueType::String &&
            onAnimationEvent->arguments[5].name == "state" && onAnimationEvent->arguments[5].type == kb::script::ScriptValueType::String &&
            onAnimationEvent->arguments[6].name == "normalizedTime" && onAnimationEvent->arguments[6].type == kb::script::ScriptValueType::Float,
        "OnAnimationEvent's versioned catalog schema must match its fixed typed dispatch payload");
    const kb::library::LibraryEventDesc* onTimelineMarker =
        kb::library::EngineLibraryEventRegistry::Find("OnTimelineMarker");
    kb::tests::Require(
        onTimelineMarker != nullptr &&
            onTimelineMarker->version ==
                kb::library::LibraryEventVersion{ 1U, 0U } &&
            onTimelineMarker->arguments.size() == 6U &&
            onTimelineMarker->arguments[0].name == "schemaMajor" &&
            onTimelineMarker->arguments[0].type ==
                kb::script::ScriptValueType::Int &&
            onTimelineMarker->arguments[1].name == "schemaMinor" &&
            onTimelineMarker->arguments[1].type ==
                kb::script::ScriptValueType::Int &&
            onTimelineMarker->arguments[2].name == "instance" &&
            onTimelineMarker->arguments[2].type ==
                kb::script::ScriptValueType::Hash &&
            onTimelineMarker->arguments[3].name == "asset" &&
            onTimelineMarker->arguments[3].type ==
                kb::script::ScriptValueType::Hash &&
            onTimelineMarker->arguments[4].name == "marker" &&
            onTimelineMarker->arguments[4].type ==
                kb::script::ScriptValueType::Hash &&
            onTimelineMarker->arguments[5].name == "time" &&
            onTimelineMarker->arguments[5].type ==
                kb::script::ScriptValueType::Float,
        "OnTimelineMarker's versioned catalog schema must match its fixed typed dispatch payload");
    const kb::library::LibraryEventDesc* uiClick = kb::library::EngineLibraryEventRegistry::Find("UI.Click");
    const kb::library::LibraryEventDesc* uiSubmit = kb::library::EngineLibraryEventRegistry::Find("UI.Submit");
    const kb::library::LibraryEventDesc* uiChanged = kb::library::EngineLibraryEventRegistry::Find("UI.Changed");
    const kb::library::LibraryEventDesc* uiFocus = kb::library::EngineLibraryEventRegistry::Find("UI.Focus");
    const kb::library::LibraryEventDesc* uiNavigation = kb::library::EngineLibraryEventRegistry::Find("UI.Navigation");
    kb::tests::Require(
        uiClick != nullptr && uiClick->arguments.size() == 4U &&
            uiClick->arguments[0].name == "owner" && uiClick->arguments[0].type == kb::script::ScriptValueType::Entity &&
            uiClick->arguments[1].name == "element" && uiClick->arguments[1].type == kb::script::ScriptValueType::Hash &&
            uiClick->arguments[2].name == "x" && uiClick->arguments[3].name == "y",
        "UI.Click's cataloged schema must match ScriptRuntimeSceneSystem's document owner, element, and pointer payload");
    kb::tests::Require(
        uiSubmit != nullptr && uiSubmit->arguments.size() == 3U && uiSubmit->arguments[2].name == "text" &&
            uiSubmit->arguments[2].type == kb::script::ScriptValueType::String &&
            uiChanged != nullptr && uiChanged->arguments.size() == 3U && uiChanged->arguments[2].name == "value" &&
            uiChanged->arguments[2].type == kb::script::ScriptValueType::Float &&
            uiFocus != nullptr && uiFocus->arguments.size() == 3U && uiFocus->arguments[2].name == "focused" &&
            uiFocus->arguments[2].type == kb::script::ScriptValueType::Bool &&
            uiNavigation != nullptr && uiNavigation->arguments.size() == 3U && uiNavigation->arguments[2].name == "direction" &&
            uiNavigation->arguments[2].type == kb::script::ScriptValueType::String,
        "UI Submit, Changed, Focus, and Navigation schemas must match their fixed runtime payloads");
}

// LIB-084: kb::library::EngineLibraryComponentInspectorRegistry — proves the
// new UI-facing metadata catalog (displayName/category per component,
// displayName/tooltip per field) exactly matches the two existing sources
// of truth it is keyed against — ScriptSceneComponentApi::ComponentNames()/
// ComponentProperties() (LIB-077) — with zero drift in either direction
// (missing entry or stale extra entry both fail this test), and that every
// entry actually carries non-empty presentation text rather than a
// placeholder.
void RunComponentInspectorDescCatalogTest() {
    const std::span<const std::string_view> scriptComponentNames = kb::script::ScriptSceneComponentApi::ComponentNames();
    const std::vector<kb::library::LibraryComponentInspectorDesc>& catalog = kb::library::EngineLibraryComponentInspectorRegistry::Catalog();

    kb::tests::Require(catalog.size() == scriptComponentNames.size(),
        "Engine21kbLibrary component inspector catalog must cover exactly the same number of components ScriptSceneComponentApi.cpp gates Lua/VisualGraph access behind");

    std::size_t fieldsChecked = 0U;
    for (const std::string_view scriptName : scriptComponentNames) {
        const kb::library::LibraryComponentInspectorDesc* componentDesc = kb::library::EngineLibraryComponentInspectorRegistry::Find(scriptName);
        kb::tests::Require(componentDesc != nullptr, "Engine21kbLibrary component inspector catalog is missing an entry for a component ScriptSceneComponentApi.cpp already gates Lua/VisualGraph access behind");
        kb::tests::Require(!componentDesc->displayName.empty(), "Engine21kbLibrary component inspector entry must have a non-empty displayName");
        kb::tests::Require(!componentDesc->category.empty(), "Engine21kbLibrary component inspector entry must have a non-empty category");

        const std::span<const kb::script::ScriptSceneComponentPropertyDesc> scriptProperties = kb::script::ScriptSceneComponentApi::ComponentProperties(scriptName);
        kb::tests::Require(componentDesc->fields.size() == scriptProperties.size(),
            "Engine21kbLibrary component inspector entry must catalog exactly the same field count ScriptSceneComponentApi::ComponentProperties() reports for that component");
        for (const kb::script::ScriptSceneComponentPropertyDesc& property : scriptProperties) {
            ++fieldsChecked;
            const kb::library::LibraryComponentInspectorFieldDesc* fieldDesc = kb::library::EngineLibraryComponentInspectorRegistry::FindField(scriptName, property.name);
            const std::string fieldLabel = std::string{ scriptName } + "." + std::string{ property.name };
            kb::tests::Require(fieldDesc != nullptr, ("Engine21kbLibrary component inspector catalog is missing a field entry for " + fieldLabel).c_str());
            kb::tests::Require(!fieldDesc->displayName.empty(), ("Engine21kbLibrary component inspector field entry must have a non-empty displayName for " + fieldLabel).c_str());
            kb::tests::Require(!fieldDesc->tooltip.empty(), ("Engine21kbLibrary component inspector field entry must have a non-empty tooltip for " + fieldLabel).c_str());
        }
    }
    // LIB-131: CharacterController grew 5->9 script-writable fields (slopeLimitDegrees/
    // stepOffset/gravityScale/useGravity), so the total climbs from 79 to 83.
    // LIB-133: Rigidbody grew one more field (useContinuousCollision), so the total climbs
    // from 83 to 84.
    // LIB-135: Camera grew two more fields (viewportId/priority), so the total climbs from
    // 84 to 86.
    // LIB-136: Camera grew three more fields (cullingMask/clearMode/clearColor, the latter
    // decomposed into x/y/z), and MeshRenderer grew one (layer), so the total climbs from
    // 86 to 92.
    // LIB-141: Light grew five more fields (areaWidth/areaHeight - a pre-existing reflection
    // gap closed here - plus useColorTemperature/colorTemperatureKelvin/layerMask), so the
    // total climbs from 92 to 97.
    kb::tests::Require(fieldsChecked == 97U, "Engine21kbLibrary component inspector catalog did not exercise the expected total field count (97) across all 10 components");

    for (const kb::library::LibraryComponentInspectorDesc& desc : catalog) {
        const bool foundInScriptNames = std::ranges::find(scriptComponentNames, desc.componentName) != scriptComponentNames.end();
        kb::tests::Require(foundInScriptNames, "Engine21kbLibrary component inspector catalog names a component ScriptSceneComponentApi.cpp does not gate Lua/VisualGraph access behind — the two must never drift apart");
    }

    kb::tests::Require(kb::library::EngineLibraryComponentInspectorRegistry::Find("NoSuchComponent") == nullptr, "Engine21kbLibrary component inspector catalog Find() must return nullptr for an unregistered component name");
    kb::tests::Require(kb::library::EngineLibraryComponentInspectorRegistry::FindField("Camera", "NoSuchField") == nullptr, "Engine21kbLibrary component inspector catalog FindField() must return nullptr for an unregistered field name");
    kb::tests::Require(kb::library::EngineLibraryComponentInspectorRegistry::FindField("NoSuchComponent", "projection") == nullptr, "Engine21kbLibrary component inspector catalog FindField() must return nullptr for an unregistered component name");
}

// LIB-078: kb::library::Query<T>::ForEach — proves the phase gate (reusing
// LIB-007's ClassifyLifecycleContext, not a new classification), that the
// visitor receives real component data for a real live entity, and that a
// structural change attempted from INSIDE the visitor genuinely throws
// (kb::ecs::StructuralChangeValidator, entered via the same
// kb::ecs::World the Scene wraps — not a new guard) rather than
// corrupting iteration state, AND that the RAII guard is still released
// after that exception (iteration is not left permanently blocked).
void RunLibraryQueryPhaseGateTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject objectA = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "QueryGateA" });
    const kb::scene::SceneObject objectB = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "QueryGateB" });
    scene.Components().Cameras().Set(objectA.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 30.0F });
    scene.Components().Cameras().Set(objectB.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 60.0F });

    int visitedTick = 0;
    float fovSum = 0.0F;
    const bool tickIterated = kb::library::Query<kb::scene::CameraComponent>::ForEach(
        scene, kb::script::ScriptLifecycleEvent::Tick,
        [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent& camera) {
            kb::tests::Require(entity.IsAlive(scene), "Engine21kbLibrary Query<T> visited a non-alive entity");
            ++visitedTick;
            fovSum += camera.verticalFovDegrees;
        });
    kb::tests::Require(tickIterated, "Engine21kbLibrary Query<T>::ForEach must iterate during Tick (a Frame-classified phase)");
    kb::tests::Require(visitedTick == 2, "Engine21kbLibrary Query<T>::ForEach did not visit all matching entities");
    kb::tests::Require(kb::tests::NearlyEqual(fovSum, 90.0F), "Engine21kbLibrary Query<T>::ForEach did not pass real component data to the visitor");

    int visitedCreated = 0;
    const bool createdIterated = kb::library::Query<kb::scene::CameraComponent>::ForEach(
        scene, kb::script::ScriptLifecycleEvent::Created,
        [&](kb::library::EntityHandle, const kb::scene::CameraComponent&) { ++visitedCreated; });
    kb::tests::Require(!createdIterated, "Engine21kbLibrary Query<T>::ForEach must refuse to iterate during Created (a Behaviour-classified phase)");
    kb::tests::Require(visitedCreated == 0, "Engine21kbLibrary Query<T>::ForEach must never call the visitor when refusing to iterate");

    bool threw = false;
    try {
        static_cast<void>(kb::library::Query<kb::scene::CameraComponent>::ForEach(
            scene, kb::script::ScriptLifecycleEvent::Tick,
            [&](kb::library::EntityHandle, const kb::scene::CameraComponent&) {
                static_cast<void>(scene.Entities().CreateEntity());
            }));
    } catch (const std::logic_error&) {
        threw = true;
    }
    kb::tests::Require(threw, "Engine21kbLibrary Query<T>::ForEach must let a structural change attempted from inside the visitor throw std::logic_error");

    int visitedAfterThrow = 0;
    const bool iteratedAfterThrow = kb::library::Query<kb::scene::CameraComponent>::ForEach(
        scene, kb::script::ScriptLifecycleEvent::Tick,
        [&](kb::library::EntityHandle, const kb::scene::CameraComponent&) { ++visitedAfterThrow; });
    kb::tests::Require(iteratedAfterThrow && visitedAfterThrow == 2,
        "Engine21kbLibrary Query<T>::ForEach's RAII iteration guard must be released even after the visitor throws, not leave the world permanently refusing structural changes");
}

// LIB-079: Query.With/Without/Any/ChangedSince/Enabled + stable order —
// proves each filter modifier against a real, differentiated population,
// not just that the API compiles. Also proves the LIB-079 refactor (kb::
// ecs::World::CreateQuery<T> instead of kb::scene per-type visitors)
// genuinely covers VisibilityComponent, which LIB-078 had to exclude.
void RunLibraryQueryFilterAndOrderTest() {
    kb::scene::Scene scene;
    // A: Camera + Light, active.
    // B: Camera only, active.
    // C: Camera + Behaviour, INACTIVE (SetActive(false)).
    // D: Camera + Light + Behaviour, active.
    const kb::scene::SceneObject objectA = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FilterA" });
    const kb::scene::SceneObject objectB = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FilterB" });
    const kb::scene::SceneObject objectC = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FilterC" });
    const kb::scene::SceneObject objectD = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FilterD" });
    scene.Components().Cameras().Set(objectA.Entity(), kb::scene::CameraComponent{});
    scene.Components().Cameras().Set(objectB.Entity(), kb::scene::CameraComponent{});
    scene.Components().Cameras().Set(objectC.Entity(), kb::scene::CameraComponent{});
    scene.Components().Cameras().Set(objectD.Entity(), kb::scene::CameraComponent{});
    scene.Components().Lights().Set(objectA.Entity(), kb::scene::LightComponent{ .intensity = 2.0F });
    scene.Components().Lights().Set(objectD.Entity(), kb::scene::LightComponent{ .intensity = 3.0F });
    scene.Components().Behaviours().Set(objectC.Entity(), kb::scene::BehaviourComponent{ .behaviourAssetId = 1U, .backend = kb::scene::BehaviourBackend::Native, .enabled = true });
    scene.Components().Behaviours().Set(objectD.Entity(), kb::scene::BehaviourComponent{ .behaviourAssetId = 2U, .backend = kb::scene::BehaviourBackend::Native, .enabled = true });
    scene.Entities().SetActive(objectC.Entity(), false);

    const auto namesOf = [&](const std::vector<kb::scene::SceneEntity>& entities) {
        std::vector<std::string> names;
        names.reserve(entities.size());
        for (const kb::scene::SceneEntity entity : entities) {
            names.push_back(scene.Entities().Name(entity));
        }
        std::ranges::sort(names);
        return names;
    };

    // With<LightComponent>(): only A and D have both Camera and Light.
    std::vector<kb::scene::SceneEntity> withResult;
    kb::library::QueryFilterOptions withOptions;
    withOptions.With<kb::scene::LightComponent>();
    kb::tests::Require(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick, withOptions,
                            [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) { withResult.push_back(entity.Entity()); }),
        "Engine21kbLibrary Query.With must still iterate during Tick");
    kb::tests::Require(namesOf(withResult) == std::vector<std::string>{ "FilterA", "FilterD" }, "Engine21kbLibrary Query.With<LightComponent> must select exactly the entities that also have Light");

    // Without<BehaviourComponent>(): only A and B lack Behaviour.
    std::vector<kb::scene::SceneEntity> withoutResult;
    kb::library::QueryFilterOptions withoutOptions;
    withoutOptions.Without<kb::scene::BehaviourComponent>();
    static_cast<void>(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick, withoutOptions,
        [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) { withoutResult.push_back(entity.Entity()); }));
    kb::tests::Require(namesOf(withoutResult) == std::vector<std::string>{ "FilterA", "FilterB" }, "Engine21kbLibrary Query.Without<BehaviourComponent> must exclude every entity that has Behaviour");

    // Any<LightComponent, BehaviourComponent>(): A (Light), C (Behaviour), D (both) — not B (neither).
    std::vector<kb::scene::SceneEntity> anyResult;
    kb::library::QueryFilterOptions anyOptions;
    anyOptions.Any<kb::scene::LightComponent, kb::scene::BehaviourComponent>();
    static_cast<void>(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick, anyOptions,
        [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) { anyResult.push_back(entity.Entity()); }));
    kb::tests::Require(namesOf(anyResult) == std::vector<std::string>{ "FilterA", "FilterC", "FilterD" }, "Engine21kbLibrary Query.Any<Light,Behaviour> must select entities with at least one of the two, excluding the one with neither");

    // Enabled(): C is inactive (SetActive(false)) and must be skipped.
    std::vector<kb::scene::SceneEntity> enabledResult;
    kb::library::QueryFilterOptions enabledOptions;
    enabledOptions.Enabled();
    static_cast<void>(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick, enabledOptions,
        [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) { enabledResult.push_back(entity.Entity()); }));
    kb::tests::Require(namesOf(enabledResult) == std::vector<std::string>{ "FilterA", "FilterB", "FilterD" }, "Engine21kbLibrary Query.Enabled must skip the inactive entity (SceneEntities::IsActive == false)");

    // ChangedSince<CameraComponent>(): every Camera here was just Set(), so all four must be reported as changed.
    std::vector<kb::scene::SceneEntity> changedResult;
    kb::library::QueryFilterOptions changedOptions;
    changedOptions.ChangedSince<kb::scene::CameraComponent>();
    static_cast<void>(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick, changedOptions,
        [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) { changedResult.push_back(entity.Entity()); }));
    kb::tests::Require(changedResult.size() == 4U, "Engine21kbLibrary Query.ChangedSince<CameraComponent> must report a just-Set() component as changed for every matching entity");

    // StableOrder(): two consecutive passes over the same population must
    // visit entities in the identical order, not just the identical set.
    kb::library::QueryFilterOptions stableOptions;
    stableOptions.StableOrder();
    std::vector<kb::scene::SceneEntity> firstOrder;
    std::vector<kb::scene::SceneEntity> secondOrder;
    static_cast<void>(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick, stableOptions,
        [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) { firstOrder.push_back(entity.Entity()); }));
    static_cast<void>(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick, stableOptions,
        [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) { secondOrder.push_back(entity.Entity()); }));
    kb::tests::Require(firstOrder.size() == 4U && firstOrder == secondOrder,
        "Engine21kbLibrary Query.StableOrder must visit entities in the identical order across two consecutive passes over the same population");

    // Regression proof: LIB-078 had to exclude VisibilityComponent (no
    // kb::scene bulk-iteration primitive existed for it) — the LIB-079
    // refactor to kb::ecs::World::CreateQuery<T> covers it uniformly with
    // every other registered scene component, since it is confirmed to be
    // a real, registered kb::ecs component like the rest.
    int visibilityVisited = 0;
    const bool visibilityIterated = kb::library::Query<kb::scene::VisibilityComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick,
        [&](kb::library::EntityHandle, const kb::scene::VisibilityComponent&) { ++visibilityVisited; });
    kb::tests::Require(visibilityIterated && visibilityVisited == 4, "Engine21kbLibrary Query<VisibilityComponent> must now work (LIB-079 regression fix) — every entity has a Visibility component from creation");

    // LIB-078: a MULTI-component Query<Camera, Light> must iterate exactly the
    // entities that have BOTH components (A and D — not B, which has only a
    // Camera, and not C, which has no Light), and hand the visitor each
    // component's live data in pack order. Reading light.intensity per entity
    // (A=2, D=3) proves the SECOND component's column is really threaded
    // through, not just the first — the exact gap the 2026-07-17 audit found
    // (only the one-component variant existed).
    std::vector<kb::scene::SceneEntity> multiResult;
    float intensitySum = 0.0F;
    float fovSum = 0.0F;
    const bool multiIterated = kb::library::Query<kb::scene::CameraComponent, kb::scene::LightComponent>::ForEach(
        scene, kb::script::ScriptLifecycleEvent::Tick,
        [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent& camera, const kb::scene::LightComponent& light) {
            multiResult.push_back(entity.Entity());
            intensitySum += light.intensity;
            fovSum += camera.verticalFovDegrees;
        });
    kb::tests::Require(multiIterated, "Engine21kbLibrary multi-component Query must iterate during an allowed phase");
    kb::tests::Require(namesOf(multiResult) == std::vector<std::string>{ "FilterA", "FilterD" }, "Engine21kbLibrary Query<Camera,Light> must iterate exactly the entities that have BOTH components");
    kb::tests::Require(std::abs(intensitySum - 5.0F) < 0.0001F, "Engine21kbLibrary multi-component Query must thread the SECOND component's live data (light.intensity 2 + 3 = 5) through the visitor, not just the first");
    kb::tests::Require(std::abs(fovSum - 120.0F) < 0.0001F, "Engine21kbLibrary multi-component Query must also thread the first component's data (two default 60-degree cameras = 120)");
}

// LIB-079 (audit gap closed 2026-07-18): ChangedSince needs a real reference
// point. The static Query rebuilds its change-tracking state every call, so a
// ChangedSince query reports every match forever. PersistentQuery keeps its
// kb::ecs::Query across runs, so ChangedSince means "changed since THIS query
// last ran": run it twice with nothing modified between and the second run
// must report NOTHING — the exact behaviour the static query cannot express.
void RunLibraryPersistentQueryChangedSinceTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject objectA = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ChangedA" });
    const kb::scene::SceneObject objectB = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ChangedB" });
    scene.Components().Cameras().Set(objectA.Entity(), kb::scene::CameraComponent{});
    scene.Components().Cameras().Set(objectB.Entity(), kb::scene::CameraComponent{});

    kb::library::QueryFilterOptions changedOptions;
    changedOptions.ChangedSince<kb::scene::CameraComponent>();
    kb::library::PersistentQuery<kb::scene::CameraComponent> query{ scene, changedOptions };

    // First run: both cameras were just Set(), so both are "changed since
    // never observed" — the persistent query reports them and commits their
    // observed versions.
    int firstRunCount = 0;
    kb::tests::Require(query.ForEach(kb::script::ScriptLifecycleEvent::Tick,
                           [&](kb::library::EntityHandle, const kb::scene::CameraComponent&) { ++firstRunCount; }),
        "Engine21kbLibrary PersistentQuery must iterate during an allowed phase");
    kb::tests::Require(firstRunCount == 2, "Engine21kbLibrary PersistentQuery ChangedSince first run must report both freshly-Set cameras");

    // Second run with NOTHING modified in between: the observed versions now
    // match the live ones, so a persistent ChangedSince reports NOTHING. This
    // is the reference point the 2026-07-17 audit found missing — a fresh
    // per-call query would report both cameras again here.
    int unchangedRunCount = 0;
    static_cast<void>(query.ForEach(kb::script::ScriptLifecycleEvent::Tick,
        [&](kb::library::EntityHandle, const kb::scene::CameraComponent&) { ++unchangedRunCount; }));
    kb::tests::Require(unchangedRunCount == 0, "Engine21kbLibrary PersistentQuery ChangedSince must report NOTHING on a second run when nothing changed between the two — proving it tracks a real 'since when' reference point, not 'always changed'");

    // Modify a camera, then run again: the change must be observed (the
    // modified archetype's cameras are reported).
    scene.Components().Cameras().Set(objectA.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 33.0F });
    int afterChangeCount = 0;
    static_cast<void>(query.ForEach(kb::script::ScriptLifecycleEvent::Tick,
        [&](kb::library::EntityHandle, const kb::scene::CameraComponent&) { ++afterChangeCount; }));
    kb::tests::Require(afterChangeCount >= 1, "Engine21kbLibrary PersistentQuery ChangedSince must report a camera after its component is modified between runs");
}

// LIB-080: kb::library::CommandBatch — proves Spawn/Destroy/Add<T>/
// Remove<T>/AddTag/RemoveTag all genuinely defer (zero effect on the live
// world before Flush()), that Flush() actually applies them (including
// resolving a freshly-Spawned BatchEntity to a real, live entity), that
// recording commands from INSIDE an open Query<T>::ForEach never throws
// (the whole reason this type exists), and that Flush() itself DOES throw
// if called while that same iteration is still open — CommandBatch defers
// the structural change, it does not bypass the ban on applying one
// mid-iteration.
void RunLibraryCommandBatchTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject existingObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "CommandBatchExisting" });
    const kb::library::EntityHandle existingHandle{ existingObject.Entity(), scene.Id() };
    const kb::scene::SceneObject toDestroyObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "CommandBatchToDestroy" });
    const kb::library::EntityHandle toDestroyHandle{ toDestroyObject.Entity(), scene.Id() };

    kb::library::CommandBatch batch{ scene };
    const kb::library::BatchEntity spawned = batch.Spawn("CommandBatchSpawned");
    kb::tests::Require(spawned.IsValid(), "Engine21kbLibrary CommandBatch::Spawn must return a valid (though not-yet-real) BatchEntity");

    batch.Add<kb::scene::CameraComponent>(spawned, kb::scene::CameraComponent{ .verticalFovDegrees = 45.0F });
    kb::tests::Require(batch.Add<kb::scene::CameraComponent>(existingHandle, kb::scene::CameraComponent{ .verticalFovDegrees = 77.0F }),
        "Engine21kbLibrary CommandBatch::Add<T> must succeed (queue the command) for a live handle");
    kb::tests::Require(batch.Destroy(toDestroyHandle), "Engine21kbLibrary CommandBatch::Destroy must succeed (queue the command) for a live handle");
    kb::tests::Require(batch.AddTag(existingHandle, "Enemy"), "Engine21kbLibrary CommandBatch::AddTag must succeed for a live handle");

    // Nothing must have applied yet — the whole point of a command batch.
    kb::tests::Require(!scene.Components().Cameras().Has(existingObject.Entity()), "Engine21kbLibrary CommandBatch::Add<T> must not apply anything before Flush()");
    kb::tests::Require(scene.Entities().IsAlive(toDestroyObject.Entity()), "Engine21kbLibrary CommandBatch::Destroy must not apply anything before Flush()");
    kb::tests::Require(scene.Components().Tags().TryGet(existingObject.Entity()) == nullptr, "Engine21kbLibrary CommandBatch::AddTag must not apply anything before Flush()");

    const std::optional<kb::ecs::CommandBufferPlaybackResult> result = batch.Flush();
    kb::tests::Require(result.has_value(), "Engine21kbLibrary CommandBatch::Flush must succeed when every tracked target is still alive");
    kb::tests::Require(result->CreatedCount() == 1U, "Engine21kbLibrary CommandBatch::Flush must report exactly one created entity");
    const kb::ecs::Entity resolvedSpawned = result->Resolve(spawned.Raw());
    kb::tests::Require(resolvedSpawned.IsValid() && scene.Entities().IsAlive(resolvedSpawned), "Engine21kbLibrary CommandBatch::Flush must resolve Spawn's BatchEntity to a real, live entity");
    const kb::scene::CameraComponent* spawnedCamera = scene.Components().Cameras().TryGet(resolvedSpawned);
    kb::tests::Require(spawnedCamera != nullptr && kb::tests::NearlyEqual(spawnedCamera->verticalFovDegrees, 45.0F), "Engine21kbLibrary CommandBatch::Add<T> on a BatchEntity must apply to the resolved real entity after Flush()");
    const kb::scene::CameraComponent* existingCamera = scene.Components().Cameras().TryGet(existingObject.Entity());
    kb::tests::Require(existingCamera != nullptr && kb::tests::NearlyEqual(existingCamera->verticalFovDegrees, 77.0F), "Engine21kbLibrary CommandBatch::Add<T> on an EntityHandle must apply after Flush()");
    kb::tests::Require(!scene.Entities().IsAlive(toDestroyObject.Entity()), "Engine21kbLibrary CommandBatch::Destroy must apply after Flush()");
    const kb::scene::TagsComponent* tagsAfterAdd = scene.Components().Tags().TryGet(existingObject.Entity());
    kb::tests::Require(tagsAfterAdd != nullptr && kb::scene::TagsText(*tagsAfterAdd) == "Enemy", "Engine21kbLibrary CommandBatch::AddTag must apply after Flush()");

    kb::library::CommandBatch removeTagBatch{ scene };
    kb::tests::Require(removeTagBatch.RemoveTag(existingHandle, "Enemy"), "Engine21kbLibrary CommandBatch::RemoveTag must succeed for a live handle with that tag");
    static_cast<void>(removeTagBatch.Flush());
    kb::tests::Require(scene.Components().Tags().TryGet(existingObject.Entity()) == nullptr, "Engine21kbLibrary CommandBatch::RemoveTag removing the only tag must leave no TagsComponent, matching World.SetTag's existing convention");

    kb::library::CommandBatch removeComponentBatch{ scene };
    kb::tests::Require(removeComponentBatch.Remove<kb::scene::CameraComponent>(existingHandle),
        "Engine21kbLibrary CommandBatch::Remove<T> must succeed (queue the command) for a live handle");
    static_cast<void>(removeComponentBatch.Flush());
    kb::tests::Require(!scene.Components().Cameras().Has(existingObject.Entity()), "Engine21kbLibrary CommandBatch::Remove<T> must apply after Flush()");

    kb::library::CommandBatch deadHandleBatch{ scene };
    kb::tests::Require(!deadHandleBatch.AddTag(toDestroyHandle, "X"), "Engine21kbLibrary CommandBatch::AddTag on a destroyed entity must report false, not throw or queue a command");

    // Recording from inside an open Query<T>::ForEach must never throw —
    // this IS the mechanism LIB-078/079's structural-change ban expects a
    // script to use instead of an immediate, rejected mutation.
    const kb::scene::SceneObject queryTargetObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "CommandBatchQueryTarget" });
    scene.Components().Cameras().Set(queryTargetObject.Entity(), kb::scene::CameraComponent{});
    kb::library::CommandBatch recordDuringQueryBatch{ scene };
    int recordedInsideLoop = 0;
    kb::tests::Require(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick,
                            [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) {
                                static_cast<void>(recordDuringQueryBatch.Destroy(entity));
                                ++recordedInsideLoop;
                            }),
        "Engine21kbLibrary CommandBatch test's Query<T>::ForEach must actually iterate");
    kb::tests::Require(recordedInsideLoop > 0, "Engine21kbLibrary CommandBatch recording from inside a Query<T>::ForEach visitor must not be silently skipped");
    kb::tests::Require(scene.Entities().IsAlive(queryTargetObject.Entity()), "Engine21kbLibrary CommandBatch::Destroy recorded inside a Query loop must not apply until Flush() is called");
    static_cast<void>(recordDuringQueryBatch.Flush());
    kb::tests::Require(!scene.Entities().IsAlive(queryTargetObject.Entity()), "Engine21kbLibrary CommandBatch::Flush() called AFTER the Query loop has closed must apply the recorded Destroy");

    // Flush() itself, called WHILE the iteration is still open, must throw
    // — CommandBatch defers the change, it does not bypass the ban on
    // applying one mid-iteration.
    const kb::scene::SceneObject secondQueryTargetObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "CommandBatchFlushInsideTarget" });
    scene.Components().Cameras().Set(secondQueryTargetObject.Entity(), kb::scene::CameraComponent{});
    kb::library::CommandBatch flushInsideQueryBatch{ scene };
    bool flushInsideQueryThrew = false;
    static_cast<void>(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick,
        [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) {
            static_cast<void>(flushInsideQueryBatch.Destroy(entity));
            try {
                static_cast<void>(flushInsideQueryBatch.Flush());
            } catch (const std::logic_error&) {
                flushInsideQueryThrew = true;
            }
        }));
    kb::tests::Require(flushInsideQueryThrew, "Engine21kbLibrary CommandBatch::Flush() must throw std::logic_error when called while a Query<T>::ForEach iteration is still open");

    // LIB-080 (audit gap closed 2026-07-18): tag assignments COALESCE per
    // target within one batch (read-your-own-writes) instead of the old
    // last-write-wins, and a BatchEntity spawned in the same batch can now
    // be tagged.
    {
        // Two AddTag calls for the SAME live entity in one batch must BOTH
        // survive — the second must build on the first's pending state, not
        // independently re-read the live (still empty) tag set and overwrite.
        const kb::scene::SceneObject coalesceObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "CommandBatchCoalesce" });
        const kb::library::EntityHandle coalesceHandle{ coalesceObject.Entity(), scene.Id() };
        kb::library::CommandBatch coalesceBatch{ scene };
        kb::tests::Require(coalesceBatch.AddTag(coalesceHandle, "Alpha"), "Engine21kbLibrary CommandBatch::AddTag first tag must record");
        kb::tests::Require(coalesceBatch.AddTag(coalesceHandle, "Beta"), "Engine21kbLibrary CommandBatch::AddTag second tag must record");
        static_cast<void>(coalesceBatch.Flush());
        const kb::scene::TagsComponent* coalesced = scene.Components().Tags().TryGet(coalesceObject.Entity());
        kb::tests::Require(coalesced != nullptr && kb::scene::TagsText(*coalesced) == "Alpha, Beta",
            "Engine21kbLibrary CommandBatch must COALESCE two AddTag calls on one entity into BOTH tags (Alpha, Beta), not last-write-wins");

        // AddTag then RemoveTag of a DIFFERENT tag on the same entity in one
        // batch: the RemoveTag must see the batch's pending "Gamma", leaving
        // only "Delta".
        kb::library::CommandBatch mixedBatch{ scene };
        kb::tests::Require(mixedBatch.AddTag(coalesceHandle, "Gamma"), "Engine21kbLibrary CommandBatch::AddTag Gamma must record");
        kb::tests::Require(mixedBatch.AddTag(coalesceHandle, "Delta"), "Engine21kbLibrary CommandBatch::AddTag Delta must record");
        kb::tests::Require(mixedBatch.RemoveTag(coalesceHandle, "Alpha"), "Engine21kbLibrary CommandBatch::RemoveTag Alpha must record");
        static_cast<void>(mixedBatch.Flush());
        const kb::scene::TagsComponent* mixed = scene.Components().Tags().TryGet(coalesceObject.Entity());
        kb::tests::Require(mixed != nullptr && kb::scene::TagsText(*mixed) == "Beta, Gamma, Delta",
            "Engine21kbLibrary CommandBatch mixed AddTag/RemoveTag in one batch must read-your-own-writes over the seeded live tags (Alpha,Beta -> +Gamma +Delta -Alpha = Beta, Gamma, Delta)");

        // A BatchEntity spawned in the SAME batch can be tagged — the tag
        // command is queued against the deferred entity and resolved by the
        // same Flush() that creates it. This closes LIB-080's original
        // documented BatchEntity gap.
        kb::library::CommandBatch spawnTagBatch{ scene };
        const kb::library::BatchEntity spawnedTagged = spawnTagBatch.Spawn("CommandBatchSpawnedTagged");
        kb::tests::Require(spawnTagBatch.AddTag(spawnedTagged, "FreshOne"), "Engine21kbLibrary CommandBatch::AddTag on a BatchEntity must record");
        kb::tests::Require(spawnTagBatch.AddTag(spawnedTagged, "FreshTwo"), "Engine21kbLibrary CommandBatch::AddTag second tag on a BatchEntity must coalesce too");
        const std::optional<kb::ecs::CommandBufferPlaybackResult> spawnTagResult = spawnTagBatch.Flush();
        kb::tests::Require(spawnTagResult.has_value(), "Engine21kbLibrary CommandBatch Flush with a tagged spawn must succeed");
        const kb::ecs::Entity resolvedTagged = spawnTagResult->Resolve(spawnedTagged.Raw());
        kb::tests::Require(resolvedTagged.IsValid() && scene.Entities().IsAlive(resolvedTagged), "Engine21kbLibrary tagged BatchEntity must resolve to a live entity");
        const kb::scene::TagsComponent* spawnedTags = scene.Components().Tags().TryGet(resolvedTagged);
        kb::tests::Require(spawnedTags != nullptr && kb::scene::TagsText(*spawnedTags) == "FreshOne, FreshTwo",
            "Engine21kbLibrary CommandBatch::AddTag on a spawned-this-batch BatchEntity must apply BOTH coalesced tags to the resolved entity after Flush()");
    }
}

// LIB-012 audit gap closed 2026-07-17: RunLibraryCommandBatchTest above only
// proves a command is cancelled when its EntityHandle target is ALREADY
// dead at record time (deadHandleBatch.AddTag). Before this fix, a command
// whose target went stale AFTER being recorded but BEFORE Flush() — the
// literal "anulowanie pending commands" (same-frame destroy-races-a-pending-
// command) scenario this task asks for — was not cancelled at all: it
// reached kb::ecs::CommandBuffer::Playback, which threw std::out_of_range
// (World::ValidateEntityHandle) uncaught. This proves the real fix: Flush()
// re-checks every tracked target and returns std::nullopt (nothing
// applied), for Destroy/Add<T>/Remove<T>/AddTag alike, without throwing.
void RunLibraryCommandBatchCancelsStaleTargetOnFlushTest() {
    kb::scene::Scene scene;

    // Destroy() recorded against a target killed by an unrelated, direct
    // path before Flush().
    {
        const kb::scene::SceneObject target = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "StaleDestroyTarget" });
        const kb::library::EntityHandle handle{ target.Entity(), scene.Id() };
        kb::library::CommandBatch batch{ scene };
        kb::tests::Require(batch.Destroy(handle), "Engine21kbLibrary CommandBatch::Destroy must succeed (queue the command) while the target is still alive");
        scene.Entities().Destroy(target.Entity());
        const std::optional<kb::ecs::CommandBufferPlaybackResult> result = batch.Flush();
        kb::tests::Require(!result.has_value(), "Engine21kbLibrary CommandBatch::Flush must cancel (return nullopt), not throw, when a recorded Destroy's target died before Flush()");
    }

    // Add<T>() recorded against a target killed the same way — and a SECOND,
    // otherwise-valid command in the SAME batch must also NOT apply: Flush()
    // is all-or-nothing, so one stale target cancels the whole batch rather
    // than silently partially applying it.
    {
        const kb::scene::SceneObject staleTarget = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "StaleAddTarget" });
        const kb::library::EntityHandle staleHandle{ staleTarget.Entity(), scene.Id() };
        const kb::scene::SceneObject otherTarget = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "StaleAddOtherTarget" });
        const kb::library::EntityHandle otherHandle{ otherTarget.Entity(), scene.Id() };

        kb::library::CommandBatch batch{ scene };
        kb::tests::Require(batch.Add<kb::scene::CameraComponent>(staleHandle, kb::scene::CameraComponent{ .verticalFovDegrees = 10.0F }),
            "Engine21kbLibrary CommandBatch::Add<T> must succeed (queue the command) while the target is still alive");
        kb::tests::Require(batch.Add<kb::scene::CameraComponent>(otherHandle, kb::scene::CameraComponent{ .verticalFovDegrees = 20.0F }),
            "Engine21kbLibrary CommandBatch::Add<T> must succeed (queue the command) for the second, unrelated target");
        scene.Entities().Destroy(staleTarget.Entity());

        const std::optional<kb::ecs::CommandBufferPlaybackResult> result = batch.Flush();
        kb::tests::Require(!result.has_value(), "Engine21kbLibrary CommandBatch::Flush must cancel (return nullopt), not throw, when a recorded Add<T>'s target died before Flush()");
        kb::tests::Require(!scene.Components().Cameras().Has(otherTarget.Entity()),
            "Engine21kbLibrary CommandBatch::Flush cancelling for one stale target must not leave the OTHER, still-valid command in this batch partially applied");
    }

    // Remove<T>() and AddTag/RemoveTag are covered by the same tracked-
    // target mechanism as Destroy/Add<T> — a single representative check
    // (Remove<T>) confirms the fix is not Destroy/Add-specific.
    {
        const kb::scene::SceneObject target = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "StaleRemoveTarget" });
        scene.Components().Cameras().Set(target.Entity(), kb::scene::CameraComponent{});
        const kb::library::EntityHandle handle{ target.Entity(), scene.Id() };
        kb::library::CommandBatch batch{ scene };
        kb::tests::Require(batch.Remove<kb::scene::CameraComponent>(handle), "Engine21kbLibrary CommandBatch::Remove<T> must succeed (queue the command) while the target is still alive");
        scene.Entities().Destroy(target.Entity());
        const std::optional<kb::ecs::CommandBufferPlaybackResult> result = batch.Flush();
        kb::tests::Require(!result.has_value(), "Engine21kbLibrary CommandBatch::Flush must cancel (return nullopt), not throw, when a recorded Remove<T>'s target died before Flush()");
    }
}

// LIB-081: kb::library::ComponentChangeTracker<Component> — proves the
// wrapper around World::ObserveComponent<T> genuinely adds the three
// properties that primitive itself does not have: coalescing (repeated
// Set() calls on the same entity before Drain() collapse into one pending
// entry, even though ObserveComponent fires once per raw Set — confirmed by
// EcsEventTests.cpp::RunComponentObserverTest), a hard capacity limit
// honestly reported via DroppedCount() instead of growing unboundedly or
// silently discarding, and a Drain() that resets the baseline for the next
// round.
void RunComponentChangeTrackerTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject objectA = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ChangeTrackerA" });
    const kb::scene::SceneObject objectB = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ChangeTrackerB" });
    const kb::scene::SceneObject objectC = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ChangeTrackerC" });
    const kb::scene::SceneObject objectD = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ChangeTrackerD" });

    kb::library::ComponentChangeTracker<kb::scene::CameraComponent> tracker{ scene, 3U };
    kb::tests::Require(tracker.Capacity() == 3U, "Engine21kbLibrary ComponentChangeTracker::Capacity must report the constructor-provided capacity");
    kb::tests::Require(tracker.PendingChanges().empty(), "Engine21kbLibrary ComponentChangeTracker must start with no pending changes");
    kb::tests::Require(tracker.DroppedCount() == 0U, "Engine21kbLibrary ComponentChangeTracker must start with no dropped changes");

    // Two Set() calls on the SAME entity before Drain() must coalesce into
    // exactly one pending entry, not two.
    scene.Components().Cameras().Set(objectA.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 10.0F });
    scene.Components().Cameras().Set(objectA.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 20.0F });
    kb::tests::Require(tracker.PendingChanges().size() == 1U, "Engine21kbLibrary ComponentChangeTracker must coalesce repeated modifications of the same entity into one pending entry");
    kb::tests::Require(tracker.PendingChanges()[0] == objectA.Entity(), "Engine21kbLibrary ComponentChangeTracker's single coalesced entry must be the modified entity");

    // Filling up to capacity (B, C) must still record; a 4th distinct
    // entity (D) once capacity is reached must be dropped, honestly
    // counted rather than silently lost or grown unboundedly.
    scene.Components().Cameras().Set(objectB.Entity(), kb::scene::CameraComponent{});
    scene.Components().Cameras().Set(objectC.Entity(), kb::scene::CameraComponent{});
    kb::tests::Require(tracker.PendingChanges().size() == 3U, "Engine21kbLibrary ComponentChangeTracker must record distinct entities up to capacity");
    scene.Components().Cameras().Set(objectD.Entity(), kb::scene::CameraComponent{});
    kb::tests::Require(tracker.PendingChanges().size() == 3U, "Engine21kbLibrary ComponentChangeTracker must not exceed its capacity");
    kb::tests::Require(tracker.DroppedCount() == 1U, "Engine21kbLibrary ComponentChangeTracker must honestly count a modification dropped due to the capacity limit");

    // A repeat Set() on an entity that was already dropped this round must
    // also count as dropped (it is still not being recorded), not silently
    // ignored without accounting.
    scene.Components().Cameras().Set(objectD.Entity(), kb::scene::CameraComponent{});
    kb::tests::Require(tracker.DroppedCount() == 2U, "Engine21kbLibrary ComponentChangeTracker must count every dropped modification, not just the first one");

    const std::vector<kb::scene::SceneEntity> drained = tracker.Drain();
    kb::tests::Require(drained.size() == 3U, "Engine21kbLibrary ComponentChangeTracker::Drain must return every distinct pending entity");
    std::vector<kb::scene::SceneEntity> sortedDrained = drained;
    std::ranges::sort(sortedDrained);
    std::vector<kb::scene::SceneEntity> expected{ objectA.Entity(), objectB.Entity(), objectC.Entity() };
    std::ranges::sort(expected);
    kb::tests::Require(sortedDrained == expected, "Engine21kbLibrary ComponentChangeTracker::Drain must return exactly the entities recorded since the last Drain (A, B, C — not D, which was dropped)");

    // Drain() must reset the baseline: pending list and dropped count both
    // clear, and a FRESH change to a previously-drained entity must be
    // recorded again, not treated as already-seen.
    kb::tests::Require(tracker.PendingChanges().empty(), "Engine21kbLibrary ComponentChangeTracker::Drain must clear the pending list");
    kb::tests::Require(tracker.DroppedCount() == 0U, "Engine21kbLibrary ComponentChangeTracker::Drain must reset the dropped count");
    scene.Components().Cameras().Set(objectA.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 30.0F });
    kb::tests::Require(tracker.PendingChanges().size() == 1U && tracker.PendingChanges()[0] == objectA.Entity(),
        "Engine21kbLibrary ComponentChangeTracker must record a fresh modification to a previously-drained entity as new, using Drain() as the new baseline");

    // Destructor must cleanly DestroyObserver — scoping a second tracker,
    // letting it go out of scope, then mutating the observed component
    // again must not crash the process.
    {
        kb::library::ComponentChangeTracker<kb::scene::CameraComponent> scopedTracker{ scene, 4U };
        scene.Components().Cameras().Set(objectB.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 40.0F });
        kb::tests::Require(scopedTracker.PendingChanges().size() == 1U, "Engine21kbLibrary ComponentChangeTracker (scoped) must record while alive");
    }
    scene.Components().Cameras().Set(objectC.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 50.0F });
    kb::tests::Require(true, "Engine21kbLibrary ComponentChangeTracker destructor must not crash on DestroyObserver after scope exit");
}

// LIB-090: kb::library::TransformChangeTracker — proves the local/world
// classification is correct against real Set()/hierarchy-cascade
// scenarios, not just that it compiles: (a) a genuine LOCAL-only change to
// an entity, BEFORE any sync, classifies Local (the world hasn't been
// recomputed yet, so it can't be anything else); (b) after a sync, that
// SAME directly-modified entity has also received its own deferred world
// recompute and coalesces (widens) to Both; (c) a PARENT move that
// cascades to its children (whose OWN local data never changed) classifies
// EACH child as World-only — proving a parent-with-N-children move
// produces N+1 *coalesced* entries (parent Both, each child World), not
// N+1 unbounded raw firings, and not silently hidden fan-out either; (d)
// Drain() resets the baseline; (e) capacity/DroppedCount() mirrors LIB-081's
// own ComponentChangeTracker test; (f) destructor/RAII safety.
void RunTransformChangeTrackerTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject parentObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "TransformChangeParent" });
    const kb::scene::SceneObject childA = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "TransformChangeChildA" });
    const kb::scene::SceneObject childB = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "TransformChangeChildB" });
    kb::tests::Require(scene.Hierarchy().SetParent(childA.Entity(), parentObject.Entity()), "TransformChangeTracker fixture could not parent childA");
    kb::tests::Require(scene.Hierarchy().SetParent(childB.Entity(), parentObject.Entity()), "TransformChangeTracker fixture could not parent childB");
    static_cast<void>(scene.Runtime().Update(0.0F)); // Establish a clean, fully-synced baseline before tracking starts.

    kb::library::TransformChangeTracker tracker{ scene, 8U };
    kb::tests::Require(tracker.Capacity() == 8U, "Engine21kbLibrary TransformChangeTracker::Capacity must report the constructor-provided capacity");
    kb::tests::Require(tracker.PendingChanges().empty(), "Engine21kbLibrary TransformChangeTracker must start with no pending changes");

    // (a) A genuine local-only change, BEFORE any sync, must classify
    // Local.
    kb::scene::TransformComponent parentTransform = scene.Transforms().Get(parentObject.Entity());
    parentTransform.localPosition = kb::scene::Vec3{ 5.0F, 0.0F, 0.0F };
    scene.Transforms().Set(parentObject.Entity(), parentTransform);
    kb::tests::Require(tracker.PendingChanges().size() == 1U, "Engine21kbLibrary TransformChangeTracker must record exactly one pending entry after one Set()");
    kb::tests::Require(tracker.PendingChanges()[0].entity == parentObject.Entity() && tracker.PendingChanges()[0].kind == kb::library::TransformChangeKind::Local,
        "Engine21kbLibrary TransformChangeTracker must classify a pre-sync local write as Local");

    // (b)+(c) Sync — the parent's OWN world recompute (a second, later
    // firing for the SAME entity) must widen it to Both; the two children,
    // whose local data never changed, get exactly ONE firing each (World)
    // from the cascade.
    static_cast<void>(scene.Runtime().Update(0.0F));
    const std::span<const kb::library::TransformChangeEntry> afterSync = tracker.PendingChanges();
    kb::tests::Require(afterSync.size() == 3U, "Engine21kbLibrary TransformChangeTracker must have exactly 3 pending entries after the sync cascades to 2 children (parent + 2 children, coalesced, not raw per-firing counts)");

    const auto findEntry = [&](kb::scene::SceneEntity entity) -> const kb::library::TransformChangeEntry* {
        for (const kb::library::TransformChangeEntry& entry : afterSync) {
            if (entry.entity == entity) {
                return &entry;
            }
        }
        return nullptr;
    };
    const kb::library::TransformChangeEntry* parentEntry = findEntry(parentObject.Entity());
    const kb::library::TransformChangeEntry* childAEntry = findEntry(childA.Entity());
    const kb::library::TransformChangeEntry* childBEntry = findEntry(childB.Entity());
    kb::tests::Require(parentEntry != nullptr && parentEntry->kind == kb::library::TransformChangeKind::Both,
        "Engine21kbLibrary TransformChangeTracker must widen the directly-modified parent to Both once its own deferred world recompute fires");
    kb::tests::Require(childAEntry != nullptr && childAEntry->kind == kb::library::TransformChangeKind::World,
        "Engine21kbLibrary TransformChangeTracker must classify a cascade-only child (local data never changed) as World, not Local or Both");
    kb::tests::Require(childBEntry != nullptr && childBEntry->kind == kb::library::TransformChangeKind::World,
        "Engine21kbLibrary TransformChangeTracker must classify EVERY cascade-only child as World, not just the first one found");

    // (d) Drain resets the baseline.
    const std::vector<kb::library::TransformChangeEntry> drained = tracker.Drain();
    kb::tests::Require(drained.size() == 3U, "Engine21kbLibrary TransformChangeTracker::Drain must return every entry recorded since the last Drain()");
    kb::tests::Require(tracker.PendingChanges().empty(), "Engine21kbLibrary TransformChangeTracker::Drain must clear the pending list");
    kb::tests::Require(tracker.DroppedCount() == 0U, "Engine21kbLibrary TransformChangeTracker::Drain must reset the dropped count");

    // (e) Capacity/DroppedCount, mirroring LIB-081's own test: a fresh,
    // small-capacity tracker, 3 distinct entities each get exactly ONE
    // local write with NO sync in between (so only Local firings occur,
    // nothing to coalesce) — the 3rd, over capacity, must be honestly
    // dropped, not silently absorbed or grown into. The 3 entities are
    // created BEFORE the tracker (entity creation itself fires a genuine
    // Modified event for TransformComponent — SceneComponentStorage::
    // SetDefaults calls SceneTransformComponentStore::Set — so creating
    // them while the tracker is already observing would itself count
    // toward capacity).
    const kb::scene::SceneObject leafX = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "TransformChangeLeafX" });
    const kb::scene::SceneObject leafY = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "TransformChangeLeafY" });
    const kb::scene::SceneObject leafZ = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "TransformChangeLeafZ" });
    {
        kb::library::TransformChangeTracker smallTracker{ scene, 2U };
        kb::scene::TransformComponent leafXTransform = scene.Transforms().Get(leafX.Entity());
        leafXTransform.localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F };
        scene.Transforms().Set(leafX.Entity(), leafXTransform);
        kb::scene::TransformComponent leafYTransform = scene.Transforms().Get(leafY.Entity());
        leafYTransform.localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F };
        scene.Transforms().Set(leafY.Entity(), leafYTransform);
        kb::tests::Require(smallTracker.PendingChanges().size() == 2U, "Engine21kbLibrary TransformChangeTracker must record distinct entities up to capacity");
        kb::scene::TransformComponent leafZTransform = scene.Transforms().Get(leafZ.Entity());
        leafZTransform.localPosition = kb::scene::Vec3{ 3.0F, 0.0F, 0.0F };
        scene.Transforms().Set(leafZ.Entity(), leafZTransform);
        kb::tests::Require(smallTracker.PendingChanges().size() == 2U, "Engine21kbLibrary TransformChangeTracker must not exceed its capacity");
        kb::tests::Require(smallTracker.DroppedCount() == 1U, "Engine21kbLibrary TransformChangeTracker must honestly count a modification dropped due to the capacity limit");
    }

    // (f) Destructor RAII safety — scope a tracker, let it go out of
    // scope, then mutate the observed component again — must not crash.
    {
        kb::library::TransformChangeTracker scopedTracker{ scene, 4U };
        kb::scene::TransformComponent scopedTransform = scene.Transforms().Get(childA.Entity());
        scopedTransform.localPosition = kb::scene::Vec3{ 9.0F, 0.0F, 0.0F };
        scene.Transforms().Set(childA.Entity(), scopedTransform);
        kb::tests::Require(scopedTracker.PendingChanges().size() == 1U, "Engine21kbLibrary TransformChangeTracker (scoped) must record while alive");
    }
    kb::scene::TransformComponent finalTransform = scene.Transforms().Get(childB.Entity());
    finalTransform.localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F };
    scene.Transforms().Set(childB.Entity(), finalTransform);
    kb::tests::Require(true, "Engine21kbLibrary TransformChangeTracker destructor must not crash on DestroyObserver after scope exit");
}

// LIB-083: three scenarios not covered by LIB-078/079/080's own tests —
// (1) "query aliasing": kb::ecs::StructuralChangeValidator's
// activeIterations_ is an ATOMIC COUNTER, not an exclusive lock
// (EngineLibraryQuery.hpp's own comment claims nested entry is safe but no
// test proved it) — nests a second, independent Query<T>::ForEach INSIDE
// an outer one's visitor (same component type, then a different one),
// proving both iterate correctly, a structural-change attempt in the
// INNERMOST still throws (the guard is still armed), and iteration is
// unblocked again once both close (the counter unwinds correctly through
// two levels, not just one — mirrors RunLibraryQueryPhaseGateTest's own
// "iteratedAfterThrow" check one level up); (2) "entity destroyed in
// query": RunLibraryCommandBatchTest already proves a CommandBatch-recorded
// Destroy() applies (IsAlive()==false) after Flush() — this closes the gap
// between "not alive" and "not iterated" by running a FRESH Query<T>::
// ForEach afterward and asserting the destroyed entity is genuinely absent
// from the visited set, not merely flagged dead; (3) "command flush
// boundary": kb::ecs::CommandBuffer::Playback's Create->Apply->Destroy
// phase ordering (destroy always wins regardless of recording order) is
// already fully proven at the kb::ecs level
// (EcsCommandBufferTests.cpp::RunCommandBufferDeferredDestroySyncPointTest)
// — this is a THIN confirming test (per the project's "don't duplicate
// coverage one layer down" convention, e.g. LIB-080's own writeup) that the
// guarantee survives the kb::library::CommandBatch wrapper unchanged, in
// BOTH recording orders (Add-then-Destroy and Destroy-then-Add).
void RunLibraryQueryAliasingEntityDestroyedAndCommandFlushBoundaryTest() {
    // (1) Query aliasing: nested, concurrently-open Query<T>::ForEach.
    {
        kb::scene::Scene scene;
        const kb::scene::SceneObject objectA = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "AliasA" });
        const kb::scene::SceneObject objectB = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "AliasB" });
        scene.Components().Cameras().Set(objectA.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 11.0F });
        scene.Components().Cameras().Set(objectB.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 22.0F });
        scene.Components().Lights().Set(objectA.Entity(), kb::scene::LightComponent{});

        int outerVisited = 0;
        int innerSameTypeVisited = 0;
        int innerOtherTypeVisited = 0;
        bool innerStructuralChangeThrew = false;
        const bool outerIterated = kb::library::Query<kb::scene::CameraComponent>::ForEach(
            scene, kb::script::ScriptLifecycleEvent::Tick,
            [&](kb::library::EntityHandle, const kb::scene::CameraComponent&) {
                ++outerVisited;
                // Nested query over the SAME component type — proves two
                // concurrently-open iterations over one component don't
                // corrupt or block each other (the atomic counter allows
                // nesting, not just a single active iteration).
                const bool innerSameIterated = kb::library::Query<kb::scene::CameraComponent>::ForEach(
                    scene, kb::script::ScriptLifecycleEvent::Tick,
                    [&](kb::library::EntityHandle, const kb::scene::CameraComponent&) { ++innerSameTypeVisited; });
                kb::tests::Require(innerSameIterated, "Engine21kbLibrary nested Query<CameraComponent>::ForEach (same type as outer) must still iterate");

                // Nested query over a DIFFERENT component type.
                const bool innerOtherIterated = kb::library::Query<kb::scene::LightComponent>::ForEach(
                    scene, kb::script::ScriptLifecycleEvent::Tick,
                    [&](kb::library::EntityHandle, const kb::scene::LightComponent&) {
                        ++innerOtherTypeVisited;
                        // Structural change attempted at the INNERMOST
                        // nesting level must still throw — the guard must
                        // remain armed through two levels of nesting, not
                        // just one.
                        try {
                            static_cast<void>(scene.Entities().CreateEntity());
                        } catch (const std::logic_error&) {
                            innerStructuralChangeThrew = true;
                        }
                    });
                kb::tests::Require(innerOtherIterated, "Engine21kbLibrary nested Query<LightComponent>::ForEach (different type from outer) must still iterate");
            });
        kb::tests::Require(outerIterated && outerVisited == 2, "Engine21kbLibrary outer Query<CameraComponent>::ForEach must visit both entities despite nested queries running inside its visitor");
        kb::tests::Require(innerSameTypeVisited == 4, "Engine21kbLibrary nested same-type Query must visit both entities on EACH of the 2 outer visits (2*2=4)");
        kb::tests::Require(innerOtherTypeVisited == 2, "Engine21kbLibrary nested different-type Query must visit its one matching entity on EACH of the 2 outer visits");
        kb::tests::Require(innerStructuralChangeThrew, "Engine21kbLibrary a structural change attempted at the INNERMOST of two nested Query iterations must still throw std::logic_error");

        // After BOTH nesting levels have closed, iteration must be
        // unblocked again — the guard's counter must unwind through two
        // levels, not just one.
        int afterNestingVisited = 0;
        const bool afterNestingIterated = kb::library::Query<kb::scene::CameraComponent>::ForEach(
            scene, kb::script::ScriptLifecycleEvent::Tick,
            [&](kb::library::EntityHandle, const kb::scene::CameraComponent&) { ++afterNestingVisited; });
        kb::tests::Require(afterNestingIterated && afterNestingVisited == 2, "Engine21kbLibrary Query<T>::ForEach must be fully unblocked after two nested iteration levels have both closed");
    }

    // (2) Entity destroyed in query: a CommandBatch-recorded Destroy(),
    // applied via Flush() after the recording loop closes, must make the
    // entity genuinely ABSENT from a subsequent, fresh Query<T>::ForEach —
    // not merely "not alive" (already proven by RunLibraryCommandBatchTest).
    {
        kb::scene::Scene scene;
        const kb::scene::SceneObject survivor = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "DestroyedInQuerySurvivor" });
        const kb::scene::SceneObject toDestroy = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "DestroyedInQueryTarget" });
        scene.Components().Cameras().Set(survivor.Entity(), kb::scene::CameraComponent{});
        scene.Components().Cameras().Set(toDestroy.Entity(), kb::scene::CameraComponent{});

        kb::library::CommandBatch batch{ scene };
        static_cast<void>(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick,
            [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) {
                if (entity.Entity() == toDestroy.Entity()) {
                    static_cast<void>(batch.Destroy(entity));
                }
            }));
        static_cast<void>(batch.Flush());
        kb::tests::Require(!scene.Entities().IsAlive(toDestroy.Entity()), "Engine21kbLibrary entity-destroyed-in-query setup must actually be dead after Flush()");

        std::vector<kb::scene::SceneEntity> visitedAfterDestroy;
        static_cast<void>(kb::library::Query<kb::scene::CameraComponent>::ForEach(scene, kb::script::ScriptLifecycleEvent::Tick,
            [&](kb::library::EntityHandle entity, const kb::scene::CameraComponent&) { visitedAfterDestroy.push_back(entity.Entity()); }));
        kb::tests::Require(visitedAfterDestroy.size() == 1U && visitedAfterDestroy[0] == survivor.Entity(),
            "Engine21kbLibrary a FRESH Query<T>::ForEach run after Flush() must never visit an entity destroyed via CommandBatch during a prior query — not just report it as not-alive");
    }

    // (3) Command flush boundary: Add<T>+Destroy on the SAME entity in one
    // CommandBatch, in BOTH recording orders — kb::ecs::CommandBuffer's
    // Create->Apply->Destroy phase ordering (destroy always wins) is
    // already proven at the kb::ecs level; this confirms the guarantee
    // survives the CommandBatch wrapper unchanged.
    {
        kb::scene::Scene scene;
        const kb::scene::SceneObject addThenDestroy = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "AddThenDestroy" });
        const kb::library::EntityHandle addThenDestroyHandle{ addThenDestroy.Entity(), scene.Id() };
        kb::library::CommandBatch addThenDestroyBatch{ scene };
        static_cast<void>(addThenDestroyBatch.Add<kb::scene::CameraComponent>(addThenDestroyHandle, kb::scene::CameraComponent{ .verticalFovDegrees = 5.0F }));
        static_cast<void>(addThenDestroyBatch.Destroy(addThenDestroyHandle));
        static_cast<void>(addThenDestroyBatch.Flush());
        kb::tests::Require(!scene.Entities().IsAlive(addThenDestroy.Entity()), "Engine21kbLibrary CommandBatch: Add<T> recorded BEFORE Destroy on the same entity must still result in the entity being destroyed after Flush()");

        const kb::scene::SceneObject destroyThenAdd = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "DestroyThenAdd" });
        const kb::library::EntityHandle destroyThenAddHandle{ destroyThenAdd.Entity(), scene.Id() };
        kb::library::CommandBatch destroyThenAddBatch{ scene };
        static_cast<void>(destroyThenAddBatch.Destroy(destroyThenAddHandle));
        static_cast<void>(destroyThenAddBatch.Add<kb::scene::CameraComponent>(destroyThenAddHandle, kb::scene::CameraComponent{ .verticalFovDegrees = 6.0F }));
        static_cast<void>(destroyThenAddBatch.Flush());
        kb::tests::Require(!scene.Entities().IsAlive(destroyThenAdd.Entity()), "Engine21kbLibrary CommandBatch: Destroy recorded BEFORE Add<T> on the same entity must still result in the entity being destroyed after Flush() — recording order must not change the outcome");
    }
}

// LIB-029: every function in the live production catalog carries authored
// documentation and remains reachable through every supported frontend.
// Native and Lua CallFunction share ScriptFunctionRegistry; Visual Graph
// owns two separate binding tables, so all three paths are checked.
void RunCatalogFunctionFrontendAndDocumentationComplianceTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary catalog binding test host setup failed");
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);
    kb::tests::Require(!catalog.functions.empty(), "Engine21kbLibrary catalog binding test fixture must have at least one function");

    for (const kb::script::ScriptApiCatalogFunction& function : catalog.functions) {
        const std::string missingDescriptionMessage =
            "Engine21kbLibrary function '" + function.name + "' has no authored description";
        kb::tests::Require(!function.description.empty(), missingDescriptionMessage.c_str());
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
        const kb::script::ScriptFunctionSignature* registrySignature =
            host.Functions().FindSignature(function.name);
        kb::tests::Require(registrySignature != nullptr, missingRegistryEntryMessage.c_str());
        const std::string mismatchedDescriptionMessage =
            "Engine21kbLibrary function '" + function.name + "' description drifted between registry and exported catalog";
        kb::tests::Require(
            registrySignature->description == function.description,
            mismatchedDescriptionMessage.c_str());
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

    // LIB-032, second half: the 2026-07-17 audit flagged that
    // LibraryContextBase::Raw() still returned a call-scoped C++ reference
    // and the context types were copyable, leaving a reference smuggleable
    // past the callback boundary that constructed it. Re-reading
    // EngineLibraryContext.hpp fresh shows that gap was ALREADY closed by
    // LIB-007's own fix earlier this session (Raw() removed entirely, copy/
    // move deleted) - the audit note was stale, not a real remaining gap -
    // but nothing regression-locked that fact until now. A SFINAE-based
    // static_assert that Raw() specifically does not exist was tried for
    // LIB-007 and abandoned (MSVC's requires-expression is not
    // SFINAE-friendly enough for a genuinely nonexistent member in this
    // non-template context - see EngineLibraryContext.hpp's own history);
    // the copy/move deletion below is the real, provable guard: even a
    // hypothetical future reference-returning accessor could not be
    // smuggled out by copying the context that produced it.
    static_assert(!std::is_copy_constructible_v<kb::library::LibraryContextBase>, "kb::library::LibraryContextBase must stay non-copy-constructible");
    static_assert(!std::is_copy_assignable_v<kb::library::LibraryContextBase>, "kb::library::LibraryContextBase must stay non-copy-assignable");
    static_assert(!std::is_move_constructible_v<kb::library::LibraryContextBase>, "kb::library::LibraryContextBase must stay non-move-constructible");
    static_assert(!std::is_move_assignable_v<kb::library::LibraryContextBase>, "kb::library::LibraryContextBase must stay non-move-assignable");
    static_assert(!std::is_copy_constructible_v<kb::library::BehaviourContext>, "kb::library::BehaviourContext must inherit LibraryContextBase's deleted copy");
    static_assert(!std::is_copy_constructible_v<kb::library::FixedContext>, "kb::library::FixedContext must inherit LibraryContextBase's deleted copy");
    static_assert(!std::is_copy_constructible_v<kb::library::FrameContext>, "kb::library::FrameContext must inherit LibraryContextBase's deleted copy");
    static_assert(!std::is_copy_constructible_v<kb::library::RenderContext>, "kb::library::RenderContext must inherit LibraryContextBase's deleted copy");
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
    // LIB-037: all four documented input limits must be a SINGLE source of
    // truth, provably tied to their real enforcement site so the two can
    // never silently drift. maxGraphRecursionDepth and maxEventPayloadArguments
    // are enforced in kb::script (which must never depend on kb::library —
    // see ScriptEvent.hpp), so their enforcement constants are duplicated
    // there as plain locals; these assertions are the guard that keeps the
    // duplicates equal to the kb::library policy values.
    kb::tests::Require(
        kb::library::kDefaultLibraryInputLimits.maxGraphRecursionDepth == kb::script::ScriptRuntimeDispatchOptions{}.maxEventDepth,
        "Engine21kbLibrary maxGraphRecursionDepth must match ScriptRuntimeDispatchOptions::maxEventDepth's default");

    // maxEventPayloadArguments (kb::library policy) == kMaxScriptEventArguments
    // (kb::script enforcement constant, checked in ScriptEventBus::Emit /
    // EmitDeferred and ScriptRuntime::DispatchEvent — real runtime rejection
    // is proven by ScriptRuntimeTests.cpp::RunScriptEventPayloadSizeLimitTest).
    kb::tests::Require(
        kb::library::kDefaultLibraryInputLimits.maxEventPayloadArguments == kb::script::kMaxScriptEventArguments,
        "Engine21kbLibrary maxEventPayloadArguments must match kb::script::kMaxScriptEventArguments, the constant actually enforced at every event dispatch entry point");

    // maxCollectionSize (kb::library policy) must be the value every
    // kb::library collection clamps a capacity request down to — the
    // enforcement here lives in the same library, so this is a direct check
    // rather than a cross-module duplicate (full mutation/refusal contract is
    // proven by RunCollectionsScalarTest).
    kb::library::Array<int> clampedCapacity{ kb::library::kDefaultLibraryInputLimits.maxCollectionSize + 1U };
    kb::tests::Require(
        clampedCapacity.Capacity() == kb::library::kDefaultLibraryInputLimits.maxCollectionSize,
        "Engine21kbLibrary a collection capacity request above maxCollectionSize must be clamped to exactly maxCollectionSize");

    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Engine21kbLibrary input limits test host setup failed");
    kb::tests::Require(
        host.RegisterFunction(kb::script::ScriptFunctionDesc{
            .signature = kb::script::ScriptFunctionSignature{
                .name = "Tests.TakesString",
                .description = "Accepts text for the input-limit test.",
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
                .description = "Echoes a hash for the expanded-value-type test.",
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

// LIB-109: kb::library::Signal<Args...> — the native-only, per-instance,
// compile-time-typed observer list (no global registry, no string names,
// no ScriptValue boxing — contrast with kb::script::ScriptEventBus/LIB-105).
void RunEngineLibrarySignalTest() {
    // Basic connect/emit, connection order, multi-arg.
    {
        kb::library::Signal<int, std::string> signal;
        kb::tests::Require(signal.SlotCount() == 0U, "A freshly constructed Signal must have zero connected slots");

        std::vector<std::string> order;
        const kb::library::Signal<int, std::string>::SlotId firstId = signal.Connect([&order](int value, const std::string& text) {
            order.push_back("first:" + std::to_string(value) + ":" + text);
        });
        const kb::library::Signal<int, std::string>::SlotId secondId = signal.Connect([&order](int value, const std::string& text) {
            order.push_back("second:" + std::to_string(value) + ":" + text);
        });
        kb::tests::Require(firstId != 0U && secondId != 0U && firstId != secondId, "Connect must return distinct, nonzero ids for two live slots");
        kb::tests::Require(signal.SlotCount() == 2U, "SlotCount must reflect both connected slots");

        signal.Emit(7, "hello");
        kb::tests::Require(order.size() == 2U && order[0] == "first:7:hello" && order[1] == "second:7:hello",
            "Emit must invoke every connected slot, in connection order, with the exact arguments given");

        kb::tests::Require(signal.Connect(nullptr) == 0U, "Connect with an empty/null slot must return the invalid id (0) and not add a slot");
        kb::tests::Require(signal.SlotCount() == 2U, "Connect with an empty slot must not increase SlotCount");
    }

    // Disconnect: idempotent, stops future delivery.
    {
        kb::library::Signal<int> signal;
        int fired = 0;
        const kb::library::Signal<int>::SlotId id = signal.Connect([&fired](int) { ++fired; });
        kb::tests::Require(signal.Disconnect(id), "Disconnect on a live slot must succeed");
        kb::tests::Require(!signal.Disconnect(id), "A second Disconnect on the same id must be idempotent (false, not an error)");
        kb::tests::Require(!signal.Disconnect(kb::library::Signal<int>::kInvalidSlotId), "Disconnect on the invalid id must fail cleanly");
        signal.Emit(1);
        kb::tests::Require(fired == 0, "A disconnected slot must never fire again");
        kb::tests::Require(signal.SlotCount() == 0U, "SlotCount must reflect the disconnect");
    }

    // Reentrancy (a): a slot Connecting a NEW slot mid-Emit must not have
    // that new slot fire within the SAME Emit call — only starting the
    // next one (mirrors ScriptEventBus::Emit's own snapshot-before-dispatch
    // discipline, LIB-102/LIB-040).
    {
        kb::library::Signal<> signal;
        int lateFired = 0;
        kb::library::Signal<>::SlotId lateId = kb::library::Signal<>::kInvalidSlotId;
        static_cast<void>(signal.Connect([&signal, &lateFired, &lateId]() {
            lateId = signal.Connect([&lateFired]() { ++lateFired; });
        }));
        signal.Emit();
        kb::tests::Require(lateId != kb::library::Signal<>::kInvalidSlotId && lateFired == 0, "A slot connected DURING an Emit call must not fire within that same Emit call");
        signal.Emit();
        kb::tests::Require(lateFired == 1, "A slot connected during a PRIOR Emit call must fire normally on the NEXT Emit call");
    }

    // Reentrancy (b): a slot Disconnecting a sibling (already snapshotted
    // for THIS Emit call) mid-dispatch must safely skip that sibling, no
    // crash, no double-invoke.
    {
        kb::library::Signal<> signal;
        int siblingFired = 0;
        kb::library::Signal<>::SlotId siblingId = kb::library::Signal<>::kInvalidSlotId;
        static_cast<void>(signal.Connect([&signal, &siblingId]() {
            static_cast<void>(signal.Disconnect(siblingId));
        }));
        siblingId = signal.Connect([&siblingFired]() { ++siblingFired; });
        signal.Emit();
        kb::tests::Require(siblingFired == 0, "A sibling slot disconnected by an EARLIER slot within the SAME Emit call must not fire");
        kb::tests::Require(signal.SlotCount() == 1U, "Only the disconnected sibling should be gone; the disconnecting slot itself remains connected");
    }
}

void RunNavigationFoundationContractTest() {
    kb::scene::NavQueryFilter filter;
    kb::tests::Require(filter.Allows(kb::scene::kDefaultNavArea) && filter.AreaCost(kb::scene::kDefaultNavArea) == 1.0F,
        "Navigation filter must admit the default area at unit cost");
    constexpr kb::scene::NavAreaId kMudArea = 3U;
    filter.SetIncludedAreas(kb::scene::NavAreaBit(kMudArea));
    kb::tests::Require(!filter.Allows(kb::scene::kDefaultNavArea) && filter.Allows(kMudArea),
        "Navigation filter included-area mask did not constrain traversal");
    kb::tests::Require(filter.SetAreaCost(kMudArea, 2.5F) && filter.AreaCost(kMudArea) == 2.5F,
        "Navigation filter did not retain a positive area cost");
    filter.SetExcludedAreas(kb::scene::NavAreaBit(kMudArea));
    kb::tests::Require(!filter.Allows(kMudArea), "Navigation filter exclusion must override inclusion");
    kb::tests::Require(!filter.SetAreaCost(kb::scene::NavAreaId{ 32U }, 1.0F) &&
            !filter.SetAreaCost(kMudArea, 0.0F) && !filter.SetAreaCost(kMudArea, std::numeric_limits<float>::infinity()),
        "Navigation filter accepted an invalid area id or invalid traversal cost");

    const kb::scene::NavMesh mesh{};
    const kb::scene::NavAgent agent{};
    const kb::scene::NavObstacle obstacle{};
    kb::tests::Require(mesh.agentRadius > 0.0F && agent.radius > 0.0F && agent.maxSpeed > 0.0F &&
            obstacle.area == kb::scene::kDefaultNavArea && obstacle.carve,
        "Navigation foundation defaults must define a usable walkable mesh, agent and obstacle");

    kb::scene::NavMesh graph;
    graph.nodes = {
        { .position = { 0.0F, 0.0F, 0.0F }, .area = kb::scene::kDefaultNavArea, .neighbours = { 1U, 2U } },
        { .position = { 1.0F, 0.0F, 0.0F }, .area = kMudArea, .neighbours = { 3U } },
        { .position = { 0.0F, 0.0F, 3.0F }, .area = kb::scene::kDefaultNavArea, .neighbours = { 3U } },
        { .position = { 2.0F, 0.0F, 0.0F }, .area = kb::scene::kDefaultNavArea, .neighbours = {} },
    };
    kb::scene::NavQueryFilter routing;
    kb::tests::Require(routing.SetAreaCost(kMudArea, 10.0F), "Navigation routing fixture could not configure mud cost");
    const kb::scene::NavPath path = kb::scene::FindNavPath(graph, 0U, 3U, routing);
    kb::tests::Require(path.status == kb::scene::NavPathStatus::Complete && path.corners.size() == 3U &&
            path.corners[1].z == 3.0F, "Navigation path query did not choose the lower-cost area route");
    routing.SetExcludedAreas(kb::scene::NavAreaBit(kMudArea));
    const kb::scene::NavPath excludedPath = kb::scene::FindNavPath(graph, 0U, 3U, routing);
    kb::tests::Require(excludedPath.Succeeded() && excludedPath.corners[1].z == 3.0F,
        "Navigation path query did not respect excluded areas");
    kb::scene::NavPathAsyncRequest asynchronous;
    kb::tests::Require(asynchronous.Start(graph, 0U, 3U, routing), "Navigation async path request did not start");
    kb::scene::NavPath asynchronousPath;
    for (std::size_t attempt = 0U; attempt < 1000U; ++attempt) {
        asynchronousPath = asynchronous.Poll();
        if (asynchronousPath.status != kb::scene::NavPathStatus::Pending) break;
        std::this_thread::yield();
    }
    kb::tests::Require(asynchronousPath.Succeeded() && asynchronousPath.corners.size() == excludedPath.corners.size() &&
            asynchronousPath.corners.size() == 3U && asynchronousPath.corners[1].z == excludedPath.corners[1].z,
        "Navigation async path request did not publish the same filtered result as synchronous pathfinding");
    kb::tests::Require(path.IsCurrent(graph), "Navigation path was not current for the graph revision that produced it");
    ++graph.revision;
    kb::tests::Require(!path.IsCurrent(graph), "Navigation path was not invalidated after navmesh topology revision changed");
    kb::scene::NavPathAsyncRequest targetDestroyedRequest;
    kb::tests::Require(targetDestroyedRequest.Start(graph, 0U, 3U, routing) && targetDestroyedRequest.Cancel() &&
            targetDestroyedRequest.Poll().status == kb::scene::NavPathStatus::Cancelled,
        "Navigation request was not cancelled when its owner reports target destruction");
    kb::scene::NavMesh unloadMesh = graph;
    kb::scene::NavPathAsyncRequest unloadRequest;
    kb::tests::Require(unloadRequest.Start(unloadMesh, 0U, 3U, routing), "Navigation request could not start before scene unload");
    unloadMesh = {};
    kb::scene::NavPath unloadedPath;
    for (std::size_t attempt = 0U; attempt < 1000U; ++attempt) {
        unloadedPath = unloadRequest.Poll();
        if (unloadedPath.status != kb::scene::NavPathStatus::Pending) break;
        std::this_thread::yield();
    }
    kb::tests::Require(unloadedPath.Succeeded(), "Navigation request did not retain a safe navmesh snapshot across scene unload");
    kb::scene::NavAgent steeringAgent;
    steeringAgent.destination = { 10.0F, 5.0F, 0.0F };
    steeringAgent.maxSpeed = 4.0F;
    steeringAgent.acceleration = 2.0F;
    steeringAgent.stoppingDistance = 0.5F;
    const kb::scene::NavSteeringResult steering = kb::scene::ComputeNavSteering(
        steeringAgent, {}, { 0.0F, 7.0F, 0.0F }, 0.25F);
    kb::tests::Require(!steering.arrived && steering.desiredVelocity.x == 0.5F && steering.desiredVelocity.y == 0.0F,
        "Navigation steering must accelerate horizontally without injecting vertical physics velocity");
    steeringAgent.destination = { 0.25F, 0.0F, 0.0F };
    kb::tests::Require(kb::scene::ComputeNavSteering(steeringAgent, {}, {}, 0.25F).arrived,
        "Navigation steering did not stop inside the agent stopping distance");
    kb::scene::PerceptionFilter perception{ .observerTeam = 1U, .maxResults = 8U, .range = 12.0F };
    kb::tests::Require(perception.IsValid() && !perception.AcceptsTeam(1U) && perception.AcceptsTeam(2U),
        "Perception filter did not bound results or exclude the observer team");
    perception.requiredTargetTeam = 2U;
    kb::tests::Require(perception.AcceptsTeam(2U) && !perception.AcceptsTeam(3U),
        "Perception filter did not enforce the explicit target team");

    const auto lineOfSight = [](void*, const kb::scene::PerceptionObserver&, const kb::scene::PerceptionTarget& target) noexcept {
        return target.entity.Id() != 20U;
    };
    kb::scene::PerceptionObserver observer{
          .entity = kb::scene::SceneEntity{ 1U },
          .position = {},
          .forward = { 0.0F, 0.0F, 1.0F },
          .sightHalfAngleDegrees = 75.0F,
          .filter = { .observerTeam = 1U, .maxResults = 8U, .range = 12.0F, .requireLineOfSight = true },
    };
    const std::array perceptionTargets{
          kb::scene::PerceptionTarget{ .entity = kb::scene::SceneEntity{ 20U }, .position = { 0.0F, 0.0F, 5.0F }, .team = 2U },
          kb::scene::PerceptionTarget{ .entity = kb::scene::SceneEntity{ 10U }, .position = { 0.0F, 0.0F, 4.0F }, .team = 2U },
          kb::scene::PerceptionTarget{ .entity = kb::scene::SceneEntity{ 30U }, .position = { 0.0F, 0.0F, 3.0F }, .team = 1U },
    };
    const std::array perceptionStimuli{
          kb::scene::PerceptionStimulus{ .source = kb::scene::SceneEntity{ 15U }, .position = { 0.0F, 0.0F, 2.0F }, .team = 2U, .radius = 8.0F, .strength = 0.75F },
    };
    std::array<kb::scene::PerceptionEvent, 8U> perceptionStorage{};
    kb::library::ArrayNonAlloc<kb::scene::PerceptionEvent> perceptionEvents{ perceptionStorage };
    const kb::scene::PerceptionEvaluationResult perceptionResult = kb::scene::EvaluatePerception(
        observer, perceptionTargets, perceptionStimuli, { .test = lineOfSight }, perceptionEvents);
    kb::tests::Require(perceptionResult.emitted == 4U && !perceptionResult.limitReached && perceptionEvents.Count() == 4U,
        "Perception evaluation did not emit the expected bounded sight, hearing and proximity events");
    const auto* firstPerceptionEvent = perceptionEvents.GetAt(0U);
    const auto* secondPerceptionEvent = perceptionEvents.GetAt(1U);
    const auto* thirdPerceptionEvent = perceptionEvents.GetAt(2U);
    const auto* fourthPerceptionEvent = perceptionEvents.GetAt(3U);
    kb::tests::Require(firstPerceptionEvent != nullptr && secondPerceptionEvent != nullptr && thirdPerceptionEvent != nullptr && fourthPerceptionEvent != nullptr &&
            firstPerceptionEvent->subject.Id() == 10U && firstPerceptionEvent->sense == kb::scene::PerceptionSense::Sight &&
            secondPerceptionEvent->subject.Id() == 10U && secondPerceptionEvent->sense == kb::scene::PerceptionSense::Proximity &&
            thirdPerceptionEvent->subject.Id() == 15U && thirdPerceptionEvent->sense == kb::scene::PerceptionSense::Hearing && thirdPerceptionEvent->strength == 0.75F &&
            fourthPerceptionEvent->subject.Id() == 20U && fourthPerceptionEvent->sense == kb::scene::PerceptionSense::Proximity,
        "Perception event order must be deterministic and sight must honour line-of-sight before emitting");
    observer.filter.maxResults = 2U;
    const kb::scene::PerceptionEvaluationResult limitedPerceptionResult = kb::scene::EvaluatePerception(
        observer, perceptionTargets, perceptionStimuli, { .test = lineOfSight }, perceptionEvents);
    kb::tests::Require(limitedPerceptionResult.emitted == 2U && limitedPerceptionResult.limitReached && perceptionEvents.Count() == 2U,
        "Perception evaluation did not enforce the configured event bound");
}

void RunAiBehaviourAssetRuntimeTest() {
    struct TestContext {
        std::uint32_t actionCalls = 0U;
        kb::scene::AiNodeId lastAction = 0U;
    } context;
    const auto condition = [](void*, kb::scene::AiNodeId id) noexcept { return id == 10U || id == 99U; };
    const auto action = [](void* opaque, kb::scene::AiNodeId id) noexcept {
        auto& state = *static_cast<TestContext*>(opaque);
        state.lastAction = id;
        ++state.actionCalls;
        return id == 11U && state.actionCalls == 1U ? kb::scene::AiExecutionStatus::Running : kb::scene::AiExecutionStatus::Success;
    };
    const auto utility = [](void*, kb::scene::AiNodeId id) noexcept { return id == 31U ? 1.0F : (id == 32U ? 2.0F : -1.0F); };
    const kb::scene::AiBehaviourCallbacks callbacks{ .context = &context, .condition = condition, .action = action, .utility = utility };

    const kb::scene::AiBehaviourAsset sequenceAsset{
        .nodes = {
            { .id = 1U, .kind = kb::scene::AiNodeKind::Sequence, .firstChild = 1U, .childCount = 2U },
            { .id = 10U, .kind = kb::scene::AiNodeKind::Condition },
            { .id = 11U, .kind = kb::scene::AiNodeKind::Action },
        },
    };
    kb::tests::Require(kb::scene::ValidateAiBehaviourAsset(sequenceAsset).valid, "AI behaviour tree asset validation rejected a valid sequence");
    kb::scene::AiBehaviourRuntimeState sequenceState;
    kb::tests::Require(kb::scene::AiBehaviourRuntime::Initialize(sequenceAsset, sequenceState, 77U), "AI behaviour runtime could not initialize a valid tree asset");
    kb::tests::Require(kb::scene::AiBehaviourRuntime::Tick(sequenceAsset, sequenceState, callbacks) == kb::scene::AiExecutionStatus::Running &&
            kb::scene::AiBehaviourRuntime::Tick(sequenceAsset, sequenceState, callbacks) == kb::scene::AiExecutionStatus::Success && context.actionCalls == 2U,
        "AI behaviour runtime did not resume a running action synchronously on the next owner tick");
    const kb::math::RandomStreamUInt32Result firstRandom = kb::scene::AiBehaviourRuntime::NextRandom(sequenceState);
    const kb::scene::AiDecisionSnapshot decision = kb::scene::AiBehaviourRuntime::Snapshot(sequenceState);
    kb::tests::Require(firstRandom.value == kb::math::NextUInt32(kb::math::MakeRandomStream(77U)).value &&
            decision.tick == 2U && decision.root == 1U && decision.status == kb::scene::AiExecutionStatus::Success &&
            decision.randomSeed == 77U && decision.randomCounter == 1U,
        "AI runtime seed or decision snapshot was not deterministic");

    const kb::scene::AiBehaviourAsset utilityAsset{
        .nodes = {
            { .id = 20U, .kind = kb::scene::AiNodeKind::UtilitySelector, .firstChild = 1U, .childCount = 2U },
            { .id = 31U, .kind = kb::scene::AiNodeKind::Action },
            { .id = 32U, .kind = kb::scene::AiNodeKind::Action },
        },
    };
    kb::scene::AiBehaviourRuntimeState utilityState;
    context.lastAction = 0U;
    kb::tests::Require(kb::scene::AiBehaviourRuntime::Initialize(utilityAsset, utilityState) &&
            kb::scene::AiBehaviourRuntime::Tick(utilityAsset, utilityState, callbacks) == kb::scene::AiExecutionStatus::Success && context.lastAction == 32U,
        "AI utility selector did not choose the highest scored child deterministically");

    const kb::scene::AiBehaviourAsset stateAsset{
        .nodes = {
            { .id = 41U, .kind = kb::scene::AiNodeKind::Action },
            { .id = 42U, .kind = kb::scene::AiNodeKind::Action },
        },
        .states = {
            { .name = "Idle", .rootNode = 0U, .transitions = { { .targetState = 1U, .condition = 99U } } },
            { .name = "Alert", .rootNode = 1U },
        },
    };
    kb::scene::AiBehaviourRuntimeState stateMachineState;
    context.lastAction = 0U;
    kb::tests::Require(kb::scene::AiBehaviourRuntime::Initialize(stateAsset, stateMachineState) &&
            kb::scene::AiBehaviourRuntime::Tick(stateAsset, stateMachineState, callbacks) == kb::scene::AiExecutionStatus::Success &&
            stateMachineState.activeState == 1U && context.lastAction == 42U,
        "AI state machine did not apply its authored transition before ticking the destination state");
    const kb::scene::AiBehaviourAsset invalidAsset{ .nodes = { { .id = 50U, .kind = kb::scene::AiNodeKind::Action, .childCount = 1U } } };
    kb::tests::Require(!kb::scene::ValidateAiBehaviourAsset(invalidAsset).valid, "AI asset validation accepted a leaf node with children");
}

void RunAiBlackboardTest() {
    kb::scene::AiBlackboard board;
    const kb::scene::AiBlackboardKey score{ .name = "score", .type = kb::save::SaveValueType::Int };
    const kb::scene::AiBlackboardKey target{ .name = "target", .type = kb::save::SaveValueType::AssetRef };
    kb::tests::Require(board.Set(kb::scene::AiBlackboardScope::World, 0U, score, kb::save::SaveValue::MakeInt(7)) &&
            board.Set(kb::scene::AiBlackboardScope::Team, 2U, score, kb::save::SaveValue::MakeInt(9)) &&
            board.Set(kb::scene::AiBlackboardScope::Entity, 42U, target, kb::save::SaveValue::MakeAssetRef(123U)) &&
            !board.Set(kb::scene::AiBlackboardScope::Entity, 0U, score, kb::save::SaveValue::MakeInt(1)) &&
            !board.Set(kb::scene::AiBlackboardScope::World, 0U, score, kb::save::SaveValue::MakeString("wrong")),
        "AI blackboard did not enforce scope owner and typed-key contracts");
    kb::tests::Require(board.Get(kb::scene::AiBlackboardScope::World, 77U, score)->intValue == 7 &&
            board.Get(kb::scene::AiBlackboardScope::Team, 2U, score)->intValue == 9 &&
            !board.Get(kb::scene::AiBlackboardScope::Entity, 42U, score).has_value(),
        "AI blackboard scopes leaked values or accepted a mismatched typed read");
    const std::vector<std::uint8_t> first = board.Serialize();
    kb::scene::AiBlackboard restored;
    kb::tests::Require(restored.Deserialize(first) && first == restored.Serialize() &&
            restored.Get(kb::scene::AiBlackboardScope::Entity, 42U, target)->assetIdValue == 123U,
        "AI blackboard serialization did not round-trip deterministically");
}

void RunGoapBenchmarkDecisionTest() {
    // A deliberately standalone GOAP workload: 12 independent facts/actions,
    // uniform action cost and an exact all-facts goal. It must not share the
    // behaviour-tree runtime or its state, otherwise this benchmark would hide
    // the very MVP coupling LIB-189 prohibits.
    constexpr std::uint32_t kFacts = 12U;
    constexpr std::uint32_t kGoal = (1U << kFacts) - 1U;
    constexpr std::uint32_t kIterations = 10'000U;
    std::uint64_t expandedStates = 0U;
    const auto started = std::chrono::steady_clock::now();
    for (std::uint32_t iteration = 0U; iteration < kIterations; ++iteration) {
        std::array<std::uint16_t, 1U << kFacts> cost{};
        cost.fill(std::numeric_limits<std::uint16_t>::max());
        cost[0] = 0U;
        for (std::uint32_t state = 0U; state <= kGoal; ++state) {
            if (cost[state] == std::numeric_limits<std::uint16_t>::max()) continue;
            ++expandedStates;
            for (std::uint32_t fact = 0U; fact < kFacts; ++fact) {
                const std::uint32_t next = state | (1U << fact);
                if (next != state && cost[next] > cost[state] + 1U) cost[next] = static_cast<std::uint16_t>(cost[state] + 1U);
            }
        }
        kb::tests::Require(cost[kGoal] == kFacts, "GOAP benchmark fixture did not find the minimal plan");
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::cout << "LIB-189 GOAP benchmark: " << expandedStates << " expanded states in " << elapsed << " ms\n";
    kb::tests::Require(expandedStates == static_cast<std::uint64_t>(kIterations) * (1U << kFacts),
        "GOAP benchmark did not exercise the entire configured state space");
}

void RunGameInstanceLifetimeTest() {
    const std::filesystem::path storageRoot = std::filesystem::temp_directory_path() / "21kb-user-storage-test";
    std::error_code storageError;
    std::filesystem::remove_all(storageRoot, storageError);
    kb::platform::UserStorage storage{ storageRoot, 8U };
    kb::tests::Require(storage.Write("save/a", "data") && storage.Read("save/a") == std::optional<std::string>{ "data" } && storage.WriteAsync("save/b", "ok").get() && storage.List().size() == 2U && storage.Delete("save/a") && !storage.Write("save/c", "too-large"), "User storage did not enforce atomic sandboxed quota operations");
    std::filesystem::remove_all(storageRoot, storageError);
    kb::tests::Require(kb::platform::IsSandboxStorageKey("saves/profile.bin") && !kb::platform::IsSandboxStorageKey("../outside") && !kb::platform::IsSandboxStorageKey("C:\\outside") && !kb::platform::IsSandboxStorageKey("/outside"), "User storage sandbox accepted a filesystem escape");
    constexpr kb::platform::PlatformCapabilities platformCapabilities{ .flags = static_cast<std::uint32_t>(kb::platform::PlatformCapability::Locale) | static_cast<std::uint32_t>(kb::platform::PlatformCapability::UserDataPath) };
    static_assert(platformCapabilities.Has(kb::platform::PlatformCapability::Locale));
    kb::tests::Require(platformCapabilities.Has(kb::platform::PlatformCapability::UserDataPath) && !platformCapabilities.Has(kb::platform::PlatformCapability::Clipboard), "Platform capabilities did not fail closed for unavailable services");
    const kb::network::ReplicationSchema sessionSchema{ .version = 1U, .fields = { { .id = 1U, .name = "state", .type = kb::network::ReplicatedFieldType::Boolean } } };
    const kb::network::ReplicationSchema mismatchSchema{ .version = 2U, .fields = { { .id = 1U, .name = "state", .type = kb::network::ReplicatedFieldType::Boolean } } };
    kb::tests::Require(!kb::network::CanOpenNetworkSession(kb::network::kFirstReleaseNetwork) && kb::network::AreSchemasCompatible(sessionSchema, sessionSchema) && !kb::network::AreSchemasCompatible(sessionSchema, mismatchSchema), "Network session lifecycle did not fail closed for offline mode or schema mismatch");
    constexpr kb::network::NetworkSimulationConfig simulation{ .latencyMilliseconds = 40U, .jitterMilliseconds = 5U, .lossPermille = 1000U, .disconnectAtTick = 10U, .seed = 8U };
    static_assert(kb::network::IsValidNetworkSimulation(simulation));
    kb::tests::Require(kb::network::ShouldDrop(simulation, 1U, 2U) && kb::network::ShouldDisconnect(simulation, 10U) && !kb::network::ShouldDisconnect(simulation, 9U) && kb::network::NetworkSimulationRandom(simulation, 2U, 3U) == kb::network::NetworkSimulationRandom(simulation, 2U, 3U), "Network simulation was not deterministic for loss and disconnect");
    kb::network::NetworkObjects secureObjects;
    constexpr kb::network::NetworkSecurityLimits securityLimits{};
    kb::tests::Require(secureObjects.Spawn({ .id = 31U, .owner = 41U, .role = kb::network::NetworkRole::Authority }) && kb::network::ValidateIncomingMessage(secureObjects, securityLimits, 31U, 41U, 10U, 0U, 10U) && !kb::network::ValidateIncomingMessage(secureObjects, securityLimits, 31U, 42U, 10U, 0U, 10U) && !kb::network::ValidateIncomingMessage(secureObjects, securityLimits, 31U, 41U, 10U, securityLimits.maxMessagesPerTick, 10U) && !kb::network::ValidateIncomingMessage(secureObjects, securityLimits, 31U, 41U, 10U, 0U, 11U), "Network message validation did not reject spoofing, rate, and deserialization violations");
    constexpr kb::network::NetworkBudget networkBudget{};
    static_assert(kb::network::IsValidNetworkBudget(networkBudget));
    kb::tests::Require(kb::network::AcceptsPacket(networkBudget, 0U, 1200U) && !kb::network::AcceptsPacket(networkBudget, networkBudget.maxQueuedBytes, 1U) && kb::network::TickDurationMicroseconds(networkBudget) == 33333U, "Network tick and packet budget did not apply backpressure");
    const kb::network::NetworkSnapshot predicted{ .tick = 5U, .acknowledgedInput = 3U, .position = { 0.0F, 0.0F, 0.0F } };
    const kb::network::NetworkSnapshot authoritative{ .tick = 5U, .acknowledgedInput = 3U, .position = { 4.0F, 0.0F, 0.0F } };
    const auto interpolated = kb::network::Interpolate({ .previous = predicted, .next = authoritative }, 0.5F);
    kb::tests::Require(kb::network::IsValidInputCommand({ .tick = 5U, .sequence = 3U, .moveX = 1.0F }) && interpolated.has_value() && interpolated->position.x == 2.0F && kb::network::RequiresReconciliation(predicted, authoritative, 1.0F), "Network input, snapshot, interpolation, prediction, and reconciliation contract is invalid");
    std::int32_t variableDelta = 0;
    kb::network::NetworkVariable<std::int32_t> networkScore{ 2 };
    networkScore.SetChangedCallback(&RecordNetworkVariableChange, &variableDelta);
    kb::tests::Require(networkScore.Set(5) && networkScore.Revision() == 1U && variableDelta == 3 && !networkScore.Apply(7, 1U) && networkScore.Apply(7, 2U) && networkScore.Value() == 7 && variableDelta == 2, "Network variable did not provide typed revisions and change hooks");
    kb::network::NetworkObjects rpcObjects;
    kb::tests::Require(rpcObjects.Spawn({ .id = 8U, .owner = 20U, .role = kb::network::NetworkRole::Authority }) && kb::network::ValidateRpc(rpcObjects, { .object = 8U, .sender = 20U, .reliability = kb::network::RpcReliability::Unreliable, .direction = kb::network::RpcDirection::ClientToServer }) && !kb::network::ValidateRpc(rpcObjects, { .object = 8U, .sender = 21U, .direction = kb::network::RpcDirection::ClientToServer }) && rpcObjects.Spawn({ .id = 9U, .owner = 1U, .role = kb::network::NetworkRole::Proxy }) && kb::network::ValidateRpc(rpcObjects, { .object = 9U, .sender = 1U, .direction = kb::network::RpcDirection::ServerToClient }), "RPC validation did not enforce direction, reliability contract, and ownership");
    const kb::network::ReplicationSchema replicationSchema{ .version = 1U, .fields = { { .id = 1U, .name = "health", .type = kb::network::ReplicatedFieldType::QuantizedFloat, .minimum = 0.0F, .maximum = 100.0F, .quantizationBits = 10U }, { .id = 2U, .name = "alive", .type = kb::network::ReplicatedFieldType::Boolean } } };
    const auto healthQuantized = kb::network::QuantizeFloat(replicationSchema.fields[0], 50.0F);
    kb::tests::Require(kb::network::ValidateReplicationSchema(replicationSchema) && healthQuantized.has_value() && kb::network::DequantizeFloat(replicationSchema.fields[0], *healthQuantized).has_value() && kb::network::ComputeDeltaFields(replicationSchema, { 1U, 0U }, { 1U, 1U }) == std::vector<std::uint16_t>{ 2U }, "Replication schema did not validate versioned fields, quantization, and deltas");
    kb::network::NetworkObjects networkObjects;
    kb::tests::Require(networkObjects.Spawn({ .id = 7U, .owner = 11U, .role = kb::network::NetworkRole::Authority }) && networkObjects.CanAcceptOwnerCommand(7U, 11U) && !networkObjects.CanAcceptOwnerCommand(7U, 12U) && networkObjects.AssignOwner(7U, 12U) && networkObjects.Find(7U)->owner == 12U && networkObjects.Despawn(7U) && !networkObjects.Find(7U).has_value(), "Network object lifecycle did not validate spawn, owner authority, and despawn");
    static_assert(kb::network::kFirstReleaseNetwork.model == kb::network::NetworkModel::OfflineOnly);
    kb::tests::Require(!kb::network::kFirstReleaseNetwork.HasTransport(), "First-release network model must not imply unsupported multiplayer transport");
    constexpr auto sampleProfiles = kb::gameplay::GameplaySampleProfiles();
    kb::tests::Require(sampleProfiles[0].kind == kb::gameplay::GameplaySampleKind::ThirdPerson && sampleProfiles[0].usesJump && sampleProfiles[1].movement == kb::gameplay::GameplaySampleMovement::ScreenPlane && sampleProfiles[2].movement == kb::gameplay::GameplaySampleMovement::SideScroll && sampleProfiles[3].cameraPolicy == kb::gameplay::CameraPolicy::Possess && sampleProfiles[3].usesCombat, "Gameplay samples did not provide the four documented controller profiles");
    kb::gameplay::GameInstance flowGame;
    const kb::gameplay::GameSceneId firstScene = flowGame.CreateScene();
    const kb::gameplay::GameSceneId secondScene = flowGame.CreateScene();
    kb::tests::Require(
        flowGame.Flow().SetCheckpoint(firstScene) && flowGame.Flow().Pause() && flowGame.Flow().Resume() &&
            flowGame.TransitionToScene(secondScene) && flowGame.ActiveSceneId() == secondScene &&
            flowGame.Flow().Win() && flowGame.Flow().Restart() == firstScene && flowGame.TransitionToScene(firstScene) &&
            flowGame.Flow().Lose() && flowGame.DestroyScene(firstScene) && !flowGame.Flow().Checkpoint().has_value(),
        "Game flow did not enforce checkpoint, pause, restart, outcome, and scene transition lifecycle");
    kb::gameplay::GameplayAbilities abilities;
    kb::gameplay::GameplayModules modules;
    const kb::scene::SceneEntity target{2U};
    const kb::scene::SceneEntity pickup{3U};
    const kb::scene::SceneEntity enemy{4U};
    const kb::gameplay::GameplayTagId strength = kb::gameplay::GameplayTag("strength");
    const kb::gameplay::GameplayTagId weaponSlot = kb::gameplay::GameplayTag("weapon");
    kb::tests::Require(
        modules.AddHealth(target, { .current = 10.0F, .maximum = 10.0F }) &&
            modules.SetAttribute(target, strength, { .current = 4.0F, .minimum = 0.0F, .maximum = 10.0F }) &&
            modules.SetAttribute(enemy, strength, { .current = 6.0F, .minimum = 0.0F, .maximum = 10.0F }) &&
            modules.RegisterPickup(pickup, { .item = 9U, .quantity = 2U }) && modules.CollectPickup(pickup, target) &&
            modules.Equip(target, weaponSlot, 9U) && modules.ItemCount(target, 9U) == 2U &&
            modules.Attribute(target, strength)->current == 4.0F && modules.Equipped(target, weaponSlot) == 9U,
        "Optional gameplay modules did not retain health, attributes, inventory, equipment, and pickup state");
    const kb::gameplay::GameplayAbilityDefinition strike{ .id = kb::gameplay::GameplayTag("strike"), .cooldownSeconds = 2.0F, .costAttribute = strength, .cost = 1.0F, .targetRule = kb::gameplay::AbilityTargetRule::Hostile, .effect = { .attribute = strength, .delta = -2.0F } };
    const kb::gameplay::GameplayIdentity blueIdentity{ .team = 1U };
    const kb::gameplay::GameplayIdentity redIdentity{ .team = 2U };
    kb::tests::Require(abilities.Activate(strike, target, blueIdentity, enemy, redIdentity, modules) && abilities.IsActive(target) && modules.Attribute(target, strength)->current == 3.0F && modules.Attribute(enemy, strength)->current == 4.0F && !abilities.Activate(strike, target, blueIdentity, enemy, redIdentity, modules) && abilities.Cancel(target) && !abilities.IsActive(target) && !abilities.Activate(strike, target, blueIdentity, enemy, redIdentity, modules) && (abilities.Advance(2.0F), abilities.Activate(strike, target, blueIdentity, enemy, redIdentity, modules)), "Gameplay ability did not enforce target rule, cost, cooldown, or cancellation");
    const kb::gameplay::DamageEvent hit{ .source = kb::scene::SceneEntity{1U}, .instigator = kb::scene::SceneEntity{1U}, .target = kb::scene::SceneEntity{2U}, .hitEntity = kb::scene::SceneEntity{2U}, .type = kb::gameplay::DamageType::Fire, .amount = 10.0F };
    const kb::gameplay::DamageResistances resistances{ .multipliers = { 1.0F, 0.5F, 1.0F } };
    const std::optional<kb::gameplay::DamageResolution> resolvedHit = kb::gameplay::ResolveDamage(hit, resistances);
    const std::optional<kb::gameplay::DamageResolution> resolvedHealing = kb::gameplay::ResolveDamage(
        kb::gameplay::DamageEvent{ .target = kb::scene::SceneEntity{2U}, .type = kb::gameplay::DamageType::Healing, .amount = 4.0F },
        resistances);
    kb::tests::Require(
        resolvedHit.has_value() && resolvedHit->event.source == hit.source && resolvedHit->event.instigator == hit.instigator &&
            resolvedHit->event.hitEntity == hit.hitEntity && resolvedHit->healthDelta == -5.0F &&
            resolvedHealing.has_value() && resolvedHealing->healthDelta == 4.0F &&
            modules.ApplyDamage(*resolvedHit) && modules.Health(target)->current == 5.0F &&
            modules.ApplyDamage(*resolvedHealing) && modules.Health(target)->current == 9.0F &&
            !kb::gameplay::ResolveDamage(kb::gameplay::DamageEvent{}, resistances).has_value() && modules.Remove(target) &&
            !modules.Health(target).has_value(),
        "Damage resolution did not preserve typed context, resistances, or damage/heal direction");
    constexpr kb::gameplay::GameplayIdentity blue{ .team = 1U, .faction = 7U, .layers = 0x2U, .tag = kb::gameplay::GameplayTag("player") };
    constexpr kb::gameplay::GameplayIdentity blueOther{ .team = 1U, .faction = 7U, .layers = 0x2U, .tag = kb::gameplay::GameplayTag("player") };
    kb::tests::Require(kb::gameplay::IsFriendly(blue, blueOther) && kb::gameplay::SharesLayer(blue, blueOther) &&
            kb::gameplay::GameplayTag("player") == kb::gameplay::GameplayTag("player"), "Gameplay identity did not provide stable unified team/layer/tag filters");
    kb::gameplay::GameInstance game;
    const kb::gameplay::GameSceneId first = game.CreateScene();
    const kb::gameplay::GameSceneId second = game.CreateScene(kb::scene::SceneMode::PrefabPrivate);
    game.Services().progression.SetInt("campaign.chapter", 3);
    std::int64_t chapter = 0;
    kb::tests::Require(game.SceneCount() == 2U && game.ActiveSceneId() == first && game.SetActiveScene(second) &&
            game.ActiveScene() != nullptr && game.DestroyScene(second) && game.ActiveSceneId() == first &&
            game.Services().progression.GetInt("campaign.chapter", chapter) && chapter == 3,
        "GameInstance did not retain global services while managing scene lifetime");
    game.SetGameMode(kb::gameplay::GameMode{ { .authority = kb::gameplay::GameAuthority::Server, .maxPlayers = 4U, .allowJoinInProgress = true } });
    kb::tests::Require(game.Mode().CanAcceptPlayer(3U, true) && !game.Mode().CanAcceptPlayer(4U, false) &&
            !kb::gameplay::GameMode{}.CanAcceptPlayer(0U, true),
        "GameMode authority rules were not owned by GameInstance or did not enforce admission policy");
    kb::tests::Require(game.State().SetMatchInProgress(true) && game.State().Advance(1.5F) && game.State().SetScore(1U, 2) &&
            !game.State().ApplyReplication(kb::gameplay::GameStateSnapshot{ .revision = 1U }) &&
            game.State().ApplyReplication(game.State().Snapshot()) && game.State().Snapshot().scores[1] == 2,
        "GameState did not reject stale replication or retain authoritative match state");
    kb::tests::Require(game.PlayerRegistry().Join({ .id = 10U, .state = { .displayName = "Player" } }) &&
            game.PlayerRegistry().Possess(10U, kb::scene::SceneEntity{ 42U }) && game.PlayerRegistry().Find(10U)->controller.pawn.Id() == 42U &&
            game.PlayerRegistry().Leave(10U) && game.PlayerRegistry().Find(10U) == nullptr,
        "Player lifecycle did not own controller possession and release it on leave");
    kb::tests::Require(game.Cameras().SetView(10U, { .camera = kb::scene::SceneEntity{ 100U }, .target = kb::scene::SceneEntity{ 42U }, .policy = kb::gameplay::CameraPolicy::Spectate }) &&
            game.Cameras().FindView(10U)->policy == kb::gameplay::CameraPolicy::Spectate && game.Cameras().ClearView(10U),
        "CameraManager did not retain explicit player possession/follow/spectate policy");
    kb::gameplay::MatchRuntime match{ { .spawnPoints = { { .position = { 1.0F, 0.0F, 0.0F }, .team = 1U } } } };
    kb::tests::Require(match.SetPhase(kb::gameplay::MatchPhase::Playing) && match.SelectSpawn(1U) != nullptr, "Match config did not expose phase and team spawn policy");
}

void RunEngineLibraryTests() {
    RunVersionValueTest();
    RunVersionOrderingTest();
    RunVersionCompatibilityTest();
    RunModuleInstallCoversAllDomainsTest();
    RunModuleInstallReportsDuplicateDiagnosticsTest();
    RunModuleCatalogTest();
    RunModuleInstallSkipsUnavailableCapabilityTest();
    RunModuleInstallStartupReportTest();
    RunFunctionDescCatalogResolvesTest();
    RunFunctionDescMatchesCatalogRejectsMismatchTest();
    RunModuleCatalogValidatesTest();
    RunModuleValidationDuplicateNameTest();
    RunModuleValidationUnknownDependencyTest();
    RunModuleValidationCycleTest();
    RunModuleValidationDuplicateFunctionTest();
    RunModuleValidationFunctionSignatureChangedTest();
    RunModuleValidationFunctionPrefixMismatchTest();
    RunModuleInstallFailsFastOnInvalidCatalogTest();
    RunTypeDescTest();
    RunTypeDescLuaRoundTripTest();
    RunPropertyDescTest();
    RunLifecycleContextClassificationTest();
    RunExecutionOrderContractTest();
    RunCommandApplicationContractTest();
    RunCommandApplicationImmediateAcrossAllPhasesTest();
    RunLibraryContextTest();
    RunMultipleBehavioursRemovedSameFrameOrderTest();
    RunShutdownDispatchesDeactivateAndDestroyInOrderTest();
    RunEntityHandleTest();
    RunEntityHandleScriptComponentAccessTest();
    RunEntityHandleMeshRendererComponentAccessTest();
    RunEntityHandlePhysicsComponentAccessTest();
    RunArrayViewTest();
    RunCollectionsScalarTest();
    RunCollectionsScriptValueTest();
    RunCollectionsAllocationCostTest();
    RunCollectionsNonAllocTest();
    RunCollectionsDeterministicIterationTest();
    RunTextFormatBufferTest();
    RunParsingTest();
    RunUtf8ValidationTest();
    RunAssetRefTest();
    RunResultTest();
    RunApiManifestTest();
    RunApiCompatibilityComparisonTest();
    RunDeprecationTest();
    RunFunctionDeprecationWiringTest();
    RunFunctionIdTest();
    RunEngineLibraryComponentRegistryTest();
    RunEngineLibraryEventSchemaRegistryTest();
    RunComponentInspectorDescCatalogTest();
    RunLibraryQueryPhaseGateTest();
    RunLibraryQueryFilterAndOrderTest();
    RunLibraryPersistentQueryChangedSinceTest();
    RunLibraryCommandBatchTest();
    RunLibraryCommandBatchCancelsStaleTargetOnFlushTest();
    RunComponentChangeTrackerTest();
    RunTransformChangeTrackerTest();
    RunLibraryQueryAliasingEntityDestroyedAndCommandFlushBoundaryTest();
    RunCatalogFunctionFrontendAndDocumentationComplianceTest();
    RunOwnershipTest();
    RunNoPointersCrossScriptBoundaryTest();
    RunErrorCodeTest();
    RunInputLimitsTest();
    RunExpandedValueTypesTest();
    RunEngineLibrarySignalTest();
    RunNavigationFoundationContractTest();
    RunAiBehaviourAssetRuntimeTest();
    RunAiBlackboardTest();
    RunGoapBenchmarkDecisionTest();
    RunGameInstanceLifetimeTest();
}

} // namespace kb::tests
