#pragma once

#include "commands/IEditorCommand.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace kb::editor {

class EditorCommandStack {
public:
    [[nodiscard]] bool Execute(std::unique_ptr<IEditorCommand> command);
    void PushExecuted(std::unique_ptr<IEditorCommand> command);

    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool CanUndo(EditorCommandHistoryKey key) const noexcept;
    [[nodiscard]] bool CanRedo(EditorCommandHistoryKey key) const noexcept;
    [[nodiscard]] bool Undo();
    [[nodiscard]] bool Redo();
    [[nodiscard]] bool Undo(EditorCommandHistoryKey key);
    [[nodiscard]] bool Redo(EditorCommandHistoryKey key);
    void Clear() noexcept;
    void Clear(EditorCommandHistoryKey key) noexcept;

    [[nodiscard]] std::size_t UndoCount() const noexcept;
    [[nodiscard]] std::size_t RedoCount() const noexcept;
    [[nodiscard]] std::size_t UndoCount(EditorCommandHistoryKey key) const noexcept;
    [[nodiscard]] std::size_t RedoCount(EditorCommandHistoryKey key) const noexcept;
    [[nodiscard]] bool LastCompletedCommandAffectsSceneDocument() const noexcept;
    [[nodiscard]] bool LastCompletedCommandAffectsHierarchySelection() const noexcept;
    [[nodiscard]] bool LastCompletedCommandAffectsOpenMaterialSource() const noexcept;
    [[nodiscard]] EditorCommandHistoryKey LastCompletedCommandHistoryKey() const noexcept;

private:
    struct HistoryPartition {
        EditorCommandHistoryKey key{};
        std::vector<std::unique_ptr<IEditorCommand>> undoStack;
        std::vector<std::unique_ptr<IEditorCommand>> redoStack;
        std::uint64_t lastOperationSequence = 0U;
    };

    [[nodiscard]] HistoryPartition& PartitionFor(EditorCommandHistoryKey key);
    [[nodiscard]] HistoryPartition* FindPartition(EditorCommandHistoryKey key) noexcept;
    [[nodiscard]] const HistoryPartition* FindPartition(EditorCommandHistoryKey key) const noexcept;
    [[nodiscard]] HistoryPartition* MostRecentUndoPartition() noexcept;
    [[nodiscard]] HistoryPartition* MostRecentRedoPartition() noexcept;
    [[nodiscard]] bool Undo(HistoryPartition& partition);
    [[nodiscard]] bool Redo(HistoryPartition& partition);
    void Touch(HistoryPartition& partition) noexcept;
    void CaptureCompletedCommandMetadata(const IEditorCommand& command) noexcept;

    std::vector<HistoryPartition> partitions_;
    std::uint64_t operationSequence_ = 0U;
    bool lastCompletedCommandAffectsSceneDocument_ = true;
    bool lastCompletedCommandAffectsHierarchySelection_ = true;
    bool lastCompletedCommandAffectsOpenMaterialSource_ = false;
    EditorCommandHistoryKey lastCompletedCommandHistoryKey_{};
};

} // namespace kb::editor
