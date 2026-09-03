#include "scene/EditorSceneContext.hpp"

#include "app/EditorCrashBreadcrumbs.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneVisitors.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"
#include "engine/scene/RegionPortalComponent.hpp"
#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/GeometrySwarmComponent.hpp"
#include "engine/scene/SurfaceCastComponent.hpp"
#include "engine/scene/FacingPanelComponent.hpp"
#include "engine/scene/SpaceStrokeComponent.hpp"
#include "engine/scene/HistoryRibbonComponent.hpp"
#include "engine/scene/TagsComponent.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "engine/scene/LensEchoComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/TimelineAssetIO.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/PhysicsDebugDraw.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "rendering/EditorMeshPreviewRasterizer.hpp"
#include "rendering/EditorMeshPreviewService.hpp"
#include "rendering/EditorMeshPreviewTypes.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
#include "scene/audio/EditorSceneAudioSettingsService.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/UIAssetIO.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/library/EngineLibraryManifest.hpp"
#include "engine/library/EngineLibraryModule.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/project/ProjectDescriptorWriter.hpp"
#include "project/EditorProjectPaths.hpp"
#include "packaging/EditorProjectPackageService.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptModule.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialSemanticHash.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include "scene/EditorScriptAssetGateway.hpp"
#include "scene/input/EditorInputActionAuthoring.hpp"
#include "scene/input/EditorInputAssetGateway.hpp"
#include "scene/input/EditorInputMappingContextAuthoring.hpp"
#include "scene/audio/EditorAudioMixerAuthoring.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include "scene/EditorAssetErrorMessage.hpp"
#include "scene/EditorDefaultSceneFactory.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"
#include "scene/EditorPluginCatalog.hpp"
#include "scene/EditorSceneAssetBrowserCommands.hpp"
#include "scene/EditorSceneCommandController.hpp"
#include "scene/EditorSceneDocumentAssetLoaders.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"
#include "scene/EditorSceneHierarchyActions.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/EditorSceneMeshAssetActions.hpp"
#include "scene/EditorTerrainService.hpp"
#include "scene/EditorSceneObjectEditCommands.hpp"
#include "scene/EditorScenePrefabActions.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/EditorMaterialGraphDebugLog.hpp"
#include "scene/material/EditorMaterialGraphSemanticAnalysis.hpp"
#include "scene/material/EditorMaterialAssetEditCommand.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"
#include "scene/material/EditorMaterialReferenceFinder.hpp"
#include "scene/material/EditorMaterialTextureSlotValidation.hpp"
#include "scene/material/EditorEmbeddedMaterialExtractor.hpp"
#include "scene/material_preview/EditorMaterialGraphCookService.hpp"
#include "scene/material_preview/EditorMaterialNodePreviewBuilder.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "scene/material_preview/EditorMaterialPreviewScene.hpp"
#include "scene/EditorAnimationPreviewScene.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "scene/transform_edit/EditorSceneTransformCommitBuilder.hpp"
#include "scene/transform_edit/EditorSceneTransformEditApplier.hpp"
#include "scene/transform_edit/EditorSceneTransformEditController.hpp"
#include "scene/transform_edit/EditorSceneTransformSnapshotBuilder.hpp"
#include "project/EditorProjectBootstrap.hpp"
#include "project/EditorProjectPaths.hpp"
#include "diagnostics/EditorLagTrace.hpp"

#include <bit>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kb::editor {
namespace {

class EditorTerrainAssetEditCommand final : public IEditorCommand {
public:
    EditorTerrainAssetEditCommand(
        kb::scene::Scene& scene,
        kb::assets::AssetId assetId,
        std::string label,
        kb::assets::TerrainAsset before,
        kb::assets::TerrainAsset after) noexcept
        : scene_(scene)
        , assetId_(assetId)
        , label_(std::move(label))
        , before_(std::move(before))
        , after_(std::move(after)) {}

    [[nodiscard]] std::string_view Label() const noexcept override { return label_; }
    [[nodiscard]] bool AffectsSceneDocument() const noexcept override { return false; }
    [[nodiscard]] bool AffectsHierarchySelection() const noexcept override { return false; }
    [[nodiscard]] bool Execute() override { return Apply(after_); }
    [[nodiscard]] bool Undo() override { return Apply(before_); }
    [[nodiscard]] bool Redo() override { return Apply(after_); }

private:
    [[nodiscard]] bool Apply(const kb::assets::TerrainAsset& terrain) {
        return EditorTerrainService::Persist(scene_, assetId_, terrain);
    }

    kb::scene::Scene& scene_;
    kb::assets::AssetId assetId_{};
    std::string label_;
    kb::assets::TerrainAsset before_{};
    kb::assets::TerrainAsset after_{};
};

[[nodiscard]] bool ContainsEntity(std::span<const kb::scene::SceneEntity> entities, kb::scene::SceneEntity entity) noexcept {
    return std::ranges::find(entities, entity) != entities.end();
}

[[nodiscard]] bool HasSelectedAncestor(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::span<const kb::scene::SceneEntity> selected) noexcept {
    kb::scene::SceneEntity parent = scene.Hierarchy().Parent(entity);
    while (parent.IsValid()) {
        if (ContainsEntity(selected, parent)) {
            return true;
        }
        parent = scene.Hierarchy().Parent(parent);
    }
    return false;
}

[[nodiscard]] std::vector<kb::scene::SceneEntity> TopLevelSelectedEntities(const kb::scene::Scene& scene, std::span<const kb::scene::SceneEntity> entities) {
    std::vector<kb::scene::SceneEntity> filtered;
    filtered.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        if (!entity.IsValid() || ContainsEntity(filtered, entity) || HasSelectedAncestor(scene, entity, entities)) {
            continue;
        }
        filtered.push_back(entity);
    }
    return filtered;
}

[[nodiscard]] bool AnyAlive(const kb::scene::Scene& scene, std::span<const kb::scene::SceneEntity> entities) noexcept {
    for (const kb::scene::SceneEntity entity : entities) {
        if (scene.Entities().IsAlive(entity)) {
            return true;
        }
    }
    return false;
}

void AppendEntityBranchRenderDirty(
    const kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    std::vector<std::uint64_t>& dirtyEntityIds) {
    if (!entity.IsValid()) {
        return;
    }
    if (std::ranges::find(dirtyEntityIds, entity.Id()) == dirtyEntityIds.end()) {
        dirtyEntityIds.push_back(entity.Id());
    }
    if (!scene.Entities().IsAlive(entity)) {
        return;
    }
    for (const kb::scene::SceneEntity child : scene.Hierarchy().ChildEntities(entity)) {
        AppendEntityBranchRenderDirty(scene, child, dirtyEntityIds);
    }
}

[[nodiscard]] std::size_t ImportStatusCount(const kb::assets::AssetImportResult& result, kb::assets::AssetImportItemStatus status) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(result.items, [status](const kb::assets::AssetImportItemResult& item) {
        return item.status == status;
    }));
}

[[nodiscard]] std::string ImportSourceLabel(const kb::assets::AssetImportItemResult& item) {
    const std::filesystem::path filename = item.sourcePath.filename();
    return filename.empty() ? item.sourcePath.generic_string() : filename.generic_string();
}

[[nodiscard]] std::string ImportReportLine(const kb::assets::AssetImportItemResult& item) {
    std::string message = std::string{ kb::assets::ToString(item.category) } + " " + std::string{ kb::assets::ToString(item.status) } + ": " + ImportSourceLabel(item);
    if (!item.virtualPath.empty()) {
        message += " -> " + item.virtualPath.generic_string();
    }
    if (!item.error.empty()) {
        message += " (" + item.error + ")";
    }
    return message;
}

void LogAssetImportReport(EditorConsoleState& console, const kb::assets::AssetImportResult& result, const std::filesystem::path& destinationVirtualFolder) {
    if (result.items.empty()) {
        return;
    }

    const std::size_t failed = ImportStatusCount(result, kb::assets::AssetImportItemStatus::Failed);
    console.Info("Assets",
        "Import report for " + destinationVirtualFolder.generic_string() +
        ": created=" + std::to_string(result.CreatedCount()) +
        ", reused=" + std::to_string(result.ReusedCount()) +
        ", missing=" + std::to_string(result.MissingCount()) +
        ", unsupported=" + std::to_string(result.UnsupportedCount()) +
        ", failed=" + std::to_string(failed));

    for (const kb::assets::AssetImportItemResult& item : result.items) {
        const std::string line = ImportReportLine(item);
        switch (item.status) {
        case kb::assets::AssetImportItemStatus::Created:
        case kb::assets::AssetImportItemStatus::Reused:
            console.Info("Assets", line);
            break;
        case kb::assets::AssetImportItemStatus::Missing:
        case kb::assets::AssetImportItemStatus::Unsupported:
            console.Warning("Assets", line);
            break;
        case kb::assets::AssetImportItemStatus::Failed:
        case kb::assets::AssetImportItemStatus::None:
        default:
            console.Error("Assets", line);
            break;
        }
    }
}

void ApplySavingPreferences(
    const EditorSavingPreferences& preferences,
    EditorAutosaveState& autosave) noexcept {
    autosave.Configure(preferences.autosaveEnabled, preferences.autosaveIntervalMinutes);
}

} // namespace

EditorSceneContext::EditorSceneContext()
    // Load the project descriptor first, then construct the scene from it so the
    // scene's engine module host honours the project's enabled/disabled module set.
    : projectBootstrap_(EditorProjectBootstrap::BootstrapDefaultProject())
    , project_(projectBootstrap_.succeeded ? projectBootstrap_.descriptor : kb::project::ProjectDescriptor{})
    , projectConfig_(projectBootstrap_.settings)
    , projectFile_(projectBootstrap_.succeeded ? projectBootstrap_.projectFile : EditorProjectPaths::ProjectFile())
    , scene_(std::make_unique<kb::scene::Scene>(project_))
    , inspectorMaterialPreviewScene_(std::make_unique<EditorMaterialPreviewScene>())
    , materialPreviewScene_(std::make_unique<EditorMaterialPreviewScene>())
    , animationPreviewScene_(std::make_unique<EditorAnimationPreviewScene>())
    , graphShaderCacheRoot_((EditorProjectPaths::ProjectRoot() / ".cache" / "graph_shaders").generic_string())
    , materialGraphCookService_(std::make_unique<EditorMaterialGraphCookService>(EditorMaterialGraphCookConfig::Resolve(graphShaderCacheRoot_))) {
    const EditorBuildGameSettingsLoadResult packageSettings = EditorBuildGameSettingsStore::Load(
        EditorBuildGameSettingsStore::FilePath(EditorProjectPaths::ProjectRoot()));
    if (packageSettings.Succeeded()) {
        if (packageSettings.found) {
            buildGameSettings_ = packageSettings.settings;
        }
    } else {
        console_.Error("Packaging", packageSettings.error);
    }
#if defined(KB_EDITOR_BUILD_ROOT)
    if (buildGameSettings_.buildRoot.empty()) {
        buildGameSettings_.buildRoot = KB_EDITOR_BUILD_ROOT;
    }
#endif
    buildGamePackageService_ = std::make_unique<EditorProjectPackageService>();
    if (projectBootstrap_.succeeded) {
        console_.Info("Project", projectBootstrap_.created ? "Created project descriptor." : "Loaded project descriptor.");
        if (!projectBootstrap_.settingsError.empty()) {
            console_.Error("Project", projectBootstrap_.settingsError);
        }
        if (projectBootstrap_.descriptorMirrorStale && SaveProjectConfiguration()) {
            console_.Info("Project", "Applied project settings edited outside the editor.");
        }
        if (!projectBootstrap_.particlePolicy.IsRunnable()) {
            console_.Warning("Project", projectBootstrap_.particlePolicy.diagnostic +
                " Choose Add Rendering.21kbParticle or Cancel before running the project.");
        }
    } else {
        console_.Error("Project", projectBootstrap_.error.empty() ? "Project descriptor bootstrap failed." : projectBootstrap_.error);
    }

    if (scene_->Assets().MountProject(EditorProjectPaths::ProjectRoot())) {
        console_.Info("Project", "Mounted project assets.");
    } else {
        console_.Error("Project", AssetErrorOr(scene_->Assets().Manager(), "Project assets could not be mounted."));
    }
#if defined(KB_21KB_PARTICLE_CONTENT_ROOT)
    if (scene_->Assets().Manager().Mounts().Mount("21kbParticle", KB_21KB_PARTICLE_CONTENT_ROOT)) {
        console_.Info("Particles", "Mounted 21kb Particle System content.");
    } else {
        console_.Error("Particles", "21kb Particle System content could not be mounted.");
    }
#endif
    RegisterEditorSceneDocumentAssetLoaders(*scene_);
    const std::size_t discovered = scene_->Assets().Discover();
    console_.Info("Assets", "Asset discovery completed. Found " + std::to_string(discovered) + " asset(s).");
    static_cast<void>(ActivateProjectPhysicsLayers(*scene_));
    currentScenePath_ = ResolveDefaultScenePath();
    std::error_code error;
    if (!currentScenePath_.empty() && std::filesystem::is_regular_file(currentScenePath_, error) && !error && kb::scene::SceneDocumentService::LoadFileIntoScene(*scene_, currentScenePath_)) {
        EditorSceneAudioSettingsService::PrepareDocument(*scene_);
        SelectFirstSceneEntityOrClear();
        console_.Info("Project", "Opened default scene: " + currentScenePath_.generic_string());
    } else {
        hierarchySelection_.SelectEntity(EditorDefaultSceneFactory::Seed(*scene_));
        if (SaveCurrentScene()) {
            console_.Info("Project", "Created default scene: " + currentScenePath_.generic_string());
        }
    }
    console_.Info("Editor", "Editor scene initialized.");
}

EditorSceneContext::~EditorSceneContext() {
    ClearBuildGameSigningPasswords();
    if (assetImportWorker_.joinable()) {
        assetImportWorker_.join();
    }
    if (particlePreviewSession_ != nullptr && particlePreviewReleaseHandler_) {
        CloseParticleEditorAsset();
    }
    // Scene shutdown dispatches the script Destroyed lifecycle and scripts may
    // legitimately call the editor-provided Log function from that callback.
    // Destroy the scene explicitly while console_ is still alive; the default
    // member order would otherwise destroy console_ before scene_ and leave the
    // registered Log callback pointing at released storage.
    scene_.reset();
    scriptModule_ = nullptr;
    scriptModuleHost_.reset();
}

void EditorSceneContext::EnsureScriptRuntime() {
    if (scriptModuleHost_ != nullptr) {
        return;
    }
    EditorConsoleState* console = &console_;

    kb::script::ScriptModuleOptions scriptOptions;
    scriptOptions.configureHost = [console](kb::script::ScriptRuntimeHost& host) {
        // Log("...") in any script prints to the editor Console. console_ outlives
        // scriptModuleHost_ (declared last, destroyed first), so capturing it is safe.
        kb::script::ScriptFunctionDesc logDesc;
        logDesc.signature.name = "Log";
        logDesc.signature.inputs = { kb::script::ScriptFunctionPin{ "message", kb::script::ScriptValueType::String, true } };
        logDesc.signature.outputs = {};
        logDesc.callback = [console](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument> arguments) {
            std::string message;
            for (const kb::script::ScriptFunctionArgument& argument : arguments) {
                if (argument.name == "message") {
                    message = argument.value.AsString();
                    break;
                }
            }
            console->Info("Script", message);
            EditorCrashBreadcrumbs::Write("runtime_script_log", message);
            return kb::script::ScriptFunctionCallResult{ .executed = true, .outputs = {}, .errors = {} };
        };
        static_cast<void>(host.RegisterFunction(std::move(logDesc)));
    };

    auto scriptModule = std::make_unique<kb::script::ScriptModule>(std::move(scriptOptions));
    kb::script::ScriptModule* scriptModuleView = scriptModule.get();
    scriptModule_ = scriptModuleView;
    // The scene's own EngineModuleHost (Scene.cpp) already loaded AND attached the
    // project's DLL plugins (physics/audio/rendering) for this scene. This play-mode
    // host exists only to add the editor's script module (Log -> Console). Loading
    // the project's plugins a SECOND time here re-shadow-copies the exact same DLLs
    // to the same temp files the scene host still holds mapped — a guaranteed
    // sharing violation, and a redundant second plugin instance. Strip the plugins
    // so this host carries only the script module.
    kb::project::ProjectDescriptor scriptRuntimeProject = project_;
    scriptRuntimeProject.plugins.clear();
    scriptModuleHost_ = std::make_unique<kb::modules::EngineModuleHost>(scriptRuntimeProject);
    scriptModuleHost_->Add(std::move(scriptModule));
    // Loading/attaching the project's engine plugins (physics, audio, rendering,
    // …) must not silently abort play. A throw here previously unwound before
    // the script scene system ran, leaving Play engaged but no behaviour ticking
    // and no message. Catch it, and surface any plugin-load diagnostics.
    try {
        scriptModuleHost_->Load(scene_->Runtime().EcsWorld());
        scriptModuleHost_->AttachScene(*scene_);
    } catch (const std::exception& error) {
        console_.Error("Plugins", std::string{ "A plugin faulted while starting play mode: " } + error.what());
    } catch (...) {
        console_.Error("Plugins", "A plugin faulted while starting play mode (unknown error).");
    }
    for (const std::string& diagnostic : scriptModuleHost_->Diagnostics()) {
        console_.Warning("Plugins", diagnostic);
    }

    if (!scriptModuleHost_->IsActive("Script")) {
        console_.Warning("Scripts", "Script module is disabled for this project; behaviours will not run.");
        return;
    }

    if (!scriptModuleView->Succeeded()) {
        for (const std::string& diagnostic : scriptModuleView->Diagnostics()) {
            console_.Error("Scripts", diagnostic);
        }
        console_.Error("Scripts", "Script runtime could not be fully initialized; behaviours may not run.");
    } else {
        console_.Info("Scripts", "Script runtime ready for play mode.");
    }
    SurfaceScriptLibraryStartupReport();
}

void EditorSceneContext::SurfaceScriptLibraryStartupReport() {
    if (scriptModule_ == nullptr || scriptModule_->Host() == nullptr) {
        return;
    }

    kb::script::ScriptRuntimeHost& host = *scriptModule_->Host();
    std::istringstream report{ kb::library::FormatStartupReport(host.LibraryStartupReport()) };
    for (std::string line; std::getline(report, line);) {
        if (!line.empty()) {
            console_.Info("Library", std::move(line));
        }
    }

    const kb::script::ScriptApiCatalog catalog =
        kb::script::ScriptApiCatalog::Build(host, scene_->Assets().Manager());
    if (!HasCompleteScriptExecutionAffinity()) {
        console_.Error("Library", "A registered script function has no execution-affinity policy.");
    }
    const kb::library::ApiManifest manifest = kb::library::BuildApiManifest(catalog);
    std::vector<std::pair<std::uint64_t, std::string>> crashAssets;
    for (const kb::assets::AssetMetadata& asset : scene_->Assets().Manager().Registry().All()) {
        crashAssets.emplace_back(asset.id.value, asset.type);
    }
    EditorCrashBreadcrumbs::ConfigureCrashReport(
        kb::library::ToString(manifest.version), manifest.manifestHash, std::move(crashAssets));
    const kb::visual::VisualGraphNodeCatalog visualGraphCatalog = host.CreateVisualGraphNodeCatalog();
    const std::size_t missingDescriptionCount =
        static_cast<std::size_t>(std::count_if(
            catalog.functions.begin(),
            catalog.functions.end(),
            [](const kb::script::ScriptApiCatalogFunction& function) {
                return function.description.empty();
            }));
    std::size_t auditedFunctionCount = 0U;
    std::size_t invalidFunctionMetadataCount = 0U;
    for (const kb::library::LibraryModuleDesc& module : kb::library::EngineLibraryModule::Catalog()) {
        for (const kb::library::LibraryFunctionDesc& function : module.functions) {
            ++auditedFunctionCount;
            if (!kb::library::FunctionDescMatchesCatalog(function, catalog)) {
                ++invalidFunctionMetadataCount;
            }
        }
    }

    std::ostringstream summary;
    summary << "Live API " << kb::library::ToString(manifest.version)
            << " hash=" << manifest.manifestHash
            << " functions=" << catalog.functions.size()
            << " components=" << catalog.components.size()
            << " luaBindings=" << catalog.luaBindings.size()
            << " visualGraphNodes=" << visualGraphCatalog.Entries().size()
            << " lifecycleEvents=" << catalog.lifecycleEvents.size()
            << " projectEntries=" << catalog.projectEntries.size()
            << " auditedMetadata=" << auditedFunctionCount
            << " missingDescriptions=" << missingDescriptionCount
            << " registryLocked=" << (host.Functions().IsLocked() ? "true" : "false");
    console_.Info("Library", summary.str());

    if (invalidFunctionMetadataCount != 0U) {
        console_.Error(
            "Library",
            std::to_string(invalidFunctionMetadataCount)
                + " audited function descriptor(s) do not match the live runtime catalog.");
    }
    if (missingDescriptionCount != 0U) {
        console_.Error(
            "Library",
            std::to_string(missingDescriptionCount)
                + " live function(s) are missing API descriptions.");
    }
}

void EditorSceneContext::ResetScriptRuntimeStateForPlayMode() {
    if (scriptModule_ == nullptr || scriptModule_->Host() == nullptr) {
        return;
    }
    kb::script::ScriptRuntimeHost& host = *scriptModule_->Host();
    host.LuaRuntime().Clear();
    host.VisualGraphInstances().Clear();
    host.SharedState().Clear();
}

void EditorSceneContext::SurfaceScriptDiagnostics() {
    if (scriptModule_ == nullptr || scriptModule_->Host() == nullptr) {
        return;
    }
    for (const std::string& diagnostic : scriptModule_->Host()->DrainSceneSystemDiagnostics()) {
        console_.Error("Scripts", diagnostic);
    }
}

bool EditorSceneContext::TickPlayModeSceneSession(float deltaSeconds) {
    if (!HasPlayModeSceneSession() || !std::isfinite(deltaSeconds) ||
        deltaSeconds < 0.0F) {
        return false;
    }
    kb::scene::SceneRuntime runtime = scene_->Runtime();
    if (!runtime.EcsProfilerEnabled()) {
        runtime.SetEcsProfilerEnabled(true);
    }
    if (!playModeRenderTopologyVersionInitialized_) {
        playModeRenderTopologyVersion_ = runtime.RenderTopologyVersion();
        playModeRenderTopologyVersionInitialized_ = true;
    }
    static_cast<void>(runtime.Update(deltaSeconds));
    for (const std::string& systemError :
         runtime.DrainSceneSystemErrors()) {
        console_.Error("Scripts", systemError);
    }
    SurfaceScriptDiagnostics();
    // Transforms and render-proxy value edits are published by SceneRuntime as
    // compact render-proxy update lists and are consumed directly by the
    // renderer. Only a render hierarchy/topology change requires rebuilding
    // the full proxy set (spawn, destroy, reparent, or a global render toggle).
    const std::uint64_t topologyVersion = runtime.RenderTopologyVersion();
    if (topologyVersion != playModeRenderTopologyVersion_) {
        MarkSceneRenderDirty();
    }
    playModeRenderTopologyVersion_ = topologyVersion;
    return !runtime.ShouldQuit();
}

kb::scene::Scene& EditorSceneContext::Scene() noexcept {
    return *scene_;
}

const kb::scene::Scene& EditorSceneContext::Scene() const noexcept {
    return *scene_;
}

bool EditorSceneContext::HasCompleteScriptExecutionAffinity() const noexcept {
    if (scriptModule_ == nullptr || scriptModule_->Host() == nullptr) {
        return false;
    }
    const std::vector<kb::script::ScriptFunctionDesc>& functions =
        scriptModule_->Host()->Functions().Functions();
    return !functions.empty() && std::ranges::all_of(functions,
        [](const kb::script::ScriptFunctionDesc& function) {
            return function.signature.executionAffinity ==
                kb::core::ExecutionAffinity::MainThread;
        });
}

EditorAssetBrowserState& EditorSceneContext::AssetBrowser() noexcept {
    return assetBrowser_;
}

const EditorAssetBrowserState& EditorSceneContext::AssetBrowser() const noexcept {
    return assetBrowser_;
}

EditorViewportPreviewState& EditorSceneContext::ViewportPreview() noexcept {
    return viewportState_.Preview();
}

const EditorViewportPreviewState& EditorSceneContext::ViewportPreview() const noexcept {
    return viewportState_.Preview();
}

EditorViewportPreviewState& EditorSceneContext::ViewportPreview(std::uint64_t viewportKey) noexcept {
    return viewportState_.Preview(viewportKey);
}

const EditorViewportPreviewState& EditorSceneContext::ViewportPreview(std::uint64_t viewportKey) const noexcept {
    return viewportState_.Preview(viewportKey);
}

AnimationPreviewContext& EditorSceneContext::AnimationPreview() noexcept {
    return animationPreview_;
}

const AnimationPreviewContext& EditorSceneContext::AnimationPreview() const noexcept {
    return animationPreview_;
}

const kb::scene::Scene& EditorSceneContext::AnimationPreviewScene() {
    const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
    const auto start = std::chrono::steady_clock::now();
    const kb::scene::Scene& previewScene = animationPreviewScene_->SceneFor(*scene_, animationPreview_);
    const double durationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    diagnostics::EditorLagTrace::Slow("skeletal-preview-scene", eventId, durationMs, "SceneFor", 8.0);
    return previewScene;
}

EditorViewportCameraState& EditorSceneContext::AnimationPreviewCamera() noexcept {
    return animationPreviewScene_->Camera();
}

const EditorViewportCameraState& EditorSceneContext::AnimationPreviewCamera() const noexcept {
    return animationPreviewScene_->Camera();
}

void EditorSceneContext::FocusAnimationPreview(float durationSeconds) noexcept {
    animationPreviewScene_->Focus(durationSeconds);
}

bool EditorSceneContext::TickAnimationPreviewCamera(
    float deltaSeconds,
    const EditorViewportCameraFlightInput& flightInput) noexcept {
    return animationPreviewScene_->TickCamera(deltaSeconds, flightInput);
}

bool EditorSceneContext::TickAnimationPreviewPlayback(float deltaSeconds) noexcept {
    return animationPreviewScene_->TickPlayback(animationPreview_, deltaSeconds);
}

AnimationPreviewOverlaySnapshot EditorSceneContext::AnimationPreviewOverlays() const {
    return animationPreviewScene_ == nullptr
        ? AnimationPreviewOverlaySnapshot{}
        : animationPreviewScene_->BuildOverlays(animationPreview_);
}

EditorViewportCameraState& EditorSceneContext::ViewportCamera() noexcept {
    return viewportState_.Camera();
}

const EditorViewportCameraState& EditorSceneContext::ViewportCamera() const noexcept {
    return viewportState_.Camera();
}

EditorViewportCameraState& EditorSceneContext::ViewportCamera(std::uint64_t viewportKey) noexcept {
    return viewportState_.Camera(viewportKey);
}

const EditorViewportCameraState& EditorSceneContext::ViewportCamera(std::uint64_t viewportKey) const noexcept {
    return viewportState_.Camera(viewportKey);
}

void EditorSceneContext::BeginViewportCameraNavigation(std::uint64_t viewportKey, EditorViewportCameraNavigationMode mode, int x, int y) noexcept {
    viewportState_.BeginCameraNavigation(viewportKey, mode, x, y);
}

bool EditorSceneContext::HasActiveViewportCameraNavigation() const noexcept {
    return viewportState_.HasActiveCameraNavigation();
}

std::uint64_t EditorSceneContext::ActiveViewportCameraKey() const noexcept {
    return viewportState_.ActiveCameraKey();
}

EditorViewportCameraState* EditorSceneContext::ActiveViewportCamera() noexcept {
    return viewportState_.ActiveCamera();
}

const EditorViewportCameraState* EditorSceneContext::ActiveViewportCamera() const noexcept {
    return viewportState_.ActiveCamera();
}

void EditorSceneContext::EndViewportCameraNavigation() noexcept {
    viewportState_.EndCameraNavigation();
}

bool EditorSceneContext::CloseViewportToolbarDropdowns() noexcept {
    bool changed = viewportState_.CloseToolbarDropdowns();
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    changed = changed || tool.brushMenuOpen || tool.brushShapeMenuOpen;
    tool.brushMenuOpen = false;
    tool.brushShapeMenuOpen = false;
    return changed;
}

InspectorPanelState& EditorSceneContext::Inspector() noexcept {
    return inspector_;
}

const InspectorPanelState& EditorSceneContext::Inspector() const noexcept {
    return inspector_;
}

EditorProjectSettingsState& EditorSceneContext::ProjectSettings() noexcept {
    return projectSettings_;
}

const EditorProjectSettingsState& EditorSceneContext::ProjectSettings() const noexcept {
    return projectSettings_;
}

const EditorConfiguration& EditorSceneContext::EditorConfig() const noexcept {
    return editorConfig_;
}

bool EditorSceneContext::SaveEditorConfig(EditorConfiguration configuration) {
    std::string error;
    const std::filesystem::path path = EditorConfigurationStore::FilePath(EditorProjectPaths::ProjectRoot());
    if (!EditorConfigurationStore::Save(path, EditorProjectPaths::ProjectRoot(), configuration, error)) {
        console_.Error("Editor Settings", error.empty() ? "Editor settings could not be saved." : error);
        return false;
    }
    editorConfig_ = std::move(configuration);
    return true;
}

EditorPluginsState& EditorSceneContext::Plugins() noexcept {
    return plugins_;
}

const EditorPluginsState& EditorSceneContext::Plugins() const noexcept {
    return plugins_;
}

EditorScriptEditorState& EditorSceneContext::ScriptEditor() noexcept {
    return scriptEditor_;
}

const EditorScriptEditorState& EditorSceneContext::ScriptEditor() const noexcept {
    return scriptEditor_;
}

EditorConsoleState& EditorSceneContext::Console() noexcept {
    return console_;
}

const EditorConsoleState& EditorSceneContext::Console() const noexcept {
    return console_;
}

EditorSceneGizmoState& EditorSceneContext::Gizmo() noexcept {
    return viewportState_.Gizmo();
}

const EditorSceneGizmoState& EditorSceneContext::Gizmo() const noexcept {
    return viewportState_.Gizmo();
}

const kb::project::ProjectDescriptor& EditorSceneContext::Project() const noexcept {
    return project_;
}

const std::filesystem::path& EditorSceneContext::ProjectFile() const noexcept {
    return projectFile_;
}

const std::filesystem::path& EditorSceneContext::CurrentScenePath() const noexcept {
    return currentScenePath_;
}

std::uint64_t EditorSceneContext::SceneDocumentGeneration() const noexcept {
    return sceneDocumentIdentity_.Generation();
}

std::uint64_t EditorSceneContext::SceneRenderRevision() const noexcept {
    return sceneRenderRevision_;
}

std::uint64_t EditorSceneContext::SceneRenderDirtyBaseRevision() const noexcept {
    return sceneRenderDirtyBaseRevision_;
}

bool EditorSceneContext::SceneRenderFullDirty() const noexcept {
    return sceneRenderFullDirty_;
}

const std::vector<std::uint64_t>& EditorSceneContext::SceneRenderDirtyEntityIds() const noexcept {
    return sceneRenderDirtyEntityIds_;
}

bool EditorSceneContext::SceneDocumentDirty() const noexcept {
    return sceneDocumentDirty_;
}

bool EditorSceneContext::TickAutosave(
    double elapsedSeconds,
    bool saveEligible) {
    const bool dirty = sceneDocumentDirty_ ||
        HasDirtyMaterialAssetEdit() || ParticleEditorDirty();
    const EditorAutosaveTickResult tick = autosave_.Tick(
        elapsedSeconds,
        saveEligible && !playModeSceneSession_.Active(),
        dirty);
    if (!tick.saveRequested) {
        return tick.visualChanged;
    }

    const std::string documentName = currentScenePath_.filename().empty()
        ? std::string{ "open documents" }
        : currentScenePath_.filename().string();
    const bool succeeded = SaveOpenDocuments();
    autosave_.Complete(succeeded, documentName);
    if (succeeded) {
        console_.Info("Autosave", "Autosaved " + documentName + ".");
    } else {
        console_.Error("Autosave", "Autosave failed. Unsaved changes were retained.");
    }
    return true;
}

const EditorAutosaveState& EditorSceneContext::Autosave() const noexcept {
    return autosave_;
}

EditorSavingPreferences EditorSceneContext::CaptureEditorSavingPreferences() const noexcept {
    return {
        .autosaveEnabled = autosave_.Enabled(),
        .autosaveIntervalMinutes = static_cast<std::uint32_t>(std::clamp(
            std::lround(autosave_.ConfiguredIntervalSeconds() / 60.0), 1L, 120L)),
    };
}

bool EditorSceneContext::LoadEditorSettings() {
    const std::filesystem::path root = EditorProjectPaths::ProjectRoot();
    EditorConfigurationLoadResult loaded = EditorConfigurationStore::Load(
        EditorConfigurationStore::FilePath(root), root);
    if (!loaded.Succeeded()) {
        console_.Warning("Editor Settings", loaded.error);
        return false;
    }
    editorConfig_ = std::move(loaded.configuration);
    ApplySavingPreferences(editorConfig_.saving, autosave_);
    return true;
}

bool EditorSceneContext::CommitEditorSettings(const EditorSavingPreferences& preferences) {
    EditorConfiguration configuration = editorConfig_;
    configuration.saving = preferences;
    if (!SaveEditorConfig(std::move(configuration))) {
        return false;
    }
    ApplySavingPreferences(preferences, autosave_);
    console_.Info("Editor Settings", "Editor settings saved.");
    return true;
}

void EditorSceneContext::MarkSceneRenderDirty() noexcept {
    ++sceneRenderRevision_;
    if (sceneRenderRevision_ == 0U) {
        sceneRenderRevision_ = 1U;
    }
    InvalidateHierarchyRows();
    sceneRenderFullDirty_ = true;
    sceneRenderDirtyBaseRevision_ = sceneRenderRevision_;
    sceneRenderDirtyEntityIds_.clear();
}

void EditorSceneContext::MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity> entities) {
    if (entities.empty()) {
        return;
    }
    if (!sceneRenderFullDirty_ && sceneRenderDirtyEntityIds_.empty()) {
        sceneRenderDirtyBaseRevision_ = sceneRenderRevision_;
    }

    ++sceneRenderRevision_;
    if (sceneRenderRevision_ == 0U) {
        sceneRenderRevision_ = 1U;
    }

    if (sceneRenderFullDirty_) {
        return;
    }
    for (const kb::scene::SceneEntity entity : entities) {
        AppendEntityBranchRenderDirty(*scene_, entity, sceneRenderDirtyEntityIds_);
    }
}

void EditorSceneContext::AcknowledgeSceneRenderSubmitted() noexcept {
    sceneRenderFullDirty_ = false;
    sceneRenderDirtyEntityIds_.clear();
    sceneRenderDirtyBaseRevision_ = sceneRenderRevision_;
}

void EditorSceneContext::MarkSceneDocumentDirty() noexcept {
    sceneDocumentDirty_ = true;
}

bool EditorSceneContext::SaveOpenDocuments() {
    LogMaterialGraphDebug(console_, "save-open-documents-request materialOpen=" +
        std::to_string(materialEditor_.OpenAssetId().value) +
        " materialDirty=" + std::string{ materialEditor_.Dirty() ? "true" : "false" } +
        " materialAssetEditDirty=" + std::string{ HasDirtyMaterialAssetEdit() ? "true" : "false" } +
        " sceneDirty=" + std::string{ sceneDocumentDirty_ ? "true" : "false" });
    if (HasDirtyMaterialAssetEdit() && !SaveMaterialEditorAsset(materialEditor_.OpenAssetId())) {
        LogMaterialGraphDebug(console_, "save-open-documents-failed material editor save failed");
        return false;
    }
    if (ParticleEditorDirty() && !SaveParticleEditorAsset()) {
        console_.Error("Particles", "Global Save could not persist the open particle effect.");
        return false;
    }
    if (!sceneDocumentDirty_) {
        LogMaterialGraphDebug(console_, "save-open-documents-ok no dirty scene");
        autosave_.ResetInterval();
        return true;
    }
    const bool savedScene = SaveCurrentScene();
    if (savedScene) {
        autosave_.ResetInterval();
    }
    LogMaterialGraphDebug(console_, "save-open-documents-scene-save result=" + std::string{ savedScene ? "true" : "false" });
    return savedScene;
}

bool EditorSceneContext::CanUndoSceneCommand() const noexcept {
    if (materialGraphFocused_ && materialEditor_.OpenAssetId().IsValid()) {
        return commandStack_.CanUndo(EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value));
    }
    return commandStack_.CanUndo(EditorCommandHistoryKey::Scene());
}

bool EditorSceneContext::CanRedoSceneCommand() const noexcept {
    if (materialGraphFocused_ && materialEditor_.OpenAssetId().IsValid()) {
        return commandStack_.CanRedo(EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value));
    }
    return commandStack_.CanRedo(EditorCommandHistoryKey::Scene());
}

bool EditorSceneContext::UndoSceneCommand() {
    static_cast<void>(CommitHierarchyRename());
    inspector_.EndTextEdit();
    const bool materialHistoryActive = materialGraphFocused_ && materialEditor_.OpenAssetId().IsValid();
    const bool undone = materialHistoryActive
        ? commandStack_.Undo(EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value))
        : SceneCommands().Undo();
    if (undone) terrainReadCache_.reset();
    if (undone && commandStack_.LastCompletedCommandAffectsOpenMaterialSource() &&
        commandStack_.LastCompletedCommandHistoryKey() == EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value)) {
        RefreshOpenMaterialEditorFromSource();
    } else if (undone && materialEditor_.OpenAssetId().IsValid()) {
        SyncMaterialEditorWorkingCopyRuntimePreview();
        sceneGraphCookPending_ = true;
        RequestOpenMaterialSceneGraphCook();
        MarkSceneRenderDirty();
    }
    return undone;
}

bool EditorSceneContext::RedoSceneCommand() {
    static_cast<void>(CommitHierarchyRename());
    inspector_.EndTextEdit();
    const bool materialHistoryActive = materialGraphFocused_ && materialEditor_.OpenAssetId().IsValid();
    const bool redone = materialHistoryActive
        ? commandStack_.Redo(EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value))
        : SceneCommands().Redo();
    if (redone) terrainReadCache_.reset();
    if (redone && commandStack_.LastCompletedCommandAffectsOpenMaterialSource() &&
        commandStack_.LastCompletedCommandHistoryKey() == EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value)) {
        RefreshOpenMaterialEditorFromSource();
    } else if (redone && materialEditor_.OpenAssetId().IsValid()) {
        SyncMaterialEditorWorkingCopyRuntimePreview();
        sceneGraphCookPending_ = true;
        RequestOpenMaterialSceneGraphCook();
        MarkSceneRenderDirty();
    }
    return redone;
}

bool EditorSceneContext::BeginSceneEditTransaction(std::string label) {
    return SceneCommands().BeginTransaction(std::move(label));
}

bool EditorSceneContext::CommitSceneEditTransaction() {
    return SceneCommands().CommitTransaction();
}

void EditorSceneContext::CancelSceneEditTransaction() {
    SceneCommands().CancelTransaction();
}

bool EditorSceneContext::HasPendingSceneEditTransaction() const noexcept {
    return pendingSceneTransactionLabel_.has_value();
}

const kb::assets::TerrainAsset* EditorSceneContext::TerrainForEditing(
    kb::scene::SceneEntity entity,
    std::string* error) const {
    if (terrainStroke_.has_value() && terrainStroke_->entity == entity) {
        if (error != nullptr) error->clear();
        return &terrainStroke_->working;
    }
    const kb::scene::MeshRendererComponent* renderer =
        scene_->Components().MeshRenderers().TryGet(entity);
    const kb::assets::AssetId assetId{
        renderer == nullptr ? 0U : renderer->meshAssetId };
    const kb::assets::AssetMetadata* metadata =
        scene_->Assets().Manager().Registry().Find(assetId);
    if (!assetId.IsValid() || metadata == nullptr ||
        !EditorTerrainService::IsTerrainEntity(*scene_, entity)) {
        if (error != nullptr) *error = "Entity does not reference a terrain asset";
        return nullptr;
    }
    if (!terrainReadCache_.has_value() ||
        terrainReadCache_->assetId != assetId ||
        terrainReadCache_->contentHash != metadata->contentHash) {
        std::optional<kb::assets::TerrainAsset> terrain =
            EditorTerrainService::Load(*scene_, entity, error);
        if (!terrain.has_value()) return nullptr;
        terrainReadCache_ = TerrainReadCache{
            .assetId = assetId,
            .contentHash = metadata->contentHash,
            .terrain = std::move(*terrain),
        };
    }
    if (error != nullptr) error->clear();
    return &terrainReadCache_->terrain;
}

bool EditorSceneContext::BeginTerrainBrushStroke(
    kb::scene::SceneEntity entity,
    std::string label,
    bool layerPaint,
    std::string* error) {
    CancelTerrainBrushStroke();

    const kb::assets::TerrainAsset* source = TerrainForEditing(entity, error);
    if (source == nullptr) return false;
    if (!terrainReadCache_.has_value()) {
        if (error != nullptr) *error = "Terrain edit cache is unavailable";
        return false;
    }

    const kb::scene::MeshRendererComponent* renderer =
        scene_->Components().MeshRenderers().TryGet(entity);
    const kb::assets::AssetId assetId{
        renderer == nullptr ? 0U : renderer->meshAssetId };
    if (!assetId.IsValid()) {
        if (error != nullptr) *error = "Entity does not reference a terrain asset";
        return false;
    }

    // TerrainForEditing owns this value through terrainReadCache_ while no stroke is active. Move that
    // value into the stroke and make only the single copy required by undo; large terrains previously
    // copied the full height/hole/weight payload twice on mouse-down.
    kb::assets::TerrainAsset working = std::move(terrainReadCache_->terrain);
    terrainReadCache_.reset();
    kb::assets::TerrainAsset before = working;
    std::shared_ptr<kb::render::RenderMeshAssetData> previewMesh =
        layerPaint
            ? EditorTerrainService::CreateLayerPreviewMesh(*scene_, assetId, working, error)
            : EditorTerrainService::CreatePreviewMesh(*scene_, assetId, working, error);
    if (previewMesh == nullptr) return false;

    terrainStroke_ = TerrainStrokeState{
        .entity = entity,
        .assetId = assetId,
        .before = std::move(before),
        .working = std::move(working),
        .previewMesh = std::move(previewMesh),
        .layerPaint = layerPaint,
        .label = std::move(label),
    };
    if (error != nullptr) error->clear();
    return true;
}

bool EditorSceneContext::ApplyTerrainBrushStamp(
    kb::scene::SceneEntity entity,
    const kb::terrain_editor::TerrainBrushSettings& settings,
    const kb::terrain_editor::TerrainBrushStamp& stamp,
    bool beginStroke,
    std::string* error) {
    if (beginStroke && !BeginTerrainBrushStroke(entity, "Sculpt Terrain", false, error)) return false;
    if (!terrainStroke_.has_value() || terrainStroke_->entity != entity) {
        if (error != nullptr) *error = "Terrain brush stroke is not active";
        return false;
    }

    const kb::terrain_editor::TerrainBrushResult result =
        kb::terrain_editor::ApplyTerrainBrushToValidatedTerrain(
            terrainStroke_->working, settings, stamp);
    if (!result.Changed()) {
        if (error != nullptr) error->clear();
        return true;
    }
    if (!EditorTerrainService::UpdatePreviewMesh(
            terrainStroke_->working, result,
            settings.mode == kb::terrain_editor::TerrainBrushMode::CutHole ||
                settings.mode == kb::terrain_editor::TerrainBrushMode::FillHole,
            terrainStroke_->previewMesh, error)) {
        CancelTerrainBrushStroke();
        return false;
    }
    if (!terrainStroke_->previewPublished && !EditorTerrainService::PublishPreview(
            *scene_, terrainStroke_->assetId,
            terrainStroke_->working, terrainStroke_->previewMesh,
            true, error)) {
        CancelTerrainBrushStroke();
        return false;
    }
    terrainStroke_->previewPublished = true;
    terrainStroke_->changed = true;
    return true;
}

bool EditorSceneContext::ApplyTerrainLayerPaintStamp(
    kb::scene::SceneEntity entity,
    const kb::terrain_editor::TerrainLayerPaintSettings& settings,
    const kb::terrain_editor::TerrainBrushStamp& stamp,
    bool beginStroke,
    std::string* error) {
    return ApplyTerrainLayerPaintSegment(
        entity, settings, stamp, stamp, beginStroke, error);
}

bool EditorSceneContext::ApplyTerrainLayerPaintSegment(
    kb::scene::SceneEntity entity,
    const kb::terrain_editor::TerrainLayerPaintSettings& settings,
    const kb::terrain_editor::TerrainBrushStamp& start,
    const kb::terrain_editor::TerrainBrushStamp& end,
    bool beginStroke,
    std::string* error) {
    if (beginStroke && !BeginTerrainBrushStroke(entity, "Paint Terrain Material", true, error)) return false;
    if (!terrainStroke_.has_value() || terrainStroke_->entity != entity) {
        if (error != nullptr) *error = "Terrain material paint stroke is not active";
        return false;
    }

    const kb::terrain_editor::TerrainLayerPaintResult result =
        kb::terrain_editor::ApplyTerrainLayerPaintSegment(
            terrainStroke_->working, settings, start, end);
    if (!result.Changed()) {
        if (error != nullptr) error->clear();
        return true;
    }
    if (!EditorTerrainService::UpdateLayerPreviewMesh(
            terrainStroke_->working, result, terrainStroke_->previewMesh, error)) {
        CancelTerrainBrushStroke();
        return false;
    }
    if (!terrainStroke_->previewPublished && !EditorTerrainService::PublishPreview(
            *scene_, terrainStroke_->assetId,
            terrainStroke_->working, terrainStroke_->previewMesh,
            true, error)) {
        CancelTerrainBrushStroke();
        return false;
    }
    terrainStroke_->previewPublished = true;
    terrainStroke_->changed = true;
    return true;
}

bool EditorSceneContext::AddTerrainMaterialLayer(
    kb::scene::SceneEntity entity,
    kb::assets::AssetId materialAssetId,
    std::string* error) {
    CancelTerrainBrushStroke();
    std::optional<EditorTerrainAssetState> captured =
        EditorTerrainService::Capture(*scene_, entity, error);
    if (!captured.has_value()) return false;
    kb::assets::TerrainAsset edited = captured->terrain;
    if (!kb::terrain_editor::AddTerrainMaterialLayer(edited, materialAssetId.value)) {
        if (error != nullptr) *error = "Terrain supports at most four material layers";
        return false;
    }
    const std::uint8_t selected = static_cast<std::uint8_t>(edited.materialLayers.size() - 1U);
    auto command = std::make_unique<EditorTerrainAssetEditCommand>(
        *scene_, captured->assetId, "Add Terrain Material Layer",
        std::move(captured->terrain), std::move(edited));
    if (!commandStack_.Execute(std::move(command))) {
        if (error != nullptr && error->empty()) *error = "Terrain material layer could not be saved";
        return false;
    }
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    tool.selectedMaterialLayer = selected;
    tool.mode = EditorTerrainToolMode::Paint;
    tool.editingEnabled = true;
    tool.brush.strength = std::min(tool.brush.strength, 1.0F);
    terrainReadCache_.reset();
    MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity>{ &entity, 1U });
    if (error != nullptr) error->clear();
    return true;
}

bool EditorSceneContext::SetTerrainMaterialLayer(
    kb::scene::SceneEntity entity,
    std::uint8_t layerIndex,
    kb::assets::AssetId materialAssetId,
    std::string* error) {
    CancelTerrainBrushStroke();
    std::optional<EditorTerrainAssetState> captured =
        EditorTerrainService::Capture(*scene_, entity, error);
    if (!captured.has_value()) return false;
    kb::assets::TerrainAsset edited = captured->terrain;
    if (!kb::terrain_editor::SetTerrainMaterialLayer(
            edited, layerIndex, materialAssetId.value)) {
        if (error != nullptr) *error = "Terrain material layer index is invalid";
        return false;
    }
    auto command = std::make_unique<EditorTerrainAssetEditCommand>(
        *scene_, captured->assetId, "Assign Terrain Material Layer",
        std::move(captured->terrain), std::move(edited));
    if (!commandStack_.Execute(std::move(command))) {
        if (error != nullptr && error->empty()) *error = "Terrain material layer could not be saved";
        return false;
    }
    EditorTerrainService::ToolState().selectedMaterialLayer = layerIndex;
    terrainReadCache_.reset();
    MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity>{ &entity, 1U });
    if (error != nullptr) error->clear();
    return true;
}

bool EditorSceneContext::RemoveTerrainMaterialLayer(
    kb::scene::SceneEntity entity,
    std::uint8_t layerIndex,
    std::string* error) {
    CancelTerrainBrushStroke();
    std::optional<EditorTerrainAssetState> captured =
        EditorTerrainService::Capture(*scene_, entity, error);
    if (!captured.has_value()) return false;
    kb::assets::TerrainAsset edited = captured->terrain;
    if (!kb::terrain_editor::RemoveTerrainMaterialLayer(edited, layerIndex)) {
        if (error != nullptr) *error = "Terrain material layer index is invalid";
        return false;
    }
    const std::size_t remainingLayerCount = edited.materialLayers.size();
    auto command = std::make_unique<EditorTerrainAssetEditCommand>(
        *scene_, captured->assetId, "Remove Terrain Material Layer",
        std::move(captured->terrain), std::move(edited));
    if (!commandStack_.Execute(std::move(command))) {
        if (error != nullptr && error->empty()) *error = "Terrain material layer could not be saved";
        return false;
    }
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    tool.selectedMaterialLayer = remainingLayerCount == 0U
        ? 0U
        : std::min<std::uint8_t>(layerIndex, static_cast<std::uint8_t>(remainingLayerCount - 1U));
    if (remainingLayerCount == 0U && tool.mode == EditorTerrainToolMode::Paint) {
        tool.mode = EditorTerrainToolMode::Sculpt;
    }
    terrainReadCache_.reset();
    MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity>{ &entity, 1U });
    if (error != nullptr) error->clear();
    return true;
}

bool EditorSceneContext::CommitTerrainBrushStroke(std::string* error) {
    if (!terrainStroke_.has_value()) {
        if (error != nullptr) error->clear();
        return true;
    }
    TerrainStrokeState completed = std::move(*terrainStroke_);
    terrainStroke_.reset();
    if (!completed.changed) {
        if (error != nullptr) error->clear();
        return true;
    }
    if (!EditorTerrainService::Persist(
            *scene_, completed.assetId, completed.working,
            completed.previewMesh, error)) {
        static_cast<void>(EditorTerrainService::PublishPreview(
            *scene_, completed.assetId, completed.before));
        terrainReadCache_.reset();
        MarkSceneEntitiesRenderDirty(
            std::span<const kb::scene::SceneEntity>{ &completed.entity, 1U });
        return false;
    }
    commandStack_.PushExecuted(std::make_unique<EditorTerrainAssetEditCommand>(
        *scene_, completed.assetId, completed.label,
        std::move(completed.before), std::move(completed.working)));
    terrainReadCache_.reset();
    MarkSceneEntitiesRenderDirty(
        std::span<const kb::scene::SceneEntity>{ &completed.entity, 1U });
    if (error != nullptr) error->clear();
    return true;
}

void EditorSceneContext::CancelTerrainBrushStroke() noexcept {
    if (!terrainStroke_.has_value()) return;
    if (terrainStroke_->changed) {
        bool restored = false;
        if (terrainStroke_->layerPaint && terrainStroke_->before.layerWeightWidth != 0U &&
            terrainStroke_->before.layerWeightHeight != 0U) {
            const kb::terrain_editor::TerrainLayerPaintResult wholeWeightMap{
                .changedTexels = terrainStroke_->before.layerWeightWidth * terrainStroke_->before.layerWeightHeight,
                .minX = 0U,
                .minY = 0U,
                .maxX = terrainStroke_->before.layerWeightWidth - 1U,
                .maxY = terrainStroke_->before.layerWeightHeight - 1U,
            };
            restored = EditorTerrainService::UpdateLayerPreviewMesh(
                terrainStroke_->before, wholeWeightMap, terrainStroke_->previewMesh) &&
                EditorTerrainService::PublishPreview(
                    *scene_, terrainStroke_->assetId, terrainStroke_->before,
                    terrainStroke_->previewMesh, true);
        }
        if (!restored) {
            static_cast<void>(EditorTerrainService::PublishPreview(
                *scene_, terrainStroke_->assetId, terrainStroke_->before));
        }
        const kb::scene::SceneEntity entity = terrainStroke_->entity;
        MarkSceneEntitiesRenderDirty(
            std::span<const kb::scene::SceneEntity>{ &entity, 1U });
    }
    terrainStroke_.reset();
    terrainReadCache_.reset();
}

bool EditorSceneContext::ImportTerrainHeightmap(
    kb::scene::SceneEntity entity,
    const std::filesystem::path& path,
    const kb::terrain_editor::TerrainHeightmapImportSettings& settings,
    std::string* error) {
    CancelTerrainBrushStroke();
    std::optional<EditorTerrainAssetState> captured =
        EditorTerrainService::Capture(*scene_, entity, error);
    if (!captured.has_value()) return false;
    std::optional<kb::assets::TerrainAsset> imported =
        EditorTerrainService::BuildHeightmapImport(
            captured->terrain, path, settings, error);
    if (!imported.has_value()) return false;
    auto command = std::make_unique<EditorTerrainAssetEditCommand>(
        *scene_, captured->assetId, "Import Terrain Heightmap",
        std::move(captured->terrain), std::move(*imported));
    if (!commandStack_.Execute(std::move(command))) {
        if (error != nullptr && error->empty()) {
            *error = "Imported terrain asset could not be saved";
        }
        return false;
    }
    terrainReadCache_.reset();
    MarkSceneEntitiesRenderDirty(
        std::span<const kb::scene::SceneEntity>{ &entity, 1U });
    if (error != nullptr) error->clear();
    return true;
}

bool EditorSceneContext::ConfigureTerrain(
    kb::scene::SceneEntity entity,
    const EditorTerrainConfiguration& configuration,
    std::string* error) {
    CancelTerrainBrushStroke();
    std::optional<EditorTerrainAssetState> captured =
        EditorTerrainService::Capture(*scene_, entity, error);
    if (!captured.has_value()) return false;
    if (captured->terrain.width == configuration.width &&
        captured->terrain.height == configuration.height &&
        captured->terrain.chunkQuads == configuration.chunkQuads &&
        captured->terrain.lodCount == configuration.lodCount &&
        captured->terrain.worldSizeX == configuration.worldSizeX &&
        captured->terrain.worldSizeZ == configuration.worldSizeZ) {
        if (error != nullptr) error->clear();
        return true;
    }
    std::optional<kb::assets::TerrainAsset> reconfigured =
        EditorTerrainService::BuildReconfigured(
            captured->terrain, configuration, error);
    if (!reconfigured.has_value()) return false;
    auto command = std::make_unique<EditorTerrainAssetEditCommand>(
        *scene_, captured->assetId, "Configure Terrain",
        std::move(captured->terrain), std::move(*reconfigured));
    if (!commandStack_.Execute(std::move(command))) {
        if (error != nullptr && error->empty()) {
            *error = "Terrain configuration could not be saved";
        }
        return false;
    }
    terrainReadCache_.reset();
    MarkSceneEntitiesRenderDirty(
        std::span<const kb::scene::SceneEntity>{ &entity, 1U });
    if (error != nullptr) error->clear();
    return true;
}

kb::scene::SceneEntity EditorSceneContext::SelectedEntity() const noexcept {
    return hierarchySelection_.Primary();
}

const std::vector<kb::scene::SceneEntity>& EditorSceneContext::SelectedHierarchyEntities() const noexcept {
    return hierarchySelection_.SelectedEntities();
}

bool EditorSceneContext::IsHierarchyEntitySelected(kb::scene::SceneEntity entity) const noexcept {
    return hierarchySelection_.IsSelected(entity);
}

void EditorSceneContext::SelectEntity(kb::scene::SceneEntity entity) noexcept {
    const kb::scene::SceneEntity previous = hierarchySelection_.Primary();
    const kb::scene::SceneEntity selected = scene_->Entities().IsAlive(entity) ? entity : kb::scene::SceneEntity{};
    if (selected != previous && terrainStroke_.has_value()) {
        CancelTerrainBrushStroke();
        EditorTerrainService::ToolState().strokeActive = false;
    }
    if (hierarchyRenameEntity_.IsValid() && hierarchyRenameEntity_ != selected) {
        static_cast<void>(CommitHierarchyRename());
    }
    materialGraphFocused_ = false;
    hierarchySelection_.SelectEntity(selected);
    assetBrowser_.ClearSelection();
    EditorTerrainToolState& terrainTool = EditorTerrainService::ToolState();
    if (selected != previous && IsProjectPluginEnabled("Editor.Terrain") &&
        EditorTerrainService::IsTerrainEntity(*scene_, selected)) {
        terrainTool.mode = EditorTerrainToolMode::Sculpt;
        terrainTool.editingEnabled = true;
        terrainTool.brushMenuOpen = false;
        terrainTool.brushShapeMenuOpen = false;
    } else if (!EditorTerrainService::IsTerrainEntity(*scene_, selected)) {
        terrainTool.strokeActive = false;
        terrainTool.hoverVisible = false;
        terrainTool.hoverEntityId = 0U;
        terrainTool.brushMenuOpen = false;
        terrainTool.brushShapeMenuOpen = false;
    }
}

void EditorSceneContext::SelectHierarchyEntities(std::span<const kb::scene::SceneEntity> entities) noexcept {
    std::vector<kb::scene::SceneEntity> alive;
    alive.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        if (scene_->Entities().IsAlive(entity) && !ContainsEntity(alive, entity)) {
            alive.push_back(entity);
        }
    }

    if (alive.empty()) {
        ClearHierarchySelection();
        return;
    }

    if (hierarchyRenameEntity_.IsValid() && !ContainsEntity(alive, hierarchyRenameEntity_)) {
        static_cast<void>(CommitHierarchyRename());
    }
    materialGraphFocused_ = false;
    hierarchySelection_.SelectEntities(alive);
    assetBrowser_.ClearSelection();
}

void EditorSceneContext::ClearHierarchySelection() noexcept {
    static_cast<void>(CommitHierarchyRename());
    materialGraphFocused_ = false;
    hierarchySelection_.Clear();
}

bool EditorSceneContext::SelectHierarchyRow(std::size_t rowIndex) noexcept {
    return SelectHierarchyRow(rowIndex, false, false);
}

bool EditorSceneContext::SelectHierarchyRow(std::size_t rowIndex, bool additive, bool range) noexcept {
    const std::vector<EditorHierarchyRow>& rows = HierarchyRows();
    if (IsHierarchyRenaming()) {
        static_cast<void>(CommitHierarchyRename());
    }
    const bool selected = hierarchySelection_.SelectRow(rows, rowIndex, additive, range);
    if (selected) {
        assetBrowser_.ClearSelection();
    }
    return selected;
}

const EditorSceneViewportBoxSelectionState& EditorSceneContext::ViewportBoxSelection() const noexcept {
    return viewportBoxSelection_;
}

void EditorSceneContext::BeginViewportBoxSelection(const EditorSceneViewportBoxSelectionState& selection) noexcept {
    viewportBoxSelection_ = selection;
}

void EditorSceneContext::UpdateViewportBoxSelection(POINT current, bool active) noexcept {
    viewportBoxSelection_.current = current;
    viewportBoxSelection_.active = active;
}

void EditorSceneContext::ClearViewportBoxSelection() noexcept {
    viewportBoxSelection_ = {};
}

const std::vector<EditorHierarchyRow>& EditorSceneContext::HierarchyRows() const {
    RebuildHierarchyRowsIfNeeded();
    return hierarchyRowsCache_;
}

std::size_t EditorSceneContext::HierarchyRowCount() const {
    return HierarchyRows().size();
}

const EditorHierarchyRow* EditorSceneContext::HierarchyRowAt(std::size_t rowIndex) const {
    const std::vector<EditorHierarchyRow>& rows = HierarchyRows();
    return rowIndex < rows.size() ? &rows[rowIndex] : nullptr;
}

int EditorSceneContext::HierarchyScrollOffset() const noexcept {
    return hierarchyScrollOffset_;
}

int EditorSceneContext::BuildGameScrollOffset() const noexcept {
    return buildGameScrollOffset_;
}

bool EditorSceneContext::IsBuildGameSectionCollapsed(int section) const noexcept {
    return section >= 0 && section < 32 &&
        (buildGameCollapsedSections_ & (1U << static_cast<unsigned>(section))) != 0U;
}

void EditorSceneContext::ToggleBuildGameSection(int section) noexcept {
    if (section < 0 || section >= 32) {
        return;
    }
    buildGameCollapsedSections_ ^= (1U << static_cast<unsigned>(section));
}

int EditorSceneContext::BuildGameSelectedTarget() const noexcept {
    return buildGameSelectedTarget_;
}

bool EditorSceneContext::SetBuildGameSelectedTarget(int target) noexcept {
    if (target < 0 || target >= static_cast<int>(kb::packaging::PackagingTargets().size()) ||
        buildGameSelectedTarget_ == target) {
        return false;
    }
    CancelBuildGameTextEdit();
    ClearBuildGameSigningPasswords();
    buildGameSelectedTarget_ = target;
    return true;
}

int EditorSceneContext::BuildGameSelectedProfile() const noexcept {
    return buildGameSelectedProfile_;
}

bool EditorSceneContext::SetBuildGameSelectedProfile(int profile) noexcept {
    if (profile < 0 || profile > 1 || buildGameSelectedProfile_ == profile) {
        return false;
    }
    CancelBuildGameTextEdit();
    ClearBuildGameSigningPasswords();
    buildGameSelectedProfile_ = profile;
    return true;
}

int EditorSceneContext::BuildGameHoveredTarget() const noexcept {
    return buildGameHoveredTarget_;
}

int EditorSceneContext::BuildGameHoveredProfile() const noexcept {
    return buildGameHoveredProfile_;
}

bool EditorSceneContext::SetBuildGameSidebarHover(int target, int profile) noexcept {
    if (buildGameHoveredTarget_ == target && buildGameHoveredProfile_ == profile) {
        return false;
    }
    buildGameHoveredTarget_ = target;
    buildGameHoveredProfile_ = profile;
    return true;
}

int EditorSceneContext::BuildGameHoveredSection() const noexcept {
    return buildGameHoveredSection_;
}

int EditorSceneContext::BuildGameHoveredRow() const noexcept {
    return buildGameHoveredRow_;
}

bool EditorSceneContext::SetBuildGameHover(int section, int row) noexcept {
    if (buildGameHoveredSection_ == section && buildGameHoveredRow_ == row) {
        return false;
    }
    buildGameHoveredSection_ = section;
    buildGameHoveredRow_ = row;
    return true;
}

bool EditorSceneContext::SetBuildGameScrollOffset(int offset, int maxOffset) noexcept {
    const int clamped = std::clamp(offset, 0, std::max(0, maxOffset));
    if (buildGameScrollOffset_ == clamped) {
        return false;
    }
    buildGameScrollOffset_ = clamped;
    return true;
}

bool EditorSceneContext::IsHierarchyScrollbarDragging() const noexcept {
    return hierarchyScrollbarDragging_;
}

bool EditorSceneContext::SetHierarchyScrollOffset(int offset, int maxOffset) noexcept {
    const int clamped = std::clamp(offset, 0, std::max(0, maxOffset));
    if (hierarchyScrollOffset_ == clamped) {
        return false;
    }
    hierarchyScrollOffset_ = clamped;
    return true;
}

void EditorSceneContext::BeginHierarchyScrollbarDrag(int y) noexcept {
    hierarchyScrollbarDragging_ = true;
    hierarchyScrollbarDragY_ = y;
    hierarchyScrollbarDragStartOffset_ = hierarchyScrollOffset_;
}

void EditorSceneContext::DragHierarchyScrollbar(int y, int trackTravel, int maxOffset) noexcept {
    if (!hierarchyScrollbarDragging_) {
        return;
    }

    const int delta = y - hierarchyScrollbarDragY_;
    const int offsetDelta = trackTravel <= 0 || maxOffset <= 0 ? 0 : (delta * maxOffset) / trackTravel;
    static_cast<void>(SetHierarchyScrollOffset(hierarchyScrollbarDragStartOffset_ + offsetDelta, maxOffset));
}

void EditorSceneContext::EndHierarchyScrollbarDrag() noexcept {
    hierarchyScrollbarDragging_ = false;
}

std::string_view EditorSceneContext::HierarchySearchQuery() const noexcept {
    return hierarchySearch_.Query();
}

bool EditorSceneContext::IsHierarchySearchFocused() const noexcept {
    return hierarchySearch_.IsFocused();
}

bool EditorSceneContext::IsHierarchyRenaming() const noexcept {
    return hierarchyRenameEntity_.IsValid() && scene_->Entities().IsAlive(hierarchyRenameEntity_);
}

bool EditorSceneContext::IsAnyInlineTextEditActive() const noexcept {
    return IsHierarchyRenaming()
        || IsHierarchySearchFocused()
        || IsSkeletalMeshEditorTreeSearchFocused()
        || assetBrowser_.IsTextEditing()
        || assetBrowser_.IsSearchFocused()
        || Inspector().IsTextEditing()
        || IsMaterialGraphNodeRenameEditing()
        || IsMaterialGraphConstantInlineEditing()
        || IsMaterialEditorFindFocused();
}

bool EditorSceneContext::FrameSelectedEntitiesInViewport() noexcept {
    const kb::scene::SceneEntity primary = SelectedEntity();
    const std::vector<kb::scene::SceneEntity>& selected = SelectedHierarchyEntities();
    const std::optional<kb::scene::Vec3> center = EditorSceneSelectionPivot::Resolve(*scene_, selected, primary);
    if (!center.has_value()) {
        return false;
    }

    // Bounding-sphere radius of the selected entities around the pivot. A
    // single point-like selection collapses to zero spread; the default floor
    // then frames it at a comfortable distance.
    float maxSpread = 0.0F;
    for (const kb::scene::SceneEntity entity : selected) {
        if (!entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
            continue;
        }
        const kb::scene::TransformComponent* transform = scene_->Transforms().TryGet(entity);
        if (transform == nullptr) {
            continue;
        }
        const kb::scene::Vec3 delta{
            transform->localPosition.x - center->x,
            transform->localPosition.y - center->y,
            transform->localPosition.z - center->z,
        };
        maxSpread = std::max(maxSpread, std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z));
    }

    // Frame every live scene-viewport camera (each docked/floating scene panel
    // renders its own per-panel camera; the parameterless default is not one of
    // them), so F actually reframes the viewport the user is looking at. The
    // 0.5s eased animation is advanced each frame by TickViewportFocusAnimations.
    constexpr float kFrameSelectionAnimationSeconds = 0.5F;
    static_cast<void>(viewportState_.FocusAllCamerasOn(*center, std::max(1.5F, maxSpread + 1.5F), kFrameSelectionAnimationSeconds));
    MarkSceneRenderDirty();
    return true;
}

bool EditorSceneContext::TickViewportFocusAnimations(float deltaSeconds) noexcept {
    return viewportState_.TickFocusAnimations(deltaSeconds);
}

bool EditorSceneContext::IsHierarchyRenaming(kb::scene::SceneEntity entity) const noexcept {
    return IsHierarchyRenaming() && hierarchyRenameEntity_ == entity;
}

bool EditorSceneContext::IsHierarchyRenameSelectingAll() const noexcept {
    return IsHierarchyRenaming() && hierarchyRenameSelectingAll_;
}

std::string_view EditorSceneContext::HierarchyRenameBuffer() const noexcept {
    return hierarchyRenameBuffer_;
}

void EditorSceneContext::FocusHierarchySearch(bool focused) noexcept {
    if (focused) {
        static_cast<void>(CommitHierarchyRename());
    }
    hierarchySearch_.Focus(focused);
}

void EditorSceneContext::SetHierarchySearchQuery(std::string query) {
    hierarchySearch_.SetQuery(std::move(query));
    InvalidateHierarchyRows();
}

void EditorSceneContext::AppendHierarchySearchText(wchar_t character) {
    hierarchySearch_.AppendAscii(character);
    InvalidateHierarchyRows();
}

void EditorSceneContext::InsertHierarchySearchText(std::string_view text) {
    hierarchySearch_.Insert(text);
    InvalidateHierarchyRows();
}

void EditorSceneContext::BackspaceHierarchySearch() {
    hierarchySearch_.Backspace();
    InvalidateHierarchyRows();
}

void EditorSceneContext::SelectAllHierarchySearch() noexcept {
    hierarchySearch_.SelectAll();
}

void EditorSceneContext::ClearHierarchySearch() {
    hierarchySearch_.Clear();
    InvalidateHierarchyRows();
}

bool EditorSceneContext::BeginHierarchyRename() {
    const kb::scene::SceneEntity entity = SelectedEntity();
    if (!scene_->Entities().IsAlive(entity)) {
        CancelHierarchyRename();
        return false;
    }

    hierarchySearch_.Focus(false);
    assetBrowser_.CancelTextEdit();
    inspector_.EndTextEdit();
    hierarchyRenameEntity_ = entity;
    hierarchyRenameBuffer_ = scene_->Entities().Name(entity);
    hierarchyRenameSelectingAll_ = true;
    InvalidateHierarchyRows();
    return true;
}

void EditorSceneContext::AppendHierarchyRenameText(wchar_t character) {
    if (!IsHierarchyRenaming()) {
        return;
    }
    if (character >= 32 && character <= 126) {
        if (hierarchyRenameSelectingAll_) {
            hierarchyRenameBuffer_.clear();
            hierarchyRenameSelectingAll_ = false;
        }
        hierarchyRenameBuffer_.push_back(static_cast<char>(character));
        InvalidateHierarchyRows();
    }
}

void EditorSceneContext::InsertHierarchyRenameText(std::string_view text) {
    if (!IsHierarchyRenaming()) {
        return;
    }
    if (hierarchyRenameSelectingAll_) {
        hierarchyRenameBuffer_.clear();
        hierarchyRenameSelectingAll_ = false;
    }
    for (const char character : text) {
        if (character >= 32 && character <= 126) {
            hierarchyRenameBuffer_.push_back(character);
        }
    }
    InvalidateHierarchyRows();
}

void EditorSceneContext::SetHierarchyRenameText(std::string text) {
    if (!IsHierarchyRenaming()) {
        return;
    }
    hierarchyRenameBuffer_ = std::move(text);
    hierarchyRenameSelectingAll_ = false;
    InvalidateHierarchyRows();
}

void EditorSceneContext::BackspaceHierarchyRename() {
    if (!IsHierarchyRenaming()) {
        return;
    }
    if (hierarchyRenameSelectingAll_) {
        hierarchyRenameBuffer_.clear();
        hierarchyRenameSelectingAll_ = false;
        InvalidateHierarchyRows();
        return;
    }
    if (!hierarchyRenameBuffer_.empty()) {
        hierarchyRenameBuffer_.pop_back();
    }
    InvalidateHierarchyRows();
}

void EditorSceneContext::SelectAllHierarchyRename() noexcept {
    if (IsHierarchyRenaming()) {
        hierarchyRenameSelectingAll_ = true;
        InvalidateHierarchyRows();
    }
}

void EditorSceneContext::ClearHierarchyRename() noexcept {
    if (!IsHierarchyRenaming()) {
        return;
    }
    hierarchyRenameBuffer_.clear();
    hierarchyRenameSelectingAll_ = false;
    InvalidateHierarchyRows();
}

bool EditorSceneContext::CommitHierarchyRename() {
    if (!IsHierarchyRenaming()) {
        CancelHierarchyRename();
        return false;
    }

    const kb::scene::SceneEntity entity = hierarchyRenameEntity_;
    const std::string name = hierarchyRenameBuffer_.empty() ? "Entity" : hierarchyRenameBuffer_;
    if (scene_->Entities().Name(entity) == name) {
        CancelHierarchyRename();
        return false;
    }

    const bool renamed = ExecuteSceneCommand("Rename Entity", [this, entity, name]() {
        if (!scene_->Entities().IsAlive(entity)) {
            return false;
        }
        scene_->Entities().SetName(entity, name);
        return true;
    });
    if (renamed) {
        console_.Info("Hierarchy", "Entity renamed.");
    }
    CancelHierarchyRename();
    return renamed;
}

void EditorSceneContext::CancelHierarchyRename() noexcept {
    const bool changed = hierarchyRenameEntity_.IsValid() || !hierarchyRenameBuffer_.empty() || hierarchyRenameSelectingAll_;
    hierarchyRenameEntity_ = {};
    hierarchyRenameBuffer_.clear();
    hierarchyRenameSelectingAll_ = false;
    if (changed) {
        InvalidateHierarchyRows();
    }
}

bool EditorSceneContext::BeginAssetFolderCreation() {
    static_cast<void>(CommitHierarchyRename());
    assetBrowser_.BeginNewFolder();
    return true;
}

bool EditorSceneContext::BeginAssetRename() {
    static_cast<void>(CommitHierarchyRename());
    return assetBrowser_.BeginRenameSelection(scene_->Assets().Manager());
}

bool EditorSceneContext::BeginAssetRename(kb::assets::AssetId id) {
    static_cast<void>(CommitHierarchyRename());
    return assetBrowser_.BeginRenameAsset(id, scene_->Assets().Manager());
}

bool EditorSceneContext::BeginAssetFolderRename(const std::filesystem::path& virtualFolder) {
    static_cast<void>(CommitHierarchyRename());
    return assetBrowser_.BeginRenameFolder(virtualFolder, scene_->Assets().Manager());
}

bool EditorSceneContext::CommitAssetTextEdit() {
    const bool committed = EditorSceneAssetBrowserCommands::CommitTextEdit(*scene_, assetBrowser_);
    if (committed) {
        console_.Info("Assets", "Asset browser text edit committed.");
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset browser text edit failed."));
    }
    return committed;
}

void EditorSceneContext::CancelAssetTextEdit() noexcept {
    assetBrowser_.CancelTextEdit();
}

bool EditorSceneContext::DeleteSelectedAssetBrowserItem() {
    const bool deleted = EditorSceneAssetBrowserCommands::DeleteSelected(*scene_, assetBrowser_);
    if (deleted) {
        console_.Info("Assets", "Selected asset browser item deleted.");
    } else {
        console_.Warning("Assets", "No asset browser item was deleted.");
    }
    return deleted;
}

bool EditorSceneContext::DeleteSelectedHierarchyEntity() noexcept {
    if (hierarchySelection_.SelectedEntities().empty()) {
        ClearHierarchySelection();
        return false;
    }

    const std::vector<kb::scene::SceneEntity> deleting = TopLevelSelectedEntities(*scene_, hierarchySelection_.SelectedEntities());
    if (!AnyAlive(*scene_, deleting)) {
        ClearHierarchySelection();
        console_.Warning("Hierarchy", "No hierarchy entity was deleted.");
        return false;
    }

    const std::vector<EditorSceneObjectPrefabPayload> payloads = EditorSceneObjectPayloadBuilder::Capture(
        *this,
        std::span<const kb::scene::SceneEntity>{ deleting.data(), deleting.size() });
    if (payloads.empty()) {
        console_.Warning("Hierarchy", "No hierarchy entity was deleted.");
        return false;
    }

    auto command = std::make_unique<EditorScenePrefabRemoveCommand>(*this, "Delete Entity", deleting, payloads);
    const bool deleted = commandStack_.Execute(std::move(command));
    if (deleted) {
        MarkSceneDocumentDirty();
        console_.Info("Hierarchy", "Selected hierarchy entity deleted.");
    } else {
        console_.Warning("Hierarchy", "No hierarchy entity was deleted.");
    }
    return deleted;
}

bool EditorSceneContext::DuplicateSelectedHierarchyEntities() {
    const std::vector<kb::scene::SceneEntity> selected = hierarchySelection_.SelectedEntities();
    const std::vector<kb::scene::SceneEntity> duplicating = TopLevelSelectedEntities(*scene_, selected);
    if (duplicating.empty() || !AnyAlive(*scene_, duplicating)) {
        console_.Warning("Hierarchy", "No hierarchy entity was duplicated.");
        return false;
    }

    const std::vector<EditorSceneObjectPrefabPayload> payloads = EditorSceneObjectPayloadBuilder::Capture(
        *this,
        std::span<const kb::scene::SceneEntity>{ duplicating.data(), duplicating.size() });
    if (payloads.empty()) {
        console_.Warning("Hierarchy", "No hierarchy entity was duplicated.");
        return false;
    }

    auto command = std::make_unique<EditorScenePrefabSpawnCommand>(*this, "Duplicate Entity", payloads);
    EditorScenePrefabSpawnCommand* duplicateCommand = command.get();
    const bool duplicated = commandStack_.Execute(std::move(command));
    if (duplicated) {
        SelectHierarchyEntities(duplicateCommand->CreatedEntities());
        MarkSceneDocumentDirty();
    }

    if (duplicated) {
        console_.Info("Hierarchy", "Selected hierarchy entity duplicated.");
    } else {
        console_.Warning("Hierarchy", "No hierarchy entity was duplicated.");
    }
    return duplicated;
}

bool EditorSceneContext::AdoptCreatedHierarchyEntities(std::string label, std::span<const kb::scene::SceneEntity> entities) {
    const std::vector<EditorSceneObjectPrefabPayload> payloads = EditorSceneObjectPayloadBuilder::Capture(*this, entities);
    if (payloads.empty() || !AnyAlive(*scene_, entities)) {
        return false;
    }

    std::vector<kb::scene::SceneEntity> alive;
    alive.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        if (scene_->Entities().IsAlive(entity) && !ContainsEntity(alive, entity)) {
            alive.push_back(entity);
        }
    }
    if (alive.empty()) {
        return false;
    }

    auto command = std::make_unique<EditorScenePrefabSpawnCommand>(*this, std::move(label), payloads, alive);
    commandStack_.PushExecuted(std::move(command));
    SelectHierarchyEntities(alive);
    MarkSceneRenderDirty();
    MarkSceneDocumentDirty();
    scene_->Runtime().SynchronizeTransforms();
    return true;
}

bool EditorSceneContext::DeleteAssetBrowserItem(kb::assets::AssetId id) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata != nullptr && EditorSceneMaterialAssetActions::IsMaterialAsset(*metadata)) {
        const std::vector<std::string> references = EditorMaterialReferenceFinder::FindSceneReferences(*scene_, id);
        if (!references.empty()) {
            console_.Warning(
                "Materials",
                "Material delete blocked; " + std::to_string(references.size()) + " scene reference(s) found. First: " + references.front());
            return false;
        }
    }

    const bool deleted = EditorSceneAssetBrowserCommands::DeleteAsset(*scene_, assetBrowser_, id);
    if (deleted) {
        console_.Info("Assets", "Asset deleted.");
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset delete failed."));
    }
    return deleted;
}

bool EditorSceneContext::DeleteAssetBrowserFolder(const std::filesystem::path& virtualFolder) {
    const bool deleted = EditorSceneAssetBrowserCommands::DeleteFolder(*scene_, assetBrowser_, virtualFolder);
    if (deleted) {
        console_.Info("Assets", "Folder deleted: " + virtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Folder delete failed."));
    }
    return deleted;
}

bool EditorSceneContext::MoveAssetToFolder(kb::assets::AssetId id, const std::filesystem::path& destinationVirtualFolder) {
    const bool moved = EditorSceneAssetBrowserCommands::MoveAssetToFolder(*scene_, assetBrowser_, id, destinationVirtualFolder);
    if (moved) {
        console_.Info("Assets", "Asset moved to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset move failed."));
    }
    return moved;
}

bool EditorSceneContext::MoveAssetFolderToFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder) {
    const bool moved = EditorSceneAssetBrowserCommands::MoveFolderToFolder(*scene_, assetBrowser_, sourceVirtualFolder, destinationVirtualFolder);
    if (moved) {
        console_.Info("Assets", "Folder moved to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Folder move failed."));
    }
    return moved;
}

bool EditorSceneContext::CopyAssetToFolder(kb::assets::AssetId id, const std::filesystem::path& destinationVirtualFolder) {
    const bool copied = EditorSceneAssetBrowserCommands::CopyAssetToFolder(*scene_, assetBrowser_, id, destinationVirtualFolder);
    if (copied) {
        console_.Info("Assets", "Asset copied to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset copy failed."));
    }
    return copied;
}

bool EditorSceneContext::CopyAssetFolderToFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder) {
    const bool copied = EditorSceneAssetBrowserCommands::CopyFolderToFolder(*scene_, assetBrowser_, sourceVirtualFolder, destinationVirtualFolder);
    if (copied) {
        console_.Info("Assets", "Folder copied to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Folder copy failed."));
    }
    return copied;
}

bool EditorSceneContext::ImportAssetFiles(std::span<const std::filesystem::path> sourceFiles) {
    return ImportAssetFiles(sourceFiles, assetBrowser_.SelectedFolder());
}

bool EditorSceneContext::ImportAssetFiles(std::span<const std::filesystem::path> sourceFiles, const std::filesystem::path& destinationVirtualFolder) {
    return ImportAssetFiles(sourceFiles, destinationVirtualFolder, {});
}

bool EditorSceneContext::ImportAssetFiles(
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    const kb::assets::AssetImportResult report = EditorSceneAssetBrowserCommands::ImportFilesWithReport(*scene_, assetBrowser_, sourceFiles, destinationVirtualFolder, options);
    LogAssetImportReport(console_, report, destinationVirtualFolder);
    if (report.ImportedCount() == 0U) {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset import failed."));
    }
    return report.ImportedCount() > 0U;
}

bool EditorSceneContext::BeginAssetImport(
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    if (sourceFiles.empty()) return false;
    if (assetImportWorker_.joinable()) {
        console_.Warning("Assets", "An asset import is already running.");
        return false;
    }
    const std::vector<std::filesystem::path> files{ sourceFiles.begin(), sourceFiles.end() };
    const std::filesystem::path projectRoot = EditorProjectPaths::ProjectRoot();
    assetImportRunning_.store(true, std::memory_order_release);
    console_.Info("Assets", "Import started in background for " + std::to_string(files.size()) + " file(s).");
    try {
        assetImportWorker_ = std::thread{
            [this, files, destinationVirtualFolder, options, projectRoot]() mutable {
                kb::assets::AssetImportResult report{};
                try {
                    kb::scene::Scene importScene{ kb::scene::SceneMode::Runtime };
                    if (!importScene.Assets().MountProject(projectRoot)) {
                        throw std::runtime_error{ "The project asset mount could not be opened by the import worker." };
                    }
                    RegisterEditorSceneDocumentAssetLoaders(importScene);
                    static_cast<void>(importScene.Assets().Discover());
                    EditorAssetBrowserState workerBrowser;
                    report = EditorSceneAssetBrowserCommands::ImportFilesWithReport(
                        importScene, workerBrowser, files, destinationVirtualFolder, options);
                } catch (const std::exception& error) {
                    report.items.reserve(files.size());
                    for (const std::filesystem::path& file : files) {
                        report.items.push_back(kb::assets::AssetImportItemResult{
                            .sourcePath = file,
                            .category = kb::assets::AssetImportCategory::Model,
                            .status = kb::assets::AssetImportItemStatus::Failed,
                            .error = std::string{ "Background import failed: " } + error.what(),
                        });
                    }
                }
                {
                    std::lock_guard<std::mutex> lock{ assetImportMutex_ };
                    completedAssetImport_ = std::move(report);
                }
                assetImportRunning_.store(false, std::memory_order_release);
            }
        };
    } catch (const std::exception& error) {
        assetImportRunning_.store(false, std::memory_order_release);
        console_.Error("Assets", std::string{ "Asset import worker could not start: " } + error.what());
        return false;
    }
    return true;
}

std::size_t EditorSceneContext::PumpAssetImportResults() {
    std::optional<kb::assets::AssetImportResult> completed;
    {
        std::lock_guard<std::mutex> lock{ assetImportMutex_ };
        if (!completedAssetImport_.has_value()) return 0U;
        completed = std::move(completedAssetImport_);
        completedAssetImport_.reset();
    }
    if (assetImportWorker_.joinable()) assetImportWorker_.join();

    static_cast<void>(scene_->Assets().Discover());
    LogAssetImportReport(console_, *completed, assetBrowser_.SelectedFolder());
    for (const kb::assets::AssetImportItemResult& item : completed->items) {
        if (!item.Succeeded()) continue;
        if (const kb::assets::AssetMetadata* metadata =
                scene_->Assets().Manager().Registry().FindByPath(item.virtualPath);
            metadata != nullptr) {
            static_cast<void>(assetBrowser_.SelectAsset(metadata->id, scene_->Assets().Manager()));
            break;
        }
    }
    if (completed->ImportedCount() == 0U) {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset import failed."));
    }
    return completed->items.size();
}

bool EditorSceneContext::AssetImportInProgress() const noexcept {
    return assetImportRunning_.load(std::memory_order_acquire);
}

bool EditorSceneContext::ToggleHierarchyRowExpanded(std::size_t rowIndex) {
    const std::vector<EditorHierarchyRow>& rows = HierarchyRows();
    if (rowIndex >= rows.size() || !rows[rowIndex].hasChildren) {
        return false;
    }

    hierarchyExpansion_.SetExpanded(rows[rowIndex].entity, !rows[rowIndex].expanded);
    InvalidateHierarchyRows();
    return true;
}

bool EditorSceneContext::ToggleEntityVisibility(kb::scene::SceneEntity entity) {
    if (!ExecuteSceneCommand("Toggle Visibility", [this, entity]() {
            return EditorSceneHierarchyActions::ToggleVisibility(*scene_, entity);
        })) {
        console_.Warning("Hierarchy", "Visibility toggle ignored for invalid entity.");
        return false;
    }
    SelectEntity(entity);
    return true;
}

kb::scene::SceneEntity EditorSceneContext::CreateHierarchyObject() {
    kb::scene::SceneEntity created{};
    if (ExecuteSceneCommand("Create Entity", [this, &created]() {
            created = EditorSceneHierarchyActions::CreateObject(*scene_);
            if (!created.IsValid()) {
                return false;
            }
            SelectEntity(created);
            return true;
        })) {
        console_.Info("Hierarchy", "Entity created.");
    }
    return created;
}

kb::scene::SceneEntity EditorSceneContext::CreateLightObject(kb::scene::LightKind kind) {
    const char* name = "Point Light";
    const char* label = "Create Point Light";
    switch (kind) {
    case kb::scene::LightKind::Directional:
        name = "Directional Light";
        label = "Create Directional Light";
        break;
    case kb::scene::LightKind::Spot:
        name = "Spot Light";
        label = "Create Spot Light";
        break;
    case kb::scene::LightKind::Point:
    default:
        break;
    }

    kb::scene::SceneEntity created{};
    if (ExecuteSceneCommand(label, [this, &created, kind, name]() {
            kb::scene::SceneObjectDesc desc{};
            desc.name = name;
            created = scene_->Entities().CreateEntity(std::move(desc));
            if (!created.IsValid()) {
                return false;
            }
            kb::scene::LightComponent light{};
            light.kind = kind;
            scene_->Components().Lights().Set(created, light);
            SelectEntity(created);
            return true;
        })) {
        console_.Info("Hierarchy", std::string{ name } + " created.");
    }
    return created;
}

bool EditorSceneContext::ReparentEntity(kb::scene::SceneEntity child, kb::scene::SceneEntity parent) {
    if (!child.IsValid() || !scene_->Entities().IsAlive(child)) {
        console_.Warning("Hierarchy", "Entity reparent ignored.");
        return false;
    }

    const bool moved = ExecuteSceneCommand("Reparent Entity", [this, child, parent]() {
        return EditorSceneHierarchyActions::Reparent(*scene_, child, parent);
    });
    if (moved) {
        SelectEntity(child);
        console_.Info("Hierarchy", "Entity reparented.");
    } else {
        console_.Warning("Hierarchy", "Entity reparent ignored.");
    }
    return moved;
}

bool EditorSceneContext::ReparentEntities(std::span<const kb::scene::SceneEntity> children, kb::scene::SceneEntity parent) {
    const std::vector<kb::scene::SceneEntity> moving = TopLevelSelectedEntities(*scene_, children);
    if (moving.empty()) {
        console_.Warning("Hierarchy", "Hierarchy reparent did not move any entity.");
        return false;
    }

    const std::span<const kb::scene::SceneEntity> movingSpan{ moving.data(), moving.size() };
    if (parent.IsValid() && (ContainsEntity(movingSpan, parent) || HasSelectedAncestor(*scene_, parent, movingSpan))) {
        console_.Warning("Hierarchy", "Cannot reparent an entity below itself or a selected descendant.");
        return false;
    }

    const bool moved = ExecuteSceneCommand("Reparent Entities", [this, moving, parent]() {
        bool anyMoved = false;
        for (const kb::scene::SceneEntity child : moving) {
            if (child == parent) {
                continue;
            }
            anyMoved = EditorSceneHierarchyActions::Reparent(*scene_, child, parent) || anyMoved;
        }
        return anyMoved;
    });
    if (moved) {
        const std::vector<EditorHierarchyRow>& rows = HierarchyRows();
        hierarchySelection_.Clear();
        bool first = true;
        for (const kb::scene::SceneEntity entity : children) {
            if (!scene_->Entities().IsAlive(entity)) {
                continue;
            }
            for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
                if (rows[rowIndex].entity != entity) {
                    continue;
                }
                static_cast<void>(hierarchySelection_.SelectRow(rows, rowIndex, !first, false));
                first = false;
                break;
            }
        }
        if (hierarchySelection_.SelectedEntities().empty() && !moving.empty()) {
            SelectEntity(moving.front());
        }
        assetBrowser_.ClearSelection();
        console_.Info("Hierarchy", "Hierarchy selection reparented.");
    } else {
        console_.Warning("Hierarchy", "Hierarchy reparent did not move any entity.");
    }
    return moved;
}

bool EditorSceneContext::CreatePrefabAsset(kb::scene::SceneEntity entity, const std::filesystem::path& path) {
    const bool created = EditorScenePrefabActions::CreateAsset(*scene_, entity, path);
    if (created) {
        InvalidateHierarchyRows();
        static_cast<void>(scene_->Assets().Discover());
        if (const std::optional<std::filesystem::path> virtualPath = scene_->Assets().Manager().Mounts().ToVirtual(path)) {
            if (const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().FindByPath(*virtualPath); metadata != nullptr) {
                static_cast<void>(assetBrowser_.SelectAsset(metadata->id, scene_->Assets().Manager()));
            }
        }
        console_.Info("Prefabs", "Prefab asset created: " + path.generic_string());
    } else {
        console_.Error("Prefabs", AssetErrorOr(scene_->Assets().Manager(), "Prefab asset creation failed."));
    }
    return created;
}

EditorInputActionAuthoring EditorSceneContext::InputActionAuthoring() noexcept {
    return EditorInputActionAuthoring{ *scene_, assetBrowser_, console_ };
}

EditorInputMappingContextAuthoring EditorSceneContext::InputMappingContextAuthoring() noexcept {
    return EditorInputMappingContextAuthoring{ *scene_, assetBrowser_, console_ };
}

EditorAudioMixerAuthoring EditorSceneContext::AudioMixerAuthoring() noexcept {
    return EditorAudioMixerAuthoring{ *scene_, assetBrowser_, console_ };
}

EditorSceneAudioSettingsService EditorSceneContext::SceneAudioSettings() noexcept {
    return EditorSceneAudioSettingsService{ *scene_, [this](std::string label, EditorSceneAudioSettingsService::Mutation mutation) {
        return ExecuteSceneCommand(std::move(label), std::move(mutation));
    } };
}

bool EditorSceneContext::CreateInputActionAsset(const std::filesystem::path& virtualFolder) {
    return InputActionAuthoring().Create(virtualFolder);
}

bool EditorSceneContext::CreateInputAxisAsset(const std::filesystem::path& virtualFolder) {
    return InputActionAuthoring().CreateAxis(virtualFolder);
}

bool EditorSceneContext::CreateInputMappingContextAsset(const std::filesystem::path& virtualFolder) {
    return InputMappingContextAuthoring().Create(virtualFolder);
}

bool EditorSceneContext::CreateAudioMixerAsset(const std::filesystem::path& virtualFolder) {
    return AudioMixerAuthoring().Create(virtualFolder);
}

bool EditorSceneContext::CreateParticleEffectAsset(const std::filesystem::path& virtualFolder) {
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    if (virtualFolder.empty()) {
        console_.Error("Particles", "Could not resolve a destination folder for the new particle effect.");
        return false;
    }
    const std::optional<std::filesystem::path> probe = manager.Mounts().Resolve(virtualFolder / "probe");
    if (!probe.has_value()) {
        console_.Error("Particles", "Could not resolve a physical folder for the new particle effect.");
        return false;
    }
    const std::filesystem::path folder = probe->parent_path();
    std::filesystem::path path = folder / (std::string{"NewParticleEffect"} + kb::scene::kParticleEffectAssetExtension);
    std::uint32_t suffix = 1U;
    while (std::filesystem::exists(path)) {
        path = folder / (std::string{"NewParticleEffect"} + std::to_string(suffix) + kb::scene::kParticleEffectAssetExtension);
        ++suffix;
    }

    kb::scene::ParticleEffectAsset effect;
    effect.effectId = 1U;
    effect.displayName = path.stem().string();
    effect.recipeCategory = "General";
    effect.determinismSeed = 0x6B62564150415254ULL;
    effect.durationSeconds = 3.0F;
    effect.looping = true;
    kb::scene::ParticleEmitterAsset emitter;
    emitter.emitterId = 1U;
    emitter.authoringOrder = 0U;
    emitter.name = "Emitter 1";
    emitter.maxParticles = 512U;
    emitter.spawn.rateOverTime.keyframes = {{.time = 0.0F, .value = 28.0F}};
    emitter.spawn.lifetimeMin = 1.1F;
    emitter.spawn.lifetimeMax = 1.8F;
    emitter.spawn.speedMin = 1.4F;
    emitter.spawn.speedMax = 3.2F;
    emitter.spawn.spreadDegrees = 22.0F;
    emitter.output.material = {.assetId = 0U, .virtualPath = "/21kbParticle/Materials/DefaultParticle.kbmat"};
    emitter.output.blend = kb::scene::ParticleBlendMode::Add;
    emitter.output.softParticles = true;
    emitter.output.antiAliasing = true;
    emitter.modules.push_back({
        .moduleId = 1U,
        .authoringOrder = 0U,
        .type = kb::scene::ParticleModuleType::ColorOverLife,
        .payload = kb::scene::ParticleColorOverLifeModule{.gradient = {.stops = {
            {.time = 0.0F, .color = {1.0F, 0.78F, 0.32F, 1.0F}},
            {.time = 0.45F, .color = {1.0F, 0.38F, 0.08F, 0.9F}},
            {.time = 1.0F, .color = {0.28F, 0.05F, 0.01F, 0.0F}},
        }}},
    });
    emitter.modules.push_back({
        .moduleId = 2U,
        .authoringOrder = 1U,
        .type = kb::scene::ParticleModuleType::SizeOverLife,
        .payload = kb::scene::ParticleSizeOverLifeModule{.curve = {.keyframes = {
            {.time = 0.0F, .value = 1.0F},
            {.time = 1.0F, .value = 0.18F},
        }}},
    });
    effect.emitters.push_back(std::move(emitter));

    const kb::particle_editor::ParticleEditorResult saved = particleEditorGateway_.Save(path, effect);
    if (!saved.Succeeded()) {
        console_.Error("Particles", "Particle effect could not be created: " + saved.message);
        return false;
    }
    static_cast<void>(scene_->Assets().Discover());
    const std::optional<std::filesystem::path> virtualPath = manager.Mounts().ToVirtual(path);
    const kb::assets::AssetMetadata* metadata = virtualPath.has_value()
        ? manager.Registry().FindByPath(*virtualPath) : nullptr;
    if (metadata != nullptr && metadata->type == kb::scene::kParticleEffectAssetType
        && assetBrowser_.SelectAsset(metadata->id, manager) && OpenParticleEditorAsset(metadata->id)) {
        console_.Info("Particles", "Particle effect created: " + path.generic_string());
        return true;
    }

    std::error_code removeError;
    static_cast<void>(std::filesystem::remove(path, removeError));
    static_cast<void>(scene_->Assets().Discover());
    console_.Error("Particles", "Particle effect creation was rolled back because the asset could not be opened.");
    return false;
}

bool EditorSceneContext::DuplicateAsset(kb::assets::AssetId assetId) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr) {
        console_.Error("Assets", "Duplicate requires an existing asset.");
        return false;
    }
    return CopyAssetToFolder(assetId, metadata->virtualPath.parent_path());
}

bool EditorSceneContext::CreateLuaScriptAsset(const std::filesystem::path& virtualFolder) {
    EditorScriptAssetGateway gateway{ *scene_, assetBrowser_ };
    const std::optional<std::filesystem::path> path = gateway.CreateLuaScript(virtualFolder);
    if (!path.has_value()) {
        console_.Error("Scripts", "Lua script could not be created in folder: " + virtualFolder.generic_string());
        return false;
    }
    console_.Info("Scripts", "Lua script created: " + path->generic_string());
    return true;
}

bool EditorSceneContext::OpenLuaScript(kb::assets::AssetId id) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr) {
        console_.Error("Scripts", "Lua script metadata was not found.");
        return false;
    }
    std::filesystem::path path = metadata->physicalPath;
    if (const std::optional<std::filesystem::path> mounted = scene_->Assets().Manager().Mounts().Resolve(metadata->virtualPath)) {
        path = *mounted;
    }
    scriptEditor_.Open(path, id, metadata->virtualPath.filename().string());
    console_.Info("Scripts", "Opened script: " + metadata->virtualPath.generic_string());
    return true;
}

std::optional<kb::input::InputActionAsset> EditorSceneContext::ReadInputActionAsset(kb::assets::AssetId id) const {
    return EditorInputAssetGateway::ReadAction(*scene_, id);
}

bool EditorSceneContext::SetInputActionName(kb::assets::AssetId id, std::string name) {
    return InputActionAuthoring().SetName(id, std::move(name));
}

bool EditorSceneContext::CycleInputActionValueType(kb::assets::AssetId id) {
    return InputActionAuthoring().CycleValueType(id);
}

bool EditorSceneContext::SetInputActionValueType(kb::assets::AssetId id, kb::input::InputActionValueType valueType) {
    return InputActionAuthoring().SetValueType(id, valueType);
}

bool EditorSceneContext::ToggleInputActionConsume(kb::assets::AssetId id) {
    return InputActionAuthoring().ToggleConsume(id);
}

std::optional<kb::audio::AudioMixerAsset> EditorSceneContext::ReadAudioMixerAsset(kb::assets::AssetId id) const {
    return EditorAudioMixerAssetGateway::Read(*scene_, id);
}

bool EditorSceneContext::AddAudioMixerBus(kb::assets::AssetId id, std::string_view name) {
    return AudioMixerAuthoring().AddBus(id, name);
}

bool EditorSceneContext::RemoveAudioMixerBus(kb::assets::AssetId id, std::string_view name) {
    return AudioMixerAuthoring().RemoveBus(id, name);
}

bool EditorSceneContext::RenameAudioMixerBus(
    kb::assets::AssetId id,
    std::string_view name,
    std::string_view replacement) {
    return AudioMixerAuthoring().RenameBus(id, name, replacement);
}

bool EditorSceneContext::SetAudioMixerBusParent(
    kb::assets::AssetId id,
    std::string_view name,
    std::string_view parent) {
    return AudioMixerAuthoring().SetBusParent(id, name, parent);
}

bool EditorSceneContext::SetAudioMixerBusVolume(
    kb::assets::AssetId id,
    std::string_view name,
    float volume) {
    return AudioMixerAuthoring().SetBusVolume(id, name, volume);
}

bool EditorSceneContext::SetAudioMixerBusMute(
    kb::assets::AssetId id,
    std::string_view name,
    bool mute) {
    return AudioMixerAuthoring().SetBusMute(id, name, mute);
}

bool EditorSceneContext::AddAudioMixerSnapshot(kb::assets::AssetId id, std::string_view name) {
    return AudioMixerAuthoring().AddSnapshot(id, name);
}

bool EditorSceneContext::RemoveAudioMixerSnapshot(kb::assets::AssetId id, std::string_view name) {
    return AudioMixerAuthoring().RemoveSnapshot(id, name);
}

bool EditorSceneContext::RenameAudioMixerSnapshot(
    kb::assets::AssetId id,
    std::string_view name,
    std::string_view replacement) {
    return AudioMixerAuthoring().RenameSnapshot(id, name, replacement);
}

bool EditorSceneContext::AddAudioMixerSnapshotOverride(
    kb::assets::AssetId id,
    std::string_view snapshot,
    std::string_view bus,
    float volume) {
    return AudioMixerAuthoring().AddSnapshotOverride(id, snapshot, bus, volume);
}

bool EditorSceneContext::RemoveAudioMixerSnapshotOverride(
    kb::assets::AssetId id,
    std::string_view snapshot,
    std::string_view bus) {
    return AudioMixerAuthoring().RemoveSnapshotOverride(id, snapshot, bus);
}

bool EditorSceneContext::SetAudioMixerSnapshotOverrideVolume(
    kb::assets::AssetId id,
    std::string_view snapshot,
    std::string_view bus,
    float volume) {
    return AudioMixerAuthoring().SetSnapshotOverrideVolume(id, snapshot, bus, volume);
}

bool EditorSceneContext::SetSceneAudioMixer(kb::assets::AssetId id) {
    return SceneAudioSettings().SetSceneAudioMixer(id);
}

bool EditorSceneContext::SetSceneAudioSnapshot(std::string_view snapshot) {
    return SceneAudioSettings().SetSceneAudioSnapshot(snapshot);
}

bool EditorSceneContext::SetSceneAudioOcclusion(
    const kb::scene::AudioOcclusionSettings& settings) {
    return SceneAudioSettings().SetSceneAudioOcclusion(settings);
}

const std::string& EditorSceneContext::GraphShaderCacheRoot() const noexcept {
    return graphShaderCacheRoot_;
}

bool EditorSceneContext::ToggleProjectInputEnabled() {
    projectConfig_.inputEnabled = !projectConfig_.inputEnabled;
    return SaveProjectConfiguration();
}

std::vector<std::string> EditorSceneContext::ProjectInputMappingContextOptions() const {
    // Empty first entry is the "(None)" choice so the project input can be cleared.
    std::vector<std::string> options{ std::string{} };
    for (const kb::assets::AssetMetadata& metadata : scene_->Assets().Manager().Registry().All()) {
        if (metadata.type == "InputMappingContext") {
            options.push_back(kb::assets::NormalizeAssetPath(metadata.virtualPath));
        }
    }
    std::sort(options.begin() + 1, options.end());
    return options;
}

bool EditorSceneContext::SetProjectInputMappingContext(std::string virtualPath) {
    if (projectConfig_.inputMappingContext == virtualPath) {
        return false;
    }
    projectConfig_.inputMappingContext = std::move(virtualPath);
    return SaveProjectConfiguration();
}

bool EditorSceneContext::SetProjectSceneLightingPath(kb::project::ProjectSceneLightingPath path) {
    if (projectConfig_.lightingPath == path) {
        return false;
    }
    projectConfig_.lightingPath = path;
    const bool saved = SaveProjectConfiguration();
    if (saved) {
        sceneGraphCookPending_ = true;
        RequestOpenMaterialSceneGraphCook();
        MarkSceneRenderDirty();
    }
    return saved;
}

bool EditorSceneContext::CycleEntityVisibilityMode(kb::scene::SceneEntity entity) {
    return ExecuteSceneCommand("Edit Visibility Mode", [this, entity]() {
        kb::scene::VisibilityComponent* visibility = scene_->Components().Visibility().TryGet(entity);
        if (visibility == nullptr) return false;
        switch (visibility->mode) {
        case kb::scene::VisibilityMode::Inherit: visibility->mode = kb::scene::VisibilityMode::Visible; break;
        case kb::scene::VisibilityMode::Visible: visibility->mode = kb::scene::VisibilityMode::Hidden; break;
        case kb::scene::VisibilityMode::Hidden: visibility->mode = kb::scene::VisibilityMode::Inherit; break;
        }
        visibility->visible = visibility->mode != kb::scene::VisibilityMode::Hidden;
        scene_->Components().Visibility().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::SetEntityVisibilityMask(kb::scene::SceneEntity entity, std::uint32_t mask) {
    return ExecuteSceneCommand("Edit Visibility Mask", [this, entity, mask]() {
        kb::scene::VisibilityComponent* visibility = scene_->Components().Visibility().TryGet(entity);
        if (visibility == nullptr || visibility->mask == mask) return false;
        visibility->mask = mask;
        scene_->Components().Visibility().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::SetRegionPortalCells(
    kb::scene::SceneEntity entity,
    kb::scene::SceneEntity sourceCell,
    kb::scene::SceneEntity targetCell) {
    const bool sourceValid = sourceCell.IsValid();
    const bool targetValid = targetCell.IsValid();
    if (!scene_->Entities().IsAlive(entity) || !scene_->Components().RegionPortals().Has(entity) ||
        (sourceValid && (!scene_->Entities().IsAlive(sourceCell) || !scene_->Components().VisibilityCells().Has(sourceCell))) ||
        (targetValid && (!scene_->Entities().IsAlive(targetCell) || !scene_->Components().VisibilityCells().Has(targetCell))) ||
        entity == sourceCell || entity == targetCell || (sourceValid && targetValid && sourceCell == targetCell)) {
        return false;
    }
    return ExecuteSceneCommand("Set Region Portal Cells", [this, entity, sourceCell, targetCell]() {
        kb::scene::SceneRegionPortalComponent* portal = scene_->Components().RegionPortals().TryGet(entity);
        if (portal == nullptr) return false;
        portal->sourceCell = sourceCell;
        portal->targetCell = targetCell;
        scene_->Components().RegionPortals().MarkModified(entity);
        return true;
    });
}

std::vector<std::string> EditorSceneContext::ProjectPhysicsLayersAssetOptions() const {
    std::vector<std::string> options{ std::string{} };
    for (const kb::assets::AssetMetadata& metadata : scene_->Assets().Manager().Registry().All()) {
        if (metadata.type == "PhysicsLayers") {
            options.push_back(kb::assets::NormalizeAssetPath(metadata.virtualPath));
        }
    }
    std::sort(options.begin() + 1, options.end());
    return options;
}

bool EditorSceneContext::SetProjectPhysicsLayersAsset(std::string virtualPath) {
    if (projectConfig_.physicsLayersAsset == virtualPath) {
        return false;
    }
    projectConfig_.physicsLayersAsset = std::move(virtualPath);
    return SaveProjectConfiguration();
}

bool EditorSceneContext::CloseProjectSettingsDropdowns() noexcept {
    return projectSettings_.CloseDropdowns();
}

bool EditorSceneContext::IsProjectPluginEnabled(std::string_view pluginId) const noexcept {
    const auto iter = std::ranges::find_if(project_.plugins, [pluginId](const kb::project::ProjectPluginReference& plugin) {
        return plugin.name == pluginId;
    });
    return iter != project_.plugins.end() && iter->enabled;
}

std::string EditorSceneContext::ProjectPluginBinaryPath(std::string_view pluginId) const {
    const auto iter = std::ranges::find_if(project_.plugins, [pluginId](const kb::project::ProjectPluginReference& plugin) {
        return plugin.name == pluginId;
    });
    return iter == project_.plugins.end() ? std::string{} : iter->binaryPath;
}

bool EditorSceneContext::ToggleProjectPlugin(std::size_t catalogIndex) {
    const EditorPluginDescriptor* descriptor = EditorPluginCatalog::At(catalogIndex);
    if (descriptor == nullptr) {
        return false;
    }

    auto iter = std::ranges::find_if(project_.plugins, [descriptor](const kb::project::ProjectPluginReference& plugin) {
        return plugin.name == descriptor->id;
    });
    if (iter == project_.plugins.end()) {
        project_.plugins.push_back(kb::project::ProjectPluginReference{
            .name = std::string{ descriptor->id },
            .binaryPath = EditorPluginCatalog::PersistentBinaryPath(descriptor->id),
            .enabled = true,
        });
        console_.Info("Plugins", std::string{ descriptor->displayName } + " enabled. Reopen the scene or enter play mode after reload to apply.");
        const bool saved = SaveProjectDescriptor();
        if (saved) {
            plugins_.MarkPendingReload();
        }
        return saved;
    }

    iter->enabled = !iter->enabled;
    if (descriptor->id == "Editor.Terrain" && !iter->enabled) {
        EditorTerrainToolState& tool = EditorTerrainService::ToolState();
        tool.editingEnabled = false;
        tool.mode = EditorTerrainToolMode::Select;
        tool.strokeActive = false;
        tool.hoverVisible = false;
        tool.hoverEntityId = 0U;
        tool.brushMenuOpen = false;
        tool.brushShapeMenuOpen = false;
    }
    if (iter->binaryPath.empty()) {
        iter->binaryPath = EditorPluginCatalog::PersistentBinaryPath(descriptor->id);
    }
    console_.Info("Plugins", std::string{ descriptor->displayName } + (iter->enabled ? " enabled." : " disabled.") + " Project plugin changes apply when the scene/module host is rebuilt.");
    const bool saved = SaveProjectDescriptor();
    if (saved) {
        plugins_.MarkPendingReload();
    }
    return saved;
}

void EditorSceneContext::ActivateProjectInput() {
    if (!projectConfig_.inputEnabled || projectConfig_.inputMappingContext.empty()) {
        return;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().FindByPath(projectConfig_.inputMappingContext);
    if (metadata != nullptr && metadata->type == "InputMappingContext") {
        static_cast<void>(scene_->Input().AddMappingContext(metadata->id.value, 0));
    }
}

bool EditorSceneContext::ActivateProjectPhysicsLayers(kb::scene::Scene& scene) {
    if (projectConfig_.physicsLayersAsset.empty()) {
        return true;
    }
    if (kb::scene::PhysicsBackend::LoadAndConfigureLayers(scene, projectConfig_.physicsLayersAsset)) {
        return true;
    }
    std::string error = "Project physics layers could not be loaded and applied: " + projectConfig_.physicsLayersAsset;
    const std::string assetError = scene.Assets().Manager().LastError();
    if (!assetError.empty()) {
        error += " (" + assetError + ")";
    }
    console_.Error("Physics", error);
    return false;
}

const kb::project::ProjectSettings& EditorSceneContext::ProjectConfiguration() const noexcept {
    return projectConfig_;
}

// Stored as the project-relative virtual path, matching how the default map is
// written, so the value stays meaningful if the project folder moves.
void EditorSceneContext::RememberLastOpenMap() {
    std::string virtualPath;
    if (!currentScenePath_.empty()) {
        const auto* metadata = scene_->Assets().Manager().Registry().FindByPath(
            scene_->Assets().Manager().Mounts().ToVirtual(currentScenePath_).value_or(std::filesystem::path{}));
        if (metadata != nullptr) {
            virtualPath = metadata->virtualPath.generic_string();
        } else if (const auto mapped = scene_->Assets().Manager().Mounts().ToVirtual(currentScenePath_)) {
            virtualPath = mapped->generic_string();
        }
    }
    if (projectConfig_.lastOpenMap == virtualPath) {
        return;
    }
    projectConfig_.lastOpenMap = std::move(virtualPath);
    static_cast<void>(SaveProjectConfiguration());
}

// The one writer of the project's settings, and now the only place they are
// stored: the descriptor no longer keeps a copy to drift out of step.
bool EditorSceneContext::SaveProjectConfiguration() {
    std::string error;
    const std::filesystem::path settingsFile =
        kb::project::ProjectSettingsStore::FilePath(EditorProjectPaths::ProjectRoot());
    if (!kb::project::ProjectSettingsStore::Save(settingsFile, projectConfig_, error)) {
        console_.Error("Project", error.empty() ? "Project settings could not be saved." : error);
        return false;
    }
    return SaveProjectDescriptor();
}

bool EditorSceneContext::SaveProjectDescriptor() {
    if (projectFile_.empty()) {
        return false;
    }
    const bool saved = kb::project::ProjectDescriptorWriter::Write(projectFile_, project_);
    if (saved) {
        console_.Info("Project", "Project settings saved.");
    } else {
        console_.Error("Project", "Project settings could not be saved.");
    }
    return saved;
}

std::optional<kb::input::InputMappingContextAsset> EditorSceneContext::ReadInputMappingContextAsset(kb::assets::AssetId id) const {
    return EditorInputAssetGateway::ReadContext(*scene_, id);
}

bool EditorSceneContext::AddInputMapping(kb::assets::AssetId id) {
    return InputMappingContextAuthoring().AddMapping(id);
}

bool EditorSceneContext::RemoveInputMapping(kb::assets::AssetId id, std::size_t index) {
    return InputMappingContextAuthoring().RemoveMapping(id, index);
}

bool EditorSceneContext::SetInputMappingKey(kb::assets::AssetId id, std::size_t index, kb::input::InputKey key) {
    return InputMappingContextAuthoring().SetMappingKey(id, index, key);
}

bool EditorSceneContext::SetInputMappingScale(kb::assets::AssetId id, std::size_t index, float scale) {
    return InputMappingContextAuthoring().SetMappingScale(id, index, scale);
}

bool EditorSceneContext::CycleInputMappingAction(kb::assets::AssetId id, std::size_t index) {
    return InputMappingContextAuthoring().CycleMappingAction(id, index);
}

bool EditorSceneContext::CycleInputMappingTrigger(kb::assets::AssetId id, std::size_t index) {
    return InputMappingContextAuthoring().CycleMappingTrigger(id, index);
}

bool EditorSceneContext::InstantiatePrefabAsset(const std::filesystem::path& path, kb::scene::SceneEntity parent) {
    return InstantiatePrefabAsset(path, {}, parent);
}

bool EditorSceneContext::InstantiatePrefabAsset(const std::filesystem::path& path, const std::filesystem::path& virtualPath, kb::scene::SceneEntity parent) {
    if (pendingSceneTransactionLabel_.has_value()) {
        console_.Warning("Edit", "Scene command ignored while another scene transaction is active.");
        return false;
    }

    const std::filesystem::path& displayPath = virtualPath.empty() ? path : virtualPath;
    const std::optional<kb::scene::SceneEntity> root = EditorScenePrefabActions::InstantiateAsset(*scene_, path, virtualPath, parent);
    if (!root.has_value() || !scene_->Entities().IsAlive(*root)) {
        console_.Error("Prefabs", "Prefab instantiation failed: " + displayPath.generic_string());
        return false;
    }

    const std::array<kb::scene::SceneEntity, 1U> created{ *root };
    if (!AdoptCreatedHierarchyEntities("Instantiate Prefab", created)) {
        scene_->Entities().Destroy(*root);
        console_.Error("Prefabs", "Prefab instantiation failed: " + displayPath.generic_string());
        return false;
    }

    console_.Info("Prefabs", "Prefab instantiated: " + displayPath.generic_string());
    return true;
}

bool EditorSceneContext::InstantiatePrefabAssetAt(
    const std::filesystem::path& path,
    const std::filesystem::path& virtualPath,
    kb::scene::Vec3 position) {
    return CreatePrefabAssetEntity(path, virtualPath, position, true).IsValid();
}

kb::scene::SceneEntity EditorSceneContext::CreatePrefabAssetEntity(
    const std::filesystem::path& path,
    const std::filesystem::path& virtualPath,
    kb::scene::Vec3 position,
    bool logCreation) {
    if (pendingSceneTransactionLabel_.has_value()) {
        console_.Warning("Edit", "Scene command ignored while another scene transaction is active.");
        return {};
    }

    const std::optional<kb::scene::SceneEntity> root = EditorScenePrefabActions::InstantiateAsset(*scene_, path, virtualPath, {});
    if (!root.has_value() || !scene_->Entities().IsAlive(*root)) {
        console_.Error("Prefabs", "Prefab instantiation failed: " + (virtualPath.empty() ? path.generic_string() : virtualPath.generic_string()));
        return {};
    }

    kb::scene::TransformComponent transform = scene_->Transforms().Get(*root);
    transform.localPosition = position;
    scene_->Transforms().Set(*root, transform);
    scene_->Runtime().SynchronizeTransforms();
    if (!logCreation) {
        SelectEntity(*root);
        MarkSceneRenderDirty();
        return *root;
    }

    const std::array<kb::scene::SceneEntity, 1U> created{ *root };
    if (!AdoptCreatedHierarchyEntities("Instantiate Prefab", created)) {
        scene_->Entities().Destroy(*root);
        console_.Error("Prefabs", "Prefab instantiation failed: " + (virtualPath.empty() ? path.generic_string() : virtualPath.generic_string()));
        return {};
    }

    console_.Info("Prefabs", "Prefab instantiated: " + (virtualPath.empty() ? path.generic_string() : virtualPath.generic_string()));
    return *root;
}

kb::scene::SceneEntity EditorSceneContext::CreateMeshAssetEntity(kb::assets::AssetId assetId) {
    return CreateMeshAssetEntity(assetId, {}, true);
}

kb::scene::SceneEntity EditorSceneContext::CreateParticleEffectEntity(kb::assets::AssetId assetId) {
    return CreateParticleEffectEntity(assetId, {}, true);
}

kb::scene::SceneEntity EditorSceneContext::CreateParticleEffectEntity(
    kb::assets::AssetId assetId,
    kb::scene::Vec3 position,
    bool logCreation) {
    if (!assetId.IsValid()) {
        console_.Warning("Particles", "Particle Effect entity creation ignored for invalid asset.");
        return {};
    }
    if (!IsProjectPluginEnabled("Rendering.21kbParticle")) {
        console_.Warning("Particles", "Enable 21kb Particle System in Edit > Plugins before placing an effect.");
        return {};
    }

    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr || metadata->type != kb::scene::kParticleEffectAssetType) {
        console_.Warning("Particles", "Only Particle Effect assets can be placed on the scene.");
        return {};
    }

    const auto createEntity = [this, assetId, metadata, position]() {
        const kb::scene::SceneEntity entity = scene_->Entities().CreateEntity(kb::scene::SceneObjectDesc{.name = metadata->name});
        if (!entity.IsValid()) {
            return kb::scene::SceneEntity{};
        }
        kb::scene::TransformComponent authoredTransform = scene_->Transforms().Get(entity);
        authoredTransform.localPosition = position;
        scene_->Transforms().Set(entity, authoredTransform);
        scene_->Runtime().SynchronizeTransforms();
        scene_->Components().ParticleEffects().Set(entity, kb::scene::ParticleEffectComponent{
            .effectAssetId = assetId.value,
        });
        if (kb::particles::ParticlePlayback::HasBackend(*scene_)) {
            const auto created = kb::particles::ParticlePlayback::Create(*scene_, assetId.value, entity);
            if (created.Succeeded()) {
                if (const kb::scene::TransformComponent* transform = scene_->Transforms().TryGet(entity)) {
                    static_cast<void>(kb::particles::ParticlePlayback::ConfigureComponent(
                        *scene_,
                        created.instanceId,
                        1.0F,
                        0U,
                        true,
                        transform->WorldPayload()));
                }
                static_cast<void>(kb::particles::ParticlePlayback::Play(*scene_, created.instanceId));
            }
        }
        return entity;
    };

    kb::scene::SceneEntity entity{};
    if (!logCreation) {
        entity = createEntity();
        if (!entity.IsValid()) {
            console_.Error("Particles", "Particle Effect entity could not be created: " + metadata->name);
            return {};
        }
        SelectEntity(entity);
        MarkSceneRenderDirty();
        return entity;
    }

    const bool created = ExecuteSceneCommand("Create Particle Effect Entity", [this, &entity, &createEntity]() {
        entity = createEntity();
        if (!entity.IsValid()) {
            return false;
        }
        SelectEntity(entity);
        return true;
    });
    if (!created || !entity.IsValid()) {
        console_.Error("Particles", "Particle Effect entity could not be created: " + metadata->name);
        return {};
    }

    console_.Info("Particles", "Particle Effect entity created: " + metadata->name);
    return entity;
}

kb::scene::SceneEntity EditorSceneContext::CreateMeshAssetEntity(kb::assets::AssetId assetId, kb::scene::Vec3 position, bool logCreation) {
    if (!assetId.IsValid()) {
        console_.Warning("Assets", "Mesh entity creation ignored for invalid asset.");
        return {};
    }

    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr) {
        console_.Error("Assets", "Mesh asset metadata was not found.");
        return {};
    }
    const bool isSkeletalMesh = metadata->type == kb::scene::kSkeletalMeshAssetType;
    if (!EditorSceneMeshAssetActions::IsScenePlaceableAsset(*metadata)) {
        console_.Warning("Assets", "Only imported mesh or Skeletal Mesh assets can be placed on the scene.");
        return {};
    }

    kb::assets::AssetId skeletonAssetId{};
    std::uint64_t skeletonCompatibilitySignature = 0U;
    if (isSkeletalMesh) {
        kb::assets::AssetManager& manager = scene_->Assets().Manager();
        const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> loaded =
            manager.AcquireLoaded<kb::scene::SkeletalMeshAsset>(assetId);
        if (loaded.IsLoaded()) {
            skeletonAssetId = kb::assets::AssetId{ loaded->skeletonAssetId };
            skeletonCompatibilitySignature = loaded->skeletonCompatibilitySignature;
        } else {
            std::string bindingError;
            const std::optional<kb::scene::SkeletalMeshAssetBinding> binding =
                kb::scene::SkeletalMeshAssetIO::LoadBinding(metadata->physicalPath, &bindingError);
            if (!binding.has_value()) {
                console_.Error("Assets", "Skeletal Mesh binding could not be read: " +
                    (bindingError.empty() ? metadata->name : bindingError));
                return {};
            }
            skeletonAssetId = kb::assets::AssetId{ binding->skeletonAssetId };
            skeletonCompatibilitySignature = binding->skeletonCompatibilitySignature;
        }
        if (!skeletonAssetId.IsValid() || skeletonCompatibilitySignature == 0U) {
            console_.Error("Assets", "Skeletal Mesh has no valid skeleton binding: " + metadata->name);
            return {};
        }
        const kb::assets::AssetMetadata* skeletonMetadata = manager.Registry().Find(skeletonAssetId);
        if (skeletonMetadata == nullptr || skeletonMetadata->type != kb::scene::kSkeletonAssetType) {
            console_.Error("Assets", "Skeletal Mesh references a missing Skeleton asset: " + metadata->name);
            return {};
        }
        if (!loaded.IsLoaded() && !manager.LoadAsync<kb::scene::SkeletalMeshAsset>(assetId)) {
            console_.Error("Assets", "Skeletal Mesh loading could not be started: " + metadata->name);
            return {};
        }
    }

    const auto createEntity = [this, assetId, skeletonAssetId, skeletonCompatibilitySignature, metadata, position, isSkeletalMesh]() {
        return isSkeletalMesh
            ? EditorSceneMeshAssetActions::CreateSkeletalMeshEntity(
                *scene_, assetId, skeletonAssetId, skeletonCompatibilitySignature, metadata->name, position)
            : EditorSceneMeshAssetActions::CreateMeshEntity(*scene_, assetId, metadata->name, position);
    };

    kb::scene::SceneEntity entity{};
    if (!logCreation) {
        entity = createEntity();
        if (!entity.IsValid()) {
            console_.Error("Assets", "Mesh entity could not be created: " + metadata->name);
            return {};
        }
        SelectEntity(entity);
        MarkSceneRenderDirty();
        return entity;
    }

    const bool created = ExecuteSceneCommand("Create Mesh Entity", [this, &entity, &createEntity]() {
        entity = createEntity();
        if (!entity.IsValid()) {
            return false;
        }
        SelectEntity(entity);
        return true;
    });
    if (!entity.IsValid()) {
        console_.Error("Assets", "Mesh entity could not be created: " + metadata->name);
        return {};
    }

    if (created && logCreation) {
        console_.Info("Assets", "Mesh entity created: " + metadata->name);
    }
    return entity;
}

bool EditorSceneContext::AddBehaviourAssetToEntity(kb::assets::AssetId assetId, kb::scene::SceneEntity entity) {
    if (!assetId.IsValid() || !entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
        console_.Warning("Scripts", "Behaviour asset assignment ignored for invalid target.");
        return false;
    }

    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr) {
        console_.Error("Scripts", "Behaviour asset metadata was not found.");
        return false;
    }

    const std::optional<kb::scene::BehaviourComponent> behaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*metadata);
    if (!behaviour.has_value()) {
        console_.Error("Scripts", "Behaviour component could not be created from asset: " + metadata->name);
        return false;
    }

    if (!ExecuteSceneCommand("Assign Behaviour", [this, entity, behaviour]() {
            if (!scene_->Entities().IsAlive(entity)) {
                return false;
            }
            scene_->Components().Behaviours().Set(entity, *behaviour);
            SelectEntity(entity);
            return true;
        })) {
        console_.Error("Scripts", "Behaviour asset assignment failed: " + metadata->name);
        return false;
    }
    console_.Info("Scripts", "Behaviour asset assigned: " + metadata->name);
    return true;
}

bool EditorSceneContext::SetMeshRendererMeshAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    if (!entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
        console_.Warning("Inspector", "Mesh assignment ignored for invalid entity.");
        return false;
    }
    if (!scene_->Components().MeshRenderers().Has(entity)) {
        console_.Warning("Inspector", "Selected entity does not have a Mesh Renderer component.");
        return false;
    }
    if (assetId.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
        if (metadata == nullptr || !EditorSceneMeshAssetActions::IsMeshAsset(*metadata)) {
            console_.Warning("Inspector", "Only mesh assets can be assigned to a Mesh Renderer.");
            return false;
        }
    }

    return ExecuteSceneCommand(assetId.IsValid() ? "Assign Mesh" : "Clear Mesh", [this, entity, assetId]() {
        if (!EditorSceneMeshAssetActions::AssignMesh(*scene_, entity, assetId)) {
            return false;
        }
        // Keep an existing Collider fitted to the new geometry (the
        // collision shape follows the mesh when you swap it). Skipped when the
        // mesh is being cleared.
        if (assetId.IsValid() && scene_->Components().Colliders().Has(entity)) {
            std::string reason;
            if (ApplyColliderFitToMesh(entity, reason)) {
                console_.Info("Physics", "Collider refit to new mesh: " + reason + ".");
            } else {
                console_.Warning("Physics", "Collider not refit to new mesh: " + reason + ".");
            }
        }
        return true;
    });
}

bool EditorSceneContext::HasEntityScript(kb::scene::SceneEntity entity) const {
    return entity.IsValid() && scene_->Components().Behaviours().Has(entity);
}

std::string EditorSceneContext::EntityScriptName(kb::scene::SceneEntity entity) const {
    const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
    if (behaviour == nullptr) {
        return {};
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(kb::assets::AssetId{ behaviour->behaviourAssetId });
    if (metadata == nullptr) {
        return "(missing script)";
    }
    return metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
}

bool EditorSceneContext::EntityScriptEnabled(kb::scene::SceneEntity entity) const {
    const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
    return behaviour != nullptr && behaviour->enabled;
}

std::size_t EditorSceneContext::EntityScriptExposedVariableCount(kb::scene::SceneEntity entity) const {
    const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
    if (behaviour == nullptr) {
        return 0U;
    }
    const kb::assets::AssetHandle<kb::script::LuaScriptAsset> asset =
        scene_->Assets().Manager().Load<kb::script::LuaScriptAsset>(kb::assets::AssetId{ behaviour->behaviourAssetId });
    return asset.IsLoaded() ? asset.Get()->exposedVariables.size() : 0U;
}

std::vector<EditorSceneContext::EntityScriptVariable> EditorSceneContext::EntityScriptExposedVariables(kb::scene::SceneEntity entity) const {
    std::vector<EntityScriptVariable> result;
    const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
    if (behaviour == nullptr) {
        return result;
    }
    const kb::assets::AssetHandle<kb::script::LuaScriptAsset> asset =
        scene_->Assets().Manager().Load<kb::script::LuaScriptAsset>(kb::assets::AssetId{ behaviour->behaviourAssetId });
    if (!asset.IsLoaded()) {
        return result;
    }
    const kb::script::LuaScriptAsset& script = *asset.Get();
    const std::span<const kb::scene::BehaviourVariableOverride> overrides = scene_->Entities().BehaviourVariableOverrides(entity);
    result.reserve(script.exposedVariables.size());
    for (std::size_t index = 0U; index < script.exposedVariables.size(); ++index) {
        const kb::script::ScriptApiPin& pin = script.exposedVariables[index];
        // Effective value = the override delta if one is stored, else the
        // script's declared @expose default.
        kb::script::ScriptValue value = index < script.exposedVariableDefaults.size()
            ? script.exposedVariableDefaults[index]
            : kb::script::ScriptValue{};
        bool overridden = false;
        for (const kb::scene::BehaviourVariableOverride& entry : overrides) {
            if (entry.name == pin.name) {
                value = entry.value;
                overridden = true;
                break;
            }
        }
        result.push_back(EntityScriptVariable{
            .name = pin.name,
            .type = pin.type,
            .value = std::move(value),
            .overridden = overridden,
        });
    }
    return result;
}

bool EditorSceneContext::SetEntityScriptVariable(kb::scene::SceneEntity entity, std::string name, kb::script::ScriptValue value) {
    if (!entity.IsValid() || name.empty() || scene_->Components().Behaviours().TryGet(entity) == nullptr) {
        return false;
    }
    // Resolve the script's declared default so an edit back to it drops the
    // override instead of storing a redundant delta (store-only-non-default).
    kb::script::ScriptValue defaultValue;
    bool hasDefault = false;
    if (const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity); behaviour != nullptr) {
        const kb::assets::AssetHandle<kb::script::LuaScriptAsset> asset =
            scene_->Assets().Manager().Load<kb::script::LuaScriptAsset>(kb::assets::AssetId{ behaviour->behaviourAssetId });
        if (asset.IsLoaded()) {
            const kb::script::LuaScriptAsset& script = *asset.Get();
            for (std::size_t index = 0U; index < script.exposedVariables.size(); ++index) {
                if (script.exposedVariables[index].name == name && index < script.exposedVariableDefaults.size()) {
                    defaultValue = script.exposedVariableDefaults[index];
                    hasDefault = true;
                    break;
                }
            }
        }
    }
    return ExecuteSceneCommand("Set Script Variable", [this, entity, name, value, defaultValue, hasDefault]() {
        if (hasDefault && value == defaultValue) {
            static_cast<void>(scene_->Entities().RemoveBehaviourVariableOverride(entity, name));
        } else {
            scene_->Entities().SetBehaviourVariableOverride(entity, name, value);
        }
        return true;
    });
}

bool EditorSceneContext::RevertEntityScriptVariable(kb::scene::SceneEntity entity, std::string_view name) {
    if (!entity.IsValid() || name.empty()) {
        return false;
    }
    return ExecuteSceneCommand("Revert Script Variable", [this, entity, name = std::string{ name }]() {
        return scene_->Entities().RemoveBehaviourVariableOverride(entity, name);
    });
}

bool EditorSceneContext::ReloadOpenScriptAsset() {
    if (!scriptEditor_.IsOpen()) {
        return false;
    }
    const kb::assets::AssetId assetId = scriptEditor_.AssetId();
    if (!assetId.IsValid()) {
        return false;
    }
    // Erase the cache entry so EntityScriptExposedVariables' next Load re-reads
    // the file the Script Editor just wrote and re-parses the Inspector schema.
    return scene_->Assets().Manager().Unload(assetId);
}

std::vector<std::pair<kb::assets::AssetId, std::string>> EditorSceneContext::AvailableScriptAssets() const {
    std::vector<std::pair<kb::assets::AssetId, std::string>> scripts;
    for (const kb::assets::AssetMetadata& metadata : scene_->Assets().Manager().Registry().All()) {
        if (kb::script::ScriptBehaviourAsset::IsBehaviourAsset(metadata)) {
            scripts.emplace_back(metadata.id, metadata.name.empty() ? metadata.virtualPath.filename().string() : metadata.name);
        }
    }
    std::sort(scripts.begin(), scripts.end(), [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });
    return scripts;
}

bool EditorSceneContext::AttachScriptToEntity(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    return AddBehaviourAssetToEntity(assetId, entity);
}

bool EditorSceneContext::RemoveScriptFromEntity(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_->Components().Behaviours().Has(entity)) {
        return false;
    }
    return ExecuteSceneCommand("Remove Component", [this, entity]() {
        if (!scene_->Entities().IsAlive(entity)) {
            return false;
        }
        scene_->Components().Behaviours().Remove(entity);
        return true;
    });
}

bool EditorSceneContext::RemoveMeshRendererFromEntity(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_->Components().MeshRenderers().Has(entity)) {
        return false;
    }
    return ExecuteSceneCommand("Remove Component", [this, entity]() {
        if (!scene_->Entities().IsAlive(entity)) {
            return false;
        }
        scene_->Components().MeshRenderers().Remove(entity);
        return true;
    });
}

bool EditorSceneContext::RemovePhysicsComponent(kb::scene::SceneEntity entity, PhysicsComponentKind kind) {
    if (!entity.IsValid()) {
        return false;
    }
    const auto present = [&]() {
        switch (kind) {
        case PhysicsComponentKind::Rigidbody: return scene_->Components().Rigidbodies().Has(entity);
        case PhysicsComponentKind::Collider: return scene_->Components().Colliders().Has(entity);
        case PhysicsComponentKind::CharacterController: return scene_->Components().CharacterControllers().Has(entity);
        case PhysicsComponentKind::Joint: return scene_->Components().Joints().Has(entity);
        }
        return false;
    };
    if (!present()) {
        return false;
    }
    return ExecuteSceneCommand("Remove Component", [this, entity, kind]() {
        if (!scene_->Entities().IsAlive(entity)) {
            return false;
        }
        switch (kind) {
        case PhysicsComponentKind::Rigidbody: scene_->Components().Rigidbodies().Remove(entity); break;
        case PhysicsComponentKind::Collider: scene_->Components().Colliders().Remove(entity); break;
        case PhysicsComponentKind::CharacterController: scene_->Components().CharacterControllers().Remove(entity); break;
        case PhysicsComponentKind::Joint: scene_->Components().Joints().Remove(entity); break;
        }
        return true;
    });
}

kb::assets::AssetId EditorSceneContext::EntityScriptAssetId(kb::scene::SceneEntity entity) const {
    const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
    return behaviour != nullptr ? kb::assets::AssetId{ behaviour->behaviourAssetId } : kb::assets::AssetId{};
}

namespace {

// Mesh-local bounds of an entity's Mesh Renderer mesh: a per-axis axis-aligned
// box (center + half-extents) plus the bounding-sphere radius. Per-axis extents
// matter — a single sphere radius fits a flat plane/quad as a large CUBE, which
// is exactly the "collider is a big box around a plane" bug; the box half-extents
// hug the geometry on each axis instead.
struct EntityMeshBounds {
    kb::scene::Vec3 center{};
    kb::scene::Vec3 halfExtents{}; // per-axis, mesh-local
    float sphereRadius = 0.0F;
};

// Resolves EntityMeshBounds by loading the mesh through the AssetManager — the
// SAME mount-aware path the renderer uses — so procedural or virtual primitives
// with no on-disk physicalPath (and therefore no preview-stats entry) still fit.
// Bounds come from the same rasterizer the preview stats use, so they match what
// the viewport draws.
[[nodiscard]] bool TryLoadEntityMeshBounds(kb::scene::Scene& scene, kb::scene::SceneEntity entity, EntityMeshBounds& out, std::string& reason) {
    const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr) {
        reason = "entity has no Mesh Renderer";
        return false;
    }
    if (renderer->meshAssetId == 0U) {
        reason = "Mesh Renderer has no mesh assigned";
        return false;
    }
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetId meshAssetId{ renderer->meshAssetId };
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(meshAssetId);
    if (metadata == nullptr) {
        reason = "mesh asset " + std::to_string(renderer->meshAssetId) + " is not registered";
        return false;
    }

    const kb::assets::AssetHandle<kb::render::RenderMeshAssetData> asset =
        manager.Load<kb::render::RenderMeshAssetData>(meshAssetId);
    if (!asset.IsLoaded()) {
        reason = "mesh '" + metadata->type + "' could not be loaded for bounds";
        return false;
    }
    const kb::editor::EditorMeshPreviewGeometry geometry = kb::editor::EditorMeshPreviewRasterizer::ExtractGeometry(*asset);
    if (geometry.positions.size() < 3U) {
        reason = "mesh '" + metadata->type + "' produced no usable geometry (vertices=" + std::to_string(geometry.positions.size()) + ")";
        return false;
    }

    kb::editor::EditorMeshPreviewVector3 minPoint = geometry.positions.front();
    kb::editor::EditorMeshPreviewVector3 maxPoint = geometry.positions.front();
    for (const kb::editor::EditorMeshPreviewVector3& p : geometry.positions) {
        minPoint.x = std::min(minPoint.x, p.x);
        minPoint.y = std::min(minPoint.y, p.y);
        minPoint.z = std::min(minPoint.z, p.z);
        maxPoint.x = std::max(maxPoint.x, p.x);
        maxPoint.y = std::max(maxPoint.y, p.y);
        maxPoint.z = std::max(maxPoint.z, p.z);
    }
    out.center = kb::scene::Vec3{
        (minPoint.x + maxPoint.x) * 0.5F,
        (minPoint.y + maxPoint.y) * 0.5F,
        (minPoint.z + maxPoint.z) * 0.5F,
    };
    out.halfExtents = kb::scene::Vec3{
        (maxPoint.x - minPoint.x) * 0.5F,
        (maxPoint.y - minPoint.y) * 0.5F,
        (maxPoint.z - minPoint.z) * 0.5F,
    };
    out.sphereRadius = geometry.stats.boundsRadius > 0.0F
        ? geometry.stats.boundsRadius
        : std::sqrt(out.halfExtents.x * out.halfExtents.x + out.halfExtents.y * out.halfExtents.y + out.halfExtents.z * out.halfExtents.z);
    reason = "mesh '" + metadata->type + "' size="
        + std::to_string(out.halfExtents.x * 2.0F) + " x "
        + std::to_string(out.halfExtents.y * 2.0F) + " x "
        + std::to_string(out.halfExtents.z * 2.0F);
    return true;
}

// Writes mesh bounds into a collider according to its shape. Box uses the per-
// axis extents (so a plane becomes a thin slab, not a cube); a small minimum
// keeps a flat axis from collapsing to a degenerate zero-thickness shape the
// physics backend would reject.
void ApplyMeshBoundsToCollider(kb::scene::ColliderComponent& collider, const EntityMeshBounds& bounds) {
    constexpr float kMinHalfExtent = 0.005F;
    collider.center = bounds.center;
    switch (collider.shape) {
    case kb::scene::ColliderShape::Sphere:
        collider.radius = std::max(0.01F, bounds.sphereRadius);
        break;
    case kb::scene::ColliderShape::Capsule: {
        const float radius = std::max(0.01F, std::max(bounds.halfExtents.x, bounds.halfExtents.z));
        collider.radius = radius;
        collider.height = std::max(2.0F * radius, 2.0F * bounds.halfExtents.y);
        break;
    }
    case kb::scene::ColliderShape::Box:
    default:
        collider.boxSize = kb::scene::Vec3{
            std::max(2.0F * kMinHalfExtent, 2.0F * bounds.halfExtents.x),
            std::max(2.0F * kMinHalfExtent, 2.0F * bounds.halfExtents.y),
            std::max(2.0F * kMinHalfExtent, 2.0F * bounds.halfExtents.z),
        };
        break;
    }
}

} // namespace

bool EditorSceneContext::CanFitColliderToMesh(kb::scene::SceneEntity entity) const {
    // Cheap check only (this runs every paint): a Collider plus a Mesh Renderer
    // that references a mesh. The actual bounds load happens on the Fit action.
    if (!entity.IsValid() || !scene_->Components().Colliders().Has(entity)) {
        return false;
    }
    const kb::scene::MeshRendererComponent* renderer = scene_->Components().MeshRenderers().TryGet(entity);
    return renderer != nullptr && renderer->meshAssetId != 0U;
}

bool EditorSceneContext::ApplyColliderFitToMesh(kb::scene::SceneEntity entity, std::string& reason) {
    kb::scene::ColliderComponent* collider = scene_->Components().Colliders().TryGet(entity);
    if (collider == nullptr) {
        reason = "the entity has no Collider";
        return false;
    }
    EntityMeshBounds bounds;
    if (!TryLoadEntityMeshBounds(*scene_, entity, bounds, reason)) {
        return false;
    }
    // Mesh-local bounds; the entity's transform scale is applied later by the
    // physics backend and the debug-draw gizmo, so we store the unscaled size.
    ApplyMeshBoundsToCollider(*collider, bounds);
    scene_->Components().Colliders().MarkModified(entity);
    return true;
}

bool EditorSceneContext::FitColliderToMesh(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_->Components().Colliders().Has(entity)) {
        console_.Warning("Physics", "Fit to Mesh: the selected entity has no Collider.");
        return false;
    }
    const kb::scene::MeshRendererComponent* renderer = scene_->Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr || renderer->meshAssetId == 0U) {
        console_.Warning("Physics", "Fit to Mesh: the entity has no Mesh Renderer mesh to fit to.");
        return false;
    }
    std::string reason;
    const bool ok = ExecuteSceneCommand("Fit Collider To Mesh", [this, entity, &reason]() {
        return ApplyColliderFitToMesh(entity, reason);
    });
    if (ok) {
        console_.Info("Physics", "Fit to Mesh: " + reason + ".");
    } else {
        console_.Warning("Physics", "Fit to Mesh failed: " + (reason.empty() ? std::string{ "no resolvable mesh bounds" } : reason) + ".");
    }
    return ok;
}

bool EditorSceneContext::AddComponentToEntity(kb::scene::SceneEntity entity, std::string_view componentId) {
    if (!entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
        console_.Warning("Inspector", "Component add ignored for invalid entity.");
        return false;
    }

    if (componentId == "Camera") {
        if (scene_->Components().Cameras().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Camera component.");
            return false;
        }
        return ExecuteSceneCommand("Add Camera Component", [this, entity]() {
            scene_->Components().Cameras().Set(
                entity, kb::scene::CameraComponent{ .primary = true });
            return true;
        });
    }
    if (componentId == "3D Radiance Emitter" || componentId == "Light") {
        if (scene_->Components().Lights().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a 3D Radiance Emitter component.");
            return false;
        }
        return ExecuteSceneCommand("Add 3D Radiance Emitter", [this, entity]() {
            scene_->Components().Lights().Set(entity, kb::scene::LightComponent{});
            return true;
        });
    }
    if (componentId == "MeshRenderer") {
        if (scene_->Components().MeshRenderers().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Mesh Renderer component.");
            return false;
        }
        return ExecuteSceneCommand("Add Mesh Renderer Component", [this, entity]() {
            scene_->Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{});
            return true;
        });
    }
    if (componentId == "Particle Effect") {
        if (!IsProjectPluginEnabled("Rendering.21kbParticle")) {
            console_.Warning("Particles", "Enable 21kb Particle System in Edit > Plugins before adding the component.");
            return false;
        }
        if (scene_->Components().ParticleEffects().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Particle Effect component.");
            return false;
        }
        return ExecuteSceneCommand("Add Particle Effect Component", [this, entity]() {
            kb::scene::ParticleEffectComponent component{};
            component.enabled = false;
            scene_->Components().ParticleEffects().Set(entity, component);
            return true;
        });
    }
    if (componentId == "AudioSource") {
        if (scene_->Components().AudioSources().Has(entity)) {
            console_.Warning("Inspector", "Entity already has an Audio Source component.");
            return false;
        }
        return ExecuteSceneCommand("Add Audio Source Component", [this, entity]() {
            scene_->Components().AudioSources().Set(entity, kb::scene::AudioSourceComponent{});
            return true;
        });
    }
    if (componentId == "AudioListener") {
        if (scene_->Components().AudioListeners().Has(entity)) {
            console_.Warning("Inspector", "Entity already has an Audio Listener component.");
            return false;
        }
        return ExecuteSceneCommand("Add Audio Listener Component", [this, entity]() {
            scene_->Components().AudioListeners().Set(entity, kb::scene::AudioListenerComponent{});
            return true;
        });
    }
    if (componentId == "Animator") {
        if (scene_->Components().Animators().Has(entity)) {
            console_.Warning("Inspector", "Entity already has an Animator component.");
            return false;
        }
        return ExecuteSceneCommand("Add Animator Component", [this, entity]() {
            scene_->Components().Animators().Set(entity, kb::scene::Animator{});
            return true;
        });
    }
    if (componentId == "UIDocument") {
        if (scene_->Components().UIDocuments().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a UI Document component.");
            return false;
        }
        return ExecuteSceneCommand("Add UI Document Component", [this, entity]() {
            scene_->Components().UIDocuments().Set(entity, kb::scene::UIDocumentComponent{});
            return true;
        });
    }
    if (componentId == "Rigidbody") {
        if (scene_->Components().Rigidbodies().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Rigidbody component.");
            return false;
        }
        return ExecuteSceneCommand("Add Rigidbody Component", [this, entity]() {
            scene_->Components().Rigidbodies().Set(entity, kb::scene::RigidbodyComponent{});
            return true;
        });
    }
    if (componentId == "Collider") {
        if (scene_->Components().Colliders().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Collider component.");
            return false;
        }
        // Auto-fit the new collider to the entity's mesh so it matches the visible
        // geometry out of the box, instead of a default 0.5 sphere.
        // Per-axis, so a plane/quad becomes a thin slab rather than a cube.
        EntityMeshBounds bounds;
        std::string reason;
        const bool fitToMesh = TryLoadEntityMeshBounds(*scene_, entity, bounds, reason);
        if (fitToMesh) {
            console_.Info("Physics", "Collider auto-fit to mesh: " + reason + ".");
        } else {
            console_.Warning("Physics", "Collider added with default size — auto-fit skipped: " + reason + ".");
        }
        return ExecuteSceneCommand("Add Collider Component", [this, entity, fitToMesh, bounds]() {
            kb::scene::ColliderComponent collider{};
            if (fitToMesh) {
                ApplyMeshBoundsToCollider(collider, bounds);
            }
            scene_->Components().Colliders().Set(entity, collider);
            return true;
        });
    }
    if (componentId == "CharacterController") {
        if (scene_->Components().CharacterControllers().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Character Controller component.");
            return false;
        }
        return ExecuteSceneCommand("Add Character Controller Component", [this, entity]() {
            scene_->Components().CharacterControllers().Set(entity, kb::scene::CharacterControllerComponent{});
            return true;
        });
    }
    if (componentId == "Joint") {
        if (scene_->Components().Joints().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Joint component.");
            return false;
        }
        return ExecuteSceneCommand("Add Joint Component", [this, entity]() {
            scene_->Components().Joints().Set(entity, kb::scene::JointComponent{});
            return true;
        });
    }
    if (componentId == "SkeletonBinding") {
        if (scene_->Components().SkeletonBindings().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Skeleton Binding component.");
            return false;
        }
        return ExecuteSceneCommand("Add Skeleton Binding Component", [this, entity]() {
            return scene_->Components().SkeletonBindings().Set(entity, kb::scene::SkeletonBindingComponent{});
        });
    }
    if (componentId == "DeformedGeometry") {
        if (scene_->Components().DeformedGeometries().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Deformed Geometry component.");
            return false;
        }
        return ExecuteSceneCommand("Add Deformed Geometry Component", [this, entity]() {
            return scene_->Components().DeformedGeometries().Set(entity, kb::scene::DrawD3DeformedGeometryComponent{});
        });
    }
    if (componentId == "TerrainEditor") {
        if (!IsProjectPluginEnabled("Editor.Terrain")) {
            console_.Warning("Terrain", "Enable Terrain Editor in Edit > Plugins before adding the component.");
            return false;
        }
        std::string error;
        bool created = false;
        const bool committed = ExecuteSceneCommand("Add Terrain Editor Component", [this, entity, &error, &created]() {
            created = EditorTerrainService::Create(*scene_, entity, EditorProjectPaths::AssetsRoot(), &error);
            return created;
        });
        if (!committed || !created) {
            console_.Warning("Terrain", error.empty() ? "Terrain Editor component could not be created." : error);
            return false;
        }
        EditorTerrainService::ToolState().editingEnabled = true;
        EditorTerrainService::ToolState().mode = EditorTerrainToolMode::Sculpt;
        static_cast<void>(FrameSelectedEntitiesInViewport());
        console_.Info("Terrain", "Created a chunked 129 x 129 terrain with four LOD levels.");
        return true;
    }
    if (componentId == "Tags") {
        if (scene_->Components().Tags().Has(entity)) {
            console_.Warning("Inspector", "Entity already has an Object Classification component.");
            return false;
        }
        return ExecuteSceneCommand("Add Object Classification", [this, entity]() {
            scene_->Components().Tags().Set(entity, kb::scene::TagsComponent{});
            return true;
        });
    }
    if (componentId == "RegionShape") {
        if (scene_->Components().RegionShapes().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Region Shape component.");
            return false;
        }
        return ExecuteSceneCommand("Add Region Shape Component", [this, entity]() {
            scene_->Components().RegionShapes().Set(entity, kb::scene::RegionShapeComponent{});
            return true;
        });
    }
    if (componentId == "GuideCurve") {
        if (scene_->Components().GuideCurves().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Guide Curve component.");
            return false;
        }
        return ExecuteSceneCommand("Add Guide Curve Component", [this, entity]() {
            scene_->Components().GuideCurves().Set(entity, kb::scene::GuideCurveComponent{});
            return true;
        });
    }
    if (componentId == "ContentInstance") {
        if (scene_->Components().ContentInstances().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Content Instance component.");
            return false;
        }
        return ExecuteSceneCommand("Add Content Instance Component", [this, entity]() {
            scene_->Components().ContentInstances().Set(entity, kb::scene::ContentInstanceComponent{});
            return true;
        });
    }
    if (componentId == "StreamFocus") {
        if (scene_->Components().StreamFocuses().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Stream Focus component.");
            return false;
        }
        return ExecuteSceneCommand("Add Stream Focus Component", [this, entity]() {
            scene_->Components().StreamFocuses().Set(entity, kb::scene::StreamFocusComponent{});
            return true;
        });
    }
    if (componentId == "WorldBackdrop") {
        if (scene_->Components().WorldBackdrops().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a World Backdrop component.");
            return false;
        }
        return ExecuteSceneCommand("Add World Backdrop Component", [this, entity]() {
            scene_->Components().WorldBackdrops().Set(entity, kb::scene::WorldBackdropComponent{});
            return true;
        });
    }
    if (componentId == "Ambient Radiance") {
        if (scene_->Components().AmbientRadiances().Has(entity)) {
            console_.Warning("Inspector", "Entity already has an Ambient Radiance component.");
            return false;
        }
        return ExecuteSceneCommand("Add Ambient Radiance Component", [this, entity]() {
            scene_->Components().AmbientRadiances().Set(entity, kb::scene::AmbientRadianceComponent{});
            return true;
        });
    }
    if (componentId == "Detail Switch") {
        if (scene_->Components().DetailSwitches().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Detail Switch component.");
            return false;
        }
        return ExecuteSceneCommand("Add Detail Switch Component", [this, entity]() {
            scene_->Components().DetailSwitches().Set(entity, kb::scene::SceneDetailSwitchComponent{});
            return true;
        });
    }
    if (componentId == "Visibility Blocker") {
        if (scene_->Components().VisibilityBlockers().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Visibility Blocker component.");
            return false;
        }
        return ExecuteSceneCommand("Add Visibility Blocker Component", [this, entity]() {
            scene_->Components().VisibilityBlockers().Set(entity, kb::scene::SceneVisibilityBlockerComponent{});
            return true;
        });
    }
    if (componentId == "Visibility Cell") {
        if (scene_->Components().VisibilityCells().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Visibility Cell component.");
            return false;
        }
        return ExecuteSceneCommand("Add Visibility Cell Component", [this, entity]() {
            if (!scene_->Components().RegionShapes().Has(entity)) scene_->Components().RegionShapes().Set(entity, kb::scene::RegionShapeComponent{});
            scene_->Components().VisibilityCells().Set(entity, kb::scene::VisibilityCellComponent{});
            return true;
        });
    }
    if (componentId == "Region Portal") {
        if (scene_->Components().RegionPortals().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Region Portal component.");
            return false;
        }
        return ExecuteSceneCommand("Add Region Portal Component", [this, entity]() {
            if (!scene_->Components().RegionShapes().Has(entity)) scene_->Components().RegionShapes().Set(entity, kb::scene::RegionShapeComponent{});
            scene_->Components().RegionPortals().Set(entity, kb::scene::SceneRegionPortalComponent{});
            return true;
        });
    }
    if (componentId == "Secondary Frame") {
        if (scene_->Components().AuxFrames().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Secondary Frame component.");
            return false;
        }
        return ExecuteSceneCommand("Add Secondary Frame Component", [this, entity]() {
            if (!scene_->Components().Cameras().Has(entity)) scene_->Components().Cameras().Set(entity, kb::scene::CameraComponent{});
            scene_->Components().AuxFrames().Set(entity, kb::scene::AuxFrameComponent{});
            return true;
        });
    }
    if (componentId == "Geometry Swarm") {
        if (scene_->Components().GeometrySwarms().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Geometry Swarm component.");
            return false;
        }
        return ExecuteSceneCommand("Add Geometry Swarm Component", [this, entity]() {
            scene_->Components().GeometrySwarms().Set(entity, kb::scene::GeometrySwarmComponent{});
            return true;
        });
    }
    if (componentId == "Surface Cast") {
        if (scene_->Components().SurfaceCasts().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Surface Cast component.");
            return false;
        }
        return ExecuteSceneCommand("Add Surface Cast Component", [this, entity]() {
            scene_->Components().SurfaceCasts().Set(entity, kb::scene::SurfaceCastComponent{});
            return true;
        });
    }
    if (componentId == "Facing Panel") {
        if (scene_->Components().FacingPanels().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Facing Panel component.");
            return false;
        }
        return ExecuteSceneCommand("Add Facing Panel Component", [this, entity]() {
            scene_->Components().FacingPanels().Set(entity, kb::scene::FacingPanelComponent{});
            return true;
        });
    }
    if (componentId == "Kreska przestrzenna") {
        if (scene_->Components().SpaceStrokes().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Kreska przestrzenna component.");
            return false;
        }
        return ExecuteSceneCommand("Add Kreska przestrzenna", [this, entity]() {
            if (!scene_->Components().GuideCurves().Has(entity)) scene_->Components().GuideCurves().Set(entity, kb::scene::GuideCurveComponent{});
            scene_->Components().SpaceStrokes().Set(entity, kb::scene::SpaceStrokeComponent{});
            return true;
        });
    }
    if (componentId == "Wst\xC4\x99" "ga historii") {
        if (scene_->Components().HistoryRibbons().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Wst\xC4\x99" "ga historii component.");
            return false;
        }
        return ExecuteSceneCommand("Add Wst\xC4\x99" "ga historii", [this, entity]() {
            scene_->Components().HistoryRibbons().Set(entity, kb::scene::HistoryRibbonComponent{});
            return true;
        });
    }
    if (componentId == "Echo soczewki") {
        if (scene_->Components().LensEchoes().Has(entity)) {
            console_.Warning("Inspector", "Entity already has an Echo soczewki component.");
            return false;
        }
        return ExecuteSceneCommand("Add Echo soczewki", [this, entity]() {
            scene_->Components().LensEchoes().Set(entity, kb::scene::LensEchoComponent{});
            return true;
        });
    }
    if (componentId == "NavAgent") {
        if (scene_->Components().NavAgents().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Nav Agent component.");
            return false;
        }
        return ExecuteSceneCommand("Add Nav Agent Component", [this, entity]() {
            scene_->Components().NavAgents().Set(entity, kb::scene::NavAgent{});
            return true;
        });
    }
    if (componentId == "NavObstacle") {
        if (scene_->Components().NavObstacles().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Nav Obstacle component.");
            return false;
        }
        return ExecuteSceneCommand("Add Nav Obstacle Component", [this, entity]() {
            scene_->Components().NavObstacles().Set(entity, kb::scene::NavObstacle{});
            return true;
        });
    }

    console_.Warning("Inspector", "Unknown component: " + std::string{ componentId });
    return false;
}

std::vector<std::string> EditorSceneContext::EntityTags(kb::scene::SceneEntity entity) const {
    std::vector<std::string> assigned;
    for (const std::string& tag : scene_->Tags().Names()) {
        if (scene_->Tags().IsAssigned(entity, tag)) {
            assigned.push_back(tag);
        }
    }
    return assigned;
}

std::vector<std::string> EditorSceneContext::KnownSceneTags() const {
    const std::span<const std::string> names = scene_->Tags().Names();
    return std::vector<std::string>{ names.begin(), names.end() };
}

bool EditorSceneContext::SetEntityTagSelected(kb::scene::SceneEntity entity, std::string_view tagText, bool selected) {
    if (!entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
        return false;
    }
    const std::string tag{ tagText };
    return ExecuteSceneCommand(selected ? "Assign Tag" : "Remove Tag", [this, entity, tag, selected]() {
        return scene_->Tags().SetAssigned(entity, tag, selected);
    });
}

bool EditorSceneContext::RemoveTagsFromEntity(kb::scene::SceneEntity entity) {
    return ExecuteSceneCommand("Remove Tags", [this, entity]() {
        return scene_->Tags().ClearAssignments(entity);
    });
}

bool EditorSceneContext::DeleteSceneTag(std::string_view tagText) {
    const std::string tag{ tagText };
    if (tag.empty()) {
        return false;
    }
    return ExecuteSceneCommand("Delete Tag", [this, tag]() {
        return scene_->Tags().Undefine(tag);
    });
}

bool EditorSceneContext::SetAudioSourceClipAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    if (!entity.IsValid()) {
        return false;
    }
    if (assetId.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
        if (metadata == nullptr || !EditorSceneAudioAssetActions::IsAudioAsset(*metadata)) {
            console_.Warning("Inspector", "Only audio assets can be assigned to an Audio Source.");
            return false;
        }
    }
    if (!scene_->Components().AudioSources().Has(entity)) {
        console_.Warning("Inspector", "Selected entity does not have an Audio Source component.");
        return false;
    }
    return ExecuteSceneCommand(assetId.IsValid() ? "Assign Audio Clip" : "Clear Audio Clip", [this, entity, assetId]() {
        return EditorSceneAudioAssetActions::AssignAudioClip(*scene_, entity, assetId);
    });
}

bool EditorSceneContext::OpenAnimationAsset(kb::assets::AssetId id) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr) {
        console_.Error("Asset Editor", "Animation asset metadata was not found.");
        return false;
    }
    if (metadata->type == kb::scene::kAnimatorControllerAssetType) return OpenAnimatorEditorAsset(id);
    if (metadata->type != kb::scene::kTimelineAssetType) {
        console_.Error("Asset Editor", "Only Timeline assets use the Script Editor.");
        return false;
    }
    std::filesystem::path path = metadata->physicalPath;
    if (const std::optional<std::filesystem::path> mounted =
            scene_->Assets().Manager().Mounts().Resolve(metadata->virtualPath)) {
        path = *mounted;
    }
    scriptEditor_.Open(path, id, metadata->virtualPath.filename().string());
    console_.Info("Asset Editor", "Opened typed animation/timeline asset: " + metadata->virtualPath.generic_string());
    return true;
}

bool EditorSceneContext::OpenAnimatorEditorAsset(kb::assets::AssetId id) {
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* controllerMetadata = manager.Registry().Find(id);
    if (controllerMetadata == nullptr || controllerMetadata->type != kb::scene::kAnimatorControllerAssetType) {
        console_.Error("Animator Editor", "Selected asset is not an Animator Controller.");
        return false;
    }
    const kb::assets::AssetHandle<kb::scene::AnimatorController> controller =
        manager.Load<kb::scene::AnimatorController>(id);
    if (!controller.IsLoaded()) {
        console_.Error("Animator Editor", "Animator Controller runtime data could not be loaded.");
        return false;
    }

    kb::assets::AssetId skeletonId{};
    std::uint64_t skeletonSignature = 0U;
    kb::assets::AssetId firstClipId{};
    auto validateClip = [&](std::string_view reference) {
        kb::assets::AssetId clipId{};
        const kb::assets::AssetMetadata* clipMetadata = nullptr;
        if (kb::assets::TryParseAssetId(reference, clipId) && clipId.IsValid()) {
            clipMetadata = manager.Registry().Find(clipId);
        } else if (!reference.empty()) {
            clipMetadata = manager.Registry().FindByPath(std::filesystem::path{ reference });
            if (clipMetadata != nullptr) clipId = clipMetadata->id;
        }
        if (clipMetadata == nullptr || clipMetadata->type != kb::scene::kAnimationClipAssetType) {
            return false;
        }
        const kb::assets::AssetHandle<kb::scene::AnimationClip> clip = manager.Load<kb::scene::AnimationClip>(clipId);
        if (!clip.IsLoaded() || clip->targetSkeletonAssetId == 0U ||
            clip->targetSkeletonCompatibilitySignature == 0U) {
            return false;
        }
        const kb::assets::AssetId candidateSkeleton{ clip->targetSkeletonAssetId };
        if (!skeletonId.IsValid()) {
            skeletonId = candidateSkeleton;
            skeletonSignature = clip->targetSkeletonCompatibilitySignature;
            firstClipId = clipId;
            return true;
        }
        return skeletonId == candidateSkeleton && skeletonSignature == clip->targetSkeletonCompatibilitySignature;
    };

    for (const kb::scene::AnimatorControllerLayer& layer : controller->layers) {
        for (const kb::scene::AnimatorControllerState& state : layer.states) {
            if (!state.clipReference.empty() && !validateClip(state.clipReference)) {
                console_.Error("Animator Editor", "Animator Controller references a missing or incompatible Animation Clip.");
                return false;
            }
            for (const kb::scene::AnimatorControllerState::BlendChild& child : state.blendChildren) {
                if (!validateClip(child.clipReference)) {
                    console_.Error("Animator Editor", "Animator Controller references a missing or incompatible Animation Clip.");
                    return false;
                }
            }
        }
    }
    if (!skeletonId.IsValid()) {
        console_.Error("Animator Editor", "Animator Controller has no Animation Clip from which to derive a preview Skeleton.");
        return false;
    }
    const kb::assets::AssetMetadata* skeletonMetadata = manager.Registry().Find(skeletonId);
    const kb::assets::AssetHandle<kb::scene::SkeletonAsset> skeleton = manager.Load<kb::scene::SkeletonAsset>(skeletonId);
    if (skeletonMetadata == nullptr || skeletonMetadata->type != kb::scene::kSkeletonAssetType ||
        !skeleton.IsLoaded() || kb::scene::SkeletonCompatibilitySignature(*skeleton) != skeletonSignature) {
        console_.Error("Animator Editor", "Animator Controller preview Skeleton could not be loaded.");
        return false;
    }

    std::vector<kb::assets::AssetMetadata> meshMetadata;
    for (const kb::assets::AssetMetadata& candidate : manager.Registry().All()) {
        if (candidate.type == kb::scene::kSkeletalMeshAssetType) meshMetadata.push_back(candidate);
    }
    std::ranges::sort(meshMetadata, {}, [](const kb::assets::AssetMetadata& value) { return value.id.value; });
    kb::assets::AssetId previewMeshId{};
    kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> previewMesh{};
    const kb::assets::AssetMetadata* previewMeshMetadata = nullptr;
    for (const kb::assets::AssetMetadata& candidate : meshMetadata) {
        const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh = manager.Load<kb::scene::SkeletalMeshAsset>(candidate.id);
        if (mesh.IsLoaded() && mesh->skeletonAssetId == skeletonId.value &&
            mesh->skeletonCompatibilitySignature == skeletonSignature) {
            previewMeshId = candidate.id;
            previewMesh = mesh;
            previewMeshMetadata = &candidate;
            break;
        }
    }
    if (!previewMeshId.IsValid() || !previewMesh.IsLoaded() || previewMeshMetadata == nullptr) {
        console_.Error("Animator Editor", "No Skeletal Mesh compatible with every Animation Clip in this Animator Controller is available for preview.");
        return false;
    }
    if (HasDirtySkeletalMeshEditorAssetEdit()) {
        console_.Warning("Animator Editor", "Save or Revert the open Skeletal Mesh before changing the shared preview document.");
        return false;
    }

    const kb::assets::AssetHandle<kb::scene::AnimationClip> firstClip = manager.Load<kb::scene::AnimationClip>(firstClipId);
    if (!firstClip.IsLoaded()) return false;
    CloseSkeletalMeshEditorAsset();
    animationPreview_.Clear();
    animationPreview_.SetAssets(skeletonId, previewMeshId, {}, id);
    animationPreview_.SetPoseMode(AnimationPreviewPoseMode::Animated);
    static_cast<void>(animationPreview_.Overlays().SetBonesVisible(true));
    static_cast<void>(animationPreview_.Transport().SetDurationSeconds(firstClip->durationSeconds));
    static_cast<void>(animationPreview_.Transport().SetLooping(firstClip->looping));
    animationClipEditorAssetId_ = {};
    animationClipEditorTimeline_ = {};
    animationClipEditorDocument_ = {};
    animatorEditorAssetId_ = id;
    animatorEditorDebugTarget_ = {};
    animatorEditorController_ = *controller;
    animatorEditorGraphDocument_.Open(*controller);
    animationPreview_.SetControllerOverride(std::make_shared<kb::scene::AnimatorController>(*controller));
    skeletalMeshEditorTree_.SetSkeleton(*skeleton);
    skeletalMeshEditorDetails_.SetDocument(*previewMesh, *skeleton, *previewMeshMetadata);
    static_cast<void>(AnimationPreviewScene());
    console_.Info("Animator Editor", "Opened document: " + controllerMetadata->virtualPath.generic_string());
    return true;
}

kb::assets::AssetId EditorSceneContext::AnimatorEditorAssetId() const noexcept {
    return animatorEditorAssetId_;
}

bool EditorSceneContext::HasAnimatorEditorAsset() const noexcept {
    return animatorEditorAssetId_.IsValid() && animationPreview_.ControllerAsset() == animatorEditorAssetId_ &&
        animationPreviewScene_ != nullptr && animationPreviewScene_->CurrentScene() != nullptr;
}

bool EditorSceneContext::AnimatorEditorDebuggingPreview() const noexcept {
    if (!animatorEditorDebugTarget_.IsValid() || !HasAnimatorEditorAsset()) return true;
    if (!scene_->Entities().IsAlive(animatorEditorDebugTarget_)) return true;
    const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> snapshot =
        scene_->Animators().DebugSnapshot();
    const kb::scene::AnimatorDebugInstanceSnapshot* instance = snapshot == nullptr
        ? nullptr
        : snapshot->Find(animatorEditorDebugTarget_);
    return instance == nullptr ||
        instance->controllerAssetId != animatorEditorAssetId_.value;
}

kb::scene::SceneEntity EditorSceneContext::AnimatorEditorDebugTarget() const noexcept {
    return AnimatorEditorDebuggingPreview() ? kb::scene::SceneEntity{} : animatorEditorDebugTarget_;
}

std::string EditorSceneContext::AnimatorEditorDebugTargetLabel() const {
    return AnimatorEditorDebuggingPreview() ? "Preview Instance" : scene_->Entities().Name(animatorEditorDebugTarget_);
}

kb::scene::SceneEntity EditorSceneContext::AnimatorEditorResolvedDebugTarget() const noexcept {
    if (!AnimatorEditorDebuggingPreview()) return animatorEditorDebugTarget_;
    return animationPreviewScene_ == nullptr ? kb::scene::SceneEntity{} :
        animationPreviewScene_->PreviewEntity();
}

std::shared_ptr<const kb::scene::AnimatorDebugSnapshot>
EditorSceneContext::AnimatorEditorDebugSnapshot() const {
    if (!HasAnimatorEditorAsset()) return {};
    if (!AnimatorEditorDebuggingPreview()) return scene_->Animators().DebugSnapshot();
    const kb::scene::Scene* preview = AnimatorEditorPreviewScene();
    return preview == nullptr ? std::shared_ptr<const kb::scene::AnimatorDebugSnapshot>{}
        : preview->Animators().DebugSnapshot();
}

void EditorSceneContext::WaitForAnimatorEditorDebugSnapshot() {
    if (!HasAnimatorEditorAsset()) return;
    if (!AnimatorEditorDebuggingPreview()) {
        scene_->Animators().WaitForDebugSnapshot();
        return;
    }
    kb::scene::Scene* preview =
        animationPreviewScene_ == nullptr ? nullptr : animationPreviewScene_->MutableScene();
    if (preview != nullptr) preview->Animators().WaitForDebugSnapshot();
}

bool EditorSceneContext::SetAnimatorEditorDebugTarget(kb::scene::SceneEntity entity) {
    const kb::scene::Animator* animator = scene_->Components().Animators().TryGet(entity);
    if (!HasAnimatorEditorAsset() || !scene_->Entities().IsAlive(entity) || animator == nullptr ||
        animator->controllerAssetId != animatorEditorAssetId_.value || !scene_->Animators().Exists(entity)) {
        console_.Warning("Animator Editor", "Debug target must be a live Animator instance using the open Animator Controller.");
        return false;
    }
    animatorEditorDebugTarget_ = entity;
    return true;
}

void EditorSceneContext::SetAnimatorEditorDebugTargetPreview() noexcept {
    animatorEditorDebugTarget_ = {};
}

const kb::scene::Scene* EditorSceneContext::AnimatorEditorPreviewScene() const noexcept {
    return HasAnimatorEditorAsset() ? animationPreviewScene_->CurrentScene() : nullptr;
}

std::uint64_t EditorSceneContext::AnimatorEditorPreviewRevision() const noexcept {
    return HasAnimatorEditorAsset() ? animationPreviewScene_->Revision() : 0U;
}

const kb::scene::AnimatorController* EditorSceneContext::AnimatorEditorController() const noexcept {
    return animatorEditorAssetId_.IsValid() ? animatorEditorGraphDocument_.Controller() : nullptr;
}

AnimatorEditorGraphDocumentState& EditorSceneContext::AnimatorEditorGraphDocument() noexcept {
    return animatorEditorGraphDocument_;
}

const AnimatorEditorGraphDocumentState& EditorSceneContext::AnimatorEditorGraphDocument() const noexcept {
    return animatorEditorGraphDocument_;
}

bool EditorSceneContext::AddAnimationClipToAnimatorEditor(kb::assets::AssetId clipId, std::int32_t graphX, std::int32_t graphY) {
    if (!HasAnimatorEditorAsset()) return false;
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(clipId);
    const kb::assets::AssetHandle<kb::scene::AnimationClip> clip = scene_->Assets().Manager().Load<kb::scene::AnimationClip>(clipId);
    if (metadata == nullptr || metadata->type != kb::scene::kAnimationClipAssetType || !clip.IsLoaded() ||
        clip->targetSkeletonAssetId != animationPreview_.SkeletonAsset().value ||
        clip->targetSkeletonCompatibilitySignature == 0U) {
        console_.Error("Animator Editor", "Dropped Animation Clip is not compatible with the active Animator preview Skeleton.");
        return false;
    }
    const kb::scene::AnimatorController* controller = animatorEditorGraphDocument_.Controller();
    if (controller == nullptr || controller->layers.empty()) return false;
    const std::string name = metadata->name.empty() ? metadata->virtualPath.stem().string() : metadata->name;
    const std::uint64_t stateId = animatorEditorGraphDocument_.AddState(
        controller->layers.front().name, name, kb::assets::ToString(clipId), graphX, graphY);
    if (stateId == 0U || !RefreshAnimatorEditorWorkingPreview()) return false;
    console_.Info("Animator Editor", "Created state from Animation Clip: " + name);
    return true;
}

bool EditorSceneContext::OpenAnimatorMotionDocument(std::uint64_t stateId) {
    if (!HasAnimatorEditorAsset() || !animatorEditorGraphDocument_.OpenMotionDocument(stateId)) return false;
    console_.Info("Animator Editor", "Opened state motion document.");
    return true;
}

bool EditorSceneContext::ReturnToAnimatorStateMachine() {
    return animatorEditorGraphDocument_.CloseMotionDocument();
}

bool EditorSceneContext::HasDirtyAnimatorEditorAssetEdit() const noexcept {
    return animatorEditorGraphDocument_.Dirty();
}

bool EditorSceneContext::UndoAnimatorEditorEdit() {
    return animatorEditorGraphDocument_.Undo() && RefreshAnimatorEditorWorkingPreview();
}

bool EditorSceneContext::RedoAnimatorEditorEdit() {
    return animatorEditorGraphDocument_.Redo() && RefreshAnimatorEditorWorkingPreview();
}

bool EditorSceneContext::SaveAnimatorEditorAsset() {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(animatorEditorAssetId_);
    const kb::scene::AnimatorController* working = animatorEditorGraphDocument_.Controller();
    std::string error;
    if (metadata == nullptr || working == nullptr ||
        !kb::scene::AnimationAssetIO::SaveController(metadata->physicalPath, *working, &error)) {
        console_.Error("Animator Editor", error.empty()
            ? "Animator Controller validation or atomic save failed; runtime preview was not reloaded."
            : "Save failed: " + error);
        return false;
    }
    static_cast<void>(scene_->Assets().Manager().DiscoverMountedAssets());
    if (!scene_->Assets().Manager().PublishRuntimeAsset(
            animatorEditorAssetId_, std::make_shared<kb::scene::AnimatorController>(*working))) {
        console_.Error("Animator Editor", "Animator Controller was saved but its runtime hot reload failed.");
        return false;
    }
    static_cast<void>(animatorEditorGraphDocument_.MarkSaved());
    animationPreviewScene_->Clear();
    static_cast<void>(AnimationPreviewScene());
    console_.Info("Animator Editor", "Saved and hot reloaded Animator Controller.");
    return true;
}

bool EditorSceneContext::RevertAnimatorEditorAsset() {
    if (!animatorEditorGraphDocument_.Revert()) return false;
    return RefreshAnimatorEditorWorkingPreview();
}

bool EditorSceneContext::RefreshAnimatorEditorWorkingPreview() {
    const kb::scene::AnimatorController* controller = animatorEditorGraphDocument_.Controller();
    if (!HasAnimatorEditorAsset() || controller == nullptr) return false;
    animationPreview_.SetControllerOverride(std::make_shared<kb::scene::AnimatorController>(*controller));
    animationPreviewScene_->Clear();
    static_cast<void>(AnimationPreviewScene());
    return true;
}

bool EditorSceneContext::OpenAnimationClipEditorAsset(kb::assets::AssetId id) {
    const kb::assets::AssetMetadata* clipMetadata = scene_->Assets().Manager().Registry().Find(id);
    if (clipMetadata == nullptr || clipMetadata->type != kb::scene::kAnimationClipAssetType) {
        console_.Error("Animation Clip Editor", "Selected asset is not an Animation Clip.");
        return false;
    }
    const kb::assets::AssetHandle<kb::scene::AnimationClip> clip =
        scene_->Assets().Manager().Load<kb::scene::AnimationClip>(id);
    if (!clip.IsLoaded() || clip->targetSkeletonAssetId == 0U ||
        clip->targetSkeletonCompatibilitySignature == 0U) {
        console_.Error("Animation Clip Editor", "Animation Clip runtime data or its Skeleton binding could not be loaded.");
        return false;
    }
    const kb::assets::AssetId skeletonId{ clip->targetSkeletonAssetId };
    const kb::assets::AssetMetadata* skeletonMetadata = scene_->Assets().Manager().Registry().Find(skeletonId);
    if (skeletonMetadata == nullptr || skeletonMetadata->type != kb::scene::kSkeletonAssetType) {
        console_.Error("Animation Clip Editor", "Animation Clip references a missing Skeleton asset.");
        return false;
    }
    const kb::assets::AssetHandle<kb::scene::SkeletonAsset> skeleton =
        scene_->Assets().Manager().Load<kb::scene::SkeletonAsset>(skeletonId);
    if (!skeleton.IsLoaded() ||
        kb::scene::SkeletonCompatibilitySignature(*skeleton) != clip->targetSkeletonCompatibilitySignature) {
        console_.Error("Animation Clip Editor", "Animation Clip and Skeleton are incompatible.");
        return false;
    }

    std::vector<kb::assets::AssetMetadata> meshMetadata;
    for (const kb::assets::AssetMetadata& candidate : scene_->Assets().Manager().Registry().All()) {
        if (candidate.type == kb::scene::kSkeletalMeshAssetType) meshMetadata.push_back(candidate);
    }
    std::ranges::sort(meshMetadata, {}, [](const kb::assets::AssetMetadata& value) { return value.id.value; });
    kb::assets::AssetId previewMeshId{};
    for (const kb::assets::AssetMetadata& candidate : meshMetadata) {
        const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh =
            scene_->Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(candidate.id);
        if (mesh.IsLoaded() && mesh->skeletonAssetId == skeletonId.value &&
            mesh->skeletonCompatibilitySignature == clip->targetSkeletonCompatibilitySignature) {
            previewMeshId = candidate.id;
            break;
        }
    }
    if (!previewMeshId.IsValid()) {
        console_.Error("Animation Clip Editor", "No Skeletal Mesh compatible with this Animation Clip's Skeleton is available for preview.");
        return false;
    }
    if (HasDirtySkeletalMeshEditorAssetEdit()) {
        console_.Warning("Animation Clip Editor", "Save or Revert the open Skeletal Mesh before changing the shared preview document.");
        return false;
    }

    const kb::assets::AssetMetadata* previewMeshMetadata = scene_->Assets().Manager().Registry().Find(previewMeshId);
    const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> previewMesh =
        scene_->Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(previewMeshId);
    if (previewMeshMetadata == nullptr || !previewMesh.IsLoaded()) {
        console_.Error("Animation Clip Editor", "The selected preview Skeletal Mesh could not be loaded.");
        return false;
    }

    CloseSkeletalMeshEditorAsset();
    animationPreview_.Clear();
    animationPreview_.SetAssets(skeletonId, previewMeshId, id, {});
    animationPreview_.SetPoseMode(AnimationPreviewPoseMode::Animated);
    static_cast<void>(animationPreview_.Overlays().SetBonesVisible(true));
    static_cast<void>(animationPreview_.Transport().SetDurationSeconds(clip->durationSeconds));
    static_cast<void>(animationPreview_.Transport().SetLooping(clip->looping));
    animatorEditorAssetId_ = {};
    animatorEditorController_.reset();
    animatorEditorGraphDocument_ = {};
    animationClipEditorAssetId_ = id;
    animationClipEditorTimeline_.SetClip(*clip);
    animationClipEditorDocument_.Open(id, *clip);
    skeletalMeshEditorTree_.SetSkeleton(*skeleton);
    skeletalMeshEditorDetails_.SetDocument(*previewMesh, *skeleton, *previewMeshMetadata);
    static_cast<void>(AnimationPreviewScene());
    console_.Info("Animation Clip Editor", "Opened document: " + clipMetadata->virtualPath.generic_string());
    return true;
}

kb::assets::AssetId EditorSceneContext::AnimationClipEditorAssetId() const noexcept {
    return animationClipEditorAssetId_;
}

bool EditorSceneContext::HasAnimationClipEditorAsset() const noexcept {
    return animationClipEditorAssetId_.IsValid() && animationPreview_.ClipAsset() == animationClipEditorAssetId_ &&
        animationPreviewScene_ != nullptr && animationPreviewScene_->CurrentScene() != nullptr;
}

const kb::scene::Scene* EditorSceneContext::AnimationClipEditorPreviewScene() const noexcept {
    return HasAnimationClipEditorAsset() ? animationPreviewScene_->CurrentScene() : nullptr;
}

std::uint64_t EditorSceneContext::AnimationClipEditorPreviewRevision() const noexcept {
    return HasAnimationClipEditorAsset() ? animationPreviewScene_->Revision() : 0U;
}

AnimationClipTimelineState& EditorSceneContext::AnimationClipEditorTimeline() noexcept {
    return animationClipEditorTimeline_;
}

const AnimationClipTimelineState& EditorSceneContext::AnimationClipEditorTimeline() const noexcept {
    return animationClipEditorTimeline_;
}

bool EditorSceneContext::BeginAnimationClipEditorEditGroup() {
    if (!HasAnimationClipEditorAsset()) return false;
    animationClipEditorDocument_.BeginGroup();
    return true;
}

void EditorSceneContext::EndAnimationClipEditorEditGroup() noexcept {
    animationClipEditorDocument_.EndGroup();
}

bool EditorSceneContext::PublishAnimationClipEditorWorkingCopy() {
    const kb::scene::AnimationClip* clip = animationClipEditorDocument_.WorkingCopy();
    if (clip == nullptr || !scene_->Assets().Manager().PublishRuntimeAsset(
            animationClipEditorAssetId_, std::make_shared<kb::scene::AnimationClip>(*clip))) {
        return false;
    }
    animationClipEditorTimeline_.SetClip(*clip);
    animationPreviewScene_->Clear();
    static_cast<void>(AnimationPreviewScene());
    return true;
}

bool EditorSceneContext::UndoAnimationClipEditorEdit() {
    return animationClipEditorDocument_.Undo() && PublishAnimationClipEditorWorkingCopy();
}

bool EditorSceneContext::RedoAnimationClipEditorEdit() {
    return animationClipEditorDocument_.Redo() && PublishAnimationClipEditorWorkingCopy();
}

bool EditorSceneContext::UpsertAnimationClipBoneKey(
    kb::scene::SkeletonBoneId boneId, float timeSeconds, kb::scene::LocalTransform transform) {
    return animationClipEditorDocument_.UpsertBoneKey(boneId, timeSeconds, transform) && PublishAnimationClipEditorWorkingCopy();
}

bool EditorSceneContext::RemoveAnimationClipBoneKey(kb::scene::SkeletonBoneId boneId, float timeSeconds) {
    return animationClipEditorDocument_.RemoveBoneKey(boneId, timeSeconds) && PublishAnimationClipEditorWorkingCopy();
}

bool EditorSceneContext::UpsertAnimationClipEvent(kb::scene::AnimationEventId id, float timeSeconds) {
    return animationClipEditorDocument_.UpsertEvent(id, timeSeconds) && PublishAnimationClipEditorWorkingCopy();
}

bool EditorSceneContext::RemoveAnimationClipEvent(kb::scene::AnimationEventId id) {
    return animationClipEditorDocument_.RemoveEvent(id) && PublishAnimationClipEditorWorkingCopy();
}

bool EditorSceneContext::SaveAnimationClipEditorAsset() {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(animationClipEditorAssetId_);
    const kb::scene::AnimationClip* working = animationClipEditorDocument_.WorkingCopy();
    if (metadata == nullptr || working == nullptr || !kb::scene::AnimationAssetIO::SaveClip(metadata->physicalPath, *working)) {
        console_.Error("Animation Clip Editor", "Animation Clip validation or atomic save failed; runtime preview was not reloaded.");
        return false;
    }
    static_cast<void>(scene_->Assets().Manager().DiscoverMountedAssets());
    if (!scene_->Assets().Manager().PublishRuntimeAsset(
            animationClipEditorAssetId_, std::make_shared<kb::scene::AnimationClip>(*working))) {
        console_.Error("Animation Clip Editor", "Animation Clip was saved but its runtime hot reload failed.");
        return false;
    }
    static_cast<void>(animationClipEditorDocument_.MarkSaved());
    animationPreviewScene_->Clear();
    static_cast<void>(AnimationPreviewScene());
    console_.Info("Animation Clip Editor", "Saved and hot reloaded Animation Clip.");
    return true;
}

std::vector<kb::assets::AssetId> EditorSceneContext::AnimationClipEditorCompatiblePreviewMeshes() {
    std::vector<kb::assets::AssetId> result;
    const kb::scene::AnimationClip* clip = animationClipEditorDocument_.WorkingCopy();
    if (clip == nullptr) return result;
    for (const kb::assets::AssetMetadata& metadata : scene_->Assets().Manager().Registry().All()) {
        if (metadata.type != kb::scene::kSkeletalMeshAssetType) continue;
        const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh =
            scene_->Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(metadata.id);
        if (mesh.IsLoaded() && mesh->skeletonAssetId == clip->targetSkeletonAssetId &&
            mesh->skeletonCompatibilitySignature == clip->targetSkeletonCompatibilitySignature) {
            result.push_back(metadata.id);
        }
    }
    std::ranges::sort(result, {}, [](kb::assets::AssetId id) { return id.value; });
    return result;
}

bool EditorSceneContext::SetAnimationClipEditorPreviewMesh(kb::assets::AssetId meshId) {
    const kb::scene::AnimationClip* clip = animationClipEditorDocument_.WorkingCopy();
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(meshId);
    const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh =
        scene_->Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(meshId);
    if (clip == nullptr || metadata == nullptr || metadata->type != kb::scene::kSkeletalMeshAssetType || !mesh.IsLoaded() ||
        mesh->skeletonAssetId != clip->targetSkeletonAssetId ||
        mesh->skeletonCompatibilitySignature != clip->targetSkeletonCompatibilitySignature) {
        console_.Error("Animation Clip Editor", "Selected preview Skeletal Mesh is incompatible with this Animation Clip.");
        return false;
    }
    const kb::assets::AssetHandle<kb::scene::SkeletonAsset> skeleton =
        scene_->Assets().Manager().Load<kb::scene::SkeletonAsset>(kb::assets::AssetId{ clip->targetSkeletonAssetId });
    if (!skeleton.IsLoaded()) {
        console_.Error("Animation Clip Editor", "Animation Clip preview Skeleton could not be loaded.");
        return false;
    }
    animationPreview_.SetAssets(kb::assets::AssetId{ clip->targetSkeletonAssetId }, meshId, animationClipEditorAssetId_, {});
    animationPreviewScene_->Clear();
    skeletalMeshEditorTree_.SetSkeleton(*skeleton);
    skeletalMeshEditorDetails_.SetDocument(*mesh, *skeleton, *metadata);
    static_cast<void>(AnimationPreviewScene());
    return true;
}

bool EditorSceneContext::HasPendingParticleProviderMigration() const noexcept {
    return !particleProviderMigrationResolved_ && !projectBootstrap_.created &&
        !projectBootstrap_.particlePolicy.IsRunnable();
}

bool EditorSceneContext::AcceptParticleProviderMigration() {
    if (!HasPendingParticleProviderMigration()) return false;
    if (!EditorProjectBootstrap::AcceptParticleProvider(projectFile_, project_)) {
        console_.Error("Project", "Rendering.21kbParticle could not be added to the project descriptor.");
        return false;
    }
    particleProviderMigrationResolved_ = true;
    plugins_.MarkPendingReload();
    console_.Info("Project", "Rendering.21kbParticle added. Reload the scene to activate it.");
    return true;
}

void EditorSceneContext::CancelParticleProviderMigration() noexcept {
    if (!HasPendingParticleProviderMigration()) return;
    particleProviderMigrationResolved_ = true;
    console_.Info("Project", "Rendering.21kbParticle migration canceled; the project descriptor was not changed.");
}

bool EditorSceneContext::CanAddSkeletonEditorSocket() const noexcept {
    const kb::scene::SkeletonAsset* working = skeletonEditorDocument_.WorkingCopy();
    return IsSkeletalMeshEditorSkeletonDocument() && working != nullptr && !working->bones.empty();
}

bool EditorSceneContext::CanDuplicateSkeletonEditorSocket() const noexcept {
    return IsSkeletalMeshEditorSkeletonDocument() && skeletonEditorDocument_.IsOpen() &&
        !skeletalMeshEditorTree_.SelectedSocket().empty();
}

bool EditorSceneContext::CanDeleteSkeletonEditorSocket() const noexcept {
    return CanDuplicateSkeletonEditorSocket();
}

bool EditorSceneContext::SetSkeletonEditorPreviewMesh(kb::assets::AssetId meshId) {
    if (!IsSkeletalMeshEditorSkeletonDocument() || !skeletonEditorDocument_.IsOpen()) return false;
    const kb::assets::AssetId skeletonId = skeletonEditorDocument_.AssetId();
    if (!meshId.IsValid()) {
        if (skeletalMeshEditorDocument_.Dirty()) {
            console_.Warning("Skeleton Editor", "Save or revert the linked Skeletal Mesh before clearing its preview.");
            return false;
        }
        skeletalMeshEditorDocument_.Close();
        skeletalMeshEditorAssetId_ = {};
        animationPreview_.SetAssets(skeletonId, {}, {}, {});
        animationPreviewScene_->Clear();
        RefreshSkeletalEditorDetails();
        static_cast<void>(AnimationPreviewScene());
        return true;
    }

    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(meshId);
    if (metadata == nullptr || metadata->type != kb::scene::kSkeletalMeshAssetType) return false;
    std::filesystem::path path = metadata->physicalPath;
    if (const std::optional<std::filesystem::path> mounted =
            scene_->Assets().Manager().Mounts().Resolve(metadata->virtualPath)) {
        path = *mounted;
    }
    const std::optional<kb::scene::SkeletalMeshAssetBinding> binding =
        kb::scene::SkeletalMeshAssetIO::LoadBinding(path);
    const kb::scene::SkeletonAsset* skeleton = skeletonEditorDocument_.WorkingCopy();
    if (!binding.has_value() || skeleton == nullptr || binding->skeletonAssetId != skeletonId.value ||
        binding->skeletonCompatibilitySignature != kb::scene::SkeletonCompatibilitySignature(*skeleton)) {
        console_.Error("Skeleton Editor", "Selected preview Skeletal Mesh is incompatible with this Skeleton.");
        return false;
    }
    if (skeletalMeshEditorDocument_.Dirty() && skeletalMeshEditorDocument_.AssetId() != meshId) {
        console_.Warning("Skeleton Editor", "Save or revert the linked Skeletal Mesh before replacing the preview.");
        return false;
    }
    if (skeletalMeshEditorAssetId_ == meshId && !HasPendingSkeletalMeshEditorOpen()) {
        return SwitchSkeletalMeshEditorDocument(true);
    }
    if (!RequestOpenSkeletalMeshEditorAsset(meshId)) return false;
    pendingSkeletalMeshEditorPrimarySkeletonId_ = skeletonId;
    return true;
}

void EditorSceneContext::RefreshSkeletalEditorDetails() {
    const kb::scene::SkeletonAsset* skeleton = skeletonEditorDocument_.WorkingCopy();
    if (skeleton == nullptr) return;
    skeletalMeshEditorTree_.SetSkeleton(*skeleton);
    const kb::assets::AssetMetadata* skeletonMetadata =
        scene_->Assets().Manager().Registry().Find(skeletonEditorDocument_.AssetId());
    const kb::assets::AssetMetadata* meshMetadata = skeletalMeshEditorAssetId_.IsValid()
        ? scene_->Assets().Manager().Registry().Find(skeletalMeshEditorAssetId_)
        : nullptr;
    if (skeletonMetadata == nullptr) return;
    if (IsSkeletalMeshEditorSkeletonDocument()) {
        skeletalMeshEditorDetails_.SetSkeletonDocument(*skeleton, *skeletonMetadata, meshMetadata);
        return;
    }
    const kb::scene::SkeletalMeshAsset* mesh = skeletalMeshEditorDocument_.WorkingCopy();
    if (mesh != nullptr && meshMetadata != nullptr) {
        skeletalMeshEditorDetails_.SetDocument(*mesh, *skeleton, *meshMetadata);
    }
}

bool EditorSceneContext::PublishSkeletonEditorWorkingCopy() {
    const kb::scene::SkeletonAsset* working = skeletonEditorDocument_.WorkingCopy();
    if (working == nullptr || !scene_->Assets().Manager().PublishRuntimeAsset(
            skeletonEditorDocument_.AssetId(), std::make_shared<kb::scene::SkeletonAsset>(*working))) {
        return false;
    }
    RefreshSkeletalEditorDetails();
    animationPreviewScene_->Clear();
    static_cast<void>(AnimationPreviewScene());
    return true;
}

bool EditorSceneContext::AddSkeletonEditorSocket() {
    if (!CanAddSkeletonEditorSocket()) return false;
    const kb::scene::SkeletonAsset* working = skeletonEditorDocument_.WorkingCopy();
    if (working == nullptr) return false;
    kb::scene::SkeletonAsset candidate = *working;
    kb::scene::SkeletonBoneId boneId = skeletalMeshEditorTree_.SelectedBone();
    if (boneId == 0U && !skeletalMeshEditorTree_.SelectedSocket().empty()) {
        const std::string& selectedSocket = skeletalMeshEditorTree_.SelectedSocket();
        const auto socket = std::ranges::find_if(
            candidate.sockets,
            [&selectedSocket](const kb::scene::SkeletonSocket& value) {
                return value.name == selectedSocket;
            });
        if (socket != candidate.sockets.end()) boneId = socket->boneId;
    }
    auto bone = std::ranges::find_if(candidate.bones, [boneId](const kb::scene::SkeletonBone& value) {
        return value.id == boneId;
    });
    if (bone == candidate.bones.end()) {
        bone = std::ranges::find_if(candidate.bones, [](const kb::scene::SkeletonBone& value) {
            return value.parentIndex < 0;
        });
        if (bone == candidate.bones.end()) bone = candidate.bones.begin();
        boneId = bone->id;
    }
    const std::string base = bone->name + " Socket";
    std::string name = base;
    for (std::uint32_t suffix = 2U; std::ranges::any_of(candidate.sockets,
             [&name](const kb::scene::SkeletonSocket& socket) { return socket.name == name; }); ++suffix) {
        name = base + " " + std::to_string(suffix);
    }
    candidate.sockets.push_back(kb::scene::SkeletonSocket{ .name = name, .boneId = boneId });
    if (!skeletonEditorDocument_.Apply(std::move(candidate)) || !PublishSkeletonEditorWorkingCopy()) return false;
    return skeletalMeshEditorTree_.SelectSocket(std::move(name));
}

bool EditorSceneContext::DuplicateSkeletonEditorSocket() {
    if (!CanDuplicateSkeletonEditorSocket()) return false;
    const kb::scene::SkeletonAsset* working = skeletonEditorDocument_.WorkingCopy();
    if (working == nullptr) return false;
    kb::scene::SkeletonAsset candidate = *working;
    const std::string sourceName = skeletalMeshEditorTree_.SelectedSocket();
    const auto source = std::ranges::find_if(candidate.sockets, [&sourceName](const kb::scene::SkeletonSocket& socket) {
        return socket.name == sourceName;
    });
    if (source == candidate.sockets.end()) return false;
    kb::scene::SkeletonSocket duplicate = *source;
    const std::string base = sourceName + " Copy";
    duplicate.name = base;
    for (std::uint32_t suffix = 2U; std::ranges::any_of(candidate.sockets,
             [&duplicate](const kb::scene::SkeletonSocket& socket) { return socket.name == duplicate.name; }); ++suffix) {
        duplicate.name = base + " " + std::to_string(suffix);
    }
    const std::string duplicateName = duplicate.name;
    candidate.sockets.push_back(std::move(duplicate));
    if (!skeletonEditorDocument_.Apply(std::move(candidate)) || !PublishSkeletonEditorWorkingCopy()) return false;
    return skeletalMeshEditorTree_.SelectSocket(duplicateName);
}

bool EditorSceneContext::DeleteSkeletonEditorSocket() {
    if (!CanDeleteSkeletonEditorSocket()) return false;
    const kb::scene::SkeletonAsset* working = skeletonEditorDocument_.WorkingCopy();
    if (working == nullptr) return false;
    kb::scene::SkeletonAsset candidate = *working;
    const std::string name = skeletalMeshEditorTree_.SelectedSocket();
    const auto socket = std::ranges::find_if(candidate.sockets, [&name](const kb::scene::SkeletonSocket& value) {
        return value.name == name;
    });
    if (socket == candidate.sockets.end()) return false;
    candidate.sockets.erase(socket);
    if (!skeletonEditorDocument_.Apply(std::move(candidate)) || !PublishSkeletonEditorWorkingCopy()) return false;
    static_cast<void>(skeletalMeshEditorTree_.ClearSelection());
    return true;
}

bool EditorSceneContext::SetAnimatorControllerAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    if (!entity.IsValid() || !scene_->Components().Animators().Has(entity)) {
        console_.Warning("Animator", "Selected entity does not have an Animator component.");
        return false;
    }
    const kb::assets::AssetMetadata* metadata = assetId.IsValid()
        ? scene_->Assets().Manager().Registry().Find(assetId)
        : nullptr;
    if (assetId.IsValid() && (metadata == nullptr || metadata->type != kb::scene::kAnimatorControllerAssetType)) {
        console_.Warning("Animator", "Only Animator Controller assets can be assigned to an Animator component.");
        return false;
    }
    return ExecuteSceneCommand(assetId.IsValid() ? "Assign Animator Controller" : "Clear Animator Controller", [this, entity, assetId]() {
        kb::scene::Animator* animator = scene_->Components().Animators().TryGet(entity);
        if (animator == nullptr) return false;
        animator->controllerAssetId = assetId.value;
        scene_->Components().Animators().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::SetSkeletonBindingAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    if (!entity.IsValid() || !scene_->Components().SkeletonBindings().Has(entity)) return false;
    kb::scene::SkeletonBindingComponent binding{};
    if (assetId.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
        if (metadata == nullptr || metadata->type != kb::scene::kSkeletonAssetType) {
            console_.Warning("Skeleton Binding", "Only Skeleton assets can be assigned.");
            return false;
        }
        const auto asset = scene_->Assets().Manager().Load<kb::scene::SkeletonAsset>(assetId);
        if (!asset.IsLoaded()) {
            console_.Warning("Skeleton Binding", "Only loadable Skeleton assets can be assigned.");
            return false;
        }
        binding.skeletonAssetId = assetId.value;
        binding.skeletonCompatibilitySignature = kb::scene::SkeletonCompatibilitySignature(*asset);
        binding.enabled = true;
    }
    return ExecuteSceneCommand(assetId.IsValid() ? "Assign Skeleton" : "Clear Skeleton", [this, entity, binding]() {
        return scene_->Components().SkeletonBindings().Set(entity, binding);
    });
}

bool EditorSceneContext::SetDeformedGeometryMeshAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    if (!entity.IsValid() || !scene_->Components().DeformedGeometries().Has(entity)) return false;
    if (assetId.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
        if (metadata == nullptr || metadata->type != kb::scene::kSkeletalMeshAssetType) {
            console_.Warning("Deformed Geometry", "Only Skeletal Mesh assets can be assigned.");
            return false;
        }
    }
    return ExecuteSceneCommand(assetId.IsValid() ? "Assign Skeletal Mesh" : "Clear Skeletal Mesh", [this, entity, assetId]() {
        kb::scene::DrawD3DeformedGeometryComponent* geometry = scene_->Components().DeformedGeometries().TryGet(entity);
        if (geometry == nullptr) return false;
        geometry->skeletalMeshAssetId = assetId.value;
        if (!assetId.IsValid()) {
            geometry->materialSlotAssetIds = {};
            geometry->materialSlotOverrideCount = 0U;
            geometry->poseSource = {};
            geometry->enabled = false;
        } else {
            geometry->enabled = true;
        }
        scene_->Components().DeformedGeometries().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::ToggleSkeletonBindingEnabled(kb::scene::SceneEntity entity) {
    const kb::scene::SkeletonBindingComponent* binding = scene_->Components().SkeletonBindings().TryGet(entity);
    if (binding == nullptr) return false;
    if (!binding->enabled && !kb::scene::IsSkeletonBindingComponentValid(*binding)) {
        console_.Warning("Skeleton Binding", "Assign a valid Skeleton asset before enabling this component.");
        return false;
    }
    return ExecuteSceneCommand("Toggle Skeleton Binding Enabled", [this, entity]() {
        kb::scene::SkeletonBindingComponent* binding = scene_->Components().SkeletonBindings().TryGet(entity);
        if (binding == nullptr) return false;
        binding->enabled = !binding->enabled;
        scene_->Components().SkeletonBindings().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::ToggleDeformedGeometryEnabled(kb::scene::SceneEntity entity) {
    const kb::scene::DrawD3DeformedGeometryComponent* geometry = scene_->Components().DeformedGeometries().TryGet(entity);
    if (geometry == nullptr) return false;
    if (!geometry->enabled && !kb::scene::IsDrawD3DeformedGeometryComponentValid(*geometry)) {
        console_.Warning("Deformed Geometry", "Assign a valid Skeletal Mesh asset before enabling this component.");
        return false;
    }
    return ExecuteSceneCommand("Toggle Deformed Geometry Enabled", [this, entity]() {
        kb::scene::DrawD3DeformedGeometryComponent* geometry = scene_->Components().DeformedGeometries().TryGet(entity);
        if (geometry == nullptr) return false;
        geometry->enabled = !geometry->enabled;
        scene_->Components().DeformedGeometries().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::ToggleDeformedGeometryCastsShadow(kb::scene::SceneEntity entity) {
    return ExecuteSceneCommand("Toggle Deformed Geometry Casts Shadow", [this, entity]() {
        kb::scene::DrawD3DeformedGeometryComponent* geometry = scene_->Components().DeformedGeometries().TryGet(entity);
        if (geometry == nullptr) return false;
        geometry->castsShadow = !geometry->castsShadow;
        scene_->Components().DeformedGeometries().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::ToggleDeformedGeometryReceivesShadow(kb::scene::SceneEntity entity) {
    return ExecuteSceneCommand("Toggle Deformed Geometry Receives Shadow", [this, entity]() {
        kb::scene::DrawD3DeformedGeometryComponent* geometry = scene_->Components().DeformedGeometries().TryGet(entity);
        if (geometry == nullptr) return false;
        geometry->receivesShadow = !geometry->receivesShadow;
        scene_->Components().DeformedGeometries().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::RemoveSkeletonBindingFromEntity(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_->Components().SkeletonBindings().Has(entity)) return false;
    return ExecuteSceneCommand("Remove Skeleton Binding", [this, entity]() { scene_->Components().SkeletonBindings().Remove(entity); return true; });
}

bool EditorSceneContext::RemoveDeformedGeometryFromEntity(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_->Components().DeformedGeometries().Has(entity)) return false;
    return ExecuteSceneCommand("Remove Deformed Geometry", [this, entity]() { scene_->Components().DeformedGeometries().Remove(entity); return true; });
}

bool EditorSceneContext::SetUIDocumentAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
    if (!entity.IsValid() || metadata == nullptr || metadata->type != kb::scene::kUIDocumentAssetType ||
        !scene_->Components().UIDocuments().Has(entity)) {
        console_.Warning("UI", "Only UI Document assets can be assigned to a UI Document component.");
        return false;
    }
    return ExecuteSceneCommand("Assign UI Document", [this, entity, assetId]() {
        kb::scene::UIDocumentComponent* document = scene_->Components().UIDocuments().TryGet(entity);
        if (document == nullptr) return false;
        document->documentAssetId = assetId.value;
        scene_->Components().UIDocuments().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::SetAnimatorSpeed(kb::scene::SceneEntity entity, float speed) {
    if (!std::isfinite(speed) || speed < 0.0F) return false;
    return ExecuteSceneCommand("Edit Animator Speed", [this, entity, speed]() {
        kb::scene::Animator* animator = scene_->Components().Animators().TryGet(entity);
        if (animator == nullptr) return false;
        animator->speed = speed;
        scene_->Components().Animators().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::ToggleAnimatorEnabled(kb::scene::SceneEntity entity) {
    return ExecuteSceneCommand("Toggle Animator Enabled", [this, entity]() {
        kb::scene::Animator* animator = scene_->Components().Animators().TryGet(entity);
        if (animator == nullptr) return false;
        animator->enabled = !animator->enabled;
        scene_->Components().Animators().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::CycleAnimatorRootMotionOwner(kb::scene::SceneEntity entity) {
    const kb::scene::Animator* animator = scene_->Components().Animators().TryGet(entity);
    if (animator == nullptr) return false;

    const bool hasCharacterController = scene_->Components().CharacterControllers().Has(entity);
    const bool hasCollider = scene_->Components().Colliders().Has(entity);
    const kb::scene::RigidbodyComponent* rigidbody = scene_->Components().Rigidbodies().TryGet(entity);
    const bool hasSimulatedRigidbody =
        rigidbody != nullptr &&
        rigidbody->bodyType != kb::scene::RigidbodyBodyType::Static &&
        hasCollider;
    const bool hasCompatibleAnimator =
        !hasCharacterController && !hasSimulatedRigidbody;
    const bool hasCompatibleCharacterController =
        hasCharacterController && rigidbody == nullptr && !hasCollider;
    const bool hasCompatibleRigidbody =
        rigidbody != nullptr &&
        rigidbody->bodyType == kb::scene::RigidbodyBodyType::Kinematic &&
        hasCollider &&
        !hasCharacterController;

    kb::scene::AnimatorRootMotionOwner next = animator->rootMotionOwner;
    do {
        switch (next) {
        case kb::scene::AnimatorRootMotionOwner::None:
            next = kb::scene::AnimatorRootMotionOwner::Animator;
            break;
        case kb::scene::AnimatorRootMotionOwner::Animator:
            next = kb::scene::AnimatorRootMotionOwner::CharacterController;
            break;
        case kb::scene::AnimatorRootMotionOwner::CharacterController:
            next = kb::scene::AnimatorRootMotionOwner::Rigidbody;
            break;
        case kb::scene::AnimatorRootMotionOwner::Rigidbody:
            next = kb::scene::AnimatorRootMotionOwner::None;
            break;
        default:
            console_.Error("Animator", "Animator root-motion owner contains an invalid persisted value.");
            return false;
        }
    } while ((next == kb::scene::AnimatorRootMotionOwner::Animator && !hasCompatibleAnimator) ||
             (next == kb::scene::AnimatorRootMotionOwner::CharacterController && !hasCompatibleCharacterController) ||
             (next == kb::scene::AnimatorRootMotionOwner::Rigidbody && !hasCompatibleRigidbody));

    return ExecuteSceneCommand("Change Animator Root Motion Owner", [this, entity, next]() {
        kb::scene::Animator* mutableAnimator = scene_->Components().Animators().TryGet(entity);
        if (mutableAnimator == nullptr) return false;
        mutableAnimator->rootMotionOwner = next;
        scene_->Components().Animators().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::RemoveAnimatorFromEntity(kb::scene::SceneEntity entity) {
    if (!scene_->Components().Animators().Has(entity)) return false;
    return ExecuteSceneCommand("Remove Animator", [this, entity]() {
        scene_->Components().Animators().Remove(entity);
        return true;
    });
}

bool EditorSceneContext::ToggleUIDocumentEnabled(kb::scene::SceneEntity entity) {
    return ExecuteSceneCommand("Toggle UI Document Enabled", [this, entity]() {
        kb::scene::UIDocumentComponent* document = scene_->Components().UIDocuments().TryGet(entity);
        if (document == nullptr) return false;
        document->enabled = !document->enabled;
        scene_->Components().UIDocuments().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::RemoveUIDocumentFromEntity(kb::scene::SceneEntity entity) {
    if (!scene_->Components().UIDocuments().Has(entity)) return false;
    return ExecuteSceneCommand("Remove UI Document", [this, entity]() {
        scene_->Components().UIDocuments().Remove(entity);
        return true;
    });
}

bool EditorSceneContext::ToggleEntityScriptEnabled(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_->Components().Behaviours().Has(entity)) {
        return false;
    }
    return ExecuteSceneCommand("Toggle Script Enabled", [this, entity]() {
        kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
        if (behaviour == nullptr) {
            return false;
        }
        behaviour->enabled = !behaviour->enabled;
        scene_->Components().Behaviours().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::BeginSelectedTransformEdit(std::string label) {
    if (activeTransformEdit_.Active()) {
        return false;
    }

    const kb::scene::SceneEntity primary = SelectedEntity();
    if (!scene_->Entities().IsAlive(primary)) {
        return false;
    }

    std::vector<kb::scene::SceneEntity> editing = TopLevelSelectedEntities(*scene_, hierarchySelection_.SelectedEntities());
    if (editing.empty()) {
        editing.push_back(primary);
    } else if (!ContainsEntity(editing, primary)) {
        editing.clear();
        editing.push_back(primary);
    }

    std::vector<EditorSceneObjectTransformChange> changes = EditorSceneTransformSnapshotBuilder::Capture(*scene_, editing);
    if (changes.empty()) {
        return false;
    }

    const kb::scene::Vec3 targetStart = EditorSceneSelectionPivot::Resolve(
        *scene_,
        hierarchySelection_.SelectedEntities(),
        primary).value_or(scene_->Transforms().Get(primary).localPosition);
    activeTransformEdit_.Begin(std::move(label), primary, targetStart, std::move(changes));
    return true;
}

bool EditorSceneContext::ApplyActiveTransformEditPrimaryPosition(kb::scene::Vec3 position) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyPrimaryPosition(position);
    return FinalizeActiveTransformEditApply(result.changed, result.touched);
}

bool EditorSceneContext::ApplyActiveTransformEditPrimaryRotation(kb::scene::Vec3 rotation) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyPrimaryRotation(rotation);
    return FinalizeActiveTransformEditApply(result.changed, result.touched);
}

bool EditorSceneContext::ApplyActiveTransformEditRotationDelta(kb::scene::Quat delta) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyRotationDelta(delta);
    return FinalizeActiveTransformEditApply(result.changed, result.touched);
}

bool EditorSceneContext::ApplyActiveTransformEditPrimaryScale(kb::scene::Vec3 scale) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyPrimaryScale(scale);
    return FinalizeActiveTransformEditApply(result.changed, result.touched);
}

bool EditorSceneContext::ApplyActiveTransformEditProperty(InspectorPropertyId property, float value) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyProperty(property, value);
    return FinalizeActiveTransformEditApply(result.changed, result.touched);
}

bool EditorSceneContext::FinalizeActiveTransformEditApply(
    bool changed,
    std::span<const kb::scene::SceneEntity> touched) {
    if (!changed) {
        return false;
    }

    MarkSceneEntitiesRenderDirty(touched);
    // Component overlays consume the canonical world transform cache directly.
    // Keep it current during an interactive edit instead of waiting for commit,
    // so colliders, joints, character controllers, lights, and descendants follow
    // the object on every drag update.
    scene_->Runtime().SynchronizeTransforms();
    return true;
}

float EditorSceneContext::ActiveTransformEditPropertyStart(InspectorPropertyId property) const noexcept {
    return EditorSceneTransformEditController::PropertyStart(activeTransformEdit_, property);
}

bool EditorSceneContext::CommitActiveTransformEdit() {
    if (!activeTransformEdit_.Active()) {
        activeTransformEdit_.Clear();
        return false;
    }

    std::vector<EditorSceneObjectTransformChange> committed = EditorSceneTransformCommitBuilder::Build(*scene_, activeTransformEdit_);
    const std::string label = activeTransformEdit_.LabelOrDefault();
    activeTransformEdit_.Clear();
    if (committed.empty()) {
        return false;
    }

    const std::vector<kb::scene::SceneEntity> touched = EditorSceneTransformCommitBuilder::TouchedEntities(committed);
    commandStack_.PushExecuted(std::make_unique<EditorSceneTransformDeltaCommand>(*this, label, std::move(committed)));
    MarkSceneEntitiesRenderDirty(touched);
    MarkSceneDocumentDirty();
    scene_->Runtime().SynchronizeTransforms();
    return true;
}

void EditorSceneContext::CancelActiveTransformEdit() noexcept {
    if (!activeTransformEdit_.Active()) {
        activeTransformEdit_.Clear();
        return;
    }

    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditApplier::RestoreBefore(*scene_, activeTransformEdit_.Changes());
    activeTransformEdit_.Clear();
    if (result.changed) {
        MarkSceneEntitiesRenderDirty(result.touched);
        scene_->Runtime().SynchronizeTransforms();
    }
}

bool EditorSceneContext::HasActiveTransformEdit() const noexcept {
    return activeTransformEdit_.Active();
}

EditorSceneCommandController EditorSceneContext::SceneCommands() noexcept {
    return EditorSceneCommandController{
        *scene_,
        commandStack_,
        console_,
        viewportState_,
        hierarchySelection_,
        assetBrowser_,
        hierarchyExpansion_,
        hierarchySearch_,
        pendingSceneTransactionLabel_,
        sceneRenderRevision_,
        sceneRenderDirtyBaseRevision_,
        sceneRenderDirtyEntityIds_,
        sceneRenderFullDirty_,
        sceneDocumentDirty_,
        hierarchyRowsDirty_,
    };
}

bool EditorSceneContext::ExecuteSceneCommand(std::string label, std::function<bool()> mutation) {
    return SceneCommands().Execute(std::move(label), std::move(mutation));
}

void EditorSceneContext::ClearSceneDocumentDirty() noexcept {
    sceneDocumentDirty_ = false;
}

void EditorSceneContext::InvalidateHierarchyRows() noexcept {
    hierarchyRowsDirty_ = true;
}

void EditorSceneContext::RebuildHierarchyRowsIfNeeded() const {
    if (!hierarchyRowsDirty_) {
        return;
    }

    hierarchyRowsCache_ = EditorHierarchyRowBuilder::Build(*scene_, hierarchyExpansion_.CollapsedEntities(), hierarchySearch_.Query());
    hierarchyRowsDirty_ = false;
}

void EditorSceneContext::ResetSceneEditState() {
    ClearMaterialEditorWorkingCopyRuntimePreview();
    terrainStroke_.reset();
    terrainReadCache_.reset();
    EditorTerrainService::ToolState().strokeActive = false;
    commandStack_.Clear();
    pendingSceneTransactionLabel_.reset();
    activeTransformEdit_.Clear();
    activeMaterialEditAsset_ = {};
    activeMaterialEditProperty_ = InspectorPropertyId::None;
    activeMaterialEditBefore_.reset();
    editorSceneParticleAccumulatorSeconds_ = 0.0;
    CancelHierarchyRename();
    inspector_.EndTextEdit();
    MarkSceneRenderDirty();
    scene_->Runtime().SynchronizeTransforms();
}

void EditorSceneContext::SelectFirstSceneEntityOrClear() noexcept {
    // A scene was (re)loaded or seeded: its referenced graph materials must be (re)cooked (MAT-84).
    sceneGraphCookPending_ = true;
    const std::vector<kb::scene::SceneEntity> roots = scene_->Hierarchy().RootEntities();
    if (roots.empty()) {
        hierarchySelection_.Clear();
        return;
    }
    hierarchySelection_.SelectEntity(roots.front());
    assetBrowser_.ClearSelection();
}

std::filesystem::path EditorSceneContext::ResolveProjectVirtualPath(const std::filesystem::path& virtualPath) const {
    if (const std::optional<std::filesystem::path> physical = scene_->Assets().Manager().Mounts().Resolve(virtualPath)) {
        return *physical;
    }
    return EditorProjectPaths::DefaultScenePath();
}

std::filesystem::path EditorSceneContext::ResolveDefaultScenePath() const {
    const std::filesystem::path defaultScene = projectConfig_.defaultMap.empty()
        ? std::filesystem::path{ "/Game/Scenes/Main.21kbscene" }
        : std::filesystem::path{ projectConfig_.defaultMap };
    return ResolveProjectVirtualPath(defaultScene);
}

} // namespace kb::editor
