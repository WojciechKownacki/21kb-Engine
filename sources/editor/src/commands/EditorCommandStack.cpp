#include "commands/EditorCommandStack.hpp"

#include <utility>

namespace kb::editor {

bool EditorCommandStack::Execute(std::unique_ptr<IEditorCommand> command) {
    if (command == nullptr || !command->Execute()) {
        return false;
    }

    CaptureCompletedCommandMetadata(*command);
    PushExecuted(std::move(command));
    return true;
}

void EditorCommandStack::PushExecuted(std::unique_ptr<IEditorCommand> command) {
    if (command == nullptr) {
        return;
    }

    undoStack_.push_back(std::move(command));
    redoStack_.clear();
}

bool EditorCommandStack::CanUndo() const noexcept {
    return !undoStack_.empty();
}

bool EditorCommandStack::CanRedo() const noexcept {
    return !redoStack_.empty();
}

bool EditorCommandStack::Undo() {
    if (undoStack_.empty()) {
        return false;
    }

    std::unique_ptr<IEditorCommand> command = std::move(undoStack_.back());
    undoStack_.pop_back();
    if (!command->Undo()) {
        undoStack_.push_back(std::move(command));
        return false;
    }

    CaptureCompletedCommandMetadata(*command);
    redoStack_.push_back(std::move(command));
    return true;
}

bool EditorCommandStack::Redo() {
    if (redoStack_.empty()) {
        return false;
    }

    std::unique_ptr<IEditorCommand> command = std::move(redoStack_.back());
    redoStack_.pop_back();
    if (!command->Redo()) {
        redoStack_.push_back(std::move(command));
        return false;
    }

    CaptureCompletedCommandMetadata(*command);
    undoStack_.push_back(std::move(command));
    return true;
}

void EditorCommandStack::Clear() noexcept {
    undoStack_.clear();
    redoStack_.clear();
}

std::size_t EditorCommandStack::UndoCount() const noexcept {
    return undoStack_.size();
}

std::size_t EditorCommandStack::RedoCount() const noexcept {
    return redoStack_.size();
}

bool EditorCommandStack::LastCompletedCommandAffectsSceneDocument() const noexcept {
    return lastCompletedCommandAffectsSceneDocument_;
}

bool EditorCommandStack::LastCompletedCommandAffectsHierarchySelection() const noexcept {
    return lastCompletedCommandAffectsHierarchySelection_;
}

void EditorCommandStack::CaptureCompletedCommandMetadata(const IEditorCommand& command) noexcept {
    lastCompletedCommandAffectsSceneDocument_ = command.AffectsSceneDocument();
    lastCompletedCommandAffectsHierarchySelection_ = command.AffectsHierarchySelection();
}

} // namespace kb::editor
