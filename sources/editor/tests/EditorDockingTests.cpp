#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "docking/EditorDockModel.hpp"
#include "rendering/DockTabControlGeometry.hpp"
#include "rendering/EditorToolbarLayout.hpp"
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

void RunClosePanelRemovesTabFromLayoutTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockLayout initialLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* sceneLayout = FindPanelLayout(initialLayout, 2U);
    const kb::editor::DockPanelLayout* scriptLayout = FindPanelLayout(initialLayout, 8U);
    kb::editor::tests::Require(sceneLayout != nullptr && scriptLayout != nullptr, "Scene and Script Editor should start in the center leaf");
    kb::editor::tests::Require(sceneLayout->leafId == scriptLayout->leafId, "Scene and Script Editor should share a tab group");

    kb::editor::tests::Require(model.Commands().ClosePanel(8U), "Closing Script Editor tab should succeed");
    const kb::editor::DockLayout closedLayout = BuildDefaultLayout(model);
    kb::editor::tests::Require(FindPanelLayout(closedLayout, 8U) == nullptr, "Closed Script Editor tab should not remain in layout");
    const kb::editor::DockPanelLayout* remainingScene = FindPanelLayout(closedLayout, 2U);
    kb::editor::tests::Require(remainingScene != nullptr && remainingScene->active, "Closing sibling tab should leave Scene active in the group");
}

void RunMaximizedLeafBuildsSingleGroupLayoutTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockLayout initialLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* sceneLayout = FindPanelLayout(initialLayout, 2U);
    kb::editor::tests::Require(sceneLayout != nullptr, "Scene panel should exist before maximizing");

    kb::editor::tests::Require(model.Commands().ToggleMaximizedLeaf(sceneLayout->leafId), "Maximizing Scene leaf should succeed");
    const kb::editor::DockLayout maximized = BuildDefaultLayout(model);
    kb::editor::tests::Require(maximized.leaves.size() == 1U, "Maximized layout should expose a single dock leaf");
    kb::editor::tests::Require(maximized.splitters.empty(), "Maximized layout should hide splitters");
    kb::editor::tests::Require(FindPanelLayout(maximized, 1U) == nullptr, "Hierarchy should not appear while center leaf is maximized");
    kb::editor::tests::Require(FindPanelLayout(maximized, 5U) == nullptr, "Project Files should not appear while center leaf is maximized");
    kb::editor::tests::Require(FindPanelLayout(maximized, 2U) != nullptr, "Scene should remain visible in its maximized leaf");

    kb::editor::tests::Require(model.Commands().ToggleMaximizedLeaf(sceneLayout->leafId), "Second maximize toggle should restore layout");
    const kb::editor::DockLayout restored = BuildDefaultLayout(model);
    kb::editor::tests::Require(restored.leaves.size() > 1U, "Restored layout should expose the original dock groups");
    kb::editor::tests::Require(FindPanelLayout(restored, 1U) != nullptr, "Hierarchy should return after restoring maximized leaf");
    kb::editor::tests::Require(FindPanelLayout(restored, 5U) != nullptr, "Project Files should return after restoring maximized leaf");
}

void RunTabCloseControlGeometryTest() {
    const kb::editor::DockRect tab{ 10, 20, 156, 28 };
    const kb::editor::DockRect close = kb::editor::DockTabControlGeometry::CloseRect(tab);
    kb::editor::tests::Require(!close.Empty(), "Tab close control should resolve for a normal tab");
    kb::editor::tests::Require(close.x + close.width <= tab.x + tab.width, "Tab close control should stay inside tab bounds");
    kb::editor::tests::Require(kb::editor::DockTabControlGeometry::ContainsClose(tab, close.x + 1, close.y + 1), "Tab close hit test should accept an interior point");
    kb::editor::tests::Require(!kb::editor::DockTabControlGeometry::ContainsClose(tab, tab.x + 4, tab.y + 4), "Tab close hit test should reject the tab title area");
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
    // The default center leaf carries Scene (2), Script Editor (8), and
    // Material Editor (10), so the docked Inspector (4) lands at tab index 3.
    kb::editor::tests::Require(preview->tabInsertionIndex == 3U, "Tab strip insertion index did not match the cursor position");
    kb::editor::tests::Require(
        preview->rect.width == 3 && preview->rect.height == inspectorLayout->tab.height,
        "Tab strip marker geometry should be a thin vertical marker");

    model.Commands().UndockPanel(5U, kb::editor::DockRect{ 80, 90, 620, 300 });
    model.Commands().DockPanelTo(5U, *preview);
    const kb::editor::DockLayout dockedLayout = BuildDefaultLayout(model);
    const std::vector<std::uint32_t> order = PanelOrderInLeaf(dockedLayout, sceneLayout->leafId);
    kb::editor::tests::Require(order.size() >= 5U, "Docked tab was not inserted into the target leaf");
    kb::editor::tests::Require(order[0] == 2U && order[1] == 8U && order[2] == 10U && order[3] == 5U && order[4] == 4U, "Docked tab was not inserted at the resolved tab strip index");
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

void RunMainToolbarTransportButtonsAreVerticallyCenteredTest() {
#if defined(_WIN32)
    const RECT toolbarRect{ 0, 24, 1280, 72 };
    const kb::editor::EditorToolbarRects toolbar = kb::editor::EditorToolbarLayout::ResolveToolbar(toolbarRect);
    const int toolbarCenterY = toolbarRect.top + ((toolbarRect.bottom - toolbarRect.top) / 2);

    const auto requireCentered = [toolbarCenterY](const RECT& button, const char* message) {
        const int buttonCenterY = button.top + ((button.bottom - button.top) / 2);
        kb::editor::tests::Require(buttonCenterY == toolbarCenterY, message);
    };

    requireCentered(toolbar.playButton, "Play transport button should be vertically centered in the toolbar");
    requireCentered(toolbar.pauseButton, "Pause transport button should be vertically centered in the toolbar");
    requireCentered(toolbar.stopButton, "Stop transport button should be vertically centered in the toolbar");
#endif
}

} // namespace

namespace kb::editor::tests {

void RunDefaultWorkspaceRegistersMaterialEditorPanelTest() {
    // KBMAT-0201: the dedicated Material Editor panel ships as part of the default workspace.
    kb::editor::EditorDockModel model;
    const kb::editor::DockPanel* panel = RequirePanel(model, 10U);
    kb::editor::tests::Require(panel->kind == kb::editor::DockPanelKind::MaterialEditor, "Default workspace should register a Material Editor panel");
    kb::editor::tests::Require(panel->title == "Material Editor", "Material Editor panel should have the expected title");

    const kb::editor::DockLayout layout = BuildDefaultLayout(model);
    const kb::editor::DockLeafLayout* materialLeaf = FindLeafForPanel(layout, 10U);
    kb::editor::tests::Require(materialLeaf != nullptr, "Material Editor panel should be placed in a dock leaf");
    const kb::editor::DockLeafLayout* sceneLeaf = FindLeafForPanel(layout, 2U);
    const kb::editor::DockLeafLayout* inspectorLeaf = FindLeafForPanel(layout, 4U);
    kb::editor::tests::Require(materialLeaf != nullptr && sceneLeaf != nullptr && materialLeaf->leafId == sceneLeaf->leafId, "Material Editor panel should share the center dock leaf with Scene View");
    kb::editor::tests::Require(inspectorLeaf != nullptr && materialLeaf->leafId != inspectorLeaf->leafId, "Material Editor panel should not be constrained to the narrow right Inspector dock");
}

void RunMaterialEditorPanelActivationTest() {
    // KBMAT-0202: the double-click router activates the Material Editor panel by kind.
    // This verifies that ActivatePanel on the registered Material Editor panel brings its
    // tab to front (the exact operation the router performs on double-click of a .kbmat).
    kb::editor::EditorDockModel model;
    const kb::editor::DockPanel* panel = RequirePanel(model, 10U);
    kb::editor::tests::Require(panel->kind == kb::editor::DockPanelKind::MaterialEditor, "Material Editor panel should be registered");

    model.Commands().ActivatePanel(10U);
    const kb::editor::DockLayout layout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* active = FindPanelLayout(layout, 10U);
    kb::editor::tests::Require(active != nullptr && active->active, "Material Editor panel did not become active after activation");
}

void RunClosedMaterialEditorReopensInCenterDockTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockLayout initialLayout = BuildDefaultLayout(model);
    const kb::editor::DockLeafLayout* sceneLeaf = FindLeafForPanel(initialLayout, 2U);
    kb::editor::tests::Require(sceneLeaf != nullptr, "Scene View leaf should exist before reopening Material Editor");

    kb::editor::tests::Require(model.Commands().ClosePanel(10U), "Closing Material Editor tab should succeed");
    const kb::editor::DockLayout closedLayout = BuildDefaultLayout(model);
    kb::editor::tests::Require(FindPanelLayout(closedLayout, 10U) == nullptr, "Closed Material Editor tab should leave the layout");

    kb::editor::tests::Require(
        model.Commands().ActivatePanelKind(kb::editor::DockPanelKind::MaterialEditor, kb::editor::DockArea::Center),
        "Double-click activation should reopen a closed Material Editor panel");
    const kb::editor::DockLayout reopenedLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* reopened = FindPanelLayout(reopenedLayout, 10U);
    const kb::editor::DockPanelLayout* scene = FindPanelLayout(reopenedLayout, 2U);
    kb::editor::tests::Require(reopened != nullptr && reopened->active, "Reopened Material Editor should be active");
    kb::editor::tests::Require(scene != nullptr && reopened->leafId == scene->leafId, "Reopened Material Editor should return to the center workspace group");
}

void RunEditorDockingTests() {
    RunTabActivationPreservesOrderTest();
    RunClosePanelRemovesTabFromLayoutTest();
    RunMaximizedLeafBuildsSingleGroupLayoutTest();
    RunTabCloseControlGeometryTest();
    RunUndockAndDockSameFrameTest();
    RunTopChromeDropPreviewTest();
    RunDockedPanelsShareEdgesWithoutVisibleGapsTest();
    RunTabStripDropInsertsAtResolvedIndexTest();
    RunSplitterAndFloatingResizeTest();
    RunFloatingWindowControlHitTest();
    RunMainToolbarTransportButtonsAreVerticallyCenteredTest();
    RunDefaultWorkspaceRegistersMaterialEditorPanelTest();
    RunMaterialEditorPanelActivationTest();
    RunClosedMaterialEditorReopensInCenterDockTest();
}

} // namespace kb::editor::tests
