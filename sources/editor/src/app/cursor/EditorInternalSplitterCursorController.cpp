#include "app/cursor/EditorInternalSplitterCursorController.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserLayout.hpp"
#include "console/EditorConsoleLayout.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>

namespace kb::editor {
namespace {

constexpr int kVerticalResizeCursor = 32645;
constexpr int kHorizontalResizeCursor = 32644;

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

EditorInternalSplitterCursorController::EditorInternalSplitterCursorController(
    HWND messageWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    const EditorSceneContext& sceneContext) noexcept
    : messageWindow_(messageWindow)
    , mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , metrics_(metrics)
    , sceneContext_(sceneContext) {}

void EditorInternalSplitterCursorController::UpdateCursor(int x, int y) const {
    if (sceneContext_.Console().IsDetailResizeDragging() || HitsConsoleDetailSplitter(x, y)) {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(kVerticalResizeCursor)));
        return;
    }
    if (sceneContext_.AssetBrowser().IsTreeWidthDragging() || HitsProjectFilesTreeSplitter(x, y)) {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(kHorizontalResizeCursor)));
    }
}

bool EditorInternalSplitterCursorController::HitsResizableSplitter(int x, int y) const {
    return sceneContext_.Console().IsDetailResizeDragging()
        || HitsConsoleDetailSplitter(x, y)
        || sceneContext_.AssetBrowser().IsTreeWidthDragging()
        || HitsProjectFilesTreeSplitter(x, y);
}

bool EditorInternalSplitterCursorController::HitsProjectFilesTreeSplitter(int x, int y) const {
    const std::optional<RECT> assetContent = EditorPanelContentResolver::Resolve(DockPanelKind::Assets, messageWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (!assetContent.has_value()) {
        return false;
    }
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(*assetContent, sceneContext_.AssetBrowser().TreeWidth());
    return PointInRect(layout.treeSplitter, x, y);
}

bool EditorInternalSplitterCursorController::HitsConsoleDetailSplitter(int x, int y) const {
    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (!consoleContent.has_value()) {
        return false;
    }
    const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(*consoleContent, sceneContext_.Console());
    return PointInRect(layout.detailSplitter, x, y);
}

} // namespace kb::editor

#endif
