#pragma once

#include "console/EditorConsoleState.hpp"
#include "localization/EditorLocalization.hpp"
#include "rendering/gdi/EditorTextMeasure.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <string>

namespace kb::editor {

#if defined(_WIN32)
struct EditorConsoleDetailScrollMetrics {
    int totalLines = 1;
    int visibleLines = 1;
    int maxLine = 0;
};

struct EditorConsoleListScrollMetrics {
    int totalRows = 0;
    int visibleRows = 1;
    int maxRow = 0;
};

struct EditorConsoleLayoutRects {
    RECT toolbar{};
    RECT infoButton{};
    RECT warningButton{};
    RECT errorButton{};
    // Empty when the toolbar is too narrow to fit it beside the buttons.
    RECT filterField{};
    RECT copyLineButton{};
    RECT saveLogButton{};
    RECT clearButton{};
    RECT list{};
    RECT listRows{};
    RECT listScrollbarTrack{};
    RECT listScrollbarThumb{};
    RECT detailSplitter{};
    RECT detailSplitterVisual{};
    RECT detail{};
    RECT detailHeader{};
    RECT detailBody{};
    RECT detailTextArea{};
    RECT detailScrollbarTrack{};
    RECT detailScrollbarThumb{};
};

[[nodiscard]] inline RECT EditorConsoleInsetRect(RECT rect, int x, int y) noexcept {
    rect.left += x;
    rect.right -= x;
    rect.top += y;
    rect.bottom -= y;
    return rect;
}

// One list row. Identical consecutive entries (same level, category and message) collapse into
// one row that shows the latest of them and how many there were, so a repeated message is one
// line with a count instead of a screenful.
struct EditorConsoleRow {
    const EditorConsoleEntry* entry = nullptr;
    std::uint64_t firstSequence = 0;
    std::uint32_t repeat = 1;
};

// Calls `visit` with every row the filters show, in order, until it returns false. This is the
// one definition of what the list holds: counting, hit-testing and painting all walk it.
template <typename Visit>
inline void ForEachEditorConsoleRow(const EditorConsoleState& console, Visit&& visit) {
    EditorConsoleRow run{};
    for (const EditorConsoleEntry& entry : console.Entries()) {
        if (!console.Shows(entry)) {
            continue;
        }
        if (run.entry != nullptr && run.entry->level == entry.level && run.entry->category == entry.category &&
            run.entry->message == entry.message) {
            run.entry = &entry;
            ++run.repeat;
            continue;
        }
        if (run.entry != nullptr && !visit(run)) {
            return;
        }
        run = EditorConsoleRow{ &entry, entry.sequence, 1U };
    }
    if (run.entry != nullptr) {
        static_cast<void>(visit(run));
    }
}

[[nodiscard]] inline int EditorConsoleAcceptedEntryCount(const EditorConsoleState& console) noexcept {
    int count = 0;
    ForEachEditorConsoleRow(console, [&count](const EditorConsoleRow&) noexcept {
        ++count;
        return true;
    });
    return count;
}

[[nodiscard]] inline EditorConsoleListScrollMetrics ResolveEditorConsoleListScrollMetrics(const RECT& listRows, const EditorConsoleState& console) noexcept {
    constexpr int rowHeight = 22;
    EditorConsoleListScrollMetrics metrics{};
    metrics.totalRows = EditorConsoleAcceptedEntryCount(console);
    metrics.visibleRows = std::max(1, static_cast<int>(listRows.bottom - listRows.top) / rowHeight);
    metrics.maxRow = std::max(0, metrics.totalRows - metrics.visibleRows);
    return metrics;
}

[[nodiscard]] inline int EditorConsoleDetailLineCount(const EditorConsoleState& console) noexcept {
    const EditorConsoleEntry* selected = console.SelectedEntry();
    if (selected == nullptr || selected->message.empty()) {
        return 1;
    }
    return static_cast<int>(std::count(selected->message.begin(), selected->message.end(), '\n')) + 1;
}

[[nodiscard]] inline EditorConsoleDetailScrollMetrics ResolveEditorConsoleDetailScrollMetrics(const RECT& detailTextArea, const EditorConsoleState& console) noexcept {
    constexpr int lineHeight = 16;
    EditorConsoleDetailScrollMetrics metrics{};
    metrics.totalLines = std::max(1, EditorConsoleDetailLineCount(console));
    metrics.visibleLines = std::max(1, static_cast<int>(detailTextArea.bottom - detailTextArea.top) / lineHeight);
    metrics.maxLine = std::max(0, metrics.totalLines - metrics.visibleLines);
    return metrics;
}

[[nodiscard]] inline EditorConsoleLayoutRects ResolveEditorConsoleLayout(const RECT& content, const EditorConsoleState& console) noexcept {
    constexpr int toolbarHeight = 34;
    constexpr int toolbarPadX = 8;
    constexpr int toolbarPadY = 6;
    constexpr int toolbarGap = 4;
    constexpr int pillWidth = 60;
    constexpr int buttonHeight = 22;

    EditorConsoleLayoutRects layout{};
    layout.toolbar = RECT{ content.left, content.top, content.right, content.top + toolbarHeight };
    layout.infoButton = RECT{ layout.toolbar.left + toolbarPadX, layout.toolbar.top + toolbarPadY, layout.toolbar.left + toolbarPadX + pillWidth, layout.toolbar.top + toolbarPadY + buttonHeight };
    layout.warningButton = RECT{ layout.infoButton.right + toolbarGap, layout.toolbar.top + toolbarPadY, layout.infoButton.right + toolbarGap + pillWidth, layout.toolbar.top + toolbarPadY + buttonHeight };
    layout.errorButton = RECT{ layout.warningButton.right + toolbarGap, layout.toolbar.top + toolbarPadY, layout.warningButton.right + toolbarGap + pillWidth, layout.toolbar.top + toolbarPadY + buttonHeight };
    // The buttons are as wide as their labels in the editor's language (12 pt, 8 px each side).
    constexpr int labelPadding = 18;
    const int clearWidth = FitTextWidth(80, { EditorText("console.clear", "Clear") }, 12, FW_NORMAL, labelPadding);
    const int saveLogWidth = FitTextWidth(88, { EditorText("console.saveLog", "Save Log") }, 12, FW_NORMAL, labelPadding);
    const int copyLineWidth = FitTextWidth(88, { EditorText("console.copyLine", "Copy Line") }, 12, FW_NORMAL, labelPadding);
    layout.clearButton = RECT{ layout.toolbar.right - toolbarPadX - clearWidth, layout.toolbar.top + toolbarPadY, layout.toolbar.right - toolbarPadX, layout.toolbar.top + toolbarPadY + buttonHeight };
    layout.saveLogButton = RECT{ layout.clearButton.left - toolbarGap - saveLogWidth, layout.toolbar.top + toolbarPadY, layout.clearButton.left - toolbarGap, layout.toolbar.top + toolbarPadY + buttonHeight };
    layout.copyLineButton = RECT{ layout.saveLogButton.left - toolbarGap - copyLineWidth, layout.toolbar.top + toolbarPadY, layout.saveLogButton.left - toolbarGap, layout.toolbar.top + toolbarPadY + buttonHeight };
    constexpr int filterGap = 12;
    constexpr int filterMaxWidth = 260;
    constexpr int filterMinWidth = 90;
    const int filterLeft = static_cast<int>(layout.errorButton.right) + filterGap;
    const int filterRight = std::min(filterLeft + filterMaxWidth, static_cast<int>(layout.copyLineButton.left) - filterGap);
    if (filterRight - filterLeft >= filterMinWidth) {
        layout.filterField = RECT{ filterLeft, layout.toolbar.top + toolbarPadY, filterRight, layout.toolbar.top + toolbarPadY + buttonHeight };
    }
    layout.list = RECT{ content.left, layout.toolbar.bottom, content.right, content.bottom };
    layout.listScrollbarTrack = EditorConsoleInsetRect(RECT{ layout.list.right - 18, layout.list.top, layout.list.right - 6, layout.list.bottom }, 0, 6);
    layout.listRows = RECT{ layout.list.left, layout.list.top, layout.listScrollbarTrack.left - 4, layout.list.bottom };
    if (console.SelectedEntry() != nullptr) {
        constexpr int splitterHeight = 6;
        constexpr int detailHeaderHeight = 24;
        const int availableHeight = static_cast<int>(content.bottom - layout.toolbar.bottom);
        const int maxDetailHeight = std::max(54, availableHeight - 42);
        const int detailHeight = std::clamp(console.DetailHeight(), 54, maxDetailHeight);
        layout.detail = RECT{ content.left, content.bottom - detailHeight, content.right, content.bottom };
        layout.detailSplitter = RECT{ content.left, layout.detail.top - splitterHeight, content.right, layout.detail.top };
        layout.detailSplitterVisual = RECT{ content.left, layout.detailSplitter.top + 2, content.right, layout.detailSplitter.top + 4 };
        layout.detailHeader = RECT{ layout.detail.left, layout.detail.top, layout.detail.right, layout.detail.top + detailHeaderHeight };
        layout.detailBody = RECT{ layout.detail.left, layout.detailHeader.bottom, layout.detail.right, layout.detail.bottom };
        layout.detailScrollbarTrack = EditorConsoleInsetRect(RECT{ layout.detailBody.right - 18, layout.detailBody.top, layout.detailBody.right - 6, layout.detailBody.bottom }, 0, 6);
        layout.detailTextArea = EditorConsoleInsetRect(RECT{ layout.detailBody.left, layout.detailBody.top, layout.detailScrollbarTrack.left - 4, layout.detailBody.bottom }, 8, 6);
        const EditorConsoleDetailScrollMetrics scroll = ResolveEditorConsoleDetailScrollMetrics(layout.detailTextArea, console);
        const int trackHeight = std::max(1, static_cast<int>(layout.detailScrollbarTrack.bottom - layout.detailScrollbarTrack.top));
        const int minThumbHeight = std::min(trackHeight, 22);
        const int proportionalThumb = scroll.totalLines <= 0 ? trackHeight : (trackHeight * scroll.visibleLines) / std::max(1, scroll.totalLines);
        const int thumbHeight = std::clamp(proportionalThumb, minThumbHeight, trackHeight);
        const int travel = std::max(0, trackHeight - thumbHeight);
        const int thumbTop = layout.detailScrollbarTrack.top + (scroll.maxLine > 0 ? (travel * std::clamp(console.DetailScrollLine(), 0, scroll.maxLine)) / scroll.maxLine : 0);
        layout.detailScrollbarThumb = RECT{ layout.detailScrollbarTrack.left + 2, thumbTop, layout.detailScrollbarTrack.right - 2, thumbTop + thumbHeight };
        layout.list.bottom = layout.detailSplitter.top;
        layout.listScrollbarTrack = EditorConsoleInsetRect(RECT{ layout.list.right - 18, layout.list.top, layout.list.right - 6, layout.list.bottom }, 0, 6);
        layout.listRows = RECT{ layout.list.left, layout.list.top, layout.listScrollbarTrack.left - 4, layout.list.bottom };
    }
    const EditorConsoleListScrollMetrics listScroll = ResolveEditorConsoleListScrollMetrics(layout.listRows, console);
    const int listTrackHeight = std::max(1, static_cast<int>(layout.listScrollbarTrack.bottom - layout.listScrollbarTrack.top));
    const int listMinThumbHeight = std::min(listTrackHeight, 22);
    const int listProportionalThumb = listScroll.totalRows <= 0 ? listTrackHeight : (listTrackHeight * listScroll.visibleRows) / std::max(1, listScroll.totalRows);
    const int listThumbHeight = std::clamp(listProportionalThumb, listMinThumbHeight, listTrackHeight);
    const int listTravel = std::max(0, listTrackHeight - listThumbHeight);
    const int listThumbTop = layout.listScrollbarTrack.top + (listScroll.maxRow > 0 ? (listTravel * std::clamp(console.ListScrollRow(), 0, listScroll.maxRow)) / listScroll.maxRow : 0);
    if (listScroll.maxRow > 0) {
        layout.listScrollbarThumb = RECT{ layout.listScrollbarTrack.left + 2, listThumbTop, layout.listScrollbarTrack.right - 2, listThumbTop + listThumbHeight };
    }
    return layout;
}
#endif

} // namespace kb::editor
