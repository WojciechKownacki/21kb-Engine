#include "commands/EditorCommandStack.hpp"

#include <algorithm>
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

    HistoryPartition& partition = PartitionFor(command->HistoryKey());
    partition.undoStack.push_back(std::move(command));
    partition.redoStack.clear();
    Touch(partition);
}

bool EditorCommandStack::CanUndo() const noexcept {
    return std::ranges::any_of(partitions_, [](const HistoryPartition& partition) {
        return !partition.undoStack.empty();
    });
}

bool EditorCommandStack::CanRedo() const noexcept {
    return std::ranges::any_of(partitions_, [](const HistoryPartition& partition) {
        return !partition.redoStack.empty();
    });
}

bool EditorCommandStack::CanUndo(EditorCommandHistoryKey key) const noexcept {
    const HistoryPartition* partition = FindPartition(key);
    return partition != nullptr && !partition->undoStack.empty();
}

bool EditorCommandStack::CanRedo(EditorCommandHistoryKey key) const noexcept {
    const HistoryPartition* partition = FindPartition(key);
    return partition != nullptr && !partition->redoStack.empty();
}

bool EditorCommandStack::Undo() {
    HistoryPartition* partition = MostRecentUndoPartition();
    return partition != nullptr && Undo(*partition);
}

bool EditorCommandStack::Redo() {
    HistoryPartition* partition = MostRecentRedoPartition();
    return partition != nullptr && Redo(*partition);
}

bool EditorCommandStack::Undo(EditorCommandHistoryKey key) {
    HistoryPartition* partition = FindPartition(key);
    return partition != nullptr && Undo(*partition);
}

bool EditorCommandStack::Redo(EditorCommandHistoryKey key) {
    HistoryPartition* partition = FindPartition(key);
    return partition != nullptr && Redo(*partition);
}

bool EditorCommandStack::Undo(HistoryPartition& partition) {
    if (partition.undoStack.empty()) {
        return false;
    }

    std::unique_ptr<IEditorCommand> command = std::move(partition.undoStack.back());
    partition.undoStack.pop_back();
    if (!command->Undo()) {
        partition.undoStack.push_back(std::move(command));
        return false;
    }

    CaptureCompletedCommandMetadata(*command);
    partition.redoStack.push_back(std::move(command));
    Touch(partition);
    return true;
}

bool EditorCommandStack::Redo(HistoryPartition& partition) {
    if (partition.redoStack.empty()) {
        return false;
    }

    std::unique_ptr<IEditorCommand> command = std::move(partition.redoStack.back());
    partition.redoStack.pop_back();
    if (!command->Redo()) {
        partition.redoStack.push_back(std::move(command));
        return false;
    }

    CaptureCompletedCommandMetadata(*command);
    partition.undoStack.push_back(std::move(command));
    Touch(partition);
    return true;
}

void EditorCommandStack::Clear() noexcept {
    partitions_.clear();
}

void EditorCommandStack::Clear(EditorCommandHistoryKey key) noexcept {
    const auto found = std::ranges::find(partitions_, key, &HistoryPartition::key);
    if (found != partitions_.end()) {
        partitions_.erase(found);
    }
}

std::size_t EditorCommandStack::UndoCount() const noexcept {
    std::size_t count = 0U;
    for (const HistoryPartition& partition : partitions_) {
        count += partition.undoStack.size();
    }
    return count;
}

std::size_t EditorCommandStack::RedoCount() const noexcept {
    std::size_t count = 0U;
    for (const HistoryPartition& partition : partitions_) {
        count += partition.redoStack.size();
    }
    return count;
}

std::size_t EditorCommandStack::UndoCount(EditorCommandHistoryKey key) const noexcept {
    const HistoryPartition* partition = FindPartition(key);
    return partition == nullptr ? 0U : partition->undoStack.size();
}

std::size_t EditorCommandStack::RedoCount(EditorCommandHistoryKey key) const noexcept {
    const HistoryPartition* partition = FindPartition(key);
    return partition == nullptr ? 0U : partition->redoStack.size();
}

bool EditorCommandStack::LastCompletedCommandAffectsSceneDocument() const noexcept {
    return lastCompletedCommandAffectsSceneDocument_;
}

bool EditorCommandStack::LastCompletedCommandAffectsHierarchySelection() const noexcept {
    return lastCompletedCommandAffectsHierarchySelection_;
}

bool EditorCommandStack::LastCompletedCommandAffectsOpenMaterialSource() const noexcept {
    return lastCompletedCommandAffectsOpenMaterialSource_;
}

EditorCommandHistoryKey EditorCommandStack::LastCompletedCommandHistoryKey() const noexcept {
    return lastCompletedCommandHistoryKey_;
}

EditorCommandStack::HistoryPartition& EditorCommandStack::PartitionFor(EditorCommandHistoryKey key) {
    if (HistoryPartition* existing = FindPartition(key); existing != nullptr) {
        return *existing;
    }
    partitions_.push_back(HistoryPartition{ .key = key });
    return partitions_.back();
}

EditorCommandStack::HistoryPartition* EditorCommandStack::FindPartition(EditorCommandHistoryKey key) noexcept {
    const auto found = std::ranges::find(partitions_, key, &HistoryPartition::key);
    return found == partitions_.end() ? nullptr : &*found;
}

const EditorCommandStack::HistoryPartition* EditorCommandStack::FindPartition(EditorCommandHistoryKey key) const noexcept {
    const auto found = std::ranges::find(partitions_, key, &HistoryPartition::key);
    return found == partitions_.end() ? nullptr : &*found;
}

EditorCommandStack::HistoryPartition* EditorCommandStack::MostRecentUndoPartition() noexcept {
    HistoryPartition* result = nullptr;
    for (HistoryPartition& partition : partitions_) {
        if (!partition.undoStack.empty() && (result == nullptr || partition.lastOperationSequence > result->lastOperationSequence)) {
            result = &partition;
        }
    }
    return result;
}

EditorCommandStack::HistoryPartition* EditorCommandStack::MostRecentRedoPartition() noexcept {
    HistoryPartition* result = nullptr;
    for (HistoryPartition& partition : partitions_) {
        if (!partition.redoStack.empty() && (result == nullptr || partition.lastOperationSequence > result->lastOperationSequence)) {
            result = &partition;
        }
    }
    return result;
}

void EditorCommandStack::Touch(HistoryPartition& partition) noexcept {
    ++operationSequence_;
    if (operationSequence_ == 0U) {
        operationSequence_ = 1U;
    }
    partition.lastOperationSequence = operationSequence_;
}

void EditorCommandStack::CaptureCompletedCommandMetadata(const IEditorCommand& command) noexcept {
    lastCompletedCommandAffectsSceneDocument_ = command.AffectsSceneDocument();
    lastCompletedCommandAffectsHierarchySelection_ = command.AffectsHierarchySelection();
    lastCompletedCommandAffectsOpenMaterialSource_ = command.AffectsOpenMaterialSource();
    lastCompletedCommandHistoryKey_ = command.HistoryKey();
}

} // namespace kb::editor
