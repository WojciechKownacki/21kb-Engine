#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

enum class EditorConsoleLevel {
    Info,
    Warning,
    Error,
};

enum class EditorConsoleButton {
    None,
    CopyLine,
    SaveLog,
    Clear,
};

struct EditorConsoleEntry {
    std::uint64_t sequence = 0;
    std::uint64_t timestampMs = 0;
    EditorConsoleLevel level = EditorConsoleLevel::Info;
    std::string category;
    std::string message;
};

class EditorConsoleState {
public:
    [[nodiscard]] const std::vector<EditorConsoleEntry>& Entries() const noexcept {
        return entries_;
    }

    [[nodiscard]] bool ShowInfo() const noexcept {
        return showInfo_;
    }

    [[nodiscard]] bool ShowWarnings() const noexcept {
        return showWarnings_;
    }

    [[nodiscard]] bool ShowErrors() const noexcept {
        return showErrors_;
    }

    [[nodiscard]] bool HasSelection() const noexcept {
        return selectedSequence_ != 0;
    }

    [[nodiscard]] std::uint64_t SelectedSequence() const noexcept {
        return selectedSequence_;
    }

    [[nodiscard]] const EditorConsoleEntry* SelectedEntry() const noexcept {
        for (const EditorConsoleEntry& entry : entries_) {
            if (entry.sequence == selectedSequence_) {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] int DetailHeight() const noexcept {
        return detailHeight_;
    }

    [[nodiscard]] bool IsDetailResizeDragging() const noexcept {
        return detailResizeDragging_;
    }

    [[nodiscard]] int DetailScrollLine() const noexcept {
        return detailScrollLine_;
    }

    [[nodiscard]] bool IsDetailScrollbarDragging() const noexcept {
        return detailScrollbarDragging_;
    }

    [[nodiscard]] int ListScrollRow() const noexcept {
        return listScrollRow_;
    }

    [[nodiscard]] bool IsListScrollbarDragging() const noexcept {
        return listScrollbarDragging_;
    }

    [[nodiscard]] EditorConsoleButton HoveredButton() const noexcept {
        return hoveredButton_;
    }

    [[nodiscard]] EditorConsoleButton PressedButton() const noexcept {
        return pressedButton_;
    }

    [[nodiscard]] std::uint32_t Count(EditorConsoleLevel level) const noexcept {
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

    [[nodiscard]] bool Accepts(EditorConsoleLevel level) const noexcept {
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

    void ToggleInfo() noexcept {
        showInfo_ = !showInfo_;
    }

    void ToggleWarnings() noexcept {
        showWarnings_ = !showWarnings_;
    }

    void ToggleErrors() noexcept {
        showErrors_ = !showErrors_;
    }

    void Select(std::uint64_t sequence) noexcept {
        if (selectedSequence_ != sequence) {
            detailScrollLine_ = 0;
        }
        selectedSequence_ = sequence;
    }

    void ClearSelection() noexcept {
        selectedSequence_ = 0;
        detailScrollLine_ = 0;
    }

    void SetDetailHeight(int height) noexcept {
        detailHeight_ = std::clamp(height, 54, 220);
    }

    void BeginDetailResizeDrag() noexcept {
        detailResizeDragging_ = true;
    }

    void EndDetailResizeDrag() noexcept {
        detailResizeDragging_ = false;
    }

    void SetDetailScrollLine(int line, int maxLine) noexcept {
        detailScrollLine_ = std::clamp(line, 0, std::max(0, maxLine));
    }

    void BeginDetailScrollbarDrag(int y) noexcept {
        detailScrollbarDragging_ = true;
        detailScrollbarDragStartY_ = y;
        detailScrollbarDragStartLine_ = detailScrollLine_;
    }

    void DragDetailScrollbar(int y, int trackPixels, int maxLine) noexcept {
        if (!detailScrollbarDragging_) {
            return;
        }
        const int travel = std::max(1, trackPixels);
        const int delta = y - detailScrollbarDragStartY_;
        const int lineDelta = (delta * std::max(1, maxLine)) / travel;
        SetDetailScrollLine(detailScrollbarDragStartLine_ + lineDelta, maxLine);
    }

    void EndDetailScrollbarDrag() noexcept {
        detailScrollbarDragging_ = false;
    }

    void SetListScrollRow(int row, int maxRow) noexcept {
        listScrollRow_ = std::clamp(row, 0, std::max(0, maxRow));
    }

    void BeginListScrollbarDrag(int y) noexcept {
        listScrollbarDragging_ = true;
        listScrollbarDragStartY_ = y;
        listScrollbarDragStartRow_ = listScrollRow_;
    }

    void DragListScrollbar(int y, int trackPixels, int maxRow) noexcept {
        if (!listScrollbarDragging_) {
            return;
        }
        const int travel = std::max(1, trackPixels);
        const int delta = y - listScrollbarDragStartY_;
        const int rowDelta = (delta * std::max(1, maxRow)) / travel;
        SetListScrollRow(listScrollbarDragStartRow_ + rowDelta, maxRow);
    }

    void EndListScrollbarDrag() noexcept {
        listScrollbarDragging_ = false;
    }

    [[nodiscard]] bool SetHoveredButton(EditorConsoleButton button) noexcept {
        if (hoveredButton_ == button) {
            return false;
        }
        hoveredButton_ = button;
        return true;
    }

    void PressButton(EditorConsoleButton button) noexcept {
        pressedButton_ = button;
    }

    void ReleaseButton() noexcept {
        pressedButton_ = EditorConsoleButton::None;
    }

    void Clear() noexcept {
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

    void Info(std::string category, std::string message) {
        Append(EditorConsoleLevel::Info, std::move(category), std::move(message));
    }

    void Warning(std::string category, std::string message) {
        Append(EditorConsoleLevel::Warning, std::move(category), std::move(message));
    }

    void Error(std::string category, std::string message) {
        Append(EditorConsoleLevel::Error, std::move(category), std::move(message));
    }

private:
    static constexpr std::size_t kCapacity = 1000;

    [[nodiscard]] static std::uint64_t NowMs() noexcept {
        using Clock = std::chrono::system_clock;
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
    }

    void Append(EditorConsoleLevel level, std::string category, std::string message) {
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

    void Increment(EditorConsoleLevel level) noexcept {
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

    void Decrement(EditorConsoleLevel level) noexcept {
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

    std::vector<EditorConsoleEntry> entries_;
    std::uint64_t nextSequence_ = 0;
    std::uint64_t selectedSequence_ = 0;
    std::uint32_t infoCount_ = 0;
    std::uint32_t warningCount_ = 0;
    std::uint32_t errorCount_ = 0;
    bool showInfo_ = true;
    bool showWarnings_ = true;
    bool showErrors_ = true;
    bool detailResizeDragging_ = false;
    bool detailScrollbarDragging_ = false;
    bool listScrollbarDragging_ = false;
    EditorConsoleButton hoveredButton_ = EditorConsoleButton::None;
    EditorConsoleButton pressedButton_ = EditorConsoleButton::None;
    int detailHeight_ = 72;
    int detailScrollLine_ = 0;
    int detailScrollbarDragStartY_ = 0;
    int detailScrollbarDragStartLine_ = 0;
    int listScrollRow_ = 0;
    int listScrollbarDragStartY_ = 0;
    int listScrollbarDragStartRow_ = 0;
};

} // namespace kb::editor
