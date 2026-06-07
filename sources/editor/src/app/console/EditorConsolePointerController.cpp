#include "app/console/EditorConsolePointerController.hpp"

#if defined(_WIN32)
#include "console/EditorConsoleLayout.hpp"
#include "rendering/ConsoleDetailTextOverlay.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] std::uint64_t ConsoleEntryAt(const RECT& list, int y, const EditorConsoleState& console) noexcept {
    constexpr int rowHeight = 22;
    if (y < list.top || y >= list.bottom) {
        return 0;
    }
    const int targetRow = console.ListScrollRow() + (y - list.top) / rowHeight;
    int row = 0;
    for (const EditorConsoleEntry& entry : console.Entries()) {
        if (!console.Accepts(entry.level)) {
            continue;
        }
        if (row == targetRow) {
            return entry.sequence;
        }
        ++row;
    }
    return 0;
}

[[nodiscard]] EditorConsoleButton ConsoleButtonAt(const EditorConsoleLayoutRects& layout, int x, int y) noexcept {
    if (PointInRect(layout.copyLineButton, x, y)) {
        return EditorConsoleButton::CopyLine;
    }
    if (PointInRect(layout.saveLogButton, x, y)) {
        return EditorConsoleButton::SaveLog;
    }
    if (PointInRect(layout.clearButton, x, y)) {
        return EditorConsoleButton::Clear;
    }
    return EditorConsoleButton::None;
}

[[nodiscard]] const char* ConsoleLevelName(EditorConsoleLevel level) noexcept {
    switch (level) {
    case EditorConsoleLevel::Info:
        return "INFO";
    case EditorConsoleLevel::Warning:
        return "WARNING";
    case EditorConsoleLevel::Error:
        return "ERROR";
    }
    return "INFO";
}

[[nodiscard]] std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return std::wstring{text.begin(), text.end()};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), required);
    return wide;
}

bool CopyTextToClipboard(HWND owner, const std::string& text) {
    const std::wstring wide = Utf8ToWide(text);
    if (!OpenClipboard(owner)) {
        return false;
    }
    EmptyClipboard();
    const SIZE_T bytes = (wide.size() + 1U) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) {
        CloseClipboard();
        return false;
    }
    void* buffer = GlobalLock(memory);
    if (buffer == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    std::memcpy(buffer, wide.c_str(), bytes);
    GlobalUnlock(memory);
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

[[nodiscard]] std::string FormatConsoleEntry(const EditorConsoleEntry& entry) {
    std::ostringstream line;
    line << '#' << entry.sequence << " [" << ConsoleLevelName(entry.level) << "] " << entry.category << ": " << entry.message;
    return line.str();
}

[[nodiscard]] std::string FormatConsoleLog(const EditorConsoleState& console) {
    std::ostringstream out;
    for (const EditorConsoleEntry& entry : console.Entries()) {
        out << FormatConsoleEntry(entry) << '\n';
    }
    return out.str();
}

bool SaveConsoleLogToFile(EditorSceneContext& sceneContext) {
    namespace fs = std::filesystem;
    try {
        const fs::path directory = fs::current_path() / "Saved" / "Logs";
        fs::create_directories(directory);
        const fs::path path = directory / "editor-console.log";
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            sceneContext.Console().Error("Console", "Failed to open Saved/Logs/editor-console.log for writing.");
            return false;
        }
        file << FormatConsoleLog(sceneContext.Console());
        sceneContext.Console().Info("Console", "Saved full console log to Saved/Logs/editor-console.log.");
        return true;
    } catch (const std::exception& exception) {
        sceneContext.Console().Error("Console", std::string{"Failed to save console log: "} + exception.what());
        return false;
    }
}

} // namespace

EditorConsolePointerController::EditorConsolePointerController(HWND owner, EditorSceneContext& sceneContext) noexcept
    : owner_(owner)
    , sceneContext_(sceneContext) {}

bool EditorConsolePointerController::HandleContextMenu(const RECT& content, int x, int y) {
    if (!PointInRect(content, x, y)) {
        return false;
    }
    EditorConsoleState& console = sceneContext_.Console();
    const EditorConsoleEntry* selected = console.SelectedEntry();
    if (selected == nullptr) {
        return true;
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return true;
    }
    constexpr UINT_PTR kCopyLine = 1;
    constexpr UINT_PTR kSaveLog = 2;
    AppendMenuW(menu, MF_STRING, kCopyLine, L"Copy Line");
    AppendMenuW(menu, MF_STRING, kSaveLog, L"Save Log to File");

    POINT screen{ x, y };
    ClientToScreen(owner_, &screen);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screen.x, screen.y, 0, owner_, nullptr);
    DestroyMenu(menu);

    if (command == kCopyLine) {
        CopyTextToClipboard(owner_, FormatConsoleEntry(*selected));
        return true;
    }
    if (command == kSaveLog) {
        static_cast<void>(SaveConsoleLogToFile(sceneContext_));
        return true;
    }
    return true;
}

bool EditorConsolePointerController::HandlePointerDown(const RECT& content, int x, int y) {
    EditorConsoleState& console = sceneContext_.Console();
    const EditorConsoleLayoutRects hit = ResolveEditorConsoleLayout(content, console);
    const EditorConsoleDetailScrollMetrics scroll = ResolveEditorConsoleDetailScrollMetrics(hit.detailTextArea, console);
    const EditorConsoleListScrollMetrics listScroll = ResolveEditorConsoleListScrollMetrics(hit.listRows, console);
    if (PointInRect(hit.infoButton, x, y)) {
        console.ToggleInfo();
        return true;
    }
    if (PointInRect(hit.warningButton, x, y)) {
        console.ToggleWarnings();
        return true;
    }
    if (PointInRect(hit.errorButton, x, y)) {
        console.ToggleErrors();
        return true;
    }
    if (PointInRect(hit.clearButton, x, y)) {
        console.PressButton(EditorConsoleButton::Clear);
        console.Clear();
        return true;
    }
    if (PointInRect(hit.copyLineButton, x, y)) {
        console.PressButton(EditorConsoleButton::CopyLine);
        const EditorConsoleEntry* selected = console.SelectedEntry();
        if (selected != nullptr) {
            CopyTextToClipboard(owner_, FormatConsoleEntry(*selected));
        }
        return true;
    }
    if (PointInRect(hit.saveLogButton, x, y)) {
        console.PressButton(EditorConsoleButton::SaveLog);
        static_cast<void>(SaveConsoleLogToFile(sceneContext_));
        return true;
    }
    if (PointInRect(hit.detailScrollbarThumb, x, y)) {
        console.BeginDetailScrollbarDrag(y);
        return true;
    }
    if (PointInRect(hit.detailScrollbarTrack, x, y)) {
        const int page = std::max(1, scroll.visibleLines - 1);
        const int direction = y < hit.detailScrollbarThumb.top ? -1 : 1;
        console.SetDetailScrollLine(console.DetailScrollLine() + direction * page, scroll.maxLine);
        return true;
    }
    if (PointInRect(hit.listScrollbarThumb, x, y)) {
        console.BeginListScrollbarDrag(y);
        return true;
    }
    if (PointInRect(hit.listScrollbarTrack, x, y)) {
        const int page = std::max(1, listScroll.visibleRows - 1);
        const int direction = y < hit.listScrollbarThumb.top ? -1 : 1;
        console.SetListScrollRow(console.ListScrollRow() + direction * page, listScroll.maxRow);
        return true;
    }
    if (PointInRect(hit.detailSplitter, x, y)) {
        console.BeginDetailResizeDrag();
        return true;
    }
    if (PointInRect(hit.listRows, x, y)) {
        const std::uint64_t sequence = ConsoleEntryAt(hit.listRows, y, console);
        if (sequence != 0) {
            console.Select(sequence);
        } else {
            console.ClearSelection();
        }
        return true;
    }
    return PointInRect(content, x, y);
}

bool EditorConsolePointerController::HandlePointerMove(const RECT& content, int y, bool leftButtonDown) {
    EditorConsoleState& console = sceneContext_.Console();
    if (console.IsListScrollbarDragging()) {
        if (!leftButtonDown) {
            console.EndListScrollbarDrag();
            return true;
        }
        const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(content, console);
        const EditorConsoleListScrollMetrics scroll = ResolveEditorConsoleListScrollMetrics(layout.listRows, console);
        const int trackHeight = static_cast<int>(layout.listScrollbarTrack.bottom - layout.listScrollbarTrack.top);
        const int thumbHeight = static_cast<int>(layout.listScrollbarThumb.bottom - layout.listScrollbarThumb.top);
        console.DragListScrollbar(y, std::max(1, trackHeight - thumbHeight), scroll.maxRow);
        return true;
    }
    if (console.IsDetailScrollbarDragging()) {
        if (!leftButtonDown) {
            console.EndDetailScrollbarDrag();
            ConsoleDetailTextOverlay::Sync(owner_, content, console);
            return true;
        }
        const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(content, console);
        const EditorConsoleDetailScrollMetrics scroll = ResolveEditorConsoleDetailScrollMetrics(layout.detailTextArea, console);
        const int trackHeight = static_cast<int>(layout.detailScrollbarTrack.bottom - layout.detailScrollbarTrack.top);
        const int thumbHeight = static_cast<int>(layout.detailScrollbarThumb.bottom - layout.detailScrollbarThumb.top);
        console.DragDetailScrollbar(y, std::max(1, trackHeight - thumbHeight), scroll.maxLine);
        ConsoleDetailTextOverlay::Sync(owner_, content, console);
        return true;
    }
    if (!console.IsDetailResizeDragging()) {
        return false;
    }
    if (!leftButtonDown) {
        console.EndDetailResizeDrag();
        ConsoleDetailTextOverlay::Sync(owner_, content, console);
        return true;
    }
    console.SetDetailHeight(content.bottom - y);
    ConsoleDetailTextOverlay::Sync(owner_, content, console);
    return true;
}

bool EditorConsolePointerController::HandlePointerMove(const std::optional<RECT>& content, int y, bool leftButtonDown) {
    if (!content.has_value()) {
        return false;
    }
    return HandlePointerMove(*content, y, leftButtonDown);
}

bool EditorConsolePointerController::HandlePointerUp() noexcept {
    sceneContext_.Console().ReleaseButton();
    if (sceneContext_.Console().IsListScrollbarDragging()) {
        sceneContext_.Console().EndListScrollbarDrag();
        return true;
    }
    if (sceneContext_.Console().IsDetailScrollbarDragging()) {
        sceneContext_.Console().EndDetailScrollbarDrag();
        return true;
    }
    if (!sceneContext_.Console().IsDetailResizeDragging()) {
        return false;
    }
    sceneContext_.Console().EndDetailResizeDrag();
    return true;
}

bool EditorConsolePointerController::HandleMouseWheel(const RECT& content, int x, int y, int wheelDelta) {
    if (!PointInRect(content, x, y) || wheelDelta == 0) {
        return false;
    }
    EditorConsoleState& console = sceneContext_.Console();
    const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(content, console);
    constexpr int rowsPerWheelNotch = 3;
    const int notches = wheelDelta / WHEEL_DELTA;
    const int directionRows = notches != 0 ? notches * rowsPerWheelNotch : (wheelDelta > 0 ? rowsPerWheelNotch : -rowsPerWheelNotch);

    if (PointInRect(layout.detail, x, y)) {
        const EditorConsoleDetailScrollMetrics scroll = ResolveEditorConsoleDetailScrollMetrics(layout.detailTextArea, console);
        console.SetDetailScrollLine(console.DetailScrollLine() - directionRows, scroll.maxLine);
        ConsoleDetailTextOverlay::Sync(owner_, content, console);
        return true;
    }

    if (PointInRect(layout.list, x, y)) {
        const EditorConsoleListScrollMetrics scroll = ResolveEditorConsoleListScrollMetrics(layout.listRows, console);
        console.SetListScrollRow(console.ListScrollRow() - directionRows, scroll.maxRow);
        return true;
    }
    return false;
}

bool EditorConsolePointerController::UpdateHover(const RECT& content, int x, int y) {
    const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(content, sceneContext_.Console());
    return sceneContext_.Console().SetHoveredButton(ConsoleButtonAt(layout, x, y));
}

bool EditorConsolePointerController::UpdateHoverOrClear(const std::optional<RECT>& content, int x, int y) {
    if (content.has_value() && PointInRect(*content, x, y)) {
        return UpdateHover(*content, x, y);
    }
    return sceneContext_.Console().SetHoveredButton(EditorConsoleButton::None);
}

} // namespace kb::editor

#endif
