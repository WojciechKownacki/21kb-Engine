#include "EditorTestSupport.hpp"

#include "commands/EditorCommandStack.hpp"

#include <memory>
#include <string_view>

namespace kb::editor::tests {
namespace {

class CountingCommand final : public IEditorCommand {
public:
    explicit CountingCommand(int& value) noexcept
        : value_(value) {}

    [[nodiscard]] std::string_view Label() const noexcept override {
        return "Counting";
    }

    [[nodiscard]] bool Execute() override {
        ++value_;
        return true;
    }

    [[nodiscard]] bool Undo() override {
        --value_;
        return true;
    }

    [[nodiscard]] bool Redo() override {
        ++value_;
        return true;
    }

private:
    int& value_;
};

void RunExecuteUndoRedoTest() {
    EditorCommandStack stack;
    int value = 0;

    Require(stack.Execute(std::make_unique<CountingCommand>(value)), "Command stack did not execute a valid command");
    Require(value == 1, "Command execute did not mutate the target value");
    Require(stack.CanUndo(), "Command stack should be undoable after execute");
    Require(!stack.CanRedo(), "Command stack should not be redoable before undo");

    Require(stack.Undo(), "Command stack undo failed");
    Require(value == 0, "Command undo did not restore the target value");
    Require(!stack.CanUndo(), "Command stack should not be undoable after undoing the only command");
    Require(stack.CanRedo(), "Command stack should be redoable after undo");

    Require(stack.Redo(), "Command stack redo failed");
    Require(value == 1, "Command redo did not reapply the target value");
}

void RunBranchClearsRedoTest() {
    EditorCommandStack stack;
    int value = 0;

    Require(stack.Execute(std::make_unique<CountingCommand>(value)), "First command execute failed");
    Require(stack.Undo(), "First command undo failed");
    Require(stack.Execute(std::make_unique<CountingCommand>(value)), "Branch command execute failed");
    Require(!stack.CanRedo(), "Command stack did not clear redo after a new branch command");
    Require(value == 1, "Branch command did not leave the expected value");
}

} // namespace

void RunEditorCommandStackTests() {
    RunExecuteUndoRedoTest();
    RunBranchClearsRedoTest();
}

} // namespace kb::editor::tests
