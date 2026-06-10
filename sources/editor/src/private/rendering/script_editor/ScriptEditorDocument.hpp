#pragma once

#include <string>
#include <vector>

namespace kb::editor {

struct ScriptCaret {
    int line = 0;
    int column = 0;
};

// The editable text model for the script editor: a list of lines, a caret, a
// selection anchor and an undo/redo history. Pure C++ (no Win32), so it is the
// single source of truth for editing and is unit-tested in isolation. View state
// (scrolling, fonts) lives elsewhere.
class ScriptEditorDocument {
public:
    void Load(const std::string& text);
    [[nodiscard]] std::string ToText() const;
    [[nodiscard]] bool IsModified(const std::string& savedText) const;

    [[nodiscard]] const std::vector<std::string>& Lines() const noexcept { return lines_; }
    [[nodiscard]] int LineCount() const noexcept;
    [[nodiscard]] int LineLength(int line) const noexcept;

    [[nodiscard]] ScriptCaret Caret() const noexcept { return caret_; }
    [[nodiscard]] ScriptCaret Anchor() const noexcept { return anchor_; }
    [[nodiscard]] bool HasSelection() const noexcept;
    void OrderedSelection(ScriptCaret& start, ScriptCaret& end) const noexcept;

    // Caret placement (e.g. from a mouse click). Collapses the selection unless
    // extendSelection keeps the existing anchor.
    void SetCaret(ScriptCaret caret, bool extendSelection);
    void SelectAll();

    // Mutations. Callers wrap structural edits in PushUndo() to group history.
    void InsertText(const std::string& text);
    void DeleteSelection();
    void DeleteBackward();
    void DeleteForward();

    // Caret movement; extend keeps the selection anchor.
    void MoveHorizontal(int delta, bool extend);
    void MoveVertical(int delta, bool extend);
    void MoveLineStart(bool extend);
    void MoveLineEnd(bool extend);

    void PushUndo();
    bool Undo();
    bool Redo();
    void ClearHistory() noexcept;

private:
    struct Snapshot {
        std::vector<std::string> lines;
        ScriptCaret caret;
    };

    void ClampCaret() noexcept;
    [[nodiscard]] bool CaretBeforeAnchor() const noexcept;
    bool RestoreFrom(std::vector<Snapshot>& from, std::vector<Snapshot>& to);

    std::vector<std::string> lines_{ std::string{} };
    ScriptCaret caret_;
    ScriptCaret anchor_;
    int desiredColumn_ = 0;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
};

} // namespace kb::editor
