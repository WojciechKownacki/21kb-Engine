#include "app/EditorWindowPointerHandler.hpp"

#if defined(_WIN32)
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorPointerDragSourceResolver.hpp"
#include "app/EditorAssetBrowserDeleteConfirmPointerHandler.hpp"
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/EditorWindowToolbarPointerHandler.hpp"
#include "assets/EditorAssetBrowserLayout.hpp"
#include "assets/EditorAssetBrowserOverlayHitTester.hpp"
#include "console/EditorConsoleLayout.hpp"
#include "docking/DockMainLayoutResolver.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "inspection/InspectorPanelInteraction.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/ConsoleDetailTextOverlay.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorHierarchyContentResolver.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace kb::editor {
namespace {

bool CommitPendingNewAssetFolder(EditorSceneContext& sceneContext) {
    if (sceneContext.AssetBrowser().TextEditMode() != EditorAssetTextEditMode::NewFolder) {
        return false;
    }
    static_cast<void>(sceneContext.CommitAssetTextEdit());
    return true;
}

bool CommitPendingHierarchyRename(EditorSceneContext& sceneContext) {
    if (!sceneContext.IsHierarchyRenaming()) {
        return false;
    }
    static_cast<void>(sceneContext.CommitHierarchyRename());
    return true;
}

bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

bool PointHitsMainSplitter(HWND window, EditorDockModel& dockModel, const EditorMetrics& metrics, int x, int y) {
    const DockLayout layout = DockMainLayoutResolver::Resolve(window, dockModel, metrics);
    return dockModel.Queries().HitTest(layout, x, y).kind == DockHitKind::Splitter;
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

bool HandleConsoleContextMenu(HWND owner, const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    if (!PointInRect(content, x, y)) {
        return false;
    }
    EditorConsoleState& console = sceneContext.Console();
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
    ClientToScreen(owner, &screen);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screen.x, screen.y, 0, owner, nullptr);
    DestroyMenu(menu);

    if (command == kCopyLine) {
        CopyTextToClipboard(owner, FormatConsoleEntry(*selected));
        return true;
    }
    if (command == kSaveLog) {
        static_cast<void>(SaveConsoleLogToFile(sceneContext));
        return true;
    }
    return true;
}

bool HandleConsolePointerDown(HWND owner, const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    EditorConsoleState& console = sceneContext.Console();
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
            CopyTextToClipboard(owner, FormatConsoleEntry(*selected));
        }
        return true;
    }
    if (PointInRect(hit.saveLogButton, x, y)) {
        console.PressButton(EditorConsoleButton::SaveLog);
        static_cast<void>(SaveConsoleLogToFile(sceneContext));
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

bool HandleConsolePointerMove(HWND owner, const RECT& content, int y, bool leftButtonDown, EditorSceneContext& sceneContext) {
    EditorConsoleState& console = sceneContext.Console();
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
            ConsoleDetailTextOverlay::Sync(owner, content, console);
            return true;
        }
        const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(content, console);
        const EditorConsoleDetailScrollMetrics scroll = ResolveEditorConsoleDetailScrollMetrics(layout.detailTextArea, console);
        const int trackHeight = static_cast<int>(layout.detailScrollbarTrack.bottom - layout.detailScrollbarTrack.top);
        const int thumbHeight = static_cast<int>(layout.detailScrollbarThumb.bottom - layout.detailScrollbarThumb.top);
        console.DragDetailScrollbar(y, std::max(1, trackHeight - thumbHeight), scroll.maxLine);
        ConsoleDetailTextOverlay::Sync(owner, content, console);
        return true;
    }
    if (!console.IsDetailResizeDragging()) {
        return false;
    }
    if (!leftButtonDown) {
        console.EndDetailResizeDrag();
        ConsoleDetailTextOverlay::Sync(owner, content, console);
        return true;
    }
    console.SetDetailHeight(content.bottom - y);
    ConsoleDetailTextOverlay::Sync(owner, content, console);
    return true;
}

bool HandleConsolePointerUp(EditorSceneContext& sceneContext) noexcept {
    sceneContext.Console().ReleaseButton();
    if (sceneContext.Console().IsListScrollbarDragging()) {
        sceneContext.Console().EndListScrollbarDrag();
        return true;
    }
    if (sceneContext.Console().IsDetailScrollbarDragging()) {
        sceneContext.Console().EndDetailScrollbarDrag();
        return true;
    }
    if (!sceneContext.Console().IsDetailResizeDragging()) {
        return false;
    }
    sceneContext.Console().EndDetailResizeDrag();
    return true;
}

bool UpdateConsoleHover(const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(content, sceneContext.Console());
    return sceneContext.Console().SetHoveredButton(ConsoleButtonAt(layout, x, y));
}

bool HandleConsoleMouseWheel(HWND owner, const RECT& content, int x, int y, int wheelDelta, EditorSceneContext& sceneContext) {
    if (!PointInRect(content, x, y) || wheelDelta == 0) {
        return false;
    }
    EditorConsoleState& console = sceneContext.Console();
    const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(content, console);
    constexpr int rowsPerWheelNotch = 3;
    const int notches = wheelDelta / WHEEL_DELTA;
    const int directionRows = notches != 0 ? notches * rowsPerWheelNotch : (wheelDelta > 0 ? rowsPerWheelNotch : -rowsPerWheelNotch);

    if (PointInRect(layout.detail, x, y)) {
        const EditorConsoleDetailScrollMetrics scroll = ResolveEditorConsoleDetailScrollMetrics(layout.detailTextArea, console);
        console.SetDetailScrollLine(console.DetailScrollLine() - directionRows, scroll.maxLine);
        ConsoleDetailTextOverlay::Sync(owner, content, console);
        return true;
    }

    if (PointInRect(layout.list, x, y)) {
        const EditorConsoleListScrollMetrics scroll = ResolveEditorConsoleListScrollMetrics(layout.listRows, console);
        console.SetListScrollRow(console.ListScrollRow() - directionRows, scroll.maxRow);
        return true;
    }
    return false;
}

[[nodiscard]] int ProjectFilesAssetContentHeight(const EditorAssetBrowserLayoutRects& layout, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    const int itemCount = static_cast<int>(state.ChildFolderRows(manager).size() + state.AssetRows(manager).size())
        + (state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1 : 0);
    if (state.ViewMode() == EditorAssetViewMode::Tiles) {
        constexpr int tileGap = 5;
        const int columns = EditorAssetBrowserLayout::AssetTileColumnCount(layout, state.ThumbnailScale());
        const int rows = (itemCount + columns - 1) / std::max(1, columns);
        return rows * (EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) + tileGap);
    }
    return itemCount * EditorAssetBrowserLayout::RowHeight;
}

[[nodiscard]] int ProjectFilesAssetViewportHeight(const EditorAssetBrowserLayoutRects& layout, const EditorAssetBrowserState& state) noexcept {
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    int height = static_cast<int>(viewport.bottom - viewport.top);
    if (state.ViewMode() == EditorAssetViewMode::List) {
        height -= EditorAssetBrowserLayout::AssetHeaderHeight;
    }
    return std::max(1, height);
}

bool HandleProjectFilesMouseWheel(const RECT& content, int x, int y, int wheelDelta, EditorSceneContext& sceneContext) {
    if (!PointInRect(content, x, y) || wheelDelta == 0) {
        return false;
    }
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(content, state.TreeWidth());
    const int notches = wheelDelta / WHEEL_DELTA;
    const int direction = notches != 0 ? notches : (wheelDelta > 0 ? 1 : -1);

    if (PointInRect(layout.tree, x, y)) {
        const RECT viewport = EditorAssetBrowserLayout::TreeViewportRect(layout);
        const int viewportHeight = static_cast<int>(viewport.bottom - viewport.top);
        const int contentHeight = static_cast<int>(state.FolderRows(manager).size()) * EditorAssetBrowserLayout::RowHeight;
        const int maxOffset = std::max(0, contentHeight - viewportHeight);
        state.SetTreeScrollOffset(state.TreeScrollOffset() - direction * EditorAssetBrowserLayout::RowHeight * 3, maxOffset);
        return true;
    }

    if (PointInRect(layout.assetView, x, y)) {
        const int contentHeight = ProjectFilesAssetContentHeight(layout, state, manager);
        const int viewportHeight = ProjectFilesAssetViewportHeight(layout, state);
        const int maxOffset = std::max(0, contentHeight - viewportHeight);
        const int step = state.ViewMode() == EditorAssetViewMode::Tiles
            ? std::max(1, EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) / 2)
            : EditorAssetBrowserLayout::RowHeight * 3;
        state.SetContentScrollOffset(state.ContentScrollOffset() - direction * step, maxOffset);
        return true;
    }
    return false;
}

bool PointHitsProjectFilesTreeSplitter(HWND messageWindow, HWND mainWindow, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, int x, int y) {
    const std::optional<RECT> assetContent = EditorPanelContentResolver::Resolve(DockPanelKind::Assets, messageWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!assetContent.has_value()) {
        return false;
    }
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(*assetContent, sceneContext.AssetBrowser().TreeWidth());
    return PointInRect(layout.treeSplitter, x, y);
}

bool PointHitsConsoleDetailSplitter(HWND messageWindow, HWND mainWindow, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, int x, int y) {
    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!consoleContent.has_value()) {
        return false;
    }
    const EditorConsoleLayoutRects hit = ResolveEditorConsoleLayout(*consoleContent, sceneContext.Console());
    return PointInRect(hit.detailSplitter, x, y);
}

void UpdateInternalSplitterCursor(HWND messageWindow, HWND mainWindow, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, int x, int y) {
    if (sceneContext.Console().IsDetailResizeDragging() || PointHitsConsoleDetailSplitter(messageWindow, mainWindow, dockModel, floatingWindows, metrics, sceneContext, x, y)) {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32645)));
        return;
    }
    if (sceneContext.AssetBrowser().IsTreeWidthDragging() || PointHitsProjectFilesTreeSplitter(messageWindow, mainWindow, dockModel, floatingWindows, metrics, sceneContext, x, y)) {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32644)));
    }
}

bool HandleGlobalDeleteConfirm(HWND window, int x, int y, EditorSceneContext& sceneContext) {
    if (!sceneContext.AssetBrowser().IsDeleteConfirmOpen()) {
        return false;
    }

    RECT client{};
    GetClientRect(window, &client);
    const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserOverlayHitTester::HitTestDeleteConfirm(
        client,
        x,
        y,
        sceneContext.AssetBrowser(),
        &client);
    return EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerDown(hit.value_or(EditorAssetBrowserHit{}), x, y, sceneContext);
}

void CloseAssetBrowserTransientUi(EditorSceneContext& sceneContext) noexcept {
    sceneContext.AssetBrowser().CloseFilterMenu();
    sceneContext.AssetBrowser().CloseContextMenu();
    sceneContext.AssetBrowser().CloseDropActionMenu();
}

} // namespace

EditorWindowPointerHandler::EditorWindowPointerHandler(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorHierarchySelectionController& hierarchySelection,
    EditorSceneContext& sceneContext,
    EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport& sceneViewport,
    EditorPlayModeState& playMode,
    EditorShellInteractionState& shellInteraction,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , hierarchySelection_(hierarchySelection)
    , sceneContext_(sceneContext)
    , renderBackendSettings_(renderBackendSettings)
    , sceneViewport_(sceneViewport)
    , playMode_(playMode)
    , shellInteraction_(shellInteraction)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

LRESULT EditorWindowPointerHandler::HandleLeftButtonDown(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    if (HandleGlobalDeleteConfirm(messageWindow, x, y, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    const bool committedNewFolder = CommitPendingNewAssetFolder(sceneContext_);
    const bool committedHierarchyRename = CommitPendingHierarchyRename(sceneContext_);
    if (committedNewFolder || committedHierarchyRename) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (EditorWindowToolbarPointerHandler::HandleLeftButtonDown(mainWindow_, messageWindow, x, y, dockModel_, playMode_, shellInteraction_, metrics_)) {
        return 0;
    }
    const std::optional<RECT> assetContent = EditorPanelContentResolver::Resolve(DockPanelKind::Assets, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    const std::optional<RECT> inspectorContent = EditorPanelContentResolver::Resolve(DockPanelKind::Inspector, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    const std::optional<RECT> hierarchyContent = EditorHierarchyContentResolver::Resolve(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    const std::optional<EditorResolvedPanelContent> scenePanelContent =
        EditorPanelContentResolver::ResolvePanel(DockPanelKind::Scene, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    std::optional<EditorResolvedPanelContent> sceneContent{};
    if (scenePanelContent.has_value() && PointInRect(scenePanelContent->content, x, y)) {
        sceneContent = scenePanelContent;
    }
    const bool inAssetPanel = assetContent.has_value() && PointInRect(*assetContent, x, y);
    const bool inInspectorPanel = inspectorContent.has_value() && PointInRect(*inspectorContent, x, y);
    const bool inConsolePanel = consoleContent.has_value() && PointInRect(*consoleContent, x, y);
    const bool inHierarchyPanel = hierarchyContent.has_value() && PointInRect(*hierarchyContent, x, y);

    if (sceneContent.has_value()) {
        const SceneViewportToolbarRects sceneToolbar = SceneViewportToolbarRenderer::Resolve(sceneContent->content);
        if (PointInRect(sceneToolbar.profileButton, x, y)) {
            sceneContext_.ViewportPreview(sceneContent->panelId).CycleProfile();
            sceneViewport_.RequestPresent();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return 0;
        }
    }

    if (messageWindow == mainWindow_ && PointHitsMainSplitter(messageWindow, dockModel_, metrics_, x, y)) {
        static_cast<void>(dockController_.HandlePointerDown(messageWindow, x, y));
        sceneViewport_.RequestPresent();
        return 0;
    }

    EditorPointerDragSourceResolver::Resolve(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
    EditorPointerDragInteraction::CaptureIfActive(messageWindow, pointerDrag_);

    if (hierarchySelection_.HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.AssetBrowser().FocusSelection(false);
        CloseAssetBrowserTransientUi(sceneContext_);
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.ClearHierarchySelection();
        if (sceneContext_.AssetBrowser().IsTreeWidthDragging()
            || sceneContext_.AssetBrowser().IsThumbnailScaleDragging()
            || sceneContext_.AssetBrowser().IsTreeScrollbarDragging()
            || sceneContext_.AssetBrowser().IsContentScrollbarDragging()) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (inConsolePanel && HandleConsolePointerDown(messageWindow, *consoleContent, x, y, sceneContext_)) {
        sceneContext_.ClearHierarchySelection();
        sceneContext_.AssetBrowser().FocusSelection(false);
        CloseAssetBrowserTransientUi(sceneContext_);
        if (sceneContext_.Console().IsDetailResizeDragging() || sceneContext_.Console().IsDetailScrollbarDragging() || sceneContext_.Console().IsListScrollbarDragging()) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (inInspectorPanel) {
        const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(*inspectorContent, sceneContext_, x, y);
        static_cast<void>(InspectorPanelInteraction::HandlePointerDown(sceneContext_, hit, x, y));
        if (hit.kind == InspectorHitKind::FloatField) {
            SetCapture(messageWindow);
        }
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (!inAssetPanel) {
        sceneContext_.AssetBrowser().FocusSelection(false);
        CloseAssetBrowserTransientUi(sceneContext_);
    }
    if (!inHierarchyPanel && !inConsolePanel) {
        sceneContext_.ClearHierarchySelection();
    }
    if (dockController_.HandlePointerDown(messageWindow, x, y)) {
        sceneViewport_.RequestPresent();
    }
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleRightButtonDown(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    if (HandleGlobalDeleteConfirm(messageWindow, x, y, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    const bool committedNewFolder = CommitPendingNewAssetFolder(sceneContext_);
    const bool committedHierarchyRename = CommitPendingHierarchyRename(sceneContext_);
    if (committedNewFolder || committedHierarchyRename) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }

    pointerDrag_.Clear();
    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (consoleContent.has_value() && HandleConsoleContextMenu(messageWindow, *consoleContent, x, y, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (EditorAssetBrowserPointerHandler::HandleRightButtonDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    return 0;
}

LRESULT EditorWindowPointerHandler::HandleLeftButtonDoubleClick(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);

    if (EditorAssetBrowserPointerHandler::HandleDoubleClick(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.ClearHierarchySelection();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    return HandleLeftButtonDown(messageWindow, lparam);
}

LRESULT EditorWindowPointerHandler::HandleMouseMove(HWND messageWindow, WPARAM wparam, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    const bool leftButtonDown = (wparam & MK_LBUTTON) != 0;
    UpdateInternalSplitterCursor(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, x, y);
    static_cast<void>(EditorWindowToolbarPointerHandler::HandleMouseMove(mainWindow_, messageWindow, x, y, dockModel_, shellInteraction_, metrics_));

    if (leftButtonDown && InspectorPanelInteraction::HandlePointerDrag(sceneContext_, x, y)) {
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (leftButtonDown && pointerDrag_.Potential() && EditorPointerDragInteraction::Move(messageWindow, mainWindow_, x, y, pointerDrag_)) {
        sceneViewport_.RequestPresent();
        return 0;
    }

    if (leftButtonDown && dockController_.HandlePointerMove(messageWindow, x, y, true)) {
        sceneViewport_.RequestPresent();
        return 0;
    }

    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (consoleContent.has_value() && PointInRect(*consoleContent, x, y)) {
        if (UpdateConsoleHover(*consoleContent, x, y, sceneContext_)) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        }
    } else if (sceneContext_.Console().SetHoveredButton(EditorConsoleButton::None)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (consoleContent.has_value() && HandleConsolePointerMove(messageWindow, *consoleContent, y, leftButtonDown, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerMove(messageWindow, mainWindow_, x, y, leftButtonDown, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }
    const std::optional<RECT> inspectorContent = EditorPanelContentResolver::Resolve(DockPanelKind::Inspector, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (inspectorContent.has_value() && PointInRect(*inspectorContent, x, y)) {
        const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(*inspectorContent, sceneContext_, x, y);
        if (InspectorPanelInteraction::UpdateHover(sceneContext_, hit)) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        }
        return 0;
    }
    if (sceneContext_.Inspector().IsAnyHovered()) {
        sceneContext_.Inspector().ClearHover();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (dockController_.HandlePointerMove(messageWindow, x, y, leftButtonDown)) {
        sceneViewport_.RequestPresent();
    }
    UpdateInternalSplitterCursor(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, x, y);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleMouseWheel(HWND messageWindow, WPARAM wparam, LPARAM lparam) {
    POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    ScreenToClient(messageWindow, &point);
    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (consoleContent.has_value() && HandleConsoleMouseWheel(messageWindow, *consoleContent, point.x, point.y, GET_WHEEL_DELTA_WPARAM(wparam), sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }
    const std::optional<RECT> assetContent = EditorPanelContentResolver::Resolve(DockPanelKind::Assets, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (assetContent.has_value() && HandleProjectFilesMouseWheel(*assetContent, point.x, point.y, GET_WHEEL_DELTA_WPARAM(wparam), sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }
    return DefWindowProcW(messageWindow, WM_MOUSEWHEEL, wparam, lparam);
}

LRESULT EditorWindowPointerHandler::HandleLeftButtonUp(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);

    shellInteraction_.ClearPressedTransport();
    if (InspectorPanelInteraction::HandlePointerUp(sceneContext_)) {
        ReleaseCapture();
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (pointerDrag_.Potential()) {
        const bool handledDrop = EditorPointerDragInteraction::Complete(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
        if (handledDrop) {
            sceneViewport_.RequestPresent();
        }
        return 0;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerUp(sceneContext_)) {
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }
    if (HandleConsolePointerUp(sceneContext_)) {
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }

    if (dockController_.HandlePointerUp(messageWindow)) {
        sceneViewport_.RequestPresent();
    }
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleSetCursor(HWND messageWindow, WPARAM wparam, LPARAM lparam) {
    if (LOWORD(lparam) != HTCLIENT) {
        return DefWindowProcW(messageWindow, WM_SETCURSOR, wparam, lparam);
    }

    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(messageWindow, &point);
    if (EditorPointerDragInteraction::UpdateCursor(pointerDrag_)) {
        return TRUE;
    }
    if (sceneContext_.Console().IsDetailResizeDragging() || PointHitsConsoleDetailSplitter(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, point.x, point.y)
        || sceneContext_.AssetBrowser().IsTreeWidthDragging() || PointHitsProjectFilesTreeSplitter(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, point.x, point.y)) {
        UpdateInternalSplitterCursor(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, point.x, point.y);
        return TRUE;
    }
    dockController_.UpdateHoverCursor(messageWindow, point.x, point.y);
    return TRUE;
}

} // namespace kb::editor

#endif
