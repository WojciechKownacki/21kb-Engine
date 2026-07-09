#include "EditorTestSupport.hpp"

#include "commands/EditorCommandStack.hpp"

#include <memory>
#include <string_view>

namespace kb::editor::tests {
namespace {

class CountingCommand final : public IEditorCommand {
public:
    explicit CountingCommand(int& value, EditorCommandHistoryKey historyKey = EditorCommandHistoryKey::Scene()) noexcept
        : value_(value)
        , historyKey_(historyKey) {}

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

    [[nodiscard]] EditorCommandHistoryKey HistoryKey() const noexcept override {
        return historyKey_;
    }

private:
    int& value_;
    EditorCommandHistoryKey historyKey_{};
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

void RunDocumentPartitionIsolationTest() {
    EditorCommandStack stack;
    int sceneValue = 0;
    int materialAValue = 0;
    int materialBValue = 0;
    constexpr EditorCommandHistoryKey materialA = EditorCommandHistoryKey::MaterialAsset(101U);
    constexpr EditorCommandHistoryKey materialB = EditorCommandHistoryKey::MaterialAsset(202U);

    Require(stack.Execute(std::make_unique<CountingCommand>(sceneValue)), "Scene history execute failed");
    Require(stack.Execute(std::make_unique<CountingCommand>(materialAValue, materialA)), "Material A history execute failed");
    Require(stack.Execute(std::make_unique<CountingCommand>(materialBValue, materialB)), "Material B history execute failed");
    Require(sceneValue == 1 && materialAValue == 1 && materialBValue == 1, "Partitioned history setup produced wrong values");

    Require(stack.Undo(materialB), "Material B scoped undo failed");
    Require(materialBValue == 0 && materialAValue == 1 && sceneValue == 1,
        "Material B scoped undo changed another document");
    Require(stack.LastCompletedCommandHistoryKey() == materialB,
        "Scoped undo did not expose the completed document key");

    Require(stack.Undo(materialA), "Material A scoped undo failed");
    Require(materialAValue == 0 && materialBValue == 0 && sceneValue == 1,
        "Material A scoped undo changed another document");
    Require(stack.Redo(materialB), "Material B scoped redo failed");
    Require(materialBValue == 1 && materialAValue == 0 && sceneValue == 1,
        "Material B scoped redo changed another document");

    stack.Clear(materialA);
    Require(!stack.CanUndo(materialA) && !stack.CanRedo(materialA),
        "Closing/reverting Material A did not invalidate both history directions");
    Require(stack.CanUndo(materialB), "Invalidating Material A also removed Material B history");
    Require(stack.Undo(EditorCommandHistoryKey::Scene()) && sceneValue == 0,
        "A closed material partition blocked older scene history");
}

void RunDocumentBranchIsolationTest() {
    EditorCommandStack stack;
    int materialAValue = 0;
    int materialBValue = 0;
    constexpr EditorCommandHistoryKey materialA = EditorCommandHistoryKey::MaterialAsset(303U);
    constexpr EditorCommandHistoryKey materialB = EditorCommandHistoryKey::MaterialAsset(404U);

    Require(stack.Execute(std::make_unique<CountingCommand>(materialAValue, materialA)), "Material A branch setup failed");
    Require(stack.Undo(materialA), "Material A branch undo failed");
    Require(stack.Execute(std::make_unique<CountingCommand>(materialBValue, materialB)), "Material B branch execute failed");
    Require(stack.CanRedo(materialA), "Editing Material B incorrectly cleared Material A redo branch");
    Require(stack.Redo(materialA) && materialAValue == 1 && materialBValue == 1,
        "Material A redo branch was not isolated from Material B");
}

} // namespace

void RunEditorCommandStackTests() {
    RunExecuteUndoRedoTest();
    RunBranchClearsRedoTest();
    RunDocumentPartitionIsolationTest();
    RunDocumentBranchIsolationTest();
}

} // namespace kb::editor::tests
