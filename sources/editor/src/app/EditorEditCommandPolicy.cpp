#include "app/EditorEditCommandPolicy.hpp"

#include "scene/EditorSceneContext.hpp"

#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] bool TextInputActive(const EditorSceneContext& sceneContext) noexcept {
    return sceneContext.IsBuildGameTextEditing() || sceneContext.Inspector().IsTextEditing() ||
        sceneContext.AssetBrowser().IsTextEditing() || sceneContext.IsHierarchyRenaming() ||
        sceneContext.IsHierarchySearchFocused() || sceneContext.IsMaterialGraphConstantInlineEditing() ||
        sceneContext.IsMaterialGraphNodeRenameEditing() || sceneContext.IsMaterialEditorFindFocused() ||
        sceneContext.ParticleEditorWorkspace().RenameActive();
}

// A graph gesture owns the working copy until the mouse-up, in one of two ways:
//
//  - A comment drag and a pin rewire have ALREADY edited the document (MoveGraphCommentGroup writes straight
//    into the working copy; a rewire deletes the link inside an open transaction). Save mid-gesture writes
//    that half-finished document to disk and re-bases the clean snapshot, so cancelling afterwards leaves
//    the editor reporting no unsaved changes over a file holding a state the user never confirmed.
//  - A node drag has not touched the document yet, but it is holding the "before" snapshot its undo record
//    will use. An Undo in between moves the document out from under that snapshot, so the command the
//    mouse-up then records describes a document that no longer existed - and the redo branch is dropped.
//
// Either way the honest answer is "not now".
[[nodiscard]] bool MaterialGraphGestureActive(const EditorSceneContext& sceneContext) noexcept {
    return sceneContext.HasMaterialGraphGestureInFlight();
}

[[nodiscard]] const char* CommandName(EditorEditCommand command) noexcept {
    switch (command) {
    case EditorEditCommand::Undo:
        return "Undo";
    case EditorEditCommand::Redo:
        return "Redo";
    case EditorEditCommand::Duplicate:
        return "Duplicate";
    case EditorEditCommand::Save:
        return "Save";
    }
    return "Command";
}

// A refused Save that says nothing is indistinguishable from a broken Save. Every sibling failure inside the
// guarded operations writes to the console, so this does too - the text-edit refusal stays silent, because
// there the keystroke visibly went into the field the user is typing in.
void WarnIfGestureRefused(EditorSceneContext& sceneContext, EditorEditCommand command) {
    if (!MaterialGraphGestureActive(sceneContext)) {
        return;
    }
    sceneContext.Console().Warning(
        "Materials", std::string{ CommandName(command) } + " is unavailable until the material graph gesture finishes.");
}

[[nodiscard]] bool Run(EditorSceneContext& sceneContext, EditorEditCommand command) {
    switch (command) {
    case EditorEditCommand::Undo:
        if (sceneContext.HasParticleEditorAsset() && sceneContext.ParticleEditorWorkspace().Focused())
            return sceneContext.UndoParticleEditorCommand();
        return sceneContext.UndoSceneCommand();
    case EditorEditCommand::Redo:
        if (sceneContext.HasParticleEditorAsset() && sceneContext.ParticleEditorWorkspace().Focused())
            return sceneContext.RedoParticleEditorCommand();
        return sceneContext.RedoSceneCommand();
    case EditorEditCommand::Duplicate:
        return sceneContext.DuplicateSelectedHierarchyEntities();
    case EditorEditCommand::Save:
        return sceneContext.SaveOpenDocuments();
    }

    return false;
}

} // namespace

bool EditorEditCommandPolicy::CanExecute(const EditorSceneContext& sceneContext) noexcept {
    return !TextInputActive(sceneContext) && !MaterialGraphGestureActive(sceneContext);
}

bool EditorEditCommandPolicy::Execute(EditorSceneContext& sceneContext, EditorEditCommand command) {
    if (!CanExecute(sceneContext)) {
        WarnIfGestureRefused(sceneContext, command);
        return false;
    }
    return Run(sceneContext, command);
}

bool EditorEditCommandPolicy::CanExecuteFromPointer(const EditorSceneContext& sceneContext) noexcept {
    return !MaterialGraphGestureActive(sceneContext);
}

bool EditorEditCommandPolicy::ExecuteFromPointer(EditorSceneContext& sceneContext, EditorEditCommand command) {
    if (!CanExecuteFromPointer(sceneContext)) {
        WarnIfGestureRefused(sceneContext, command);
        return false;
    }
    return Run(sceneContext, command);
}

} // namespace kb::editor
