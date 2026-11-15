#include "app/docking/EditorDockTabMenu.hpp"

#if defined(_WIN32)
#include "app/panels/EditorPanelCloser.hpp"
#include "docking/DefaultDockWorkspace.hpp"
#include "docking/DockMainLayoutResolver.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "localization/EditorLocalization.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

namespace kb::editor {
namespace {

constexpr UINT kFirstCommandId = 1U;

// The area a panel belongs to in the arrangement a new project starts with.
[[nodiscard]] DockArea HomeArea(std::uint32_t panelId) {
    for (const DockPanel& panel : DefaultDockWorkspace{}.CreatePanels()) {
        if (panel.id == panelId) {
            return panel.area;
        }
    }
    return DockArea::Center;
}

// A floating panel returns as a tab of the group that holds its area's panels.
[[nodiscard]] DockDropPreview DockBackTarget(const EditorDockModel& dockModel, std::uint32_t panelId) {
    const DockArea home = HomeArea(panelId);
    for (const DockPanel& candidate : dockModel.Queries().Panels()) {
        if (candidate.id == panelId || !candidate.visible || candidate.area != home) {
            continue;
        }
        if (const std::uint32_t leaf = dockModel.Queries().LeafOfPanel(candidate.id); leaf != 0U) {
            return DockDropPreview{
                .zone = DockDropZone::Center,
                .kind = DockDropPreviewKind::StripMarker,
                .leafId = leaf,
                .tabInsertionIndex = dockModel.Queries().PanelCountInLeaf(leaf),
            };
        }
    }
    switch (home) {
    case DockArea::Left:
        return DockDropPreview{ .zone = DockDropZone::Left };
    case DockArea::Right:
        return DockDropPreview{ .zone = DockDropZone::Right };
    case DockArea::Bottom:
        return DockDropPreview{ .zone = DockDropZone::Bottom };
    default:
        return DockDropPreview{ .zone = DockDropZone::Center };
    }
}

} // namespace

std::vector<EditorDockTabMenuItem> EditorDockTabMenu::Items(const EditorDockModel& dockModel, std::uint32_t panelId) {
    const DockPanel* panel = dockModel.Queries().FindPanel(panelId);
    if (panel == nullptr || !panel->visible) {
        return {};
    }
    std::vector<EditorDockTabMenuItem> items;
    items.push_back({ EditorDockTabCommand::Close, std::string{ EditorText("dockTab.close", "Close Tab") } });
    if (panel->area == DockArea::Floating) {
        items.push_back({ EditorDockTabCommand::DockBack, std::string{ EditorText("dockTab.dock", "Dock in Main Window") } });
        return items;
    }
    const std::uint32_t leaf = dockModel.Queries().LeafOfPanel(panelId);
    items.push_back({ EditorDockTabCommand::CloseOthers, std::string{ EditorText("dockTab.closeOthers", "Close Other Tabs") },
        dockModel.Queries().PanelCountInLeaf(leaf) > 1U });
    items.push_back({ EditorDockTabCommand::ToggleMaximize, std::string{ dockModel.Queries().MaximizedLeafId() == leaf
        ? EditorText("dockTab.restore", "Restore Size")
        : EditorText("dockTab.maximize", "Maximize") } });
    items.push_back({ EditorDockTabCommand::Float, std::string{ EditorText("dockTab.float", "Move to New Window") }, panel->detachable });
    return items;
}

bool EditorDockTabMenu::Execute(
    EditorDockTabCommand command, HWND owner, std::uint32_t panelId,
    EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext) {
    const DockPanel* panel = dockModel.Queries().FindPanel(panelId);
    if (panel == nullptr || !panel->visible) {
        return false;
    }
    switch (command) {
    case EditorDockTabCommand::Close:
        return EditorPanelCloser::Close(owner, panelId, dockModel, floatingWindows, sceneContext);
    case EditorDockTabCommand::CloseOthers:
        EditorPanelCloser::CloseOthersInGroup(owner, panelId, dockModel, floatingWindows, sceneContext);
        return true;
    case EditorDockTabCommand::ToggleMaximize: {
        const std::uint32_t leaf = dockModel.Queries().LeafOfPanel(panelId);
        dockModel.Commands().ActivatePanel(panelId);
        return dockModel.Commands().ToggleMaximizedLeaf(leaf);
    }
    case EditorDockTabCommand::Float: {
        if (panel->area == DockArea::Floating || !panel->detachable) {
            return false;
        }
        const DockRect rect = panel->floatingRect;
        const std::string title = panel->title;
        dockModel.Commands().UndockPanel(panelId, rect);
        if (!floatingWindows.Commands().Create(panelId, title, rect)) {
            dockModel.Commands().DockPanelTo(panelId, DockBackTarget(dockModel, panelId));
            return false;
        }
        return true;
    }
    case EditorDockTabCommand::DockBack:
        if (panel->area != DockArea::Floating) {
            return false;
        }
        dockModel.Commands().DockPanelTo(panelId, DockBackTarget(dockModel, panelId));
        floatingWindows.Commands().Destroy(panelId);
        return true;
    case EditorDockTabCommand::None:
    default:
        return false;
    }
}

std::uint32_t EditorDockTabMenu::PanelAt(
    HWND messageWindow, HWND mainWindow, int x, int y,
    const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics) {
    if (messageWindow == mainWindow) {
        const DockLayout layout = DockMainLayoutResolver::Resolve(mainWindow, dockModel, metrics);
        const DockHit hit = dockModel.Queries().HitTest(layout, x, y);
        return hit.kind == DockHitKind::Tab ? hit.panelId : 0U;
    }
    const std::uint32_t panelId = floatingWindows.Queries().PanelId(messageWindow);
    if (panelId == 0U) {
        return 0U;
    }
    RECT client{};
    GetClientRect(messageWindow, &client);
    return FloatingWindowControlLayout::StripDragRect(metrics, client.right - client.left).Contains(x, y) ? panelId : 0U;
}

bool EditorDockTabMenu::HandleRightButtonDown(
    HWND messageWindow, HWND mainWindow, int x, int y,
    EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext, const EditorMetrics& metrics) {
    const std::uint32_t panelId = PanelAt(messageWindow, mainWindow, x, y, dockModel, floatingWindows, metrics);
    if (panelId == 0U) {
        return false;
    }
    const std::vector<EditorDockTabMenuItem> items = Items(dockModel, panelId);
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr || items.empty()) {
        return true;
    }
    for (std::size_t index = 0; index < items.size(); ++index) {
        AppendMenuW(menu, MF_STRING | (items[index].enabled ? 0U : MF_GRAYED), kFirstCommandId + static_cast<UINT>(index),
            WidenUtf8(items[index].label.c_str()).c_str());
    }
    POINT screen{ x, y };
    ClientToScreen(messageWindow, &screen);
    SetForegroundWindow(messageWindow);
    const UINT chosen = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, screen.x, screen.y, 0, messageWindow, nullptr));
    DestroyMenu(menu);
    if (chosen >= kFirstCommandId && chosen < kFirstCommandId + items.size()) {
        static_cast<void>(Execute(items[chosen - kFirstCommandId].command, messageWindow, panelId, dockModel, floatingWindows, sceneContext));
        InvalidateRect(mainWindow, nullptr, FALSE);
    }
    return true;
}

bool EditorDockTabMenu::HandleMiddleButtonDown(
    HWND messageWindow, HWND mainWindow, int x, int y,
    EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext, const EditorMetrics& metrics) {
    const std::uint32_t panelId = PanelAt(messageWindow, mainWindow, x, y, dockModel, floatingWindows, metrics);
    if (panelId == 0U) {
        return false;
    }
    static_cast<void>(Execute(EditorDockTabCommand::Close, messageWindow, panelId, dockModel, floatingWindows, sceneContext));
    InvalidateRect(mainWindow, nullptr, FALSE);
    return true;
}

} // namespace kb::editor

#endif
