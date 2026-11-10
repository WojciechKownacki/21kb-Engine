#include "app/EditorHeadlessPopupChecks.hpp"

#if defined(_WIN32)
#include "app/EditorKeyState.hpp"
#include "app/EditorWindowUnderTest.hpp"
#include "app/docking/EditorDockTabMenu.hpp"
#include "rendering/DockTabControlGeometry.hpp"
#include "rendering/EditorToolbarLayout.hpp"
#include "rendering/EditorToolbarRenderer.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

using namespace headless;

// The pointer as the mouse delivers it: button messages sent to the window, the moves
// between two points in small steps, so a drag crosses its threshold and every band on the way.
class Pointer final {
public:
    explicit Pointer(const EditorWindowUnderTest& editor) noexcept : editor_(editor) {}

    void Down(HWND window, POINT point) {
        SendMessageW(window, WM_MOUSEMOVE, 0, PointParam(point.x, point.y));
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, PointParam(point.x, point.y));
        at_ = point;
        editor_.Pump();
    }

    void MoveTo(HWND window, POINT point) {
        const int distance = std::max(std::abs(point.x - at_.x), std::abs(point.y - at_.y));
        const int steps = std::max(1, distance / 10);
        for (int step = 1; step <= steps; ++step) {
            const POINT next{ at_.x + ((point.x - at_.x) * step) / steps, at_.y + ((point.y - at_.y) * step) / steps };
            SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, PointParam(next.x, next.y));
        }
        at_ = point;
        editor_.Pump();
    }

    // For a window that follows the pointer: every step is a screen point, re-expressed in the
    // client area the window has at that moment, the way the mouse reports it.
    void MoveToScreen(HWND window, POINT screen) {
        POINT from = at_;
        ClientToScreen(window, &from);
        const int distance = std::max(std::abs(screen.x - from.x), std::abs(screen.y - from.y));
        const int steps = std::max(1, distance / 10);
        POINT local{};
        for (int step = 1; step <= steps; ++step) {
            local = POINT{ from.x + ((screen.x - from.x) * step) / steps, from.y + ((screen.y - from.y) * step) / steps };
            ScreenToClient(window, &local);
            SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, PointParam(local.x, local.y));
        }
        at_ = local;
        editor_.Pump();
    }

    void Up(HWND window) {
        SendMessageW(window, WM_LBUTTONUP, 0, PointParam(at_.x, at_.y));
        editor_.Pump();
    }

private:
    const EditorWindowUnderTest& editor_;
    POINT at_{};
};

[[nodiscard]] const DockPanel* PanelOfKind(const EditorDockModel& dock, DockPanelKind kind) {
    for (const DockPanel& panel : dock.Queries().Panels()) {
        if (panel.kind == kind) {
            return dock.Queries().FindPanel(panel.id);
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<DockPanelLayout> LayoutOf(const DockLayout& layout, std::uint32_t panelId) {
    for (const DockPanelLayout& panel : layout.panels) {
        if (panel.panelId == panelId) {
            return panel;
        }
    }
    return std::nullopt;
}

// A point on the tab's title, clear of its close button.
[[nodiscard]] POINT TabGrip(const DockPanelLayout& panel) noexcept {
    return POINT{ panel.tab.x + std::min(18, std::max(1, panel.tab.width / 3)), panel.tab.y + (panel.tab.height / 2) };
}

[[nodiscard]] POINT ContentCenter(const DockPanelLayout& panel) noexcept {
    return POINT{ panel.content.x + (panel.content.width / 2), panel.content.y + (panel.content.height / 2) };
}

[[nodiscard]] std::vector<std::uint32_t> TabOrder(const DockLayout& layout, std::uint32_t leafId) {
    std::vector<std::uint32_t> order;
    for (const DockPanelLayout& panel : layout.panels) {
        if (panel.leafId == leafId) {
            order.push_back(panel.panelId);
        }
    }
    return order;
}

} // namespace

EditorHeadlessPopupCheckResult EditorHeadlessPopupChecks::VerifyDockingInteractions(
    EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport, const std::filesystem::path& screenshots) {
    EditorWindowUnderTest editor(sceneContext, viewport);
    if (!editor.Create()) return { false, "window creation" };
    const EditorScriptedInputScope scriptedInput;
    EditorDockModel& dock = editor.Dock();
    const HWND main = editor.Window();
    Pointer pointer(editor);
    std::string failures;
    const auto expect = [&](bool condition, std::string_view what) {
        if (!condition) {
            failures += std::string{ what } + "; ";
        }
        return condition;
    };
    const auto capture = [&](HWND window, std::string_view name) {
        if (!screenshots.empty()) {
            static_cast<void>(CaptureClient(window).Save(screenshots / (std::string{ name } + ".bmp")));
        }
    };
    const auto id = [&](DockPanelKind kind) {
        const DockPanel* panel = PanelOfKind(dock, kind);
        return panel != nullptr ? panel->id : 0U;
    };
    const auto tabOf = [&](DockPanelKind kind) { return LayoutOf(editor.Layout(), id(kind)); };
    const auto floatingWindow = [&](DockPanelKind kind) { return editor.FloatingWindows().Queries().WindowForPanel(id(kind)); };

    // A new project opens with the everyday panels and nothing else.
    {
        std::set<DockPanelKind> open;
        for (const DockPanel& panel : dock.Queries().Panels()) {
            if (panel.visible) open.insert(panel.kind);
        }
        const std::set<DockPanelKind> everyday{ DockPanelKind::Hierarchy, DockPanelKind::Scene, DockPanelKind::Inspector,
            DockPanelKind::Assets, DockPanelKind::Console, DockPanelKind::Agent };
        expect(open == everyday, "default workspace opens only the everyday panels");
        expect(editor.Layout().panels.size() == everyday.size(), "every everyday panel is docked");
    }
    const std::string defaultTree = dock.Commands().SerializeWorkspace();
    capture(main, "dock-default");
    const kb::scene::SceneEntity selectedAtStart = sceneContext.SelectedEntity();

    // A click on a tab brings its panel forward.
    if (const auto console = tabOf(DockPanelKind::Console); expect(console.has_value(), "console tab")) {
        pointer.Down(main, TabGrip(*console));
        pointer.Up(main);
        expect(editor.ActivePanel(DockPanelKind::Console).has_value(), "tab click activates");
    }

    // Dragging a tab along its strip changes the order of the group.
    {
        const auto console = tabOf(DockPanelKind::Console);
        const auto assets = tabOf(DockPanelKind::Assets);
        if (expect(console && assets && console->leafId == assets->leafId, "console shares the drawer group")) {
            const std::vector<std::uint32_t> before = TabOrder(editor.Layout(), console->leafId);
            pointer.Down(main, TabGrip(*console));
            pointer.MoveTo(main, POINT{ assets->tab.x + 6, assets->tab.y + (assets->tab.height / 2) });
            pointer.Up(main);
            const std::vector<std::uint32_t> after = TabOrder(editor.Layout(), assets->leafId);
            expect(after.size() == before.size() && after != before && after.front() == console->panelId, "tab reorder within the strip");
            expect(dock.Queries().FindPanel(console->panelId)->area != DockArea::Floating, "reorder keeps the tab docked");
        }
    }

    // Dragging a tab off its strip tears the panel into its own window; released over the middle of a panel it stays floating.
    const auto scene = tabOf(DockPanelKind::Scene);
    if (const auto console = tabOf(DockPanelKind::Console); expect(console && scene, "console and scene tabs")) {
        pointer.Down(main, TabGrip(*console));
        pointer.MoveTo(main, ContentCenter(*scene));
        expect(editor.DockController().ActiveDrag() != nullptr && editor.DockController().ActiveDrag()->detached, "tab tears off");
        expect(editor.DockController().DropPreview() == nullptr, "no drop target over the middle of a panel");
        pointer.Up(main);
        const DockPanel* panel = dock.Queries().FindPanel(console->panelId);
        expect(panel->area == DockArea::Floating && panel->visible, "released panel floats");
        expect(floatingWindow(DockPanelKind::Console) != nullptr, "floating panel has a window");
        expect(!tabOf(DockPanelKind::Console).has_value(), "floating panel left the dock tree");
        if (const HWND window = floatingWindow(DockPanelKind::Console); window != nullptr) {
            expect(MonitorFromWindow(window, MONITOR_DEFAULTTONULL) == nullptr, "floating panel of an off-screen host stays off screen");
            capture(window, "dock-floating-console");
        }
    }

    // The floating window's title drags it back: over another group's strip it joins that group.
    if (const HWND window = floatingWindow(DockPanelKind::Console); expect(window != nullptr, "floating console window")) {
        const auto hierarchy = tabOf(DockPanelKind::Hierarchy);
        POINT target{ hierarchy->tab.x + hierarchy->tab.width + 20, hierarchy->tab.y + (hierarchy->tab.height / 2) };
        ClientToScreen(main, &target);
        pointer.Down(window, POINT{ 40, 12 });
        pointer.MoveToScreen(window, target);
        const DockDropPreview* preview = editor.DockController().DropPreview();
        expect(preview != nullptr && preview->kind == DockDropPreviewKind::StripMarker && preview->leafId == hierarchy->leafId,
            "strip marker over the target group");
        capture(main, "dock-drop-preview");
        pointer.Up(window);
        const auto console = tabOf(DockPanelKind::Console);
        expect(console && console->leafId == hierarchy->leafId, "floating panel joins a group by its strip");
        expect(floatingWindow(DockPanelKind::Console) == nullptr, "docked panel gives up its window");
    }
    expect(sceneContext.SelectedEntity() == selectedAtStart, "rearranging panels keeps the scene selection");

    // Dragged out of the group to the edge of a panel, it leaves the group for a place of its own.
    if (const auto console = tabOf(DockPanelKind::Console); expect(console.has_value(), "grouped console tab")) {
        const auto sceneNow = tabOf(DockPanelKind::Scene);
        pointer.Down(main, TabGrip(*console));
        pointer.MoveTo(main, POINT{ sceneNow->content.x + (sceneNow->content.width / 2), sceneNow->content.y + sceneNow->content.height - 12 });
        const DockDropPreview* preview = editor.DockController().DropPreview();
        expect(preview != nullptr && preview->kind == DockDropPreviewKind::Glow, "edge highlight under a dragged panel");
        pointer.Up(main);
        const auto moved = tabOf(DockPanelKind::Console);
        const auto hierarchy = tabOf(DockPanelKind::Hierarchy);
        expect(moved && hierarchy && moved->leafId != hierarchy->leafId, "panel leaves its group at a panel edge");
        expect(moved && dock.Queries().PanelCountInLeaf(moved->leafId) == 1U, "ungrouped panel has its own group");
    }
    capture(main, "dock-rearranged");

    // Escape during a drag puts everything back.
    if (const auto inspector = tabOf(DockPanelKind::Inspector); expect(inspector.has_value(), "inspector tab")) {
        const std::string before = dock.Commands().SerializeWorkspace();
        pointer.Down(main, TabGrip(*inspector));
        pointer.MoveTo(main, ContentCenter(*tabOf(DockPanelKind::Scene)));
        expect(dock.Queries().FindPanel(inspector->panelId)->area == DockArea::Floating, "inspector torn off before Escape");
        SendMessageW(main, WM_KEYDOWN, VK_ESCAPE, 0);
        editor.Pump();
        expect(editor.DockController().ActiveDrag() == nullptr, "Escape ends the drag");
        expect(dock.Commands().SerializeWorkspace() == before, "Escape restores the arrangement");
        expect(floatingWindow(DockPanelKind::Inspector) == nullptr, "Escape removes the torn-off window");
        pointer.Up(main);
        expect(dock.Commands().SerializeWorkspace() == before, "releasing after Escape changes nothing");
    }

    // Double click on a tab fills the workspace with its group; again restores it.
    if (const auto sceneTab = tabOf(DockPanelKind::Scene); expect(sceneTab.has_value(), "scene tab")) {
        const POINT grip = TabGrip(*sceneTab);
        const std::size_t leaves = editor.Layout().leaves.size();
        SendMessageW(main, WM_LBUTTONDBLCLK, MK_LBUTTON, PointParam(grip.x, grip.y));
        editor.Pump();
        expect(dock.Queries().MaximizedLeafId() == sceneTab->leafId && editor.Layout().leaves.size() == 1U, "double click maximizes the group");
        capture(main, "dock-maximized");
        const POINT maximizedGrip = TabGrip(*tabOf(DockPanelKind::Scene));
        SendMessageW(main, WM_LBUTTONDBLCLK, MK_LBUTTON, PointParam(maximizedGrip.x, maximizedGrip.y));
        editor.Pump();
        expect(dock.Queries().MaximizedLeafId() == 0U && editor.Layout().leaves.size() == leaves, "double click restores the group");
    }

    // The tab menu: maximize, float, dock back, close others.
    if (const std::uint32_t inspector = id(DockPanelKind::Inspector); expect(inspector != 0U, "inspector panel")) {
        const std::vector<EditorDockTabMenuItem> items = EditorDockTabMenu::Items(dock, inspector);
        expect(items.size() == 4U && items[0].command == EditorDockTabCommand::Close && items[3].command == EditorDockTabCommand::Float,
            "docked tab menu rows");
        expect(EditorDockTabMenu::Execute(EditorDockTabCommand::Float, main, inspector, dock, editor.FloatingWindows(), sceneContext) &&
            floatingWindow(DockPanelKind::Inspector) != nullptr, "tab menu floats the panel");
        const std::vector<EditorDockTabMenuItem> floatingItems = EditorDockTabMenu::Items(dock, inspector);
        expect(floatingItems.size() == 2U && floatingItems[1].command == EditorDockTabCommand::DockBack, "floating title menu rows");
        if (const HWND window = floatingWindow(DockPanelKind::Inspector); window != nullptr) {
            expect(EditorDockTabMenu::PanelAt(window, main, 40, 12, dock, editor.FloatingWindows(), editor.Metrics()) == inspector,
                "floating title resolves to its panel");
        }
        expect(EditorDockTabMenu::Execute(EditorDockTabCommand::DockBack, main, inspector, dock, editor.FloatingWindows(), sceneContext) &&
            floatingWindow(DockPanelKind::Inspector) == nullptr && tabOf(DockPanelKind::Inspector).has_value(), "floating panel docks back from its menu");
        const auto agent = tabOf(DockPanelKind::Agent);
        const auto docked = tabOf(DockPanelKind::Inspector);
        expect(agent && docked && agent->leafId == docked->leafId, "docked back beside its area's panels");
        static_cast<void>(EditorDockTabMenu::Execute(EditorDockTabCommand::CloseOthers, main, inspector, dock, editor.FloatingWindows(), sceneContext));
        expect(!dock.Queries().FindPanel(id(DockPanelKind::Agent))->visible && dock.Queries().FindPanel(inspector)->visible,
            "close others keeps only the chosen tab");
    }

    // Middle click closes a tab; the close button of a floating panel closes the panel.
    if (const auto hierarchy = tabOf(DockPanelKind::Hierarchy); expect(hierarchy.has_value(), "hierarchy tab")) {
        const POINT grip = TabGrip(*hierarchy);
        SendMessageW(main, WM_MBUTTONDOWN, MK_MBUTTON, PointParam(grip.x, grip.y));
        SendMessageW(main, WM_MBUTTONUP, 0, PointParam(grip.x, grip.y));
        editor.Pump();
        expect(!dock.Queries().FindPanel(hierarchy->panelId)->visible && !tabOf(DockPanelKind::Hierarchy).has_value(), "middle click closes the tab");
    }
    if (const std::uint32_t console = id(DockPanelKind::Console); console != 0U) {
        static_cast<void>(EditorDockTabMenu::Execute(EditorDockTabCommand::Float, main, console, dock, editor.FloatingWindows(), sceneContext));
        if (const HWND window = floatingWindow(DockPanelKind::Console); expect(window != nullptr, "console floats for the close check")) {
            RECT client{};
            GetClientRect(window, &client);
            const DockRect close = FloatingWindowControlLayout::Rect(editor.Metrics(), client.right, FloatingWindowControlKind::Close);
            pointer.Down(window, POINT{ close.x + (close.width / 2), close.y + (close.height / 2) });
            editor.Pump();
            expect(!dock.Queries().FindPanel(console)->visible, "floating close button closes the panel");
            expect(floatingWindow(DockPanelKind::Console) == nullptr, "closed floating panel loses its window");
            expect(!tabOf(DockPanelKind::Console).has_value(), "closed floating panel does not dock itself");
        }
    }
    capture(main, "dock-after-closing");

    // The Layout menu lists the everyday panels and reopens the closed ones.
    {
        const EditorMenuRects bar = EditorToolbarRenderer::ResolveMenu(ToRect(editor.Layout().menu), EditorMenuCommand::None, 0);
        const auto openLayoutMenu = [&] { editor.Click(main, Center(EditorToolbarLayout::MenuRectByCommand(bar, EditorMenuCommand::Layout))); };
        const auto rowOf = [&](DockPanelKind kind) {
            const std::vector<EditorLayoutMenuRow>& rows = editor.Shell().LayoutMenu().Rows();
            for (std::size_t index = 0; index < rows.size(); ++index) {
                if (rows[index].action == EditorLayoutMenuAction::TogglePanel && rows[index].enabled && rows[index].panelKind == kind) {
                    return static_cast<int>(index);
                }
            }
            return -1;
        };
        for (const DockPanelKind kind : { DockPanelKind::Hierarchy, DockPanelKind::Console, DockPanelKind::Agent }) {
            openLayoutMenu();
            const int row = rowOf(kind);
            if (!expect(editor.Shell().OpenMenu() == EditorMenuCommand::Layout && row >= 0, "layout menu lists the panel")) {
                continue;
            }
            expect(!editor.Shell().LayoutMenu().Rows()[static_cast<std::size_t>(row)].active, "closed panel is unmarked in the menu");
            if (kind == DockPanelKind::Hierarchy) {
                if (const HWND popup = FindWindowExW(nullptr, nullptr, L"KBEditorToolbarMenuOverlay", nullptr); popup != nullptr) {
                    capture(popup, "dock-layout-menu");
                }
            }
            const EditorMenuRects rects = EditorToolbarRenderer::ResolveMenu(
                ToRect(editor.Layout().menu), EditorMenuCommand::Layout, editor.Shell().MenuRowCount(EditorMenuCommand::Layout));
            editor.Click(main, Center(rects.dropdownRows[static_cast<std::size_t>(row)]));
            expect(dock.Queries().FindPanel(id(kind))->visible && tabOf(kind).has_value(), "layout menu reopens the panel");
            expect(editor.Shell().OpenMenu() == EditorMenuCommand::None, "panel row closes the menu");
        }
        openLayoutMenu();
        const int inspectorRow = rowOf(DockPanelKind::Inspector);
        expect(inspectorRow >= 0 && editor.Shell().LayoutMenu().Rows()[static_cast<std::size_t>(inspectorRow)].active, "open panel is marked in the menu");
        // Default puts the workspace back on the arrangement a new project starts with.
        const EditorMenuRects rects = EditorToolbarRenderer::ResolveMenu(
            ToRect(editor.Layout().menu), EditorMenuCommand::Layout, editor.Shell().MenuRowCount(EditorMenuCommand::Layout));
        editor.Click(main, Center(rects.dropdownRows[0]));
        expect(dock.Commands().SerializeWorkspace() == defaultTree, "Layout > Default restores the starting arrangement");
    }

    // A group with more tabs than its strip has room for keeps every tab reachable.
    {
        for (const DockPanelKind kind : { DockPanelKind::ProjectSettings, DockPanelKind::Plugins, DockPanelKind::EditorSettings }) {
            static_cast<void>(dock.Commands().ActivatePanelKind(kind, DockArea::Right));
        }
        const DockLayout layout = editor.Layout();
        const auto inspector = LayoutOf(layout, id(DockPanelKind::Inspector));
        bool reachable = inspector.has_value() && dock.Queries().PanelCountInLeaf(inspector->leafId) >= 5U;
        for (const DockPanelLayout& panel : layout.panels) {
            if (inspector && panel.leafId == inspector->leafId) {
                reachable = reachable && panel.tab.width >= 16 && panel.tab.x + panel.tab.width <= panel.tabStrip.x + panel.tabStrip.width;
            }
        }
        expect(reachable, "crowded strip keeps every tab inside it");
        const auto active = LayoutOf(layout, id(DockPanelKind::EditorSettings));
        expect(active && inspector && active->tab.width > inspector->tab.width, "crowded strip leaves the active tab its title");
        InvalidateRect(main, nullptr, FALSE);
        editor.Pump();
        capture(main, "dock-crowded-strip");
        if (const auto last = LayoutOf(layout, id(DockPanelKind::EditorSettings)); last.has_value()) {
            pointer.Down(main, POINT{ last->tab.x + (last->tab.width / 3), last->tab.y + (last->tab.height / 2) });
            pointer.Up(main);
        }
        dock.Commands().ResetWorkspace();
    }

    // Settings opens in a window of its own, wide enough for its pages, and closes from its title.
    {
        const EditorMenuRects bar = EditorToolbarRenderer::ResolveMenu(ToRect(editor.Layout().menu), EditorMenuCommand::None, 0);
        editor.Click(main, Center(EditorToolbarLayout::MenuRectByCommand(bar, EditorMenuCommand::Options)));
        const EditorMenuRects rects = EditorToolbarRenderer::ResolveMenu(
            ToRect(editor.Layout().menu), EditorMenuCommand::Options, editor.Shell().MenuRowCount(EditorMenuCommand::Options));
        editor.Click(main, Center(rects.dropdownRows[1]));
        const DockPanel* settings = PanelOfKind(dock, DockPanelKind::ProjectSettings);
        const HWND window = floatingWindow(DockPanelKind::ProjectSettings);
        if (expect(settings->visible && settings->area == DockArea::Floating && window != nullptr, "Settings opens in its own window")) {
            RECT client{};
            GetClientRect(window, &client);
            expect(client.right >= 480 && client.bottom >= 360, "Settings window has room for its pages");
            InvalidateRect(window, nullptr, FALSE);
            editor.Pump();
            capture(window, "dock-settings-window");
            const DockRect close = FloatingWindowControlLayout::Rect(editor.Metrics(), client.right, FloatingWindowControlKind::Close);
            pointer.Down(window, POINT{ close.x + (close.width / 2), close.y + (close.height / 2) });
            editor.Pump();
            expect(!PanelOfKind(dock, DockPanelKind::ProjectSettings)->visible, "Settings window closes from its title");
        }
        expect(dock.Commands().SerializeWorkspace() == defaultTree, "Settings leaves the docked arrangement alone");
    }

    // Dragging the seam between two panels resizes both.
    {
        const DockLayout layout = editor.Layout();
        const DockSplitterLayout split = layout.splitters.front();
        const POINT grip = Center(ToRect(split.rect));
        const std::string before = dock.Commands().SerializeWorkspace();
        pointer.Down(main, grip);
        pointer.MoveTo(main, POINT{ grip.x + (split.axis == DockSplitAxis::Horizontal ? 60 : 0), grip.y + (split.axis == DockSplitAxis::Vertical ? 60 : 0) });
        expect(dock.Commands().SerializeWorkspace() != before, "splitter drag resizes");
        SendMessageW(main, WM_KEYDOWN, VK_ESCAPE, 0);
        editor.Pump();
        expect(dock.Commands().SerializeWorkspace() == before, "Escape undoes a splitter drag");
        pointer.Up(main);
    }

    // Repainting the whole window stays well inside a frame.
    InvalidateRect(main, nullptr, FALSE);
    editor.Pump();
    static_cast<void>(CaptureClient(main));
    constexpr int kPaints = 12;
    const auto started = std::chrono::steady_clock::now();
    for (int paint = 0; paint < kPaints; ++paint) {
        static_cast<void>(CaptureClient(main));
    }
    const double paintMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() / kPaints;
    expect(paintMs < 50.0, "full window repaint under 50 ms");

    if (!failures.empty()) {
        return { false, failures + "paint " + std::to_string(paintMs) + " ms" };
    }
    return { true, "defaults; tab click, reorder, tear-off, group, ungroup, Escape, maximize, tab menu, middle click, floating close, "
        "Layout panels, crowded strip, splitter; paint " + std::to_string(paintMs) + " ms" };
}

} // namespace kb::editor
#endif
