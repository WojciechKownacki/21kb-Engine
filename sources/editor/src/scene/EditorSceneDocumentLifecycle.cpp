#include "scene/EditorSceneContext.hpp"
#include "platform/win32/EditorMap2DWindow.hpp"

#include "app/EditorPlayModeSceneSession.hpp"
#include "diagnostics/EditorLagTrace.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/audio/EditorSceneAudioSettingsService.hpp"
#include "scene/material_preview/EditorMaterialPreviewScene.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "engine/scene/SceneLightProbeAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptModule.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "engine/assets/AssetImportPreset.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialParameterValidation.hpp"
#include "project/EditorProjectPaths.hpp"
#include "engine/scene/SceneTemplates.hpp"
#include "scene/EditorPlayCameraResolver.hpp"
#include "scene/EditorSceneDocumentAssetLoaders.hpp"
#include "scene/EditorMap2DService.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

constexpr std::string_view kSceneDocumentExtension = ".21kbscene";

class ScopedPlayTransitionTrace final {
public:
    explicit ScopedPlayTransitionTrace(std::string_view totalDetail) noexcept
        : eventId_(diagnostics::EditorLagTrace::NextEventId())
        , totalDetail_(totalDetail)
        , started_(std::chrono::steady_clock::now()) {}

    ~ScopedPlayTransitionTrace() {
        diagnostics::EditorLagTrace::Slow(
            "play-mode-transition", eventId_, ElapsedMs(started_), totalDetail_);
    }

    void Phase(
        std::chrono::steady_clock::time_point started,
        std::string_view detail) const noexcept {
        diagnostics::EditorLagTrace::Slow(
            "play-mode-transition", eventId_, ElapsedMs(started), detail);
    }

private:
    [[nodiscard]] static double ElapsedMs(
        std::chrono::steady_clock::time_point started) noexcept {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    }

    std::uint64_t eventId_ = 0U;
    std::string_view totalDetail_;
    std::chrono::steady_clock::time_point started_{};
};

class ScopedSceneSaveTrace final {
public:
    explicit ScopedSceneSaveTrace(std::string detail) noexcept
        : eventId_(diagnostics::EditorLagTrace::NextEventId())
        , detail_(std::move(detail))
        , started_(std::chrono::steady_clock::now()) {}

    ~ScopedSceneSaveTrace() {
        diagnostics::EditorLagTrace::Slow(
            "scene-save", eventId_, ElapsedMs(started_), detail_, 0.0);
    }

    void Phase(
        std::chrono::steady_clock::time_point started,
        std::string_view detail) const noexcept {
        diagnostics::EditorLagTrace::Slow(
            "scene-save", eventId_, ElapsedMs(started), detail, 0.0);
    }

private:
    [[nodiscard]] static double ElapsedMs(
        std::chrono::steady_clock::time_point started) noexcept {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    }

    std::uint64_t eventId_ = 0U;
    std::string detail_;
    std::chrono::steady_clock::time_point started_{};
};

[[nodiscard]] bool HasSceneBehaviours(const kb::scene::Scene& scene) {
    bool found = false;
    scene.Components().Behaviours().ForEach(
        [](kb::scene::SceneEntity, const kb::scene::BehaviourComponent&, void* opaque) {
            *static_cast<bool*>(opaque) = true;
        },
        &found);
    return found;
}

[[nodiscard]] std::string AssetErrorOr(const kb::assets::AssetManager& manager, const char* fallback) {
    const std::string error = manager.LastError();
    return error.empty() ? std::string{ fallback } : error;
}

[[nodiscard]] std::filesystem::path EnsureSceneDocumentExtension(std::filesystem::path path) {
    if (path.extension() != kSceneDocumentExtension) {
        path.replace_extension(kSceneDocumentExtension);
    }
    return path;
}

} // namespace

void RegisterEditorSceneDocumentAssetLoaders(kb::scene::Scene& scene) {
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialGraphAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialInstanceAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialParameterCollectionAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialTypeAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::PostProcessProfileAssetLoader>()));
    // Import presets (.kbimportpreset): Project Files lists them and the import dialog offers them.
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::assets::AssetImportPresetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()));
    kb::render::InstallRuntimeMaterialParameterValidation(scene);
    // ParticleEffectAssetLoader is registered unconditionally by every kb::scene::Scene's own
    // constructor (Scene.cpp, mirrors PhysicsLayersAssetLoader's precedent). The built-in
    // particle quad mesh needs no registration at all - see BuiltInParticleQuadMesh.hpp's own
    // doc comment for why it deliberately bypasses AssetRegistry entirely.
}

bool EditorSceneContext::SaveDirtySceneDocument(std::string_view reason) {
    if (!sceneDocumentDirty_) {
        return true;
    }
    if (!SaveCurrentScene()) {
        console_.Error("Project", "Dirty scene save failed before " + std::string{ reason } + ".");
        return false;
    }
    return true;
}

void EditorSceneContext::DiscardDirtySceneDocument(std::string_view reason) {
    if (!sceneDocumentDirty_) {
        return;
    }
    sceneDocumentDirty_ = false;
    console_.Warning("Project", "Unsaved scene changes discarded before " + std::string{ reason } + ".");
}

void EditorSceneContext::SetRenderSceneReleaseHandler(
    std::function<void(const kb::scene::Scene&)> handler) {
    renderSceneReleaseHandler_ = std::move(handler);
    timelineEditorPreview_.SetReleaseHandler(renderSceneReleaseHandler_);
    SynchronizeBlendSpace2DReleaseHandler();
    // The material previews render through the same renderer as the main scene.
    for (EditorMaterialPreviewScene* preview : { materialPreviewScene_.get(), inspectorMaterialPreviewScene_.get(), materialThumbnailScene_.get() }) {
        if (preview != nullptr) {
            preview->SetReleaseHandler(renderSceneReleaseHandler_);
        }
    }
}

void EditorSceneContext::ReleaseRenderedSceneResources() {
    ClearGameplayGraphPreview();
    ClearBlendSpace2DPreview();
    ClearTimelineEditorPreview();
    if (renderSceneReleaseHandler_) {
        renderSceneReleaseHandler_(*scene_);
    }
}

bool EditorSceneContext::PrepareDirtySceneTransition(std::string_view reason, EditorDirtySceneResolution resolution) {
    if (!sceneDocumentDirty_) {
        return true;
    }

    if (resolution == EditorDirtySceneResolution::Discard) {
        console_.Warning("Project", "Unsaved scene changes discarded before " + std::string{ reason } + ".");
        return true;
    }

    return SaveDirtySceneDocument(reason);
}

bool EditorSceneContext::BeginPlayModeSceneSession() {
    PauseTimelineEditorPreview();
    static_cast<void>(PlayBlendSpace2DPreview(false));
    ScopedPlayTransitionTrace trace{"transition=begin phase=total"};
    if (playModeSceneSession_.Active()) {
        return true;
    }
    if (RejectWhilePrefabEditing()) {
        return false;
    }
#if defined(_WIN32)
    if (!EditorMap2DWindow::SaveAll(*this)) return false;
    EditorMap2DWindow::FinishSceneStroke(*this);
#endif
    EditorMap2DService::ClearPreviews(*scene_);
    static_cast<void>(CommitHierarchyRename());
    inspector_.EndTextEdit();
    static_cast<void>(CommitActiveTransformEdit());
    if (pendingSceneTransactionLabel_.has_value()) static_cast<void>(CommitSceneEditTransaction());
    auto phaseStarted = std::chrono::steady_clock::now();
    if (plugins_.HasPendingReload() && !ReloadSceneFromProject()) {
        return false;
    }
    trace.Phase(phaseStarted, "transition=begin phase=pending-plugin-reload");
    // Play runs the scene as it is in the editor, saved or not: the snapshot below
    // brings the edited scene back afterwards, still unsaved when it was.
    playModeDocumentWasDirty_ = sceneDocumentDirty_;
    kb::audio::AudioPlayback::StopAll(*scene_);

    // Asset authoring paths publish or invalidate the edited asset at the mutation
    // boundary. Entering Play must not rescan and re-hash every mounted file.
    phaseStarted = std::chrono::steady_clock::now();
    if (!ActivateProjectPhysicsLayers(*scene_)) {
        return false;
    }
    scene_->Runtime().ApplyProjectSimulation(projectConfig_.simulation);
    static_cast<void>(kb::audio::AudioPlayback::SetMasterVolume(*scene_, projectConfig_.masterVolume));
    trace.Phase(phaseStarted, "transition=begin phase=physics-layers");

    phaseStarted = std::chrono::steady_clock::now();
    const std::string name = SceneDocumentName(currentScenePath_);
    playModeSelectionSnapshot_.CaptureAuthoredHierarchy(*scene_);
    playModeRenderTopologyVersion_ = 0U;
    playModeRenderTopologyVersionInitialized_ = false;
    worldPartition_.PrepareSceneReload();
    if (!playModeSceneSession_.Begin(*scene_, name)) {
        playModeSelectionSnapshot_.Clear();
        console_.Error("Play Mode", "Scene snapshot could not be captured.");
        return false;
    }
    trace.Phase(phaseStarted, "transition=begin phase=capture-snapshot");
    playModeDocumentCommands_.Clear();
    commandStack_.SwapHistory(EditorCommandHistoryKey::Scene(), playModeDocumentCommands_);
    phaseStarted = std::chrono::steady_clock::now();
    // Establish the complete play-session state before attaching the Script
    // module. On its first attachment the module immediately runs
    // Created/Activated/Ready, so clearing mapping contexts or Lua instances
    // afterwards invalidates state that Ready legitimately initialized.
    ResetScriptRuntimeStateForPlayMode();
    trace.Phase(phaseStarted, "transition=begin phase=reset-script-state");
    phaseStarted = std::chrono::steady_clock::now();
    kb::scene::SceneInputActivation::Apply(*scene_);
    ActivateProjectInput();
    trace.Phase(phaseStarted, "transition=begin phase=input-setup");
    phaseStarted = std::chrono::steady_clock::now();
    ReleaseEditModeScripts();
    if (HasSceneBehaviours(*scene_)) {
        EnsureScriptRuntime();
    }
    editorSceneParticleAccumulatorSeconds_ = 0.0;
    trace.Phase(phaseStarted, "transition=begin phase=script-runtime");
    playCameraEntity_ = EditorPlayCameraResolver::Resolve(*scene_);
    // Lifecycle callbacks may create entities or modify renderable state before
    // the first runtime tick. Establish one complete Play snapshot; subsequent
    // frames use compact runtime proxy updates until topology changes again.
    MarkSceneRenderDirty();
    console_.Info("Play Mode", "Captured editor scene snapshot.");
    return true;
}

bool EditorSceneContext::RestorePlayModeSceneSession() {
    ScopedPlayTransitionTrace trace{"transition=restore phase=total"};
    if (!playModeSceneSession_.Active()) {
        return true;
    }
    playModeSelectionSnapshot_.CaptureSelection(hierarchySelection_);
    auto phaseStarted = std::chrono::steady_clock::now();
    // Fire each behaviour's shutdown hook while the play scene is still live,
    // before reverting to the snapshot. Surface handler errors to the Console.
    if (scriptModule_ != nullptr && scriptModule_->Host() != nullptr) {
        static_cast<void>(scriptModule_->Host()->DispatchShutdownLifecycle(0.0F));
        SurfaceScriptDiagnostics();
    }
    trace.Phase(phaseStarted, "transition=restore phase=script-shutdown");
    kb::audio::AudioPlayback::StopAll(*scene_);
    kb::scene::SceneInputActivation::Clear(*scene_);
    phaseStarted = std::chrono::steady_clock::now();
    if (!playModeSceneSession_.Restore(*scene_)) {
        playModeSelectionSnapshot_.Clear();
        console_.Error("Play Mode", "Editor scene snapshot could not be restored.");
        return false;
    }
    // The restore recreated every root in order; keep each one bound to its world object.
    worldPartition_.CompleteSceneReload();
    trace.Phase(phaseStarted, "transition=restore phase=restore-snapshot");
    phaseStarted = std::chrono::steady_clock::now();
    ReleaseRenderedSceneResources();

    playModeSelectionSnapshot_.Restore(*scene_, hierarchySelection_);
    playModeSelectionSnapshot_.Clear();
    ResetSceneEditState(false);
    commandStack_.SwapHistory(EditorCommandHistoryKey::Scene(), playModeDocumentCommands_);
    commandStack_.RemapEntities(EditorCommandHistoryKey::Scene(), playModeSceneSession_.RestoredEntities());
    ClearSceneDocumentDirty();
    if (playModeDocumentWasDirty_) {
        MarkSceneDocumentDirty();
    }
    playModeDocumentWasDirty_ = false;
    playCameraEntity_ = {};
    playModeRenderTopologyVersion_ = 0U;
    playModeRenderTopologyVersionInitialized_ = false;
    editorSceneParticleAccumulatorSeconds_ = 0.0;
    trace.Phase(phaseStarted, "transition=restore phase=editor-reset");
    console_.Info("Play Mode", "Restored editor scene snapshot.");
    return true;
}

bool EditorSceneContext::HasPlayModeSceneSession() const noexcept {
    return playModeSceneSession_.Active();
}

kb::scene::SceneEntity EditorSceneContext::PlayCameraEntity() const noexcept {
    if (!playModeSceneSession_.Active() ||
        !scene_->Entities().IsAlive(playCameraEntity_)) {
        return {};
    }
    const kb::scene::CameraComponent* camera =
        scene_->Components().Cameras().TryGet(playCameraEntity_);
    return camera != nullptr && camera->primary &&
            kb::scene::ResolveVisibility(*scene_, playCameraEntity_).visible
        ? playCameraEntity_
        : kb::scene::SceneEntity{};
}

bool EditorSceneContext::ReloadSceneFromProject() {
    if (RejectWhilePrefabEditing() || !RestorePlayModeSceneSession()) {
        return false;
    }
    // A partitioned world reopens from its files, so it is saved first; a scene is carried over as
    // edited - saved or not - and stays unsaved when it was.
    const bool reloadFromWorld = worldPartition_.IsOpen();
    if (reloadFromWorld && !SaveDirtySceneDocument("reloading project plugins")) {
        return false;
    }
    const bool documentWasDirty = sceneDocumentDirty_ && !reloadFromWorld;
    // Unsaved edits are carried over from memory; a scene without any is read from its file as before.
    std::optional<kb::scene::SceneDocument> edited;
    if (documentWasDirty) {
        edited = kb::scene::SceneDocumentService::Capture(*scene_, SceneDocumentName(currentScenePath_));
    }

    DropEditModeScripts();
    if (scriptModuleHost_ != nullptr) {
        scriptModuleHost_->DetachScene(*scene_);
        scriptModuleHost_->Unload();
        scriptModuleHost_.reset();
        scriptModule_ = nullptr;
    }

    auto nextScene = std::make_unique<kb::scene::Scene>(project_);
    if (nextScene->Assets().MountProject(EditorProjectPaths::ProjectRoot())) {
        console_.Info("Project", "Mounted project assets.");
    } else {
        console_.Error("Project", AssetErrorOr(nextScene->Assets().Manager(), "Project assets could not be mounted."));
        return false;
    }
    RegisterEditorSceneDocumentAssetLoaders(*nextScene);
    const std::size_t discovered = nextScene->Assets().Discover();
    console_.Info("Assets", "Asset discovery completed. Found " + std::to_string(discovered) + " asset(s).");
    if (!ActivateProjectPhysicsLayers(*nextScene)) {
        return false;
    }
    nextScene->Runtime().ApplyProjectSimulation(projectConfig_.simulation);
    ApplyProjectAudioAndTags(*nextScene);

    kb::scene::SceneDocumentLoadResult loaded;
    if (worldPartition_.IsOpen()) {
        // The world was saved above; reopen it in the new scene with the same cells loaded.
        const std::vector<kb::world::WorldCellCoord> cells = worldPartition_.Session().LoadedCells();
        std::string error;
        worldPartition_.Close();
        if (!worldPartition_.Open(*nextScene, currentScenePath_, error)) {
            console_.Error("World", "World could not be reopened: " + error);
            return false;
        }
        for (const kb::world::WorldCellCoord& cell : cells) {
            static_cast<void>(worldPartition_.Session().LoadRegion(cell, cell, error));
        }
    } else if (edited.has_value()) {
        loaded.document = std::move(*edited);
        loaded.succeeded = true;
        if (!kb::scene::SceneDocumentService::LoadIntoScene(*nextScene, loaded.document)) {
            console_.Error("Project", "Scene could not be rebuilt with the new plugin settings: " +
                currentScenePath_.generic_string() + ". The scene file was not changed.");
            return false;
        }
    } else if (!currentScenePath_.empty()) {
        loaded = kb::scene::SceneDocumentService::Load(currentScenePath_);
        if (!loaded.succeeded) {
            console_.Error("Project", "Scene could not be reloaded: " + currentScenePath_.generic_string() + " could not be read" +
                (loaded.error.empty() ? std::string{ "." } : ": " + loaded.error));
            return false;
        }
        if (!kb::scene::SceneDocumentService::LoadIntoScene(*nextScene, loaded.document)) {
            console_.Error("Project", "Scene could not be reloaded: the content of " + currentScenePath_.generic_string() +
                " could not be built into the scene.");
            return false;
        }
    }
    EditorSceneAudioSettingsService::PrepareDocument(*nextScene);

    ReleaseRenderedSceneResources();
    if (documentScene_) EditorMap2DService::ClearPreviews(*documentScene_);
    documentScene_ = std::move(nextScene);
    scene_ = documentScene_.get();
    AdvanceSceneDocumentGeneration();
    plugins_.ClearPendingReload();
    AdoptSceneTagsIntoProject();
    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    ClearSceneDocumentDirty();
    if (documentWasDirty) {
        MarkSceneDocumentDirty();
    }
    console_.Info("Project", "Reloaded scene with current project plugin settings.");
    UpgradePrefabLinksOnSave(loaded.document);
    CompleteLoadedUIComponents();
    return true;
}

bool EditorSceneContext::ReloadSceneAfterExternalChange() {
    if (RejectWhilePrefabEditing() || !RestorePlayModeSceneSession()) {
        return false;
    }
    EditorPlayModeSelectionSnapshot paths;
    paths.CaptureAuthoredHierarchy(*scene_);
    paths.CaptureSelection(hierarchySelection_);

    // New or removed plugins and modules need a scene built with them: the whole project reloads.
    const kb::project::ProjectDescriptorReadResult onDisk = kb::project::ProjectManager::LoadProject(ProjectFile());
    const auto plugins = [](const kb::project::ProjectDescriptor& descriptor) {
        std::vector<std::pair<std::string, bool>> enabled;
        for (const kb::project::ProjectPluginReference& plugin : descriptor.plugins) enabled.emplace_back(plugin.name, plugin.enabled);
        for (const kb::project::ProjectModuleDescriptor& module : descriptor.modules) enabled.emplace_back(module.name, true);
        return enabled;
    };
    if (worldPartition_.IsOpen() || currentScenePath_.empty() || (onDisk.succeeded && plugins(onDisk.descriptor) != plugins(project_))) {
        if (onDisk.succeeded) project_ = onDisk.descriptor;
        if (!ReloadSceneFromProject()) return false;
        paths.Restore(*scene_, hierarchySelection_);
        return true;
    }

    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(currentScenePath_);
    if (!loaded.succeeded) {
        console_.Error("Project", "Scene could not be reloaded: " + currentScenePath_.generic_string() +
            (loaded.error.empty() ? std::string{} : ": " + loaded.error));
        return false;
    }
    // Assets the tool added become known, and changed ones load again from their files.
    static_cast<void>(scene_->Assets().Discover());
    scene_->Assets().Manager().ClearRuntimeCache();
    ReleaseRenderedSceneResources();

    // Puts a document into the open scene with the scene's history set aside meanwhile, then lets
    // the earlier undo steps and the selection follow the objects still there (same place in the
    // hierarchy, same name), as leaving Play does.
    const auto apply = [this](const kb::scene::SceneDocument& document) {
        EditorPlayModeSelectionSnapshot before;
        before.CaptureAuthoredHierarchy(*scene_);
        before.CaptureSelection(hierarchySelection_);
        std::unordered_map<std::uint64_t, std::string> names;
        for (std::vector<kb::scene::SceneEntity> pending = scene_->Hierarchy().RootEntities(); !pending.empty();) {
            const kb::scene::SceneEntity entity = pending.back();
            pending.pop_back();
            names.emplace(entity.Id(), scene_->Entities().Name(entity));
            for (const kb::scene::SceneEntity child : scene_->Hierarchy().ChildEntities(entity)) pending.push_back(child);
        }
        scene_->History().Commit();
        kb::scene::SceneHistorySnapshot history = scene_->History().Suspend();
        const bool loaded = kb::scene::SceneDocumentService::LoadIntoScene(*scene_, document);
        std::vector<kb::scene::SceneEntityRemap> remap = before.RecreatedEntities(*scene_);
        std::erase_if(remap, [&](const kb::scene::SceneEntityRemap& entry) {
            const auto name = names.find(entry.from.Id());
            return name == names.end() || name->second != scene_->Entities().Name(entry.to);
        });
        const bool resumed = scene_->History().Resume(history, remap);
        commandStack_.RemapEntities(EditorCommandHistoryKey::Scene(), remap);
        if (!loaded || !resumed) return false;
        EditorSceneAudioSettingsService::PrepareDocument(*scene_);
        AdoptSceneTagsIntoProject();
        before.Restore(*scene_, hierarchySelection_);
        ReleaseRenderedSceneResources();
        CompleteLoadedUIComponents();
        return true;
    };
    class ExternalChangeCommand final : public IEditorCommand {
    public:
        ExternalChangeCommand(std::function<bool(const kb::scene::SceneDocument&)> apply, kb::scene::SceneDocument before, kb::scene::SceneDocument after)
            : apply_(std::move(apply)), before_(std::move(before)), after_(std::move(after)) {}
        [[nodiscard]] std::string_view Label() const noexcept override { return "Agent changes"; }
        [[nodiscard]] bool Execute() override { return apply_(after_); }
        [[nodiscard]] bool Undo() override { return apply_(before_); }
        [[nodiscard]] bool Redo() override { return apply_(after_); }

    private:
        std::function<bool(const kb::scene::SceneDocument&)> apply_;
        kb::scene::SceneDocument before_;
        kb::scene::SceneDocument after_;
    };
    kb::scene::SceneDocument current = kb::scene::SceneDocumentService::Capture(*scene_, SceneDocumentName(currentScenePath_));
    if (!ExecuteSceneCommand(std::make_unique<ExternalChangeCommand>(apply, std::move(current), loaded.document))) {
        console_.Error("Project", "Scene could not be rebuilt from " + currentScenePath_.generic_string() + ".");
        return false;
    }
    AdvanceSceneDocumentGeneration();
    // The open scene is what its file says now; undoing the step makes it differ again.
    ClearSceneDocumentDirty();
    return true;
}

bool EditorSceneContext::NewScene(EditorDirtySceneResolution dirtyResolution, kb::scene::SceneTemplate sceneTemplate) {
    if (RejectWhilePrefabEditing() || !RestorePlayModeSceneSession()) {
        return false;
    }
    if (!PrepareDirtySceneTransition("creating a new scene", dirtyResolution)) {
        return false;
    }
    worldPartition_.Close();

    const std::vector<kb::scene::SceneEntity> roots = scene_->Hierarchy().RootEntities();
    for (const kb::scene::SceneEntity root : roots) {
        scene_->Entities().Destroy(root);
    }
    EditorSceneAudioSettingsService::ResetForNewDocument(*scene_);
    ReleaseRenderedSceneResources();

    if (std::string filled = kb::scene::SceneTemplates::Fill(*scene_, sceneTemplate, Project()); !filled.empty()) {
        console_.Error("Project", filled);
    }
    SelectFirstSceneEntityOrClear();
    currentScenePath_ = EditorProjectPaths::UniqueScenePath("Untitled");
    AdvanceSceneDocumentGeneration();
    ResetSceneEditState();
    MarkSceneDocumentDirty();
    console_.Info("Project", "New scene created: " + currentScenePath_.generic_string());
    return true;
}

std::string EditorSceneContext::NewScenePathError(std::string_view folder, std::string_view name) const {
    return kb::scene::SceneTemplates::ResolveNewScene(EditorProjectPaths::ProjectRoot(), folder, name).error;
}

bool EditorSceneContext::CreateScene(std::string_view folder, std::string_view name, kb::scene::SceneTemplate sceneTemplate,
    bool setAsStartupScene, EditorDirtySceneResolution dirtyResolution) {
    if (RejectWhilePrefabEditing() || !RestorePlayModeSceneSession()) {
        return false;
    }
    const kb::scene::NewScenePath target =
        kb::scene::SceneTemplates::ResolveNewScene(EditorProjectPaths::ProjectRoot(), folder, name);
    if (!target.error.empty()) {
        console_.Error("Project", "Scene could not be created: " + target.error);
        return false;
    }
    // The open scene is settled first, so a cancelled or failed save leaves no new file behind.
    if (!PrepareDirtySceneTransition("creating a new scene", dirtyResolution)) {
        return false;
    }
    if (std::string created = kb::scene::SceneTemplates::WriteSceneFile(target.file, sceneTemplate, Project()); !created.empty()) {
        console_.Error("Project", "Scene could not be created: " + created);
        return false;
    }
    static_cast<void>(scene_->Assets().Discover());
    if (!OpenScene(target.file, EditorDirtySceneResolution::Discard)) {
        return false;
    }
    if (setAsStartupScene) {
        projectConfig_.defaultMap = target.virtualPath;
        static_cast<void>(SaveProjectConfiguration());
    }
    console_.Info("Project", "Scene created: " + target.virtualPath + " (template " +
        std::string{ kb::scene::SceneTemplates::Name(sceneTemplate) } + ")" +
        (setAsStartupScene ? std::string{ "; the game now starts in it." } : std::string{ "." }));
    return true;
}

bool EditorSceneContext::RenameOpenScene(std::string_view name) {
    if (RejectWhilePrefabEditing()) {
        return false;
    }
    if (playModeSceneSession_.Active() || worldPartition_.IsOpen()) {
        console_.Warning("Project", "Stop play mode and close the world before renaming a scene.");
        return false;
    }
    if (std::string problem = kb::assets::AssetFileNameError(name); !problem.empty()) {
        console_.Error("Project", "Scene could not be renamed: " + problem);
        return false;
    }
    const std::string fileName = std::string{ name } + currentScenePath_.extension().string();
    const std::filesystem::path target = currentScenePath_.parent_path() / kb::assets::Utf8Path(fileName);
    std::error_code error;
    if (std::filesystem::exists(target, error)) {
        console_.Error("Project", "A scene named " + std::string{ name } + " already exists there. Choose another name.");
        return false;
    }
    static_cast<void>(scene_->Assets().Discover());
    kb::assets::AssetManager& assets = scene_->Assets().Manager();
    const std::optional<std::filesystem::path> virtualPath = assets.Mounts().ToVirtual(currentScenePath_);
    const kb::assets::AssetMetadata* metadata = virtualPath.has_value() ? assets.Registry().FindByPath(*virtualPath) : nullptr;
    if (metadata == nullptr) {
        console_.Error("Project", "Only a saved scene of this project can be renamed.");
        return false;
    }
    const bool startup = std::filesystem::weakly_canonical(currentScenePath_, error) == std::filesystem::weakly_canonical(ResolveDefaultScenePath(), error);
    if (!assets.RenameAsset(metadata->id, std::string{ name })) {
        console_.Error("Project", "Scene could not be renamed: " + assets.LastError());
        return false;
    }
    currentScenePath_ = target;
    RememberLastOpenMap();
    const std::string renamedVirtual = (virtualPath->parent_path() / kb::assets::Utf8Path(fileName)).generic_string();
    const std::string previousVirtual = virtualPath->generic_string();
    // The scenes listed for the package follow the file to its new name.
    const auto listed = std::ranges::find(projectConfig_.additionalScenes, previousVirtual);
    if (listed != projectConfig_.additionalScenes.end()) {
        *listed = renamedVirtual;
    }
    if (startup) {
        projectConfig_.defaultMap = renamedVirtual;
    }
    if (startup || listed != projectConfig_.additionalScenes.end()) {
        static_cast<void>(SaveProjectConfiguration());
    }
    console_.Info("Project", "Scene renamed to " + fileName + ".");
    return true;
}

bool EditorSceneContext::DuplicateOpenScene() {
    if (RejectWhilePrefabEditing()) {
        return false;
    }
    if (playModeSceneSession_.Active() || worldPartition_.IsOpen()) {
        console_.Warning("Project", "Stop play mode and close the world before duplicating a scene.");
        return false;
    }
    const std::filesystem::path folder = currentScenePath_.parent_path();
    const std::u8string stem = currentScenePath_.stem().u8string();
    const std::string base = std::string{ reinterpret_cast<const char*>(stem.data()), stem.size() } + " Copy";
    std::filesystem::path copy = folder / kb::assets::Utf8Path(base + ".21kbscene");
    std::error_code error;
    for (int number = 2; std::filesystem::exists(copy, error); ++number) {
        copy = folder / kb::assets::Utf8Path(base + " " + std::to_string(number) + ".21kbscene");
    }
    if (!kb::scene::SceneDocumentService::Save(*scene_, copy, SceneDocumentName(copy))) {
        console_.Error("Project", "The scene copy could not be written: " + copy.generic_string());
        return false;
    }
    static_cast<void>(scene_->Assets().Discover());
    console_.Info("Project", "Scene copied to " + copy.generic_string());
    return true;
}

bool EditorSceneContext::DeleteOpenScene() {
    if (RejectWhilePrefabEditing()) {
        return false;
    }
    if (playModeSceneSession_.Active() || worldPartition_.IsOpen()) {
        console_.Warning("Project", "Stop play mode and close the world before deleting a scene.");
        return false;
    }
    static_cast<void>(scene_->Assets().Discover());
    kb::assets::AssetManager& assets = scene_->Assets().Manager();
    std::error_code error;
    const std::filesystem::path doomed = std::filesystem::weakly_canonical(currentScenePath_, error);
    const std::optional<std::filesystem::path> doomedVirtual = assets.Mounts().ToVirtual(currentScenePath_);
    const kb::assets::AssetMetadata* metadata = doomedVirtual.has_value() ? assets.Registry().FindByPath(*doomedVirtual) : nullptr;
    if (metadata == nullptr) {
        console_.Error("Project", "Only a saved scene of this project can be deleted.");
        return false;
    }
    const kb::assets::AssetId id = metadata->id;
    const std::filesystem::path startupScene = std::filesystem::weakly_canonical(ResolveDefaultScenePath(), error);

    // The startup scene when that is another one, or else the first other scene.
    std::string nextVirtual;
    std::filesystem::path nextPath;
    for (const std::string& candidate : ProjectScenePaths()) {
        const std::optional<std::filesystem::path> resolved = assets.Mounts().Resolve(std::filesystem::path{ candidate });
        if (!resolved.has_value()) {
            continue;
        }
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(*resolved, error);
        if (canonical == doomed || !std::filesystem::is_regular_file(canonical, error)) {
            continue;
        }
        if (nextPath.empty() || canonical == startupScene) {
            nextVirtual = candidate;
            nextPath = *resolved;
        }
    }
    if (nextPath.empty()) {
        console_.Warning("Project", "This is the project's only scene, so it stays. Create another one first.");
        return false;
    }
    const bool startup = doomed == startupScene;
    if (!OpenScene(nextPath, EditorDirtySceneResolution::Discard)) {
        return false;
    }
    // A deleted scene leaves the package list too.
    const std::string doomedVirtualPath = doomedVirtual->generic_string();
    const std::size_t listedBefore = projectConfig_.additionalScenes.size();
    std::erase(projectConfig_.additionalScenes, doomedVirtualPath);
    if (startup) {
        projectConfig_.defaultMap = nextVirtual;
        std::erase(projectConfig_.additionalScenes, nextVirtual);
    }
    if (startup || projectConfig_.additionalScenes.size() != listedBefore) {
        static_cast<void>(SaveProjectConfiguration());
    }
    return DeleteAssetBrowserItem(id);
}

// The scene document's name: the file's name in UTF-8 (any letters), "Main" for a path without one.
std::string EditorSceneContext::SceneDocumentName(const std::filesystem::path& scenePath) {
    const std::u8string stem = scenePath.stem().u8string();
    return stem.empty() ? std::string{ "Main" } : std::string{ reinterpret_cast<const char*>(stem.data()), stem.size() };
}

bool EditorSceneContext::OpenDefaultScene() {
    return OpenScene(ResolveDefaultScenePath());
}

bool EditorSceneContext::OpenScene(const std::filesystem::path& path, EditorDirtySceneResolution dirtyResolution) {
    if (path.extension() == kb::world::WorldDescriptor::Extension) {
        return OpenWorld(path, dirtyResolution);
    }
    if (RejectWhilePrefabEditing() || !RestorePlayModeSceneSession()) {
        return false;
    }
    if (!PrepareDirtySceneTransition("opening a scene", dirtyResolution)) {
        return false;
    }

    const std::filesystem::path scenePath = EnsureSceneDocumentExtension(path);

    const auto loaded = kb::scene::SceneDocumentService::Load(scenePath);
    if (!loaded.succeeded) {
        console_.Error("Project", "Scene could not be opened: " + scenePath.generic_string() + ": " + loaded.error);
        return false;
    }
    worldPartition_.Close();
    // Loading replaces the open scene's objects before it can fail; the scene that was open comes
    // back then, so a later save can never write an empty scene over its file.
    const kb::scene::SceneDocument previous = kb::scene::SceneDocumentService::Capture(*scene_, SceneDocumentName(currentScenePath_));
    if (!kb::scene::SceneDocumentService::LoadIntoScene(*scene_, loaded.document)) {
        const bool restored = kb::scene::SceneDocumentService::LoadIntoScene(*scene_, previous);
        console_.Error("Project", "Scene could not be instantiated: " + scenePath.generic_string() +
            (restored ? std::string{ ". The scene that was open is still open." } : std::string{}));
        if (!restored) {
            for (const kb::scene::SceneEntity root : scene_->Hierarchy().RootEntities()) {
                scene_->Entities().Destroy(root);
            }
            StartUntitledBasicScene();
        }
        ReleaseRenderedSceneResources();
        AdvanceSceneDocumentGeneration();
        return false;
    }
    EditorSceneAudioSettingsService::PrepareDocument(*scene_);
    ReleaseRenderedSceneResources();

    currentScenePath_ = scenePath;
    kb::scene::SceneLightProbeAccess::AssignBakedGrid(*scene_, scenePath.stem().generic_string());
    RememberLastOpenMap();
    AdoptSceneTagsIntoProject();
    AdvanceSceneDocumentGeneration();
    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    ClearSceneDocumentDirty();
    console_.Info("Project", "Opened scene: " + currentScenePath_.generic_string());
    UpgradePrefabLinksOnSave(loaded.document);
    CompleteLoadedUIComponents();
    return true;
}

bool EditorSceneContext::RejectWhilePrefabEditing() {
    if (!InPrefabEditMode()) {
        return false;
    }
    console_.Warning("Prefabs", "Save or close the prefab being edited first.");
    return true;
}

bool EditorSceneContext::InPrefabEditMode() const noexcept {
    return prefabEdit_.IsValid();
}

// While a prefab is edited the dirty flag tracks the prefab; the scene document's is kept aside.
bool EditorSceneContext::HasUnsavedPrefabEdit() const noexcept {
    return InPrefabEditMode() && sceneDocumentDirty_;
}

std::string EditorSceneContext::PrefabEditModeName() const {
    return prefabEditPath_.stem().string();
}

bool EditorSceneContext::OpenPrefabEditMode(const std::filesystem::path& prefabPath) {
    if (playModeSceneSession_.Active() || RejectWhilePrefabEditing()) {
        return false;
    }
    const kb::scene::ScenePrefabHandle prefab = documentScene_->Prefabs().Load(prefabPath);
    kb::scene::ScenePrefabPrivateScene edit = prefab.IsValid() ? documentScene_->Prefabs().OpenPrivateScene(prefab) : kb::scene::ScenePrefabPrivateScene{};
    if (!edit.IsValid()) {
        console_.Error("Prefabs", "Prefab could not be opened: " + prefabPath.generic_string());
        return false;
    }
    // The prefab's meshes and materials resolve through the project assets, like the scene's.
    kb::scene::Scene& editScene = edit.EditScene();
    static_cast<void>(editScene.Assets().MountProject(EditorProjectPaths::ProjectRoot()));
    RegisterEditorSceneDocumentAssetLoaders(editScene);
    static_cast<void>(editScene.Assets().Discover());

    ReleaseRenderedSceneResources();
    documentSelection_ = hierarchySelection_.SelectedEntities();
    documentDirty_ = sceneDocumentDirty_;
    sceneDocumentDirty_ = false;
    std::swap(commandStack_, documentCommands_);
    prefabEdit_ = std::move(edit);
    prefabEditPath_ = prefabPath;
    DropEditModeScripts();
    scene_ = &prefabEdit_.EditScene();
    ResetSceneEditState();
    InvalidateHierarchyRows();
    hierarchySelection_.SelectEntity(prefabEdit_.RootObject().Entity());
    console_.Info("Prefabs", "Editing prefab: " + prefabPath.generic_string());
    return true;
}

// Writes the edited prefab to its asset and brings the scene document's instances to it.
bool EditorSceneContext::SavePrefabEditMode() {
    if (!InPrefabEditMode()) {
        return false;
    }
    // Only what is under the prefab root is the prefab; an object beside it would be dropped unseen.
    const kb::scene::SceneEntity prefabRoot = prefabEdit_.RootObject().Entity();
    for (const kb::scene::SceneEntity root : scene_->Hierarchy().RootEntities()) {
        if (root != prefabRoot) {
            console_.Warning("Prefabs", "Prefab not saved: move " + scene_->Entities().Name(root) + " under " +
                scene_->Entities().Name(prefabRoot) + " or delete it.");
            return false;
        }
    }
    if (!prefabEdit_.Apply() || !documentScene_->Prefabs().Save(prefabEdit_.SourcePrefab(), prefabEditPath_)) {
        console_.Error("Prefabs", "Prefab could not be saved: " + prefabEditPath_.generic_string());
        return false;
    }
    sceneDocumentDirty_ = false;
    documentDirty_ = true;
    console_.Info("Prefabs", "Prefab saved: " + prefabEditPath_.generic_string());
    return true;
}

bool EditorSceneContext::ClosePrefabEditMode() {
    if (!InPrefabEditMode()) {
        return false;
    }
    ReleaseRenderedSceneResources();
    ResetSceneEditState();
    DropEditModeScripts();
    scene_ = documentScene_.get();
    prefabEdit_ = {};
    prefabEditPath_.clear();
    std::swap(commandStack_, documentCommands_);
    sceneDocumentDirty_ = documentDirty_;
    // Project Files worked through the prefab's scene meanwhile; catch the scene's asset registry up.
    static_cast<void>(scene_->Assets().Discover());
    InvalidateHierarchyRows();
    MarkSceneRenderDirty();
    std::erase_if(documentSelection_, [this](kb::scene::SceneEntity entity) {
        return !scene_->Entities().IsAlive(entity);
    });
    hierarchySelection_.SelectEntities(documentSelection_);
    documentSelection_.clear();
    return true;
}

void EditorSceneContext::AdvanceSceneDocumentGeneration() noexcept {
    sceneDocumentIdentity_.Advance();
}

bool EditorSceneContext::SaveCurrentScene() {
    if (playModeSceneSession_.Active()) {
        console_.Warning("Project", "Scene save ignored while play mode is active. Stop play mode before saving.");
        return false;
    }
    if (currentScenePath_.empty()) {
        currentScenePath_ = EditorProjectPaths::DefaultScenePath();
    }

    return SaveSceneToPath(currentScenePath_);
}

bool EditorSceneContext::SaveCurrentSceneAs(const std::filesystem::path& path) {
    if (playModeSceneSession_.Active()) {
        console_.Warning("Project", "Save As ignored while play mode is active. Stop play mode before saving.");
        return false;
    }
    std::error_code error;
    const std::filesystem::path assets = std::filesystem::weakly_canonical(EditorProjectPaths::AssetsRoot(), error);
    const std::filesystem::path relative = std::filesystem::weakly_canonical(path, error).lexically_relative(assets);
    if (error || relative.empty() || *relative.begin() == "..") {
        console_.Error("Project", "Scenes are saved inside the project's Assets folder (" + assets.generic_string() +
            "); nothing was saved to " + path.generic_string() + ".");
        return false;
    }
    return SaveSceneToPath(path);
}

bool EditorSceneContext::SaveSceneToPath(const std::filesystem::path& path) {
    if (RejectWhilePrefabEditing()) {
        return false;
    }
#if defined(_WIN32)
    if (!EditorMap2DWindow::SaveAll(*this)) return false;
#endif
    if (worldPartition_.IsOpen()) {
        if (!path.empty() && path != currentScenePath_) {
            console_.Warning("World", "A world is saved as its object files; Save As is not available while a world is open.");
            return false;
        }
        std::string error;
        if (!worldPartition_.Session().Save(error)) {
            console_.Error("World", "World could not be saved: " + error);
            return false;
        }
        kb::assets::AssetManager& assets = scene_->Assets().Manager();
        const std::optional<std::filesystem::path> virtualPath = assets.Mounts().ToVirtual(currentScenePath_);
        if (const kb::assets::AssetMetadata* descriptor = virtualPath.has_value() ? assets.Registry().FindByPath(*virtualPath) : nullptr) {
            static_cast<void>(assets.RefreshAsset(descriptor->id));
        }
        const kb::world::WorldSaveStats stats = worldPartition_.Session().LastSaveStats();
        worldPartition_.InvalidateGrid();
        ClearSceneDocumentDirty();
        autosave_.ResetInterval();
        console_.Info("World", "Saved world " + currentScenePath_.generic_string() + ": " + std::to_string(stats.written) + " object file(s) written, " +
            std::to_string(stats.deleted) + " deleted, " + std::to_string(stats.unchanged) + " unchanged.");
        return true;
    }
    const std::filesystem::path scenePath = EnsureSceneDocumentExtension(path.empty() ? EditorProjectPaths::DefaultScenePath() : path);
    ScopedSceneSaveTrace trace{
        "phase=total path=" + scenePath.generic_string() };
    kb::assets::AssetManager& assets = scene_->Assets().Manager();
    const std::optional<std::filesystem::path> virtualPath =
        assets.Mounts().ToVirtual(scenePath);
    const kb::assets::AssetMetadata* existingMetadata = virtualPath.has_value()
        ? assets.Registry().FindByPath(*virtualPath)
        : nullptr;
    const kb::assets::AssetId existingAssetId = existingMetadata == nullptr
        ? kb::assets::AssetId{}
        : existingMetadata->id;

    auto phaseStarted = std::chrono::steady_clock::now();
    std::error_code error;
    if (!scenePath.parent_path().empty()) {
        std::filesystem::create_directories(scenePath.parent_path(), error);
        if (error) {
            console_.Error("Project", "Scene directory could not be created: " + scenePath.parent_path().generic_string());
            return false;
        }
    }
    trace.Phase(phaseStarted, "phase=create-directory");

    const std::string name = SceneDocumentName(scenePath);
    phaseStarted = std::chrono::steady_clock::now();
    if (!kb::scene::SceneDocumentService::Save(*scene_, scenePath, name)) {
        console_.Error("Project", "Scene could not be saved: " + scenePath.generic_string());
        return false;
    }
    trace.Phase(phaseStarted, "phase=atomic-document-write");

    phaseStarted = std::chrono::steady_clock::now();
    if (existingAssetId.IsValid()) {
        if (!assets.RefreshAsset(existingAssetId)) {
            console_.Error(
                "Project",
                "Saved scene metadata could not be refreshed: " +
                    AssetErrorOr(assets, "unknown asset refresh error"));
            return false;
        }
        trace.Phase(phaseStarted, "phase=single-asset-refresh");
    } else if (virtualPath.has_value()) {
        static_cast<void>(scene_->Assets().Discover());
        if (assets.Registry().FindByPath(*virtualPath) == nullptr) {
            console_.Error(
                "Project",
                "Saved scene could not be registered: " +
                    scenePath.generic_string());
            return false;
        }
        trace.Phase(phaseStarted, "phase=new-asset-discovery");
    }

    currentScenePath_ = scenePath;
    RememberLastOpenMap();
    ClearSceneDocumentDirty();
    autosave_.ResetInterval();
    console_.Info("Project", "Saved scene: " + currentScenePath_.generic_string());
    return true;
}

} // namespace kb::editor
