#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "docking/EditorDockModel.hpp"
#include "windowing/FloatingWindowControlHitTester.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
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

[[nodiscard]] const kb::editor::DockLeafLayout* FindLeafForPanel(const kb::editor::DockLayout& layout, std::uint32_t panelId) noexcept {
    const kb::editor::DockPanelLayout* panel = FindPanelLayout(layout, panelId);
    if (panel == nullptr) {
        return nullptr;
    }

    const auto iter = std::find_if(layout.leaves.begin(), layout.leaves.end(), [panel](const kb::editor::DockLeafLayout& leaf) {
        return leaf.leafId == panel->leafId;
    });
    return iter == layout.leaves.end() ? nullptr : &(*iter);
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
    const kb::editor::DockPanelLayout* inspectorLayout = FindPanelLayout(initialLayout, 4U);
    kb::editor::tests::Require(sceneLayout != nullptr && inspectorLayout != nullptr, "Scene and Inspector panels should exist in default layout");
    model.Commands().UndockPanel(4U, kb::editor::DockRect{ 80, 90, 380, 300 });
    model.Commands().DockPanelTo(4U, kb::editor::DockDropPreview{
                                      .zone = kb::editor::DockDropZone::Center,
                                      .kind = kb::editor::DockDropPreviewKind::Glow,
                                      .leafId = sceneLayout->leafId,
                                      .rect = sceneLayout->content,
                                  });
    const kb::editor::DockLayout dockedLayout = BuildDefaultLayout(model);
    sceneLayout = FindPanelLayout(dockedLayout, 2U);
    inspectorLayout = FindPanelLayout(dockedLayout, 4U);
    kb::editor::tests::Require(sceneLayout != nullptr && inspectorLayout != nullptr, "Inspector did not dock next to Scene");
    kb::editor::tests::Require(sceneLayout->leafId == inspectorLayout->leafId, "Scene/Inspector tabs should share a leaf");
    const std::vector<std::uint32_t> initialOrder = PanelOrderInLeaf(dockedLayout, sceneLayout->leafId);

    model.Commands().ActivatePanel(4U);
    const kb::editor::DockLayout inspectorActiveLayout = BuildDefaultLayout(model);
    kb::editor::tests::Require(PanelOrderInLeaf(inspectorActiveLayout, sceneLayout->leafId) == initialOrder, "Activating Inspector reordered tabs");
    const kb::editor::DockPanelLayout* activeInspector = FindPanelLayout(inspectorActiveLayout, 4U);
    kb::editor::tests::Require(activeInspector != nullptr && activeInspector->active, "Inspector did not become active");

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
    kb::editor::tests::Require(sceneLayout != nullptr, "Scene panel should start docked");
    kb::editor::tests::Require(sceneLayout->active, "Scene View should be the active default center panel");

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

void RunTopChromeDropPreviewTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockLayout layout = BuildDefaultLayout(model);
    const std::optional<kb::editor::DockDropPreview> preview =
        model.Queries().ResolveDropPreview(layout, layout.workspace.x + 16, layout.workspace.y - 1);

    kb::editor::tests::Require(preview.has_value(), "Top chrome did not resolve a root drop preview");
    kb::editor::tests::Require(preview->zone == kb::editor::DockDropZone::Top, "Top chrome drop did not resolve to the top root zone");
    kb::editor::tests::Require(preview->leafId == 0U, "Top chrome drop should target the root, not an existing leaf");
    kb::editor::tests::Require(
        preview->rect.x == layout.workspace.x && preview->rect.y == layout.workspace.y,
        "Top chrome preview was not anchored to the workspace");
    kb::editor::tests::Require(preview->rect.width == layout.workspace.width, "Top chrome preview should span the workspace width");
    kb::editor::tests::Require(
        preview->rect.height > 0 && preview->rect.height < layout.workspace.height / 2,
        "Top chrome preview should use the root split band");
}

void RunDockedPanelsShareEdgesWithoutVisibleGapsTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::EditorMetrics metrics{};
    const kb::editor::DockLayout layout = BuildDefaultLayout(model);

    kb::editor::tests::Require(layout.workspace.y == metrics.menuHeight + metrics.toolbarHeight, "Dock workspace should start directly below the toolbar");

    const kb::editor::DockLeafLayout* hierarchy = FindLeafForPanel(layout, 1U);
    const kb::editor::DockLeafLayout* scene = FindLeafForPanel(layout, 2U);
    const kb::editor::DockLeafLayout* inspector = FindLeafForPanel(layout, 4U);
    const kb::editor::DockLeafLayout* assets = FindLeafForPanel(layout, 5U);
    kb::editor::tests::Require(hierarchy != nullptr && scene != nullptr && inspector != nullptr && assets != nullptr, "Default dock leaves were not found");

    const kb::editor::DockPanelLayout* scenePanel = FindPanelLayout(layout, 2U);
    kb::editor::tests::Require(scenePanel != nullptr, "Scene panel layout should be available");
    kb::editor::tests::Require(scenePanel->frame.x == scene->frame.x && scenePanel->frame.width == scene->frame.width, "Panel layout should carry the owning leaf frame");
    kb::editor::tests::Require(scenePanel->tabStrip.y == scene->tabStrip.y && scenePanel->tabStrip.height == scene->tabStrip.height, "Panel layout should carry the owning tab strip");
    kb::editor::tests::Require(scenePanel->contentClip.x == scenePanel->content.x && scenePanel->contentClip.width == scenePanel->content.width, "Panel content clip should match the active content bounds");

    kb::editor::tests::Require(hierarchy->frame.x + hierarchy->frame.width == scene->frame.x, "Hierarchy and Scene panels should share a vertical edge without a gap");
    kb::editor::tests::Require(scene->frame.x + scene->frame.width == inspector->frame.x, "Scene and Inspector panels should share a vertical edge without a gap");
    kb::editor::tests::Require(scene->frame.y + scene->frame.height == assets->frame.y, "Scene and bottom panels should share a horizontal edge without a gap");

    const kb::editor::DockHit sceneLeftEdgeHit = model.Queries().HitTest(layout, scene->frame.x, scene->frame.y + 12);
    kb::editor::tests::Require(sceneLeftEdgeHit.kind == kb::editor::DockHitKind::Splitter, "Shared panel edge should remain a splitter hit target");
}

void RunTabStripDropInsertsAtResolvedIndexTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockLayout initialLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* sceneLayout = FindPanelLayout(initialLayout, 2U);
    const kb::editor::DockPanelLayout* inspectorLayout = FindPanelLayout(initialLayout, 4U);
    kb::editor::tests::Require(sceneLayout != nullptr && inspectorLayout != nullptr, "Scene and Inspector panels should exist in default layout");
    model.Commands().UndockPanel(4U, kb::editor::DockRect{ 80, 90, 380, 300 });
    model.Commands().DockPanelTo(4U, kb::editor::DockDropPreview{
                                      .zone = kb::editor::DockDropZone::Center,
                                      .kind = kb::editor::DockDropPreviewKind::Glow,
                                      .leafId = sceneLayout->leafId,
                                      .rect = sceneLayout->content,
                                  });
    const kb::editor::DockLayout tabbedLayout = BuildDefaultLayout(model);
    sceneLayout = FindPanelLayout(tabbedLayout, 2U);
    inspectorLayout = FindPanelLayout(tabbedLayout, 4U);
    kb::editor::tests::Require(sceneLayout != nullptr && inspectorLayout != nullptr, "Inspector did not dock next to Scene");
    kb::editor::tests::Require(sceneLayout->leafId == inspectorLayout->leafId, "Scene/Inspector tabs should share a leaf");

    const std::optional<kb::editor::DockDropPreview> preview =
        model.Queries().ResolveDropPreview(tabbedLayout, inspectorLayout->tab.x + 1, inspectorLayout->tab.y + 1);
    kb::editor::tests::Require(preview.has_value(), "Tab strip did not resolve a drop marker");
    kb::editor::tests::Require(preview->kind == kb::editor::DockDropPreviewKind::StripMarker, "Tab strip drop should use a strip marker preview");
    kb::editor::tests::Require(preview->tabInsertionIndex == 1U, "Tab strip insertion index did not match the cursor position");
    kb::editor::tests::Require(
        preview->rect.width == 3 && preview->rect.height == inspectorLayout->tab.height,
        "Tab strip marker geometry should be a thin vertical marker");

    model.Commands().UndockPanel(5U, kb::editor::DockRect{ 80, 90, 620, 300 });
    model.Commands().DockPanelTo(5U, *preview);
    const kb::editor::DockLayout dockedLayout = BuildDefaultLayout(model);
    const std::vector<std::uint32_t> order = PanelOrderInLeaf(dockedLayout, sceneLayout->leafId);
    kb::editor::tests::Require(order.size() >= 3U, "Docked tab was not inserted into the target leaf");
    kb::editor::tests::Require(order[0] == 2U && order[1] == 5U && order[2] == 4U, "Docked tab was not inserted at the resolved tab strip index");
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
    RunTopChromeDropPreviewTest();
    RunDockedPanelsShareEdgesWithoutVisibleGapsTest();
    RunTabStripDropInsertsAtResolvedIndexTest();
    RunSplitterAndFloatingResizeTest();
    RunFloatingWindowControlHitTest();
}

} // namespace kb::editor::tests
