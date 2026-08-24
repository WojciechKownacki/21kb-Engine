#include "scene/EditorSceneContext.hpp"

#include "app/EditorPlayModeSceneSession.hpp"
#include "diagnostics/EditorLagTrace.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/audio/EditorSceneAudioSettingsService.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "engine/script/ScriptModule.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialParameterValidation.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorDefaultSceneFactory.hpp"
#include "scene/EditorSceneDocumentAssetLoaders.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
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
}

void EditorSceneContext::ReleaseRenderedSceneResources() {
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
    ScopedPlayTransitionTrace trace{"transition=begin phase=total"};
    if (playModeSceneSession_.Active()) {
        return true;
    }
    auto phaseStarted = std::chrono::steady_clock::now();
    if (plugins_.HasPendingReload() && !ReloadSceneFromProject()) {
        return false;
    }
    trace.Phase(phaseStarted, "transition=begin phase=pending-plugin-reload");
    phaseStarted = std::chrono::steady_clock::now();
    if (!SaveDirtySceneDocument("entering play mode")) {
        return false;
    }
    trace.Phase(phaseStarted, "transition=begin phase=save-dirty-scene");
    kb::audio::AudioPlayback::StopAll(*scene_);

    // Asset authoring paths publish or invalidate the edited asset at the mutation
    // boundary. Entering Play must not rescan and re-hash every mounted file.
    phaseStarted = std::chrono::steady_clock::now();
    if (!ActivateProjectPhysicsLayers(*scene_)) {
        return false;
    }
    trace.Phase(phaseStarted, "transition=begin phase=physics-layers");

    phaseStarted = std::chrono::steady_clock::now();
    const std::string name = currentScenePath_.stem().string().empty() ? std::string{ "Main" } : currentScenePath_.stem().string();
    if (!playModeSceneSession_.Begin(*scene_, name)) {
        console_.Error("Play Mode", "Scene snapshot could not be captured.");
        return false;
    }
    trace.Phase(phaseStarted, "transition=begin phase=capture-snapshot");
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
    if (HasSceneBehaviours(*scene_)) {
        EnsureScriptRuntime();
    }
    editorSceneParticleAccumulatorSeconds_ = 0.0;
    trace.Phase(phaseStarted, "transition=begin phase=script-runtime");
    console_.Info("Play Mode", "Captured editor scene snapshot.");
    return true;
}

bool EditorSceneContext::RestorePlayModeSceneSession() {
    ScopedPlayTransitionTrace trace{"transition=restore phase=total"};
    if (!playModeSceneSession_.Active()) {
        return true;
    }
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
        console_.Error("Play Mode", "Editor scene snapshot could not be restored.");
        return false;
    }
    trace.Phase(phaseStarted, "transition=restore phase=restore-snapshot");
    phaseStarted = std::chrono::steady_clock::now();
    ReleaseRenderedSceneResources();

    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    ClearSceneDocumentDirty();
    editorSceneParticleAccumulatorSeconds_ = 0.0;
    trace.Phase(phaseStarted, "transition=restore phase=editor-reset");
    console_.Info("Play Mode", "Restored editor scene snapshot.");
    return true;
}

bool EditorSceneContext::HasPlayModeSceneSession() const noexcept {
    return playModeSceneSession_.Active();
}

bool EditorSceneContext::ReloadSceneFromProject() {
    if (!RestorePlayModeSceneSession()) {
        return false;
    }
    if (!SaveDirtySceneDocument("reloading project plugins")) {
        return false;
    }
    if (currentScenePath_.empty() && !SaveCurrentScene()) {
        console_.Error("Project", "Scene could not be saved before reloading project plugins.");
        return false;
    }

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

    if (!currentScenePath_.empty() && !kb::scene::SceneDocumentService::LoadFileIntoScene(*nextScene, currentScenePath_)) {
        console_.Error("Project", "Scene could not be reloaded: " + currentScenePath_.generic_string());
        return false;
    }
    EditorSceneAudioSettingsService::PrepareDocument(*nextScene);

    ReleaseRenderedSceneResources();
    scene_ = std::move(nextScene);
    AdvanceSceneDocumentGeneration();
    plugins_.ClearPendingReload();
    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    ClearSceneDocumentDirty();
    console_.Info("Project", "Reloaded scene with current project plugin settings.");
    return true;
}

bool EditorSceneContext::NewScene(EditorDirtySceneResolution dirtyResolution) {
    if (!RestorePlayModeSceneSession()) {
        return false;
    }
    if (!PrepareDirtySceneTransition("creating a new scene", dirtyResolution)) {
        return false;
    }

    const std::vector<kb::scene::SceneEntity> roots = scene_->Hierarchy().RootEntities();
    for (const kb::scene::SceneEntity root : roots) {
        scene_->Entities().Destroy(root);
    }
    EditorSceneAudioSettingsService::ResetForNewDocument(*scene_);
    ReleaseRenderedSceneResources();

    hierarchySelection_.SelectEntity(EditorDefaultSceneFactory::Seed(*scene_));
    currentScenePath_ = EditorProjectPaths::UniqueScenePath("Untitled");
    AdvanceSceneDocumentGeneration();
    ResetSceneEditState();
    MarkSceneDocumentDirty();
    console_.Info("Project", "New scene created: " + currentScenePath_.generic_string());
    return true;
}

bool EditorSceneContext::OpenDefaultScene() {
    return OpenScene(ResolveDefaultScenePath());
}

bool EditorSceneContext::OpenScene(const std::filesystem::path& path, EditorDirtySceneResolution dirtyResolution) {
    if (!RestorePlayModeSceneSession()) {
        return false;
    }
    if (!PrepareDirtySceneTransition("opening a scene", dirtyResolution)) {
        return false;
    }

    const std::filesystem::path scenePath = EnsureSceneDocumentExtension(path);

    if (!kb::scene::SceneDocumentService::LoadFileIntoScene(*scene_, scenePath)) {
        console_.Error("Project", "Scene could not be opened: " + scenePath.generic_string());
        return false;
    }
    EditorSceneAudioSettingsService::PrepareDocument(*scene_);
    ReleaseRenderedSceneResources();

    currentScenePath_ = scenePath;
    AdvanceSceneDocumentGeneration();
    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    ClearSceneDocumentDirty();
    console_.Info("Project", "Opened scene: " + currentScenePath_.generic_string());
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
    return SaveSceneToPath(path);
}

bool EditorSceneContext::SaveSceneToPath(const std::filesystem::path& path) {
    const std::filesystem::path scenePath = EnsureSceneDocumentExtension(path.empty() ? EditorProjectPaths::DefaultScenePath() : path);
    std::error_code error;
    if (!scenePath.parent_path().empty()) {
        std::filesystem::create_directories(scenePath.parent_path(), error);
        if (error) {
            console_.Error("Project", "Scene directory could not be created: " + scenePath.parent_path().generic_string());
            return false;
        }
    }

    const std::string name = scenePath.stem().string().empty() ? std::string{ "Main" } : scenePath.stem().string();
    if (!kb::scene::SceneDocumentService::Save(*scene_, scenePath, name)) {
        console_.Error("Project", "Scene could not be saved: " + scenePath.generic_string());
        return false;
    }

    currentScenePath_ = scenePath;
    static_cast<void>(scene_->Assets().Discover());
    ClearSceneDocumentDirty();
    console_.Info("Project", "Saved scene: " + currentScenePath_.generic_string());
    return true;
}

} // namespace kb::editor
