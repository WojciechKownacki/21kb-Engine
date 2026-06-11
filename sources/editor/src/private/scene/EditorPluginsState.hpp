#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <limits>

namespace kb::editor {

class EditorPluginsState {
public:
    [[nodiscard]] std::int64_t ScrollOffset() const noexcept {
        return scrollOffset_;
    }

    [[nodiscard]] bool SetScrollOffset(std::int64_t offset, std::int64_t maxOffset) noexcept {
        const std::int64_t clamped = std::clamp(offset, std::int64_t{ 0 }, std::max(std::int64_t{ 0 }, maxOffset));
        if (scrollOffset_ == clamped) {
            return false;
        }
        scrollOffset_ = clamped;
        return true;
    }

    [[nodiscard]] std::size_t HoveredPluginIndex() const noexcept {
        return hoveredPluginIndex_;
    }

    [[nodiscard]] bool SetHoveredPluginIndex(std::size_t index) noexcept {
        if (hoveredPluginIndex_ == index) {
            return false;
        }
        hoveredPluginIndex_ = index;
        return true;
    }

    [[nodiscard]] bool IsScrollbarDragging() const noexcept {
        return scrollbarDragging_;
    }

    void BeginScrollbarDrag(int y) noexcept {
        scrollbarDragging_ = true;
        scrollbarDragY_ = y;
        scrollbarDragStartOffset_ = scrollOffset_;
    }

    [[nodiscard]] bool DragScrollbar(int y, int trackTravel, std::int64_t maxOffset) noexcept {
        if (!scrollbarDragging_) {
            return false;
        }
        const int travel = std::max(1, trackTravel);
        const int delta = y - scrollbarDragY_;
        return SetScrollOffset(scrollbarDragStartOffset_ + (static_cast<std::int64_t>(delta) * std::max(std::int64_t{ 0 }, maxOffset)) / travel, maxOffset);
    }

    void EndScrollbarDrag() noexcept {
        scrollbarDragging_ = false;
    }

    [[nodiscard]] bool HasPendingReload() const noexcept {
        return pendingReload_;
    }

    void MarkPendingReload() noexcept {
        pendingReload_ = true;
    }

    void ClearPendingReload() noexcept {
        pendingReload_ = false;
    }

private:
    std::int64_t scrollOffset_ = 0;
    std::size_t hoveredPluginIndex_ = std::numeric_limits<std::size_t>::max();
    int scrollbarDragY_ = 0;
    std::int64_t scrollbarDragStartOffset_ = 0;
    bool scrollbarDragging_ = false;
    bool pendingReload_ = false;
};

} // namespace kb::editor
