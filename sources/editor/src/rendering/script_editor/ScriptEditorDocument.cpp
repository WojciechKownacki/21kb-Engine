#include "rendering/script_editor/ScriptEditorDocument.hpp"

#include <algorithm>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::size_t kMaxUndo = 256U;

} // namespace

void ScriptEditorDocument::Load(const std::string& text) {
    lines_.clear();
    std::string current;
    for (const char ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            lines_.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    lines_.push_back(std::move(current));
    if (lines_.empty()) {
        lines_.emplace_back();
    }
    caret_ = {};
    anchor_ = {};
    desiredColumn_ = 0;
    ClearHistory();
}

std::string ScriptEditorDocument::ToText() const {
    std::string text;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        text += lines_[i];
        if (i + 1 < lines_.size()) {
            text.push_back('\n');
        }
    }
    return text;
}

bool ScriptEditorDocument::IsModified(const std::string& savedText) const {
    return ToText() != savedText;
}

int ScriptEditorDocument::LineCount() const noexcept {
    return static_cast<int>(lines_.size());
}

int ScriptEditorDocument::LineLength(int line) const noexcept {
    return (line >= 0 && line < LineCount()) ? static_cast<int>(lines_[static_cast<std::size_t>(line)].size()) : 0;
}

bool ScriptEditorDocument::HasSelection() const noexcept {
    return caret_.line != anchor_.line || caret_.column != anchor_.column;
}

bool ScriptEditorDocument::CaretBeforeAnchor() const noexcept {
    return caret_.line < anchor_.line || (caret_.line == anchor_.line && caret_.column < anchor_.column);
}

void ScriptEditorDocument::OrderedSelection(ScriptCaret& start, ScriptCaret& end) const noexcept {
    if (CaretBeforeAnchor()) {
        start = caret_;
        end = anchor_;
    } else {
        start = anchor_;
        end = caret_;
    }
}

void ScriptEditorDocument::ClampCaret() noexcept {
    caret_.line = std::clamp(caret_.line, 0, LineCount() - 1);
    caret_.column = std::clamp(caret_.column, 0, LineLength(caret_.line));
}

void ScriptEditorDocument::SetCaret(ScriptCaret caret, bool extendSelection) {
    caret_ = caret;
    ClampCaret();
    desiredColumn_ = caret_.column;
    if (!extendSelection) {
        anchor_ = caret_;
    }
}

void ScriptEditorDocument::SelectAll() {
    anchor_ = ScriptCaret{ 0, 0 };
    caret_ = ScriptCaret{ LineCount() - 1, LineLength(LineCount() - 1) };
    desiredColumn_ = caret_.column;
}

void ScriptEditorDocument::DeleteSelection() {
    if (!HasSelection()) {
        return;
    }
    ScriptCaret start;
    ScriptCaret end;
    OrderedSelection(start, end);
    std::string& startLine = lines_[static_cast<std::size_t>(start.line)];
    const std::string& endLine = lines_[static_cast<std::size_t>(end.line)];
    startLine = startLine.substr(0, static_cast<std::size_t>(start.column)) + endLine.substr(static_cast<std::size_t>(end.column));
    if (end.line > start.line) {
        lines_.erase(lines_.begin() + start.line + 1, lines_.begin() + end.line + 1);
    }
    caret_ = start;
    anchor_ = start;
    desiredColumn_ = caret_.column;
}

void ScriptEditorDocument::InsertText(const std::string& text) {
    DeleteSelection();
    std::string& line = lines_[static_cast<std::size_t>(caret_.line)];
    const std::string tail = line.substr(static_cast<std::size_t>(caret_.column));
    line = line.substr(0, static_cast<std::size_t>(caret_.column));

    std::vector<std::string> inserted{ std::string{} };
    for (const char ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            inserted.emplace_back();
        } else {
            inserted.back().push_back(ch);
        }
    }

    if (inserted.size() == 1U) {
        line += inserted[0];
        caret_.column += static_cast<int>(inserted[0].size());
        line += tail;
    } else {
        line += inserted.front();
        std::vector<std::string> newLines;
        for (std::size_t i = 1; i + 1 < inserted.size(); ++i) {
            newLines.push_back(inserted[i]);
        }
        std::string lastLine = inserted.back();
        caret_.line += static_cast<int>(inserted.size()) - 1;
        caret_.column = static_cast<int>(lastLine.size());
        lastLine += tail;
        newLines.push_back(std::move(lastLine));
        lines_.insert(lines_.begin() + (caret_.line - static_cast<int>(newLines.size()) + 1), newLines.begin(), newLines.end());
    }
    anchor_ = caret_;
    desiredColumn_ = caret_.column;
}

void ScriptEditorDocument::DeleteBackward() {
    if (HasSelection()) {
        DeleteSelection();
        return;
    }
    if (caret_.column > 0) {
        lines_[static_cast<std::size_t>(caret_.line)].erase(static_cast<std::size_t>(caret_.column) - 1, 1);
        --caret_.column;
    } else if (caret_.line > 0) {
        const std::string removed = lines_[static_cast<std::size_t>(caret_.line)];
        lines_.erase(lines_.begin() + caret_.line);
        --caret_.line;
        caret_.column = LineLength(caret_.line);
        lines_[static_cast<std::size_t>(caret_.line)] += removed;
    }
    anchor_ = caret_;
    desiredColumn_ = caret_.column;
}

void ScriptEditorDocument::DeleteForward() {
    if (HasSelection()) {
        DeleteSelection();
        return;
    }
    std::string& line = lines_[static_cast<std::size_t>(caret_.line)];
    if (caret_.column < static_cast<int>(line.size())) {
        line.erase(static_cast<std::size_t>(caret_.column), 1);
    } else if (caret_.line + 1 < LineCount()) {
        line += lines_[static_cast<std::size_t>(caret_.line) + 1];
        lines_.erase(lines_.begin() + caret_.line + 1);
    }
    anchor_ = caret_;
}

void ScriptEditorDocument::MoveHorizontal(int delta, bool extend) {
    if (delta < 0) {
        if (caret_.column > 0) {
            --caret_.column;
        } else if (caret_.line > 0) {
            --caret_.line;
            caret_.column = LineLength(caret_.line);
        }
    } else {
        if (caret_.column < LineLength(caret_.line)) {
            ++caret_.column;
        } else if (caret_.line + 1 < LineCount()) {
            ++caret_.line;
            caret_.column = 0;
        }
    }
    desiredColumn_ = caret_.column;
    if (!extend) {
        anchor_ = caret_;
    }
}

void ScriptEditorDocument::MoveVertical(int delta, bool extend) {
    caret_.line = std::clamp(caret_.line + delta, 0, LineCount() - 1);
    caret_.column = std::min(desiredColumn_, LineLength(caret_.line));
    if (!extend) {
        anchor_ = caret_;
    }
}

void ScriptEditorDocument::MoveLineStart(bool extend) {
    caret_.column = 0;
    desiredColumn_ = 0;
    if (!extend) {
        anchor_ = caret_;
    }
}

void ScriptEditorDocument::MoveLineEnd(bool extend) {
    caret_.column = LineLength(caret_.line);
    desiredColumn_ = caret_.column;
    if (!extend) {
        anchor_ = caret_;
    }
}

void ScriptEditorDocument::PushUndo() {
    undo_.push_back(Snapshot{ lines_, caret_ });
    if (undo_.size() > kMaxUndo) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
}

bool ScriptEditorDocument::RestoreFrom(std::vector<Snapshot>& from, std::vector<Snapshot>& to) {
    if (from.empty()) {
        return false;
    }
    to.push_back(Snapshot{ lines_, caret_ });
    Snapshot snapshot = std::move(from.back());
    from.pop_back();
    lines_ = std::move(snapshot.lines);
    caret_ = snapshot.caret;
    anchor_ = snapshot.caret;
    desiredColumn_ = caret_.column;
    ClampCaret();
    return true;
}

bool ScriptEditorDocument::Undo() {
    return RestoreFrom(undo_, redo_);
}

bool ScriptEditorDocument::Redo() {
    return RestoreFrom(redo_, undo_);
}

void ScriptEditorDocument::ClearHistory() noexcept {
    undo_.clear();
    redo_.clear();
}

} // namespace kb::editor
