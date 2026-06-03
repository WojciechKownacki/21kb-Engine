#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "docking/EditorDockModel.hpp"
#include "windowing/FloatingWindowControlHitTester.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

[[nodiscard]] kb::editor::DockLayout BuildDefaultLayout(const kb::editor::EditorDockModel& model) {
    const kb::editor::EditorMetrics metrics{};
    return model.Queries().BuildLayout(
        1280,
        720,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize,
        metrics.panelPadding);
}

[[nodiscard]] const kb::editor::DockPanelLayout* FindPanelLayout(const kb::editor::DockLayout& layout, std::uint32_t panelId) noexcept {
    const auto iter = std::find_if(layout.panels.begin(), layout.panels.end(), [panelId](const kb::editor::DockPanelLayout& panel) {
        return panel.panelId == panelId;
    });
    return iter == layout.panels.end() ? nullptr : &(*iter);
}

[[nodiscard]] const kb::editor::DockPanel* RequirePanel(const kb::editor::EditorDockModel& model, std::uint32_t panelId) {
    const kb::editor::DockPanel* panel = model.Queries().FindPanel(panelId);
    kb::editor::tests::Require(panel != nullptr, "Expected dock panel was not found");
    return panel;
}

[[nodiscard]] std::vector<std::uint32_t> PanelOrderInLeaf(const kb::editor::DockLayout& layout, std::uint32_t leafId) {
    std::vector<std::uint32_t> order;
    for (const kb::editor::DockPanelLayout& panel : layout.panels) {
        if (panel.leafId == leafId) {
            order.push_back(panel.panelId);
        }
    }
    return order;
}

void RunTabActivationPreservesOrderTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockLayout initialLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* sceneLayout = FindPanelLayout(initialLayout, 2U);
    const kb::editor::DockPanelLayout* gameLayout = FindPanelLayout(initialLayout, 3U);
    kb::editor::tests::Require(sceneLayout != nullptr && gameLayout != nullptr, "Scene/Game tabs should exist in default layout");
    kb::editor::tests::Require(sceneLayout->leafId == gameLayout->leafId, "Scene/Game tabs should share a leaf");
    const std::vector<std::uint32_t> initialOrder = PanelOrderInLeaf(initialLayout, sceneLayout->leafId);

    model.Commands().ActivatePanel(3U);
    const kb::editor::DockLayout gameActiveLayout = BuildDefaultLayout(model);
    kb::editor::tests::Require(PanelOrderInLeaf(gameActiveLayout, sceneLayout->leafId) == initialOrder, "Activating Game View reordered tabs");
    const kb::editor::DockPanelLayout* activeGame = FindPanelLayout(gameActiveLayout, 3U);
    kb::editor::tests::Require(activeGame != nullptr && activeGame->active, "Game View did not become active");

    model.Commands().ActivatePanel(2U);
    const kb::editor::DockLayout sceneActiveLayout = BuildDefaultLayout(model);
    kb::editor::tests::Require(PanelOrderInLeaf(sceneActiveLayout, sceneLayout->leafId) == initialOrder, "Activating Scene View reordered tabs");
    const kb::editor::DockPanelLayout* activeScene = FindPanelLayout(sceneActiveLayout, 2U);
    kb::editor::tests::Require(activeScene != nullptr && activeScene->active, "Scene View did not become active");
}

void RunUndockAndDockSameFrameTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockLayout initialLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* sceneLayout = FindPanelLayout(initialLayout, 2U);
    const kb::editor::DockPanelLayout* gameLayout = FindPanelLayout(initialLayout, 3U);
    kb::editor::tests::Require(sceneLayout != nullptr, "Scene panel should start docked");
    kb::editor::tests::Require(gameLayout != nullptr, "Game panel should start docked");
    kb::editor::tests::Require(sceneLayout->leafId == gameLayout->leafId, "Scene and Game views should start as tabs in the same center leaf");
    kb::editor::tests::Require(sceneLayout->active && !gameLayout->active, "Scene View should be the active default center tab");

    model.Commands().UndockPanel(2U, kb::editor::DockRect{ 200, 160, 640, 420 });

    const kb::editor::DockPanel* floatingScene = RequirePanel(model, 2U);
    kb::editor::tests::Require(floatingScene->area == kb::editor::DockArea::Floating, "Undocked scene panel was not marked floating");
    kb::editor::tests::Require(floatingScene->floatingRect.x == 200 && floatingScene->floatingRect.y == 160, "Undocked scene panel did not keep the floating origin");
    kb::editor::tests::Require(floatingScene->floatingRect.width == 640 && floatingScene->floatingRect.height == 420, "Undocked scene panel did not keep the floating size");
    kb::editor::tests::Require(FindPanelLayout(BuildDefaultLayout(model), 2U) == nullptr, "Undocked scene panel still appeared in docked layout");

    const kb::editor::DockLayout undockedLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* hierarchyLayout = FindPanelLayout(undockedLayout, 1U);
    kb::editor::tests::Require(hierarchyLayout != nullptr, "Hierarchy panel should remain docked after scene undock");
    model.Commands().DockPanelTo(2U, kb::editor::DockDropPreview{
                                      .zone = kb::editor::DockDropZone::Center,
                                      .kind = kb::editor::DockDropPreviewKind::Glow,
                                      .leafId = hierarchyLayout->leafId,
                                      .rect = hierarchyLayout->content,
                                  });

    const kb::editor::DockLayout redockedLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* redockedScene = FindPanelLayout(redockedLayout, 2U);
    kb::editor::tests::Require(redockedScene != nullptr, "Scene panel did not dock back in the same frame");
    kb::editor::tests::Require(redockedScene->leafId == hierarchyLayout->leafId, "Scene panel did not dock into the requested leaf");
    kb::editor::tests::Require(model.Queries().PanelCountInLeaf(hierarchyLayout->leafId) == 2U, "Dock target leaf should contain hierarchy and scene panels");
    kb::editor::tests::Require(RequirePanel(model, 2U)->area == kb::editor::DockArea::Center, "Center drop did not restore scene panel area");
}

void RunSplitterAndFloatingResizeTest() {
    kb::editor::EditorDockModel model;
    kb::editor::DockLayout layout = BuildDefaultLayout(model);
    kb::editor::tests::Require(!layout.splitters.empty(), "Default dock workspace should expose splitters");

    const kb::editor::DockSplitterLayout splitter = layout.splitters.front();
    const int oldPosition = splitter.axis == kb::editor::DockSplitAxis::Horizontal ? splitter.rect.x : splitter.rect.y;
    const int resizeX = splitter.axis == kb::editor::DockSplitAxis::Horizontal ? splitter.container.x + splitter.container.width - 1 : splitter.container.x;
    const int resizeY = splitter.axis == kb::editor::DockSplitAxis::Vertical ? splitter.container.y + splitter.container.height - 1 : splitter.container.y;

    model.Commands().ResizeSplitter(splitter.nodeId, resizeX, resizeY, layout);
    layout = BuildDefaultLayout(model);
    const auto resized = std::find_if(layout.splitters.begin(), layout.splitters.end(), [splitter](const kb::editor::DockSplitterLayout& candidate) {
        return candidate.nodeId == splitter.nodeId;
    });
    kb::editor::tests::Require(resized != layout.splitters.end(), "Resized splitter disappeared from layout");
    const int newPosition = resized->axis == kb::editor::DockSplitAxis::Horizontal ? resized->rect.x : resized->rect.y;
    kb::editor::tests::Require(newPosition > oldPosition, "Splitter resize did not move the splitter toward the requested edge");

    model.Commands().UndockPanel(4U, kb::editor::DockRect{ 50, 60, 380, 300 });
    model.Commands().MoveFloatingPanel(4U, 90, 110);
    model.Commands().ResizeFloatingPanel(4U, 1, 1);
    const kb::editor::DockPanel* inspector = RequirePanel(model, 4U);
    kb::editor::tests::Require(inspector->floatingRect.x == 90 && inspector->floatingRect.y == 110, "Floating panel move did not update origin");
    kb::editor::tests::Require(inspector->floatingRect.width == 260 && inspector->floatingRect.height == 180, "Floating panel resize did not clamp minimum size");
}

void RunFloatingWindowControlHitTest() {
    const kb::editor::EditorMetrics metrics{};
    const int clientWidth = 480;
    const kb::editor::FloatingWindowControlHitTester hitTester;

    const kb::editor::DockRect minimize = kb::editor::FloatingWindowControlLayout::Rect(metrics, clientWidth, kb::editor::FloatingWindowControlKind::Minimize);
    const kb::editor::DockRect restore = kb::editor::FloatingWindowControlLayout::Rect(metrics, clientWidth, kb::editor::FloatingWindowControlKind::MaximizeRestore);
    const kb::editor::DockRect close = kb::editor::FloatingWindowControlLayout::Rect(metrics, clientWidth, kb::editor::FloatingWindowControlKind::Close);

    kb::editor::tests::Require(hitTester.HitTest(metrics, clientWidth, minimize.x + 1, minimize.y + 1) == kb::editor::FloatingWindowControlKind::Minimize, "Minimize control hit test failed");
    kb::editor::tests::Require(hitTester.HitTest(metrics, clientWidth, restore.x + 1, restore.y + 1) == kb::editor::FloatingWindowControlKind::MaximizeRestore, "Maximize/restore control hit test failed");
    kb::editor::tests::Require(hitTester.HitTest(metrics, clientWidth, close.x + 1, close.y + 1) == kb::editor::FloatingWindowControlKind::Close, "Close control hit test failed");
    kb::editor::tests::Require(hitTester.HitTest(metrics, clientWidth, close.x + close.width, close.y + 1) == kb::editor::FloatingWindowControlKind::None, "Control hit test should reject the exclusive right edge");
}

} // namespace

namespace kb::editor::tests {

void RunEditorDockingTests() {
    RunTabActivationPreservesOrderTest();
    RunUndockAndDockSameFrameTest();
    RunSplitterAndFloatingResizeTest();
    RunFloatingWindowControlHitTest();
}

} // namespace kb::editor::tests
