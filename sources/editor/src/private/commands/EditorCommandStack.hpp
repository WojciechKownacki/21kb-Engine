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
    [[nodiscard]] bool Undo();
    [[nodiscard]] bool Redo();
    void Clear() noexcept;

    [[nodiscard]] std::size_t UndoCount() const noexcept;
    [[nodiscard]] std::size_t RedoCount() const noexcept;

private:
    std::vector<std::unique_ptr<IEditorCommand>> undoStack_;
    std::vector<std::unique_ptr<IEditorCommand>> redoStack_;
};

} // namespace kb::editor
