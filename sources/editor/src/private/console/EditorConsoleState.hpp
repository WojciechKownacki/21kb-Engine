#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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
    [[nodiscard]] const std::vector<EditorConsoleEntry>& Entries() const noexcept { return entries_; }
    [[nodiscard]] bool ShowInfo() const noexcept { return showInfo_; }
    [[nodiscard]] bool ShowWarnings() const noexcept { return showWarnings_; }
    [[nodiscard]] bool ShowErrors() const noexcept { return showErrors_; }
    [[nodiscard]] bool HasSelection() const noexcept { return selectedSequence_ != 0; }
    [[nodiscard]] std::uint64_t SelectedSequence() const noexcept { return selectedSequence_; }
    [[nodiscard]] const EditorConsoleEntry* SelectedEntry() const noexcept;
    [[nodiscard]] int DetailHeight() const noexcept { return detailHeight_; }
    [[nodiscard]] bool IsDetailResizeDragging() const noexcept { return detailResizeDragging_; }
    [[nodiscard]] int DetailScrollLine() const noexcept { return detailScrollLine_; }
    [[nodiscard]] bool IsDetailScrollbarDragging() const noexcept { return detailScrollbarDragging_; }
    [[nodiscard]] int ListScrollRow() const noexcept { return listScrollRow_; }
    [[nodiscard]] bool IsListScrollbarDragging() const noexcept { return listScrollbarDragging_; }
    [[nodiscard]] EditorConsoleButton HoveredButton() const noexcept { return hoveredButton_; }
    [[nodiscard]] EditorConsoleButton PressedButton() const noexcept { return pressedButton_; }
    [[nodiscard]] std::uint32_t Count(EditorConsoleLevel level) const noexcept;
    [[nodiscard]] bool Accepts(EditorConsoleLevel level) const noexcept;
    [[nodiscard]] static constexpr std::size_t Capacity() noexcept { return kCapacity; }

    void ToggleInfo() noexcept { showInfo_ = !showInfo_; }
    void ToggleWarnings() noexcept { showWarnings_ = !showWarnings_; }
    void ToggleErrors() noexcept { showErrors_ = !showErrors_; }
    void Select(std::uint64_t sequence) noexcept;
    void ClearSelection() noexcept;
    void SetDetailHeight(int height) noexcept;
    void BeginDetailResizeDrag() noexcept { detailResizeDragging_ = true; }
    void EndDetailResizeDrag() noexcept { detailResizeDragging_ = false; }
    void SetDetailScrollLine(int line, int maxLine) noexcept;
    void BeginDetailScrollbarDrag(int y) noexcept;
    void DragDetailScrollbar(int y, int trackPixels, int maxLine) noexcept;
    void EndDetailScrollbarDrag() noexcept { detailScrollbarDragging_ = false; }
    void SetListScrollRow(int row, int maxRow) noexcept;
    void BeginListScrollbarDrag(int y) noexcept;
    void DragListScrollbar(int y, int trackPixels, int maxRow) noexcept;
    void EndListScrollbarDrag() noexcept { listScrollbarDragging_ = false; }
    [[nodiscard]] bool SetHoveredButton(EditorConsoleButton button) noexcept;
    void PressButton(EditorConsoleButton button) noexcept { pressedButton_ = button; }
    void ReleaseButton() noexcept { pressedButton_ = EditorConsoleButton::None; }
    void Clear() noexcept;
    void Info(std::string category, std::string message);
    void Warning(std::string category, std::string message);
    void Error(std::string category, std::string message);

private:
    static constexpr std::size_t kCapacity = 1000;

    [[nodiscard]] static std::uint64_t NowMs() noexcept;
    void Append(EditorConsoleLevel level, std::string category, std::string message);
    void Increment(EditorConsoleLevel level) noexcept;
    void Decrement(EditorConsoleLevel level) noexcept;

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
