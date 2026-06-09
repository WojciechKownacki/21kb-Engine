#include "scene/EditorSceneCommandController.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "commands/EditorCommandStack.hpp"
#include "commands/EditorSceneHistoryCommand.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "scene/EditorHierarchyExpansionState.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"
#include "scene/EditorHierarchySearchState.hpp"
#include "scene/EditorHierarchySelectionState.hpp"
#include "scene/EditorSceneViewportStateStore.hpp"

#include <utility>

namespace kb::editor {

EditorSceneCommandController::EditorSceneCommandController(
    kb::scene::Scene& scene,
    EditorCommandStack& commandStack,
    EditorConsoleState& console,
    EditorSceneViewportStateStore& viewportState,
    EditorHierarchySelectionState& hierarchySelection,
    EditorAssetBrowserState& assetBrowser,
    EditorHierarchyExpansionState& hierarchyExpansion,
    EditorHierarchySearchState& hierarchySearch,
    std::optional<std::string>& pendingTransactionLabel,
    std::uint64_t& sceneRenderRevision,
    bool& sceneDocumentDirty,
    bool& hierarchyRowsDirty) noexcept
    : scene_(scene)
    , commandStack_(commandStack)
    , console_(console)
    , viewportState_(viewportState)
    , hierarchySelection_(hierarchySelection)
    , assetBrowser_(assetBrowser)
    , hierarchyExpansion_(hierarchyExpansion)
    , hierarchySearch_(hierarchySearch)
    , pendingTransactionLabel_(pendingTransactionLabel)
    , sceneRenderRevision_(sceneRenderRevision)
    , sceneDocumentDirty_(sceneDocumentDirty)
    , hierarchyRowsDirty_(hierarchyRowsDirty) {}

bool EditorSceneCommandController::Undo() {
    if (!commandStack_.Undo()) {
        console_.Warning("Edit", "Undo ignored.");
        return false;
    }

    NormalizeHierarchySelectionAfterSceneRestore();
    NotifySceneChanged(true);
    console_.Info("Edit", "Undo.");
    return true;
}

bool EditorSceneCommandController::Redo() {
    if (!commandStack_.Redo()) {
        console_.Warning("Edit", "Redo ignored.");
        return false;
    }

    NormalizeHierarchySelectionAfterSceneRestore();
    NotifySceneChanged(true);
    console_.Info("Edit", "Redo.");
    return true;
}

bool EditorSceneCommandController::BeginTransaction(std::string label) {
    if (pendingTransactionLabel_.has_value()) {
        return false;
    }
    if (!scene_.History().Record(label)) {
        return false;
    }

    pendingTransactionLabel_ = std::move(label);
    return true;
}

bool EditorSceneCommandController::CommitTransaction() {
    if (!pendingTransactionLabel_.has_value()) {
        return false;
    }

    commandStack_.PushExecuted(EditorSceneHistoryCommand::CreateRecorded(scene_, *pendingTransactionLabel_));
    pendingTransactionLabel_.reset();
    NormalizeHierarchySelectionAfterSceneRestore();
    NotifySceneChanged(true);
    return true;
}

void EditorSceneCommandController::CancelTransaction() {
    if (pendingTransactionLabel_.has_value()) {
        static_cast<void>(scene_.History().Undo());
        NormalizeHierarchySelectionAfterSceneRestore();
        NotifySceneChanged(false);
    }
    pendingTransactionLabel_.reset();
}

bool EditorSceneCommandController::Execute(std::string label, Mutation mutation) {
    if (pendingTransactionLabel_.has_value()) {
        console_.Warning("Edit", "Scene command ignored while another scene transaction is active.");
        return false;
    }

    const std::string labelCopy = label;
    if (!commandStack_.Execute(EditorSceneHistoryCommand::Create(scene_, std::move(label), std::move(mutation)))) {
        console_.Warning("Edit", "Scene command failed: " + labelCopy);
        return false;
    }

    NormalizeHierarchySelectionAfterSceneRestore();
    NotifySceneChanged(true);
    return true;
}

std::vector<EditorHierarchyRow> EditorSceneCommandController::HierarchyRows() const {
    return EditorHierarchyRowBuilder::Build(scene_, hierarchyExpansion_.CollapsedEntities(), hierarchySearch_.Query());
}

void EditorSceneCommandController::NormalizeHierarchySelectionAfterSceneRestore() {
    const kb::scene::SceneEntity selected = hierarchySelection_.Primary();
    if (selected.IsValid() && scene_.Entities().IsAlive(selected)) {
        return;
    }

    const std::vector<EditorHierarchyRow> rows = HierarchyRows();
    for (const EditorHierarchyRow& row : rows) {
        if (row.entity.IsValid() && scene_.Entities().IsAlive(row.entity)) {
            hierarchySelection_.SelectEntity(row.entity);
            assetBrowser_.ClearSelection();
            return;
        }
    }

    hierarchySelection_.Clear();
}

void EditorSceneCommandController::NotifySceneChanged(bool documentChanged) {
    ++sceneRenderRevision_;
    if (sceneRenderRevision_ == 0U) {
        sceneRenderRevision_ = 1U;
    }
    if (documentChanged) {
        sceneDocumentDirty_ = true;
    }
    hierarchyRowsDirty_ = true;
    scene_.Runtime().SynchronizeTransforms();
    EditorSceneGizmoState& gizmo = viewportState_.Gizmo();
    gizmo.hoveredAxis = -1;
    gizmo.draggedAxis = -1;
    gizmo.centerDrag = false;
}

} // namespace kb::editor
