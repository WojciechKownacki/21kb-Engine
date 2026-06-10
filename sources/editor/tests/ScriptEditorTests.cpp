#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "rendering/script_editor/LuaSyntaxHighlighter.hpp"
#include "rendering/script_editor/ScriptEditorDocument.hpp"

#include <string>

namespace {

using kb::editor::tests::Require;

void RunHighlighterTest() {
    bool keyword = false;
    bool string = false;
    bool comment = false;
    for (const kb::editor::ScriptToken& token : kb::editor::LuaSyntaxHighlighter::Tokenize(R"(local x = "hi" -- note)")) {
        keyword = keyword || token.kind == kb::editor::ScriptTokenKind::Keyword;
        string = string || token.kind == kb::editor::ScriptTokenKind::String;
        comment = comment || token.kind == kb::editor::ScriptTokenKind::Comment;
    }
    Require(keyword, "Lua highlighter did not classify 'local' as a keyword");
    Require(string, "Lua highlighter did not classify a string literal");
    Require(comment, "Lua highlighter did not classify a line comment");

    bool function = false;
    for (const kb::editor::ScriptToken& token : kb::editor::LuaSyntaxHighlighter::Tokenize("Log(\"x\")")) {
        function = function || token.kind == kb::editor::ScriptTokenKind::Function;
    }
    Require(function, "Lua highlighter did not classify an identifier before '(' as a function");
}

void RunDocumentTest() {
    kb::editor::ScriptEditorDocument document;
    document.Load("alpha\nbeta\ngamma");
    Require(document.LineCount() == 3, "Document did not split into three lines");
    Require(document.ToText() == "alpha\nbeta\ngamma", "Document text did not round-trip");

    document.InsertText("X");
    Require(document.Lines()[0] == "Xalpha", "Insert at caret start failed");

    document.SetCaret(kb::editor::ScriptCaret{ 0, 6 }, false);
    document.InsertText("\n");
    Require(document.LineCount() == 4, "Newline insert did not split the line");

    // Selection + delete + undo/redo.
    document.Load("hello world");
    document.SetCaret(kb::editor::ScriptCaret{ 0, 0 }, false);
    document.SetCaret(kb::editor::ScriptCaret{ 0, 5 }, true);
    Require(document.HasSelection(), "Shift-click selection was not recorded");
    document.PushUndo();
    document.DeleteSelection();
    Require(document.Lines()[0] == " world", "Selection delete failed");
    Require(document.Undo(), "Undo returned false");
    Require(document.Lines()[0] == "hello world", "Undo did not restore the deleted text");
    Require(document.Redo(), "Redo returned false");
    Require(document.Lines()[0] == " world", "Redo did not re-apply the delete");

    // Vertical movement keeps the desired column across short lines.
    document.Load("longline\nab\nlongline2");
    document.SetCaret(kb::editor::ScriptCaret{ 0, 6 }, false);
    document.MoveVertical(1, false);
    Require(document.Caret().line == 1 && document.Caret().column == 2, "Vertical move did not clamp to the short line");
    document.MoveVertical(1, false);
    Require(document.Caret().column == 6, "Vertical move did not restore the desired column");

    Require(!document.IsModified("longline\nab\nlongline2"), "Document reported modified when matching saved text");
    document.InsertText("z");
    Require(document.IsModified("longline\nab\nlongline2"), "Document did not report modified after an edit");
}

} // namespace

namespace kb::editor::tests {

void RunScriptEditorTests() {
    RunHighlighterTest();
    RunDocumentTest();
}

} // namespace kb::editor::tests
