#include "scene/EditorSceneContext.hpp"

#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/project/ProjectDescriptorWriter.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include "scene/EditorScriptAssetGateway.hpp"
#include "scene/input/EditorInputActionAuthoring.hpp"
#include "scene/input/EditorInputAssetGateway.hpp"
#include "scene/input/EditorInputMappingContextAuthoring.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include "scene/EditorDefaultSceneFactory.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"
#include "scene/EditorSceneAssetBrowserCommands.hpp"
#include "scene/EditorSceneCommandController.hpp"
#include "scene/EditorSceneHierarchyActions.hpp"
#include "scene/EditorSceneMeshAssetActions.hpp"
#include "scene/EditorSceneObjectEditCommands.hpp"
#include "scene/EditorScenePrefabActions.hpp"
#include "project/EditorProjectBootstrap.hpp"
#include "project/EditorProjectPaths.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::string_view kSceneDocumentExtension = ".21kbscene";


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

[[nodiscard]] bool SameVec3(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool SameQuat(kb::scene::Quat lhs, kb::scene::Quat rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] bool SameTransform(const kb::scene::TransformComponent& lhs, const kb::scene::TransformComponent& rhs) noexcept {
    return SameVec3(lhs.localPosition, rhs.localPosition) &&
        SameQuat(lhs.localRotation, rhs.localRotation) &&
        SameVec3(lhs.localScale, rhs.localScale);
}

[[nodiscard]] EditorSceneObjectTransformChange* FindTransformChange(
    std::vector<EditorSceneObjectTransformChange>& changes,
    kb::scene::SceneEntity entity) noexcept {
    const auto iter = std::ranges::find_if(changes, [entity](const EditorSceneObjectTransformChange& change) {
        return change.entity == entity;
    });
    return iter == changes.end() ? nullptr : &*iter;
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

EditorSceneContext::EditorSceneContext() {
    const EditorProjectBootstrapResult project = EditorProjectBootstrap::BootstrapDefaultProject();
    if (project.succeeded) {
        project_ = project.descriptor;
        projectFile_ = project.projectFile;
        console_.Info("Project", project.created ? "Created project descriptor." : "Loaded project descriptor.");
    } else {
        projectFile_ = EditorProjectPaths::ProjectFile();
        console_.Error("Project", project.error.empty() ? "Project descriptor bootstrap failed." : project.error);
    }

    if (scene_.Assets().MountProject(EditorProjectPaths::ProjectRoot())) {
        console_.Info("Project", "Mounted project assets.");
    } else {
        console_.Error("Project", AssetErrorOr(scene_.Assets().Manager(), "Project assets could not be mounted."));
    }
    const std::size_t discovered = scene_.Assets().Discover();
    console_.Info("Assets", "Asset discovery completed. Found " + std::to_string(discovered) + " asset(s).");
    currentScenePath_ = ResolveDefaultScenePath();
    std::error_code error;
    if (!currentScenePath_.empty() && std::filesystem::is_regular_file(currentScenePath_, error) && !error && kb::scene::SceneDocumentService::LoadFileIntoScene(scene_, currentScenePath_)) {
        SelectFirstSceneEntityOrClear();
        console_.Info("Project", "Opened default scene: " + currentScenePath_.generic_string());
    } else {
        hierarchySelection_.SelectEntity(EditorDefaultSceneFactory::Seed(scene_));
        if (SaveCurrentScene()) {
            console_.Info("Project", "Created default scene: " + currentScenePath_.generic_string());
        }
    }
    console_.Info("Editor", "Editor scene initialized.");
}

EditorSceneContext::~EditorSceneContext() = default;

void EditorSceneContext::EnsureScriptRuntime() {
    if (scriptHost_ != nullptr) {
        return;
    }
    // Behaviour scripts (Lua / Visual Graph / native) only tick once a script
    // host is installed into the scene runtime. Created lazily on first play and
    // reused; the installed scene system only runs while play mode ticks
    // Runtime().Update, so it is inert in edit mode.
    scriptHost_ = std::make_unique<kb::script::ScriptRuntimeHost>(
        scene_, kb::script::ScriptRuntimeHostOptions{ .installSceneSystem = true });

    // Log("...") in any script prints to the editor Console. console_ outlives
    // scriptHost_ (declared last, destroyed first), so capturing it is safe.
    kb::script::ScriptFunctionDesc logDesc;
    logDesc.signature.name = "Log";
    logDesc.signature.inputs = { kb::script::ScriptFunctionPin{ "message", kb::script::ScriptValueType::String, true } };
    logDesc.signature.outputs = {};
    EditorConsoleState* console = &console_;
    logDesc.callback = [console](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument> arguments) {
        std::string message;
        for (const kb::script::ScriptFunctionArgument& argument : arguments) {
            if (argument.name == "message") {
                message = argument.value.AsString();
                break;
            }
        }
        console->Info("Script", message);
        return kb::script::ScriptFunctionCallResult{ .executed = true, .outputs = {}, .errors = {} };
    };
    static_cast<void>(scriptHost_->RegisterFunction(std::move(logDesc)));

    if (!scriptHost_->Succeeded()) {
        for (const std::string& diagnostic : scriptHost_->Diagnostics()) {
            console_.Error("Scripts", diagnostic);
        }
        console_.Error("Scripts", "Script runtime could not be fully initialized; behaviours may not run.");
    } else {
        console_.Info("Scripts", "Script runtime ready for play mode.");
    }
}

kb::scene::Scene& EditorSceneContext::Scene() noexcept {
    return scene_;
}

const kb::scene::Scene& EditorSceneContext::Scene() const noexcept {
    return scene_;
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
    return viewportState_.CloseToolbarDropdowns();
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
        AppendEntityBranchRenderDirty(scene_, entity, sceneRenderDirtyEntityIds_);
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
    if (!SaveDirtySceneDocument("entering play mode")) {
        return false;
    }

    const std::string name = currentScenePath_.stem().string().empty() ? std::string{ "Main" } : currentScenePath_.stem().string();
    if (!playModeSceneSession_.Begin(scene_, name)) {
        console_.Error("Play Mode", "Scene snapshot could not be captured.");
        return false;
    }
    EnsureScriptRuntime();
    kb::scene::SceneInputActivation::Apply(scene_);
    ActivateProjectInput();
    console_.Info("Play Mode", "Captured editor scene snapshot.");
    return true;
}

bool EditorSceneContext::RestorePlayModeSceneSession() {
    if (!playModeSceneSession_.Active()) {
        return true;
    }
    kb::scene::SceneInputActivation::Clear(scene_);
    if (!playModeSceneSession_.Restore(scene_)) {
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

bool EditorSceneContext::NewScene(EditorDirtySceneResolution dirtyResolution) {
    if (!RestorePlayModeSceneSession()) {
        return false;
    }
    if (!PrepareDirtySceneTransition("creating a new scene", dirtyResolution)) {
        return false;
    }

    const std::vector<kb::scene::SceneEntity> roots = scene_.Hierarchy().RootEntities();
    for (const kb::scene::SceneEntity root : roots) {
        scene_.Entities().Destroy(root);
    }

    hierarchySelection_.SelectEntity(EditorDefaultSceneFactory::Seed(scene_));
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

    if (!kb::scene::SceneDocumentService::LoadFileIntoScene(scene_, scenePath)) {
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
    if (currentScenePath_.empty()) {
        currentScenePath_ = EditorProjectPaths::DefaultScenePath();
    }

    return SaveSceneToPath(currentScenePath_);
}

bool EditorSceneContext::SaveCurrentSceneAs(const std::filesystem::path& path) {
    if (!RestorePlayModeSceneSession()) {
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
    if (!kb::scene::SceneDocumentService::Save(scene_, scenePath, name)) {
        console_.Error("Project", "Scene could not be saved: " + scenePath.generic_string());
        return false;
    }

    currentScenePath_ = scenePath;
    static_cast<void>(scene_.Assets().Discover());
    ClearSceneDocumentDirty();
    console_.Info("Project", "Saved scene: " + currentScenePath_.generic_string());
    return true;
}

bool EditorSceneContext::CanUndoSceneCommand() const noexcept {
    return commandStack_.CanUndo();
}

bool EditorSceneContext::CanRedoSceneCommand() const noexcept {
    return commandStack_.CanRedo();
}

bool EditorSceneContext::UndoSceneCommand() {
    static_cast<void>(CommitHierarchyRename());
    inspector_.EndTextEdit();
    return SceneCommands().Undo();
}

bool EditorSceneContext::RedoSceneCommand() {
    static_cast<void>(CommitHierarchyRename());
    inspector_.EndTextEdit();
    return SceneCommands().Redo();
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
    const kb::scene::SceneEntity selected = scene_.Entities().IsAlive(entity) ? entity : kb::scene::SceneEntity{};
    if (hierarchyRenameEntity_.IsValid() && hierarchyRenameEntity_ != selected) {
        static_cast<void>(CommitHierarchyRename());
    }
    hierarchySelection_.SelectEntity(selected);
    assetBrowser_.ClearSelection();
}

void EditorSceneContext::SelectHierarchyEntities(std::span<const kb::scene::SceneEntity> entities) noexcept {
    std::vector<kb::scene::SceneEntity> alive;
    alive.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        if (scene_.Entities().IsAlive(entity) && !ContainsEntity(alive, entity)) {
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
    hierarchySelection_.SelectEntities(alive);
    assetBrowser_.ClearSelection();
}

void EditorSceneContext::ClearHierarchySelection() noexcept {
    static_cast<void>(CommitHierarchyRename());
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
    return hierarchyRenameEntity_.IsValid() && scene_.Entities().IsAlive(hierarchyRenameEntity_);
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
    if (!scene_.Entities().IsAlive(entity)) {
        CancelHierarchyRename();
        return false;
    }

    hierarchySearch_.Focus(false);
    assetBrowser_.CancelTextEdit();
    inspector_.EndTextEdit();
    hierarchyRenameEntity_ = entity;
    hierarchyRenameBuffer_ = scene_.Entities().Name(entity);
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
    if (scene_.Entities().Name(entity) == name) {
        CancelHierarchyRename();
        return false;
    }

    const bool renamed = ExecuteSceneCommand("Rename Entity", [this, entity, name]() {
        if (!scene_.Entities().IsAlive(entity)) {
            return false;
        }
        scene_.Entities().SetName(entity, name);
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
    return assetBrowser_.BeginRenameSelection(scene_.Assets().Manager());
}

bool EditorSceneContext::BeginAssetRename(kb::assets::AssetId id) {
    static_cast<void>(CommitHierarchyRename());
    return assetBrowser_.BeginRenameAsset(id, scene_.Assets().Manager());
}

bool EditorSceneContext::BeginAssetFolderRename(const std::filesystem::path& virtualFolder) {
    static_cast<void>(CommitHierarchyRename());
    return assetBrowser_.BeginRenameFolder(virtualFolder, scene_.Assets().Manager());
}

bool EditorSceneContext::CommitAssetTextEdit() {
    const bool committed = EditorSceneAssetBrowserCommands::CommitTextEdit(scene_, assetBrowser_);
    if (committed) {
        console_.Info("Assets", "Asset browser text edit committed.");
    } else {
        console_.Error("Assets", AssetErrorOr(scene_.Assets().Manager(), "Asset browser text edit failed."));
    }
    return committed;
}

void EditorSceneContext::CancelAssetTextEdit() noexcept {
    assetBrowser_.CancelTextEdit();
}

bool EditorSceneContext::DeleteSelectedAssetBrowserItem() {
    const bool deleted = EditorSceneAssetBrowserCommands::DeleteSelected(scene_, assetBrowser_);
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

    const std::vector<kb::scene::SceneEntity> deleting = TopLevelSelectedEntities(scene_, hierarchySelection_.SelectedEntities());
    if (!AnyAlive(scene_, deleting)) {
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
    const std::vector<kb::scene::SceneEntity> duplicating = TopLevelSelectedEntities(scene_, selected);
    if (duplicating.empty() || !AnyAlive(scene_, duplicating)) {
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
    if (payloads.empty() || !AnyAlive(scene_, entities)) {
        return false;
    }

    std::vector<kb::scene::SceneEntity> alive;
    alive.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        if (scene_.Entities().IsAlive(entity) && !ContainsEntity(alive, entity)) {
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
    scene_.Runtime().SynchronizeTransforms();
    return true;
}

bool EditorSceneContext::DeleteAssetBrowserItem(kb::assets::AssetId id) {
    const bool deleted = EditorSceneAssetBrowserCommands::DeleteAsset(scene_, assetBrowser_, id);
    if (deleted) {
        console_.Info("Assets", "Asset deleted.");
    } else {
        console_.Error("Assets", AssetErrorOr(scene_.Assets().Manager(), "Asset delete failed."));
    }
    return deleted;
}

bool EditorSceneContext::DeleteAssetBrowserFolder(const std::filesystem::path& virtualFolder) {
    const bool deleted = EditorSceneAssetBrowserCommands::DeleteFolder(scene_, assetBrowser_, virtualFolder);
    if (deleted) {
        console_.Info("Assets", "Folder deleted: " + virtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_.Assets().Manager(), "Folder delete failed."));
    }
    return deleted;
}

bool EditorSceneContext::MoveAssetToFolder(kb::assets::AssetId id, const std::filesystem::path& destinationVirtualFolder) {
    const bool moved = EditorSceneAssetBrowserCommands::MoveAssetToFolder(scene_, assetBrowser_, id, destinationVirtualFolder);
    if (moved) {
        console_.Info("Assets", "Asset moved to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_.Assets().Manager(), "Asset move failed."));
    }
    return moved;
}

bool EditorSceneContext::MoveAssetFolderToFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder) {
    const bool moved = EditorSceneAssetBrowserCommands::MoveFolderToFolder(scene_, assetBrowser_, sourceVirtualFolder, destinationVirtualFolder);
    if (moved) {
        console_.Info("Assets", "Folder moved to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_.Assets().Manager(), "Folder move failed."));
    }
    return moved;
}

bool EditorSceneContext::CopyAssetToFolder(kb::assets::AssetId id, const std::filesystem::path& destinationVirtualFolder) {
    const bool copied = EditorSceneAssetBrowserCommands::CopyAssetToFolder(scene_, assetBrowser_, id, destinationVirtualFolder);
    if (copied) {
        console_.Info("Assets", "Asset copied to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_.Assets().Manager(), "Asset copy failed."));
    }
    return copied;
}

bool EditorSceneContext::CopyAssetFolderToFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder) {
    const bool copied = EditorSceneAssetBrowserCommands::CopyFolderToFolder(scene_, assetBrowser_, sourceVirtualFolder, destinationVirtualFolder);
    if (copied) {
        console_.Info("Assets", "Folder copied to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_.Assets().Manager(), "Folder copy failed."));
    }
    return copied;
}

bool EditorSceneContext::ImportAssetFiles(std::span<const std::filesystem::path> sourceFiles) {
    return ImportAssetFiles(sourceFiles, assetBrowser_.SelectedFolder());
}

bool EditorSceneContext::ImportAssetFiles(std::span<const std::filesystem::path> sourceFiles, const std::filesystem::path& destinationVirtualFolder) {
    const bool imported = EditorSceneAssetBrowserCommands::ImportFiles(scene_, assetBrowser_, sourceFiles, destinationVirtualFolder);
    if (imported) {
        console_.Info("Assets", "Imported " + std::to_string(sourceFiles.size()) + " file(s) to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_.Assets().Manager(), "Asset import failed."));
    }
    return imported;
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
            return EditorSceneHierarchyActions::ToggleVisibility(scene_, entity);
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
            created = EditorSceneHierarchyActions::CreateObject(scene_);
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

bool EditorSceneContext::ReparentEntity(kb::scene::SceneEntity child, kb::scene::SceneEntity parent) {
    if (!child.IsValid() || !scene_.Entities().IsAlive(child)) {
        console_.Warning("Hierarchy", "Entity reparent ignored.");
        return false;
    }

    const bool moved = ExecuteSceneCommand("Reparent Entity", [this, child, parent]() {
        return EditorSceneHierarchyActions::Reparent(scene_, child, parent);
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
    const std::vector<kb::scene::SceneEntity> moving = TopLevelSelectedEntities(scene_, children);
    if (moving.empty()) {
        console_.Warning("Hierarchy", "Hierarchy reparent did not move any entity.");
        return false;
    }

    const std::span<const kb::scene::SceneEntity> movingSpan{ moving.data(), moving.size() };
    if (parent.IsValid() && (ContainsEntity(movingSpan, parent) || HasSelectedAncestor(scene_, parent, movingSpan))) {
        console_.Warning("Hierarchy", "Cannot reparent an entity below itself or a selected descendant.");
        return false;
    }

    const bool moved = ExecuteSceneCommand("Reparent Entities", [this, moving, parent]() {
        bool anyMoved = false;
        for (const kb::scene::SceneEntity child : moving) {
            if (child == parent) {
                continue;
            }
            anyMoved = EditorSceneHierarchyActions::Reparent(scene_, child, parent) || anyMoved;
        }
        return anyMoved;
    });
    if (moved) {
        const std::vector<EditorHierarchyRow>& rows = HierarchyRows();
        hierarchySelection_.Clear();
        bool first = true;
        for (const kb::scene::SceneEntity entity : children) {
            if (!scene_.Entities().IsAlive(entity)) {
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
    const bool created = EditorScenePrefabActions::CreateAsset(scene_, entity, path);
    if (created) {
        static_cast<void>(scene_.Assets().Discover());
        if (const std::optional<std::filesystem::path> virtualPath = scene_.Assets().Manager().Mounts().ToVirtual(path)) {
            if (const kb::assets::AssetMetadata* metadata = scene_.Assets().Manager().Registry().FindByPath(*virtualPath); metadata != nullptr) {
                static_cast<void>(assetBrowser_.SelectAsset(metadata->id, scene_.Assets().Manager()));
            }
        }
        console_.Info("Prefabs", "Prefab asset created: " + path.generic_string());
    } else {
        console_.Error("Prefabs", AssetErrorOr(scene_.Assets().Manager(), "Prefab asset creation failed."));
    }
    return created;
}

EditorInputActionAuthoring EditorSceneContext::InputActionAuthoring() noexcept {
    return EditorInputActionAuthoring{ scene_, assetBrowser_, console_ };
}

EditorInputMappingContextAuthoring EditorSceneContext::InputMappingContextAuthoring() noexcept {
    return EditorInputMappingContextAuthoring{ scene_, assetBrowser_, console_ };
}

bool EditorSceneContext::CreateInputActionAsset(const std::filesystem::path& virtualFolder) {
    return InputActionAuthoring().Create(virtualFolder);
}

bool EditorSceneContext::CreateInputMappingContextAsset(const std::filesystem::path& virtualFolder) {
    return InputMappingContextAuthoring().Create(virtualFolder);
}

bool EditorSceneContext::CreateLuaScriptAsset(const std::filesystem::path& virtualFolder) {
    EditorScriptAssetGateway gateway{ scene_, assetBrowser_ };
    const std::optional<std::filesystem::path> path = gateway.CreateLuaScript(virtualFolder);
    if (!path.has_value()) {
        console_.Error("Scripts", "Lua script could not be created in folder: " + virtualFolder.generic_string());
        return false;
    }
    console_.Info("Scripts", "Lua script created: " + path->generic_string());
    return true;
}

bool EditorSceneContext::OpenLuaScript(kb::assets::AssetId id) {
    const kb::assets::AssetMetadata* metadata = scene_.Assets().Manager().Registry().Find(id);
    if (metadata == nullptr) {
        console_.Error("Scripts", "Lua script metadata was not found.");
        return false;
    }
    std::filesystem::path path = metadata->physicalPath;
    if (const std::optional<std::filesystem::path> mounted = scene_.Assets().Manager().Mounts().Resolve(metadata->virtualPath)) {
        path = *mounted;
    }
    scriptEditor_.Open(path, id, metadata->virtualPath.filename().string());
    console_.Info("Scripts", "Opened script: " + metadata->virtualPath.generic_string());
    return true;
}

std::optional<kb::input::InputActionAsset> EditorSceneContext::ReadInputActionAsset(kb::assets::AssetId id) const {
    return EditorInputAssetGateway::ReadAction(scene_, id);
}

bool EditorSceneContext::SetInputActionName(kb::assets::AssetId id, std::string name) {
    return InputActionAuthoring().SetName(id, std::move(name));
}

bool EditorSceneContext::CycleInputActionValueType(kb::assets::AssetId id) {
    return InputActionAuthoring().CycleValueType(id);
}

bool EditorSceneContext::ToggleInputActionConsume(kb::assets::AssetId id) {
    return InputActionAuthoring().ToggleConsume(id);
}

bool EditorSceneContext::ToggleProjectInputEnabled() {
    project_.inputEnabled = !project_.inputEnabled;
    return SaveProjectDescriptor();
}

std::vector<std::string> EditorSceneContext::ProjectInputMappingContextOptions() const {
    // Empty first entry is the "(None)" choice so the project input can be cleared.
    std::vector<std::string> options{ std::string{} };
    for (const kb::assets::AssetMetadata& metadata : scene_.Assets().Manager().Registry().All()) {
        if (metadata.type == "InputMappingContext") {
            options.push_back(kb::assets::NormalizeAssetPath(metadata.virtualPath));
        }
    }
    std::sort(options.begin() + 1, options.end());
    return options;
}

bool EditorSceneContext::SetProjectInputMappingContext(std::string virtualPath) {
    if (project_.inputMappingContext == virtualPath) {
        return false;
    }
    project_.inputMappingContext = std::move(virtualPath);
    return SaveProjectDescriptor();
}

bool EditorSceneContext::CloseProjectSettingsDropdowns() noexcept {
    return projectSettings_.CloseDropdowns();
}

void EditorSceneContext::ActivateProjectInput() {
    if (!project_.inputEnabled || project_.inputMappingContext.empty()) {
        return;
    }
    const kb::assets::AssetMetadata* metadata = scene_.Assets().Manager().Registry().FindByPath(project_.inputMappingContext);
    if (metadata != nullptr && metadata->type == "InputMappingContext") {
        static_cast<void>(scene_.Input().AddMappingContext(metadata->id.value, 0));
    }
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
    return EditorInputAssetGateway::ReadContext(scene_, id);
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
    std::optional<kb::scene::SceneEntity> root;
    if (!ExecuteSceneCommand("Instantiate Prefab", [this, &root, path, virtualPath, parent]() {
            root = EditorScenePrefabActions::InstantiateAsset(scene_, path, virtualPath, parent);
            if (!root.has_value()) {
                return false;
            }
            SelectEntity(*root);
            return true;
        })) {
        console_.Error("Prefabs", "Prefab instantiation failed: " + path.generic_string());
        return false;
    }
    console_.Info("Prefabs", "Prefab instantiated: " + path.generic_string());
    return true;
}

kb::scene::SceneEntity EditorSceneContext::CreateMeshAssetEntity(kb::assets::AssetId assetId) {
    return CreateMeshAssetEntity(assetId, {}, true);
}

kb::scene::SceneEntity EditorSceneContext::CreateMeshAssetEntity(kb::assets::AssetId assetId, kb::scene::Vec3 position, bool logCreation) {
    if (!assetId.IsValid()) {
        console_.Warning("Assets", "Mesh entity creation ignored for invalid asset.");
        return {};
    }

    const kb::assets::AssetMetadata* metadata = scene_.Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr) {
        console_.Error("Assets", "Mesh asset metadata was not found.");
        return {};
    }
    if (metadata->importCategory != "Mesh" || metadata->type != "RenderMesh") {
        console_.Warning("Assets", "Only imported mesh assets can be placed on the scene.");
        return {};
    }

    kb::scene::SceneEntity entity{};
    if (!logCreation) {
        entity = EditorSceneMeshAssetActions::CreateMeshEntity(scene_, assetId, metadata->name, position);
        if (!entity.IsValid()) {
            console_.Error("Assets", "Mesh entity could not be created: " + metadata->name);
            return {};
        }
        SelectEntity(entity);
        MarkSceneRenderDirty();
        return entity;
    }

    const bool created = ExecuteSceneCommand("Create Mesh Entity", [this, assetId, position, metadata, &entity]() {
        entity = EditorSceneMeshAssetActions::CreateMeshEntity(scene_, assetId, metadata->name, position);
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
    if (!assetId.IsValid() || !entity.IsValid() || !scene_.Entities().IsAlive(entity)) {
        console_.Warning("Scripts", "Behaviour asset assignment ignored for invalid target.");
        return false;
    }

    const kb::assets::AssetMetadata* metadata = scene_.Assets().Manager().Registry().Find(assetId);
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
            if (!scene_.Entities().IsAlive(entity)) {
                return false;
            }
            scene_.Components().Behaviours().Set(entity, *behaviour);
            SelectEntity(entity);
            return true;
        })) {
        console_.Error("Scripts", "Behaviour asset assignment failed: " + metadata->name);
        return false;
    }
    console_.Info("Scripts", "Behaviour asset assigned: " + metadata->name);
    return true;
}

bool EditorSceneContext::HasEntityScript(kb::scene::SceneEntity entity) const {
    return entity.IsValid() && scene_.Components().Behaviours().Has(entity);
}

std::string EditorSceneContext::EntityScriptName(kb::scene::SceneEntity entity) const {
    const kb::scene::BehaviourComponent* behaviour = scene_.Components().Behaviours().TryGet(entity);
    if (behaviour == nullptr) {
        return {};
    }
    const kb::assets::AssetMetadata* metadata = scene_.Assets().Manager().Registry().Find(kb::assets::AssetId{ behaviour->behaviourAssetId });
    if (metadata == nullptr) {
        return "(missing script)";
    }
    return metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
}

bool EditorSceneContext::EntityScriptEnabled(kb::scene::SceneEntity entity) const {
    const kb::scene::BehaviourComponent* behaviour = scene_.Components().Behaviours().TryGet(entity);
    return behaviour != nullptr && behaviour->enabled;
}

std::vector<std::pair<kb::assets::AssetId, std::string>> EditorSceneContext::AvailableScriptAssets() const {
    std::vector<std::pair<kb::assets::AssetId, std::string>> scripts;
    for (const kb::assets::AssetMetadata& metadata : scene_.Assets().Manager().Registry().All()) {
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
    if (!entity.IsValid() || !scene_.Components().Behaviours().Has(entity)) {
        return false;
    }
    return ExecuteSceneCommand("Remove Script", [this, entity]() {
        if (!scene_.Entities().IsAlive(entity)) {
            return false;
        }
        scene_.Components().Behaviours().Remove(entity);
        return true;
    });
}

bool EditorSceneContext::ToggleEntityScriptEnabled(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_.Components().Behaviours().Has(entity)) {
        return false;
    }
    return ExecuteSceneCommand("Toggle Script Enabled", [this, entity]() {
        kb::scene::BehaviourComponent* behaviour = scene_.Components().Behaviours().TryGet(entity);
        if (behaviour == nullptr) {
            return false;
        }
        behaviour->enabled = !behaviour->enabled;
        scene_.Components().Behaviours().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::BeginSelectedTransformEdit(std::string label) {
    if (activeTransformEdit_.Active()) {
        return false;
    }

    const kb::scene::SceneEntity primary = SelectedEntity();
    if (!scene_.Entities().IsAlive(primary)) {
        return false;
    }

    std::vector<kb::scene::SceneEntity> editing = TopLevelSelectedEntities(scene_, hierarchySelection_.SelectedEntities());
    if (editing.empty()) {
        editing.push_back(primary);
    } else if (!ContainsEntity(editing, primary)) {
        editing.clear();
        editing.push_back(primary);
    }

    activeTransformEdit_.label = std::move(label);
    activeTransformEdit_.primary = primary;
    activeTransformEdit_.changes.clear();
    activeTransformEdit_.changes.reserve(editing.size());
    for (const kb::scene::SceneEntity entity : editing) {
        if (!scene_.Entities().IsAlive(entity) || FindTransformChange(activeTransformEdit_.changes, entity) != nullptr) {
            continue;
        }
        const kb::scene::TransformComponent* transform = scene_.Transforms().TryGet(entity);
        if (transform == nullptr) {
            continue;
        }
        activeTransformEdit_.changes.push_back(EditorSceneObjectTransformChange{
            .entity = entity,
            .before = *transform,
            .after = *transform,
        });
    }

    if (activeTransformEdit_.changes.empty() || FindTransformChange(activeTransformEdit_.changes, primary) == nullptr) {
        activeTransformEdit_.Clear();
        return false;
    }
    return true;
}

bool EditorSceneContext::ApplyActiveTransformEditPrimaryPosition(kb::scene::Vec3 position) {
    if (!activeTransformEdit_.Active()) {
        return false;
    }

    EditorSceneObjectTransformChange* primaryChange = FindTransformChange(activeTransformEdit_.changes, activeTransformEdit_.primary);
    if (primaryChange == nullptr) {
        return false;
    }

    const kb::scene::Vec3 delta{
        position.x - primaryChange->before.localPosition.x,
        position.y - primaryChange->before.localPosition.y,
        position.z - primaryChange->before.localPosition.z,
    };

    bool changed = false;
    std::vector<kb::scene::SceneEntity> touched;
    touched.reserve(activeTransformEdit_.changes.size());
    for (EditorSceneObjectTransformChange& change : activeTransformEdit_.changes) {
        if (!scene_.Entities().IsAlive(change.entity)) {
            continue;
        }
        kb::scene::TransformComponent next = change.before;
        next.localPosition = kb::scene::Vec3{
            change.before.localPosition.x + delta.x,
            change.before.localPosition.y + delta.y,
            change.before.localPosition.z + delta.z,
        };
        change.after = next;

        const kb::scene::TransformComponent current = scene_.Transforms().Get(change.entity);
        if (SameTransform(current, next)) {
            continue;
        }
        scene_.Transforms().Set(change.entity, next);
        touched.push_back(change.entity);
        changed = true;
    }

    if (changed) {
        MarkSceneEntitiesRenderDirty(touched);
    }
    return changed;
}

bool EditorSceneContext::CommitActiveTransformEdit() {
    if (!activeTransformEdit_.Active()) {
        activeTransformEdit_.Clear();
        return false;
    }

    std::vector<EditorSceneObjectTransformChange> committed;
    committed.reserve(activeTransformEdit_.changes.size());
    for (EditorSceneObjectTransformChange& change : activeTransformEdit_.changes) {
        if (!scene_.Entities().IsAlive(change.entity)) {
            continue;
        }
        if (const kb::scene::TransformComponent* current = scene_.Transforms().TryGet(change.entity); current != nullptr) {
            change.after = *current;
        }
        if (!SameTransform(change.before, change.after)) {
            committed.push_back(change);
        }
    }

    const std::string label = activeTransformEdit_.label.empty() ? std::string{ "Move Entity" } : activeTransformEdit_.label;
    activeTransformEdit_.Clear();
    if (committed.empty()) {
        return false;
    }

    std::vector<kb::scene::SceneEntity> touched;
    touched.reserve(committed.size());
    for (const EditorSceneObjectTransformChange& change : committed) {
        touched.push_back(change.entity);
    }
    commandStack_.PushExecuted(std::make_unique<EditorSceneTransformDeltaCommand>(*this, label, std::move(committed)));
    MarkSceneEntitiesRenderDirty(touched);
    MarkSceneDocumentDirty();
    scene_.Runtime().SynchronizeTransforms();
    return true;
}

void EditorSceneContext::CancelActiveTransformEdit() noexcept {
    if (!activeTransformEdit_.Active()) {
        activeTransformEdit_.Clear();
        return;
    }

    bool changed = false;
    std::vector<kb::scene::SceneEntity> touched;
    touched.reserve(activeTransformEdit_.changes.size());
    for (const EditorSceneObjectTransformChange& change : activeTransformEdit_.changes) {
        if (!scene_.Entities().IsAlive(change.entity)) {
            continue;
        }
        scene_.Transforms().Set(change.entity, change.before);
        touched.push_back(change.entity);
        changed = true;
    }
    activeTransformEdit_.Clear();
    if (changed) {
        MarkSceneEntitiesRenderDirty(touched);
        scene_.Runtime().SynchronizeTransforms();
    }
}

bool EditorSceneContext::HasActiveTransformEdit() const noexcept {
    return activeTransformEdit_.Active();
}

EditorSceneCommandController EditorSceneContext::SceneCommands() noexcept {
    return EditorSceneCommandController{
        scene_,
        commandStack_,
        console_,
        viewportState_,
        hierarchySelection_,
        assetBrowser_,
        hierarchyExpansion_,
        hierarchySearch_,
        pendingSceneTransactionLabel_,
        sceneRenderRevision_,
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

    hierarchyRowsCache_ = EditorHierarchyRowBuilder::Build(scene_, hierarchyExpansion_.CollapsedEntities(), hierarchySearch_.Query());
    hierarchyRowsDirty_ = false;
}

void EditorSceneContext::ResetSceneEditState() {
    commandStack_.Clear();
    pendingSceneTransactionLabel_.reset();
    activeTransformEdit_.Clear();
    CancelHierarchyRename();
    inspector_.EndTextEdit();
    MarkSceneRenderDirty();
    scene_.Runtime().SynchronizeTransforms();
}

void EditorSceneContext::SelectFirstSceneEntityOrClear() noexcept {
    const std::vector<kb::scene::SceneEntity> roots = scene_.Hierarchy().RootEntities();
    if (roots.empty()) {
        hierarchySelection_.Clear();
        return;
    }
    hierarchySelection_.SelectEntity(roots.front());
    assetBrowser_.ClearSelection();
}

std::filesystem::path EditorSceneContext::ResolveProjectVirtualPath(const std::filesystem::path& virtualPath) const {
    if (const std::optional<std::filesystem::path> physical = scene_.Assets().Manager().Mounts().Resolve(virtualPath)) {
        return *physical;
    }
    return EditorProjectPaths::DefaultScenePath();
}

std::filesystem::path EditorSceneContext::ResolveDefaultScenePath() const {
    const std::filesystem::path defaultScene = project_.defaultScene.empty()
        ? std::filesystem::path{ "/Game/Scenes/Main.21kbscene" }
        : std::filesystem::path{ project_.defaultScene };
    return ResolveProjectVirtualPath(defaultScene);
}

} // namespace kb::editor
