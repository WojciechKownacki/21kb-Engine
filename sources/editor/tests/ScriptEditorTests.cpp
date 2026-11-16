#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "rendering/script_editor/LuaSyntaxHighlighter.hpp"
#include "rendering/script_editor/ScriptEditorDocument.hpp"
#include "rendering/script_editor/ScriptEditorLayout.hpp"
#include "rendering/script_editor/ScriptProblemService.hpp"
#include "engine/script/ScriptLanguageSubset.hpp"
#include "app/EditorScriptFileWatcher.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>

namespace {

using kb::editor::tests::Require;
using kb::editor::ScriptProblem;
using kb::editor::ScriptProblemService;
using Clock = ScriptProblemService::Clock;
using namespace std::chrono_literals;

// The default analyzer, counting its runs and noting the thread it ran on; `gate` (when set) holds the first run.
struct CountingAnalyzer {
    std::atomic<int> started{ 0 };
    std::atomic<int> finished{ 0 };
    std::atomic<std::thread::id> thread{};
    std::shared_future<void> gate;

    [[nodiscard]] ScriptProblemService::Analyzer Make() {
        return [this](const std::string& text) {
            const int run = ++started;
            thread = std::this_thread::get_id();
            if (run == 1 && gate.valid()) {
                gate.wait();
            }
            std::vector<ScriptProblem> problems = ScriptProblemService::AnalyzeLuaSubset(text);
            ++finished;
            return problems;
        };
    }
};

// Updates until the service has nothing left to do.
void Settle(ScriptProblemService& service, Clock::time_point now, const std::string& text) {
    for (int attempt = 0; !service.Idle(); ++attempt) {
        Require(attempt < 10000, "The script analysis never finished");
        static_cast<void>(service.Update(now, [&text] { return text; }));
        std::this_thread::sleep_for(1ms);
    }
}

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

    const std::uint64_t revision = document.Revision();
    document.MoveHorizontal(1, false);
    Require(document.Revision() == revision, "Moving the caret counted as a text edit");
    Require(!document.IsModified("longline\nab\nlongline2"), "Document reported modified when matching saved text");
    document.InsertText("z");
    Require(document.IsModified("longline\nab\nlongline2"), "Document did not report modified after an edit");
    Require(document.Revision() != revision, "A text edit did not change the document revision");
}

void RunProblemTypingTest() {
    CountingAnalyzer counter;
    ScriptProblemService service{ counter.Make() };
    const Clock::time_point t0 = Clock::now();
    std::string text = "local x = 1\nlocal a = 7 // 2\nlocal b = x & 3\n";
    service.Reset(t0);
    service.TextChanged(t0);
    static_cast<void>(service.Update(t0 + 299ms, [&text] { return text; }));
    Require(counter.started == 0, "The analysis started before the typing delay passed");
    static_cast<void>(service.Update(t0 + 300ms, [&text] { return text; }));
    Settle(service, t0 + 300ms, text);
    Require(counter.started == 1, "The analysis did not start once the delay passed");

    // Same line and column as the validator used at Play and in project validation reports.
    const auto findings = kb::script::FindOutOfSubsetConstructs(text);
    Require(service.Problems().size() == 2U && findings.size() == 2U, "A text with two 5.4-only operators did not give two warnings");
    Require(service.Problems()[0].line == 1 && service.Problems()[0].column == 12 && service.Problems()[0].length == 2, "The '//' warning is on the wrong line or column");
    Require(service.Problems()[1].line == 2 && service.Problems()[1].column == 12 && service.Problems()[1].length == 1, "The '&' warning is on the wrong line or column");
    for (std::size_t i = 0; i < findings.size(); ++i) {
        Require(service.Problems()[i].line + 1 == static_cast<int>(findings[i].line) && service.Problems()[i].column + 1 == static_cast<int>(findings[i].column),
            "The warning position differs from the validator's");
    }

    text = "local x = 1\nlocal a = math.floor(7 / 2)\nlocal b = bit.band(x, 3)\n";
    service.TextChanged(t0 + 1s);
    Settle(service, t0 + 1s + 300ms, text);
    Require(service.Problems().empty(), "The warnings stayed after the text was corrected");
}

void RunProblemDebounceTest() {
    CountingAnalyzer counter;
    ScriptProblemService service{ counter.Make() };
    const Clock::time_point t0 = Clock::now();
    std::string text;
    int textReads = 0;
    const auto read = [&] {
        ++textReads;
        return text;
    };
    for (int edit = 0; edit < 10; ++edit) { // ten edits within 100 ms
        text += "x = " + std::to_string(edit) + " // 2\n";
        service.TextChanged(t0 + std::chrono::milliseconds{ edit * 10 });
        static_cast<void>(service.Update(t0 + std::chrono::milliseconds{ edit * 10 }, read));
    }
    Require(counter.started == 0 && textReads == 0, "The analysis ran while the text was still being typed");
    static_cast<void>(service.Update(t0 + 390ms, read));
    Require(counter.started == 0, "The analysis started before the delay after the last edit");
    static_cast<void>(service.Update(t0 + 400ms, read));
    Settle(service, t0 + 400ms, text);
    Require(counter.started == 1 && textReads == 1, "Ten quick edits did not give exactly one analysis");
    Require(service.Problems().size() == 10U && service.Problems().back().line == 9, "The result is not for the last version of the text");
}

void RunProblemStaleResultTest() {
    std::promise<void> release;
    CountingAnalyzer counter;
    counter.gate = release.get_future().share();
    ScriptProblemService service{ counter.Make() };
    const Clock::time_point t0 = Clock::now();
    std::string text = "a = 1 // 2\n"; // one warning
    service.TextChanged(t0);
    static_cast<void>(service.Update(t0 + 300ms, [&text] { return text; }));
    while (counter.started == 0) {
        std::this_thread::sleep_for(1ms);
    }

    text = "a = 1 // 2\nb = 1 & 2\n"; // edited while the first analysis runs: two warnings
    service.TextChanged(t0 + 310ms);
    release.set_value();
    while (counter.finished == 0) {
        std::this_thread::sleep_for(1ms);
    }
    static_cast<void>(service.Update(t0 + 310ms, [&text] { return text; }));
    Require(service.Problems().empty(), "The result of an older text replaced the newer text's");
    Require(!service.Idle(), "The edited text was not left waiting for its own analysis");
    Settle(service, t0 + 700ms, text);
    Require(counter.started == 2 && service.Problems().size() == 2U, "The edited text was not analysed after the stale result");
}

void RunProblemOffUiThreadTest() {
    std::string text;
    for (int line = 0; line < 10000; ++line) {
        text += "local v" + std::to_string(line) + " = a // b\n";
    }
    std::promise<void> release;
    CountingAnalyzer counter;
    counter.gate = release.get_future().share();
    ScriptProblemService service{ counter.Make() };
    const Clock::time_point t0 = Clock::now();
    service.Reset(t0);
    static_cast<void>(service.Update(t0, [&text] { return text; }));
    // The analysis is held inside the worker, yet the calling thread is free and nothing is reported yet.
    Require(!service.Idle() && service.Problems().empty(), "The UI thread waited for the analysis of a large script");
    while (counter.started == 0) {
        std::this_thread::sleep_for(1ms);
    }
    Require(counter.thread.load() != std::this_thread::get_id(), "The analysis ran on the UI thread");
    release.set_value();
    Settle(service, t0, text);
    Require(service.Problems().size() == 10000U, "A 10000 line script did not give a warning per line");
}

void RunProblemListMatchesValidatorTest() {
    const std::string source = "local a = 1 << 2\nlocal s = \"x // y\" -- 3 | 4\nlocal b <const> = math.pow(2, 3)\nlocal c = 0xFFFFFFFFFFFFFFFF\nlocal d = ~a\nprint(utf8.char(65))\n";
    const auto findings = kb::script::FindOutOfSubsetConstructs(source);
    const std::vector<ScriptProblem> problems = ScriptProblemService::AnalyzeLuaSubset(source);
    Require(!findings.empty() && problems.size() == findings.size(), "The editor list and the validator differ in how many warnings they give");
    for (std::size_t i = 0; i < findings.size(); ++i) {
        Require(problems[i].line + 1 == static_cast<int>(findings[i].line) && problems[i].column + 1 == static_cast<int>(findings[i].column) &&
                problems[i].length == static_cast<int>(findings[i].length) && problems[i].message == findings[i].message,
            "The editor list and the validator differ in a warning");
    }
}

#if defined(_WIN32)
void RunProblemLayoutTest() {
    using kb::editor::ScriptEditorLayout;
    kb::editor::ScriptEditorMetrics metrics{ .charWidth = 8, .lineHeight = 20, .fontHeight = 16 };
    Require(ScriptEditorLayout::ProblemPanelHeight(0, metrics) == 0, "An empty problem list takes room");
    Require(ScriptEditorLayout::ProblemPanelHeight(2, metrics) == 41 && ScriptEditorLayout::ProblemPanelHeight(50, metrics) == 81, "The problem list height is wrong");
    Require(ScriptEditorLayout::ShownProblemCount(3) == 3 && ScriptEditorLayout::ShownProblemCount(50) == 3, "A long problem list does not keep a row for 'and N more'");

    kb::editor::ScriptEditorDocument document;
    document.Load("local a = 1 // 2\nlocal b = 3 & 4\n");
    const std::vector<ScriptProblem> problems = ScriptProblemService::AnalyzeLuaSubset(document.ToText());
    const RECT client{ 0, 0, 400, 300 };
    const kb::editor::ScriptEditorViewport viewport{};
    const int textLeft = ScriptEditorLayout::TextLeft(document, metrics);
    Require(ScriptEditorLayout::ProblemAt(problems, document, viewport, client, metrics, textLeft + 12 * 8 + 4, 5) == 0, "The mouse over '//' did not find its warning");
    Require(ScriptEditorLayout::ProblemAt(problems, document, viewport, client, metrics, textLeft + 13 * 8, 25) == -1, "A warning was found on the wrong line");
    Require(ScriptEditorLayout::ProblemAt(problems, document, viewport, client, metrics, textLeft + 2 * 8, 5) == -1, "A warning was found away from its text");
    Require(ScriptEditorLayout::ProblemAt(problems, document, viewport, client, metrics, textLeft + 12 * 8, 25) == 1, "The mouse over '&' did not find its warning");

    metrics.reservedBottom = ScriptEditorLayout::ProblemPanelHeight(problems.size(), metrics);
    Require(ScriptEditorLayout::VisibleLineCount(client, metrics) == (300 - 41) / 20, "The text area did not shrink for the problem list");
    Require(ScriptEditorLayout::ProblemRowAt(problems.size(), client, metrics, 300 - 41 + 1 + 20 + 5) == 1, "A click on the second row did not find its warning");
    Require(ScriptEditorLayout::ProblemRowAt(problems.size(), client, metrics, 100) == -1, "A click in the text found a list row");
}
#endif

void RunExternalSaveObservationTest() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "21kb_script_external_save_test.lua";
    {
        std::ofstream file{ path, std::ios::binary | std::ios::trunc };
        file << "function Tick(self, dt) end\n";
        Require(file.good(), "External save test could not create a script file");
    }

    kb::editor::EditorScriptFileWatcher watcher;
    watcher.Track(path);
    Require(!watcher.HasChangedOnDisk(), "Opening a script was reported as an external save");
    const auto initialWriteTime = std::filesystem::last_write_time(path);
    std::filesystem::last_write_time(path, initialWriteTime + std::chrono::seconds{ 2 });
    Require(watcher.HasChangedOnDisk(), "An external script save was not observed");
    watcher.Acknowledge();
    Require(!watcher.HasChangedOnDisk(), "An acknowledged script save was reported again");
    watcher.Track({});
    Require(!watcher.HasChangedOnDisk(), "An untracked script was reported as changed");
    std::error_code error;
    std::filesystem::remove(path, error);
}

} // namespace

namespace kb::editor::tests {

void RunScriptEditorTests() {
    RunHighlighterTest();
    RunDocumentTest();
    RunProblemTypingTest();
    RunProblemDebounceTest();
    RunProblemStaleResultTest();
    RunProblemOffUiThreadTest();
    RunProblemListMatchesValidatorTest();
#if defined(_WIN32)
    RunProblemLayoutTest();
#endif
    RunExternalSaveObservationTest();
}

} // namespace kb::editor::tests
