#include "scene/EditorSceneContext.hpp"

#include "app/EditorPlayModeSceneSession.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
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
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorDefaultSceneFactory.hpp"
#include "scene/EditorSceneDocumentAssetLoaders.hpp"

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
    if (playModeSceneSession_.Active()) {
        return true;
    }
    if (plugins_.HasPendingReload() && !ReloadSceneFromProject()) {
        return false;
    }
    if (!SaveDirtySceneDocument("entering play mode")) {
        return false;
    }
    kb::audio::AudioPlayback::StopAll(*scene_);

    // Pick up on-disk asset edits made since the last play — most importantly a
    // script saved in the Script Editor (which only writes the file, it does not
    // touch the asset system). Discover re-hashes files, and on a content change
    // evicts the cached asset and updates its contentHash, so the runtime's
    // IsScriptCurrent check reloads the new source instead of re-running the
    // stale compiled script.
    static_cast<void>(scene_->Assets().Discover());

    const std::string name = currentScenePath_.stem().string().empty() ? std::string{ "Main" } : currentScenePath_.stem().string();
    if (!playModeSceneSession_.Begin(*scene_, name)) {
        console_.Error("Play Mode", "Scene snapshot could not be captured.");
        return false;
    }
    EnsureScriptRuntime();
    ResetScriptRuntimeStateForPlayMode();
    kb::scene::SceneInputActivation::Apply(*scene_);
    ActivateProjectInput();
    console_.Info("Play Mode", "Captured editor scene snapshot.");
    return true;
}

bool EditorSceneContext::RestorePlayModeSceneSession() {
    if (!playModeSceneSession_.Active()) {
        return true;
    }
    // Fire each behaviour's Destroyed (OnDestroy-equivalent) hook while the play
    // scene is still live, BEFORE reverting to the snapshot — matching Unity,
    // where stopping play tears behaviours down. Surface any error a Destroyed
    // handler raises to the Console.
    if (scriptModule_ != nullptr && scriptModule_->Host() != nullptr) {
        static_cast<void>(scriptModule_->Host()->DispatchShutdownLifecycle(0.0F));
        SurfaceScriptDiagnostics();
    }
    kb::audio::AudioPlayback::StopAll(*scene_);
    kb::scene::SceneInputActivation::Clear(*scene_);
    if (!playModeSceneSession_.Restore(*scene_)) {
        console_.Error("Play Mode", "Editor scene snapshot could not be restored.");
        return false;
    }

    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    ClearSceneDocumentDirty();
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

    if (!currentScenePath_.empty() && !kb::scene::SceneDocumentService::LoadFileIntoScene(*nextScene, currentScenePath_)) {
        console_.Error("Project", "Scene could not be reloaded: " + currentScenePath_.generic_string());
        return false;
    }

    scene_ = std::move(nextScene);
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

    hierarchySelection_.SelectEntity(EditorDefaultSceneFactory::Seed(*scene_));
    currentScenePath_ = EditorProjectPaths::UniqueScenePath("Untitled");
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

    currentScenePath_ = scenePath;
    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    ClearSceneDocumentDirty();
    console_.Info("Project", "Opened scene: " + currentScenePath_.generic_string());
    return true;
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
