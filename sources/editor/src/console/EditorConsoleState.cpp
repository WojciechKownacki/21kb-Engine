#include "console/EditorConsoleState.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace kb::editor {

const EditorConsoleEntry* EditorConsoleState::SelectedEntry() const noexcept {
    for (const EditorConsoleEntry& entry : entries_) {
        if (entry.sequence == selectedSequence_) {
            return &entry;
        }
    }
    return nullptr;
}

std::uint32_t EditorConsoleState::Count(EditorConsoleLevel level) const noexcept {
    switch (level) {
    case EditorConsoleLevel::Info:
        return infoCount_;
    case EditorConsoleLevel::Warning:
        return warningCount_;
    case EditorConsoleLevel::Error:
        return errorCount_;
    }
    return 0;
}

bool EditorConsoleState::Accepts(EditorConsoleLevel level) const noexcept {
    switch (level) {
    case EditorConsoleLevel::Info:
        return showInfo_;
    case EditorConsoleLevel::Warning:
        return showWarnings_;
    case EditorConsoleLevel::Error:
        return showErrors_;
    }
    return true;
}

void EditorConsoleState::Select(std::uint64_t sequence) noexcept {
    if (selectedSequence_ != sequence) {
        detailScrollLine_ = 0;
    }
    selectedSequence_ = sequence;
}

void EditorConsoleState::ClearSelection() noexcept {
    selectedSequence_ = 0;
    detailScrollLine_ = 0;
}

void EditorConsoleState::SetDetailHeight(int height) noexcept {
    detailHeight_ = std::clamp(height, 54, 220);
}

void EditorConsoleState::SetDetailScrollLine(int line, int maxLine) noexcept {
    detailScrollLine_ = std::clamp(line, 0, std::max(0, maxLine));
}

void EditorConsoleState::BeginDetailScrollbarDrag(int y) noexcept {
    detailScrollbarDragging_ = true;
    detailScrollbarDragStartY_ = y;
    detailScrollbarDragStartLine_ = detailScrollLine_;
}

void EditorConsoleState::DragDetailScrollbar(int y, int trackPixels, int maxLine) noexcept {
    if (!detailScrollbarDragging_) {
        return;
    }
    const int travel = std::max(1, trackPixels);
    const int delta = y - detailScrollbarDragStartY_;
    const int lineDelta = (delta * std::max(1, maxLine)) / travel;
    SetDetailScrollLine(detailScrollbarDragStartLine_ + lineDelta, maxLine);
}

void EditorConsoleState::SetListScrollRow(int row, int maxRow) noexcept {
    listScrollRow_ = std::clamp(row, 0, std::max(0, maxRow));
}

void EditorConsoleState::BeginListScrollbarDrag(int y) noexcept {
    listScrollbarDragging_ = true;
    listScrollbarDragStartY_ = y;
    listScrollbarDragStartRow_ = listScrollRow_;
}

void EditorConsoleState::DragListScrollbar(int y, int trackPixels, int maxRow) noexcept {
    if (!listScrollbarDragging_) {
        return;
    }
    const int travel = std::max(1, trackPixels);
    const int delta = y - listScrollbarDragStartY_;
    const int rowDelta = (delta * std::max(1, maxRow)) / travel;
    SetListScrollRow(listScrollbarDragStartRow_ + rowDelta, maxRow);
}

bool EditorConsoleState::SetHoveredButton(EditorConsoleButton button) noexcept {
    if (hoveredButton_ == button) {
        return false;
    }
    hoveredButton_ = button;
    return true;
}

void EditorConsoleState::Clear() noexcept {
    entries_.clear();
    infoCount_ = 0;
    warningCount_ = 0;
    errorCount_ = 0;
    selectedSequence_ = 0;
    detailScrollLine_ = 0;
    detailScrollbarDragging_ = false;
    listScrollRow_ = 0;
    listScrollbarDragging_ = false;
}

void EditorConsoleState::Info(std::string category, std::string message) {
    Append(EditorConsoleLevel::Info, std::move(category), std::move(message));
}

void EditorConsoleState::Warning(std::string category, std::string message) {
    Append(EditorConsoleLevel::Warning, std::move(category), std::move(message));
}

void EditorConsoleState::Error(std::string category, std::string message) {
    Append(EditorConsoleLevel::Error, std::move(category), std::move(message));
}

std::uint64_t EditorConsoleState::NowMs() noexcept {
    using Clock = std::chrono::system_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}

void EditorConsoleState::Append(EditorConsoleLevel level, std::string category, std::string message) {
    if (entries_.size() == kCapacity) {
        const EditorConsoleLevel removed = entries_.front().level;
        entries_.erase(entries_.begin());
        Decrement(removed);
    }

    entries_.push_back(EditorConsoleEntry{
        .sequence = ++nextSequence_,
        .timestampMs = NowMs(),
        .level = level,
        .category = std::move(category),
        .message = std::move(message),
    });
    Increment(level);
}

void EditorConsoleState::Increment(EditorConsoleLevel level) noexcept {
    switch (level) {
    case EditorConsoleLevel::Info:
        ++infoCount_;
        break;
    case EditorConsoleLevel::Warning:
        ++warningCount_;
        break;
    case EditorConsoleLevel::Error:
        ++errorCount_;
        break;
    }
}

void EditorConsoleState::Decrement(EditorConsoleLevel level) noexcept {
    switch (level) {
    case EditorConsoleLevel::Info:
        infoCount_ = infoCount_ > 0 ? infoCount_ - 1 : 0;
        break;
    case EditorConsoleLevel::Warning:
        warningCount_ = warningCount_ > 0 ? warningCount_ - 1 : 0;
        break;
    case EditorConsoleLevel::Error:
        errorCount_ = errorCount_ > 0 ? errorCount_ - 1 : 0;
        break;
    }
}

} // namespace kb::editor
