#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "docking/EditorDockModel.hpp"
#include "rendering/DockTabControlGeometry.hpp"
#include "rendering/EditorToolbarLayout.hpp"
#include "rendering/SkeletalMeshEditorPanelLayout.hpp"
#include "rendering/AnimatorEditorPanelRenderer.hpp"
#include "rendering/components/CategoryHeader.hpp"
#include "rendering/components/DenseListRow.hpp"
#include "rendering/components/PropertyRow.hpp"
#include "scene/SkeletalMeshEditorTreeState.hpp"
#include "scene/SkeletalMeshEditorDetailsState.hpp"
#include "scene/SkeletalMeshEditorPanelResizeState.hpp"
#include "scene/SkeletalMeshEditorDocumentState.hpp"
#include "scene/SkeletonEditorDocumentState.hpp"
#include "scene/AnimationClipTimelineState.hpp"
#include "scene/AnimationClipEditorDocumentState.hpp"
#include "scene/AnimatorEditorGraphDocumentState.hpp"
#include "scene/EditorAutosaveState.hpp"
#include "settings/EditorConfigurationStore.hpp"
#include "windowing/FloatingWindowControlHitTester.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
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
    const std::vector<std::uint32_t> tabOrder = PanelOrderInLeaf(tabbedLayout, sceneLayout->leafId);
    const auto inspectorOrder = std::find(tabOrder.begin(), tabOrder.end(), 4U);
    kb::editor::tests::Require(inspectorOrder != tabOrder.end(), "Inspector tab is missing from its resolved leaf");
    const std::uint32_t expectedInsertionIndex = static_cast<std::uint32_t>(
        std::distance(tabOrder.begin(), inspectorOrder));

    const std::optional<kb::editor::DockDropPreview> preview =
        model.Queries().ResolveDropPreview(tabbedLayout, inspectorLayout->tab.x + 1, inspectorLayout->tab.y + 1);
    kb::editor::tests::Require(preview.has_value(), "Tab strip did not resolve a drop marker");
    kb::editor::tests::Require(preview->kind == kb::editor::DockDropPreviewKind::StripMarker, "Tab strip drop should use a strip marker preview");
    kb::editor::tests::Require(
        preview->tabInsertionIndex == expectedInsertionIndex,
        "Tab strip insertion index did not match the cursor position");
    kb::editor::tests::Require(
        preview->rect.width == 3 && preview->rect.height == inspectorLayout->tab.height,
        "Tab strip marker geometry should be a thin vertical marker");

    model.Commands().UndockPanel(5U, kb::editor::DockRect{ 80, 90, 620, 300 });
    model.Commands().DockPanelTo(5U, *preview);
    const kb::editor::DockLayout dockedLayout = BuildDefaultLayout(model);
    const std::vector<std::uint32_t> order = PanelOrderInLeaf(dockedLayout, sceneLayout->leafId);
    std::vector<std::uint32_t> expectedOrder = tabOrder;
    expectedOrder.insert(
        expectedOrder.begin() + static_cast<std::ptrdiff_t>(expectedInsertionIndex),
        5U);
    kb::editor::tests::Require(
        order == expectedOrder,
        "Docked tab was not inserted at the resolved tab strip index");
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

void RunSkeletalMeshEditorWorkspaceActivationTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockPanel* panel = RequirePanel(model, 11U);
    kb::editor::tests::Require(
        panel->kind == kb::editor::DockPanelKind::SkeletalMeshEditor &&
            panel->title == "Skeletal Mesh Editor",
        "Default workspace should register the Skeletal Mesh Editor");
    const kb::editor::DockLayout initialLayout = BuildDefaultLayout(model);
    const kb::editor::DockLeafLayout* sceneLeaf = FindLeafForPanel(initialLayout, 2U);
    const kb::editor::DockLeafLayout* skeletalMeshLeaf = FindLeafForPanel(initialLayout, 11U);
    kb::editor::tests::Require(
        sceneLeaf != nullptr && skeletalMeshLeaf != nullptr &&
            sceneLeaf->leafId == skeletalMeshLeaf->leafId,
        "Skeletal Mesh Editor should use the central workspace");
    kb::editor::tests::Require(
        model.Commands().SetPanelTitle(kb::editor::DockPanelKind::SkeletalMeshEditor, "Y Bot.kbskeletalmesh") &&
            RequirePanel(model, 11U)->title == "Y Bot.kbskeletalmesh",
        "Skeletal Mesh Editor tab should accept the opened asset filename");

    kb::editor::tests::Require(model.Commands().ClosePanel(11U),
        "Closing Skeletal Mesh Editor should succeed");
    kb::editor::tests::Require(
        model.Commands().ActivatePanelKind(
            kb::editor::DockPanelKind::SkeletalMeshEditor,
            kb::editor::DockArea::Center),
        "Reopening a Skeletal Mesh document should restore its workspace");
    const kb::editor::DockLayout reopenedLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* reopened = FindPanelLayout(reopenedLayout, 11U);
    kb::editor::tests::Require(reopened != nullptr && reopened->active,
        "Reopened Skeletal Mesh Editor should receive focus");
}

void RunAnimationClipEditorWorkspaceActivationTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockPanel* panel = RequirePanel(model, 12U);
    kb::editor::tests::Require(
        panel != nullptr && panel->kind == kb::editor::DockPanelKind::AnimationClipEditor,
        "Animation Clip Editor should be registered as a typed dock panel");
    const kb::editor::DockLayout initialLayout = BuildDefaultLayout(model);
    const kb::editor::DockLeafLayout* sceneLeaf = FindLeafForPanel(initialLayout, 2U);
    const kb::editor::DockLeafLayout* clipLeaf = FindLeafForPanel(initialLayout, 12U);
    kb::editor::tests::Require(
        sceneLeaf != nullptr && clipLeaf != nullptr && sceneLeaf->leafId == clipLeaf->leafId,
        "Animation Clip Editor should use the central workspace");
    kb::editor::tests::Require(
        model.Commands().ActivatePanelKind(kb::editor::DockPanelKind::AnimationClipEditor, kb::editor::DockArea::Center),
        "Animation Clip Editor should activate through typed dock dispatch");
    const kb::editor::DockLayout activatedLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* activated = FindPanelLayout(activatedLayout, 12U);
    kb::editor::tests::Require(activated != nullptr && activated->active,
        "Animation Clip Editor should receive focus after typed dispatch");
}

void RunAnimatorEditorWorkspaceActivationTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockPanel* panel = RequirePanel(model, 13U);
    kb::editor::tests::Require(
        panel != nullptr && panel->kind == kb::editor::DockPanelKind::AnimatorEditor,
        "Animator Editor should be registered as a typed dock panel");
    const kb::editor::DockLayout initialLayout = BuildDefaultLayout(model);
    const kb::editor::DockLeafLayout* sceneLeaf = FindLeafForPanel(initialLayout, 2U);
    const kb::editor::DockLeafLayout* animatorLeaf = FindLeafForPanel(initialLayout, 13U);
    kb::editor::tests::Require(
        sceneLeaf != nullptr && animatorLeaf != nullptr && sceneLeaf->leafId == animatorLeaf->leafId,
        "Animator Editor should use the central workspace");
    kb::editor::tests::Require(
        model.Commands().ActivatePanelKind(kb::editor::DockPanelKind::AnimatorEditor, kb::editor::DockArea::Center),
        "Animator Editor should activate through typed dock dispatch");
    const kb::editor::DockLayout activatedLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* activated = FindPanelLayout(activatedLayout, 13U);
    kb::editor::tests::Require(activated != nullptr && activated->active,
        "Animator Editor should receive focus after typed dispatch");
}

void RunAnimatorEditorDefaultLayoutTest() {
    const RECT content{ 20, 30, 1300, 730 };
    const kb::editor::AnimatorEditorPanelLayout layout =
        kb::editor::AnimatorEditorPanelRenderer::ResolveLayout(content);
    kb::editor::tests::Require(
        layout.preview.right - layout.preview.left == 320 &&
            layout.graph.right - layout.graph.left == 704 &&
            layout.details.right - layout.details.left == 256,
        "Animator Editor should keep the default 25/55/20 workspace split");
    kb::editor::tests::Require(
        layout.preview.left == content.left && layout.graph.left == layout.preview.right &&
            layout.details.left == layout.graph.right && layout.details.right == content.right,
        "Animator Editor workspace panes should be contiguous");
}

void RunAnimatorEditorGraphDocumentStateTest() {
    kb::scene::AnimatorController controller{};
    controller.layers = {{ .name = "Base", .defaultState = "Idle", .states = {
        { .id = 10U, .name = "Idle", .clipReference = "idle" },
        { .id = 11U, .name = "Walk", .clipReference = "walk" },
    }, .transitions = {{ .id = 12U, .fromState = "Idle", .toState = "Walk", .conditions = {{ .parameter = "Speed", .mode = kb::scene::AnimatorConditionMode::FloatGreater }} }} }};
    kb::editor::AnimatorEditorGraphDocumentState document;
    document.Open(controller);
    kb::editor::tests::Require(document.SetSelection({ 10U, 11U }) && document.CopySelection(),
        "Animator graph document should support multi-selection copy");
    kb::editor::tests::Require(document.PasteIntoLayer("Base", 100, 50),
        "Animator graph document should paste selected states with fresh stable ids");
    const kb::scene::AnimatorController* pasted = document.Controller();
    kb::editor::tests::Require(pasted != nullptr && pasted->layers.front().states.size() == 4U && document.Selection().size() == 2U,
        "Animator graph paste should select exactly the new state nodes");
    const std::uint64_t comment = document.AddComment("Locomotion", 0, 0, 320, 160);
    kb::editor::tests::Require(comment != 0U && document.AddGroup("Locomotion", document.Selection()) != 0U,
        "Animator graph document should persist comments and groups with stable ids");
    kb::editor::tests::Require(document.RenameState(10U, "Rest") && document.SetSelection({ 10U }) && document.DeleteSelectedStates(),
        "Animator graph document should rename and delete a state while retaining a valid layer");
    kb::editor::tests::Require(document.Dirty() && document.CanUndo() && document.Undo() && document.CanRedo() && document.Redo(),
        "Animator graph document should retain per-document undo and redo history");
    kb::editor::tests::Require(document.MarkSaved() && !document.Dirty() && !document.CanUndo() && !document.CanRedo(),
        "Animator graph document should clear its dirty marker and history after saving");
}

void RunSkeletalMeshEditorDefaultLayoutTest() {
    const RECT content{ 20, 30, 1220, 730 };
    const kb::editor::SkeletalMeshEditorPanelLayout layout =
        kb::editor::SkeletalMeshEditorPanelLayoutResolver::Resolve(content);
    kb::editor::tests::Require(
        layout.toolbox.right - layout.toolbox.left == 240 &&
            layout.viewport.right - layout.viewport.left == 560 &&
            layout.skeletonTree.right - layout.skeletonTree.left == 400,
        "Skeletal Mesh Editor should reserve enough default width for complete nested bone names");
    kb::editor::tests::Require(
        layout.documentBar.left == content.left && layout.documentBar.right == content.right &&
            layout.documentBar.top == content.top && layout.documentBar.bottom == content.top + 38 &&
            layout.commandBar.top == layout.documentBar.bottom && layout.commandBar.bottom == content.top + 74 &&
            layout.toolbox.top == layout.commandBar.bottom &&
            layout.toolbox.left == content.left && layout.viewport.left == layout.toolbox.right &&
            layout.skeletonTree.left == layout.viewport.right && layout.skeletonTree.right == content.right,
        "Skeletal Mesh Editor should place its linked-asset and command bars above the workspace without gaps");
    kb::editor::tests::Require(
        layout.skeletonTree.bottom == layout.assetDetails.top &&
            layout.skeletonTree.top == layout.commandBar.bottom && layout.assetDetails.bottom == content.bottom,
        "Skeletal Mesh Editor right column should stack Skeleton Tree over Asset Details");
    kb::editor::tests::Require(
        layout.skeletonTree.bottom - layout.skeletonTree.top == 375 &&
            layout.assetDetails.bottom - layout.assetDetails.top == 251 &&
            layout.treeDetailsSplitter.top < layout.skeletonTree.bottom &&
            layout.treeDetailsSplitter.bottom > layout.skeletonTree.bottom,
        "Skeletal Mesh Editor should expose a wide horizontal hit target at its default 3/5 Tree split");
    kb::editor::tests::Require(
        layout.meshDocument.left == content.left + 112 &&
            layout.meshDocument.right == layout.skeletonDocument.left &&
            layout.skeletonDocument.right == content.right - 8 &&
            layout.meshDocument.top == layout.skeletonDocument.top &&
            layout.meshDocument.bottom == layout.skeletonDocument.bottom,
        "Skeletal Mesh Editor linked-asset bar should expose separate Mesh and Skeleton hit targets");

    const kb::editor::SkeletalMeshEditorPanelLayout resized =
        kb::editor::SkeletalMeshEditorPanelLayoutResolver::Resolve(content, 320, 300);
    kb::editor::tests::Require(
        resized.toolbox.right - resized.toolbox.left == 320 &&
            resized.viewport.right - resized.viewport.left == 580 &&
            resized.skeletonTree.right - resized.skeletonTree.left == 300,
        "Skeletal Mesh Editor should honor independent left and right panel widths");
    kb::editor::tests::Require(
        resized.toolboxSplitter.left < resized.toolbox.right &&
            resized.toolboxSplitter.right > resized.toolbox.right &&
            resized.skeletonTreeSplitter.left < resized.skeletonTree.left &&
            resized.skeletonTreeSplitter.right > resized.skeletonTree.left,
        "Skeletal Mesh Editor splitters should expose wide hit targets around both panel edges");

    kb::editor::SkeletalMeshEditorPanelResizeState resizeState;
    resizeState.SetToolboxWidth(320);
    resizeState.SetSkeletonTreeWidth(300);
    resizeState.BeginDrag(kb::editor::SkeletalMeshEditorPanelDrag::TreeDetailsHeight);
    const int pointerHeight = kb::editor::SkeletalMeshEditorPanelLayoutResolver::
        SkeletonTreeHeightFromPointer(content, 524);
    resizeState.SetSkeletonTreeHeight(pointerHeight);
    const kb::editor::SkeletalMeshEditorPanelLayout verticallyResized =
        kb::editor::SkeletalMeshEditorPanelLayoutResolver::Resolve(
            content,
            resizeState.ToolboxWidth(),
            resizeState.SkeletonTreeWidth(),
            resizeState.SkeletonTreeHeight());
    kb::editor::tests::Require(
        resizeState.IsDragging(kb::editor::SkeletalMeshEditorPanelDrag::TreeDetailsHeight) &&
            !resizeState.IsDragging(kb::editor::SkeletalMeshEditorPanelDrag::SkeletonTreeWidth) &&
            pointerHeight == 420 &&
            verticallyResized.skeletonTree.bottom - verticallyResized.skeletonTree.top == 420 &&
            verticallyResized.assetDetails.bottom - verticallyResized.assetDetails.top == 206,
        "Skeletal Mesh Editor pointer routing should resize Tree and Details through one exclusive session drag");
    resizeState.EndDrag();
    kb::editor::tests::Require(
        !resizeState.IsDragging(kb::editor::SkeletalMeshEditorPanelDrag::TreeDetailsHeight),
        "Skeletal Mesh Editor pointer release should end the horizontal splitter capture state");

    const kb::editor::SkeletalMeshEditorPanelLayout clampedTop =
        kb::editor::SkeletalMeshEditorPanelLayoutResolver::Resolve(content, 320, 300, 1);
    const kb::editor::SkeletalMeshEditorPanelLayout clampedBottom =
        kb::editor::SkeletalMeshEditorPanelLayoutResolver::Resolve(content, 320, 300, 10000);
    kb::editor::tests::Require(
        clampedTop.skeletonTree.bottom - clampedTop.skeletonTree.top == 120 &&
            clampedBottom.assetDetails.bottom - clampedBottom.assetDetails.top == 120,
        "Skeletal Mesh Editor horizontal splitter should preserve the minimum height of both panels");

    const kb::editor::SkeletalMeshEditorPanelLayout tallerWindow =
        kb::editor::SkeletalMeshEditorPanelLayoutResolver::Resolve(
            RECT{ 20, 30, 1220, 930 }, 320, 300, pointerHeight);
    kb::editor::tests::Require(
        tallerWindow.skeletonTree.bottom - tallerWindow.skeletonTree.top == pointerHeight,
        "Skeletal Mesh Editor should retain its session splitter height after resizing the host window");

    const kb::editor::SkeletalMeshEditorPanelLayout tinyHeight =
        kb::editor::SkeletalMeshEditorPanelLayoutResolver::Resolve(
            RECT{ 0, 0, 600, 200 }, 160, 180, pointerHeight);
    kb::editor::tests::Require(
        tinyHeight.skeletonTree.bottom - tinyHeight.skeletonTree.top == 63 &&
            tinyHeight.assetDetails.bottom - tinyHeight.assetDetails.top == 63,
        "Skeletal Mesh Editor should share an undersized right stack without hiding either panel");

    const kb::editor::SkeletalMeshEditorPanelLayout constrained =
        kb::editor::SkeletalMeshEditorPanelLayoutResolver::Resolve(
            RECT{ 0, 0, 600, 500 }, 400, 400);
    kb::editor::tests::Require(
        constrained.viewport.right - constrained.viewport.left == 280,
        "Skeletal Mesh Editor panel resizing should preserve the minimum viewport width");
}

void RunClosedUtilityPanelsReopenInRightDockTest() {
    constexpr std::array utilityPanels{
        kb::editor::DockPanelKind::ProjectSettings,
        kb::editor::DockPanelKind::EditorSettings,
        kb::editor::DockPanelKind::Plugins,
    };

    kb::editor::EditorDockModel model;
    for (const kb::editor::DockPanelKind kind : utilityPanels) {
        const auto panel = std::ranges::find_if(
            model.Queries().Panels(),
            [kind](const kb::editor::DockPanel& candidate) {
                return candidate.kind == kind;
            });
        kb::editor::tests::Require(
            panel != model.Queries().Panels().end(),
            "Utility panel should be registered in the default workspace");
        const std::uint32_t panelId = panel->id;

        kb::editor::tests::Require(
            model.Commands().ClosePanel(panelId),
            "Utility panel should close before the reopen test");
        kb::editor::tests::Require(
            FindPanelLayout(BuildDefaultLayout(model), panelId) == nullptr,
            "Closed utility panel should leave the dock layout");
        kb::editor::tests::Require(
            model.Commands().ActivatePanelKind(kind, kb::editor::DockArea::Right),
            "Utility panel command should reopen a closed tab");

        const kb::editor::DockLayout reopenedLayout = BuildDefaultLayout(model);
        const kb::editor::DockPanelLayout* reopened =
            FindPanelLayout(reopenedLayout, panelId);
        const kb::editor::DockPanelLayout* inspector =
            FindPanelLayout(reopenedLayout, 4U);
        kb::editor::tests::Require(
            reopened != nullptr && reopened->active,
            "Reopened utility panel should become the active tab");
        kb::editor::tests::Require(
            inspector != nullptr && reopened->leafId == inspector->leafId,
            "Reopened utility panel should return to the right dock group");
    }
}

void RunParticleEditorWorkspaceAndSessionPersistenceTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockPanel* panel = RequirePanel(model, 14U);
    kb::editor::tests::Require(
        panel->kind == kb::editor::DockPanelKind::ParticleEditor &&
            panel->title == "21kb Particle System",
        "Panel 14 must be the typed 21kb Particle System editor panel");
    const kb::editor::DockLayout initial = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* particle = FindPanelLayout(initial, 14U);
    const kb::editor::DockPanelLayout* scene = FindPanelLayout(initial, 2U);
    kb::editor::tests::Require(
        particle != nullptr && scene != nullptr && particle->leafId == scene->leafId,
        "particle editor must start in the center document workspace");
    kb::editor::tests::Require(model.Commands().ClosePanel(14U) &&
            model.Commands().ActivatePanelKind(
                kb::editor::DockPanelKind::ParticleEditor, kb::editor::DockArea::Center),
        "particle editor close/open routing did not restore panel 14");
    model.Commands().UndockPanel(14U, {180, 160, 920, 660});
    model.Commands().MoveFloatingPanel(14U, 210, 190);
    model.Commands().ResizeFloatingPanel(14U, 940, 680);
    panel = RequirePanel(model, 14U);
    kb::editor::tests::Require(
        panel->area == kb::editor::DockArea::Floating && panel->floatingRect.x == 210 &&
            panel->floatingRect.y == 190 && panel->floatingRect.width == 940 &&
            panel->floatingRect.height == 680,
        "particle editor floating move/resize layout was not retained");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "21kb_particle_editor_host_session";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Assets", error);
    kb::editor::EditorConfiguration configuration;
    configuration.particleEditor = kb::editor::EditorPanelSession{
        .visible = true,
        .area = panel->area,
        .floatingRect = panel->floatingRect,
        .documentPath = root / "Assets" / "Open.kbvfx",
    };
    std::string saveError;
    const std::filesystem::path statePath = kb::editor::EditorConfigurationStore::FilePath(root);
    kb::editor::tests::Require(
        kb::editor::EditorConfigurationStore::Save(statePath, root, configuration, saveError),
        "editor configuration could not be saved atomically");
    const auto loaded = kb::editor::EditorConfigurationStore::Load(statePath, root);
    kb::editor::tests::Require(
        loaded.Succeeded() && loaded.found && loaded.configuration.particleEditor.visible &&
            loaded.configuration.particleEditor.area == kb::editor::DockArea::Floating &&
            loaded.configuration.particleEditor.floatingRect.width == 940 &&
            loaded.configuration.particleEditor.documentPath.lexically_normal() ==
                configuration.particleEditor.documentPath.lexically_normal(),
        "particle editor layout/session path did not survive persistence roundtrip");
    configuration.particleEditor.documentPath = root.parent_path() / "Outside.kbvfx";
    kb::editor::tests::Require(
        !kb::editor::EditorConfigurationStore::Save(statePath, root, configuration, saveError),
        "editor configuration accepted a document path outside the project");
}

void RunSkeletalMeshEditorTreeStateTest() {
    kb::scene::SkeletonAsset skeleton{};
    skeleton.bones = {
        { .id = 10U, .parentIndex = -1, .name = "Root" },
        { .id = 20U, .parentIndex = 0, .name = "Spine" },
        { .id = 30U, .parentIndex = 1, .name = "Hand" },
    };
    skeleton.sockets = {
        { .name = "Weapon", .boneId = 30U },
        { .name = "Root Socket", .boneId = 10U },
    };
    kb::editor::SkeletalMeshEditorTreeState tree;
    tree.SetSkeleton(skeleton);
    const std::vector<kb::editor::SkeletalMeshEditorTreeRow> unfilteredRows = tree.Rows();
    kb::editor::tests::Require(
        unfilteredRows.size() == 5U && unfilteredRows[0].label == "Root" &&
            unfilteredRows[1].label == "Root Socket" && unfilteredRows[1].depth == 1U &&
            unfilteredRows[3].label == "Hand" && unfilteredRows[4].label == "Weapon" &&
            unfilteredRows[4].depth == 3U,
        "Skeleton Tree should render sockets directly under their owning bones with UE-style indentation");
    kb::editor::tests::Require(
        unfilteredRows[0].hasChildren && unfilteredRows[0].expanded &&
            unfilteredRows[2].hasChildren && unfilteredRows[2].expanded &&
            unfilteredRows[3].hasChildren && unfilteredRows[3].expanded,
        "Skeleton Tree should initially expand every hierarchy branch like UE Persona");
    kb::editor::tests::Require(tree.ToggleExpanded(20U) && !tree.IsExpanded(20U),
        "Skeleton Tree disclosure should retain an independent collapsed state per bone");
    const std::vector<kb::editor::SkeletalMeshEditorTreeRow> collapsedRows = tree.Rows();
    kb::editor::tests::Require(
        collapsedRows.size() == 3U && collapsedRows.back().label == "Spine" &&
            collapsedRows.back().hasChildren && !collapsedRows.back().expanded,
        "Collapsing a hierarchy branch should hide all descendants without removing the branch row");
    kb::editor::tests::Require(tree.SelectBone(30U) && tree.SelectedBone() == 30U && tree.SelectedSocket().empty(),
        "Skeleton Tree should retain viewport-selected bones");
    kb::editor::tests::Require(tree.SelectSocket("Weapon") && tree.SelectedBone() == 0U && tree.SelectedSocket() == "Weapon",
        "Skeleton Tree should retain selected sockets independently from bones");
    kb::editor::tests::Require(tree.ToggleExpanded(20U) && !tree.IsExpanded(20U),
        "Skeleton Tree should allow a branch revealed by selection to be collapsed again");
    kb::editor::tests::Require(tree.SetFilter("hand"), "Skeleton Tree should accept a case-insensitive search filter");
    const std::vector<kb::editor::SkeletalMeshEditorTreeRow> rows = tree.Rows();
    kb::editor::tests::Require(rows.size() == 3U && rows[0].label == "Root" && rows[1].label == "Spine" && rows[2].label == "Hand",
        "Skeleton Tree filtering should retain matching bones and their hierarchy ancestors");
    kb::editor::tests::Require(!tree.IsExpanded(20U),
        "Skeleton Tree filtering should not overwrite the persistent expansion state");
    kb::editor::tests::Require(tree.SetFilter("") && tree.Rows().size() == 3U,
        "Clearing a Skeleton Tree filter should restore the remembered collapsed view");
    kb::editor::tests::Require(tree.SelectBone(30U) && tree.IsExpanded(20U) && tree.Rows().size() == 5U,
        "Viewport selection should reveal a hidden bone by expanding its ancestors");
    kb::editor::tests::Require(tree.SetFilter("weapon"), "Skeleton Tree should accept a socket filter");
    const std::vector<kb::editor::SkeletalMeshEditorTreeRow> socketRows = tree.Rows();
    kb::editor::tests::Require(
        socketRows.size() == 4U && socketRows[0].label == "Root" && socketRows[1].label == "Spine" &&
            socketRows[2].label == "Hand" && socketRows[3].label == "Weapon" && socketRows[3].depth == 3U,
        "Skeleton Tree filtering should retain the owning bone path for a matching socket");
    static_cast<void>(tree.SetFilter("hand"));
    tree.FocusSearch(true);
    tree.SelectAllSearch();
    tree.AppendSearchText(L's');
    tree.AppendSearchText(L'p');
    kb::editor::tests::Require(tree.IsSearchFocused() && tree.Filter() == "sp",
        "Skeleton Tree search should accept focused text input");
    tree.BackspaceSearch();
    kb::editor::tests::Require(tree.Filter() == "s", "Skeleton Tree search should handle backspace");
    kb::editor::tests::Require(tree.SetScrollOffset(80, 200) && tree.ScrollOffset() == 80,
        "Skeleton Tree should retain a clamped pixel scroll offset");
    tree.BeginScrollbarDrag(100);
    tree.DragScrollbar(150, 100, 200);
    kb::editor::tests::Require(tree.IsScrollbarDragging() && tree.ScrollOffset() == 180,
        "Skeleton Tree scrollbar drag should map track travel to the full row range");
    tree.EndScrollbarDrag();
    kb::editor::tests::Require(!tree.IsScrollbarDragging(),
        "Skeleton Tree scrollbar drag should terminate cleanly");
    kb::editor::tests::Require(tree.SetFilter("root") && tree.ScrollOffset() == 0,
        "Changing the Skeleton Tree filter should reveal results from the top");
}

void RunSkeletalMeshEditorDetailsStateTest() {
    kb::scene::SkeletonAsset skeleton{};
    skeleton.bones = {
        { .id = 10U, .parentIndex = -1, .name = "Root" },
        { .id = 20U, .parentIndex = 0, .name = "Child" },
    };
    skeleton.sockets = {{ .name = "Weapon", .boneId = 10U }};
    kb::scene::SkeletalMeshAsset mesh{};
    mesh.skeletonAssetId = 99U;
    mesh.skeletonCompatibilitySignature = 456U;
    mesh.lods = {{ .vertices = std::vector<kb::scene::SkeletalMeshVertex>(3U), .indices = { 0U, 1U, 2U, 0U, 1U, 2U },
        .sections = {{ .firstIndex = 0U, .indexCount = 6U, .materialAssetId = 123U, .boneMap = { 10U, 20U } }}, .requiredBones = { 10U, 20U } }};
    mesh.lods[0].vertices[0].jointIndices = { 0U, 1U, 0U, 0U };
    mesh.lods[0].vertices[0].jointWeights = { 0.25F, 0.75F, 0.0F, 0.0F };
    mesh.lods[0].vertices[1].jointIndices = { 0U, 1U, 0U, 0U };
    mesh.lods[0].vertices[1].jointWeights = { 0.5F, 0.5F, 0.0F, 0.0F };
    kb::assets::AssetMetadata metadata{};
    metadata.name = "Hero";
    metadata.virtualPath = "/Game/Hero.kbskeletalmesh";
    metadata.importCategory = "glTF";
    kb::editor::SkeletalMeshEditorDetailsState details;
    details.SetDocument(mesh, skeleton, metadata);
    const kb::editor::SkeletalMeshEditorDetailsModel asset = details.Build(0U, {});
    kb::editor::tests::Require(
        asset.sections.size() >= 8U && asset.sections[1].title == "Materials" &&
            asset.sections[1].fields.size() == 1U &&
            asset.sections[1].fields[0].action == kb::editor::SkeletalMeshEditorDetailsAction::SectionMaterial &&
            asset.sections[2].title == "LOD Picker" &&
            asset.sections[2].fields[0].action == kb::editor::SkeletalMeshEditorDetailsAction::PreviewLod &&
            asset.sections[3].title == "LOD 0" &&
            asset.sections[3].fields[6].action == kb::editor::SkeletalMeshEditorDetailsAction::LodScreenCoverage,
        "Skeletal Mesh Details should expose typed material, preview LOD and authored LOD controls");
    kb::editor::tests::Require(details.ToggleSection("Materials") &&
            !details.Build(0U, {}).sections[1].expanded,
        "Skeletal Mesh Details categories should retain an independent collapsed state");
    kb::editor::tests::Require(details.SetScrollOffset(80, 200) && details.ScrollOffset() == 80,
        "Skeletal Mesh Details should retain a clamped pixel scroll offset");
    details.BeginScrollbarDrag(100);
    details.DragScrollbar(150, 100, 200);
    kb::editor::tests::Require(details.IsScrollbarDragging() && details.ScrollOffset() == 180,
        "Skeletal Mesh Details scrollbar drag should map track travel to content range");
    details.EndScrollbarDrag();
    kb::editor::tests::Require(!details.IsScrollbarDragging(),
        "Skeletal Mesh Details scrollbar drag should terminate cleanly");
    const kb::editor::SkeletalMeshEditorDetailsModel bone = details.Build(10U, {});
    kb::editor::tests::Require(bone.title == "Bone: Root" && bone.sections[0].fields[0].value == "10" &&
            bone.sections[0].fields[5].value == "3",
        "Skeletal Mesh Details should expose selected bone data");
    const kb::editor::SkeletalMeshEditorDetailsModel child = details.Build(20U, {});
    kb::editor::tests::Require(child.sections[0].fields[5].value == "2" &&
            child.sections[0].fields[6].value == "0.625" && child.sections[0].fields[7].value == "0.75",
        "Skeletal Mesh Details should precompute per-bone vertex counts and weights without counting repeated indices");
    const kb::editor::SkeletalMeshEditorDetailsModel socket = details.Build(0U, "Weapon");
    kb::editor::tests::Require(socket.title == "Socket: Weapon" && socket.sections[0].fields[1].value == "10",
        "Skeletal Mesh Details should expose selected socket data");
    mesh.morphTargets = {{ .name = "Smile", .lodIndex = 0U, .deltas = {{ .vertexIndex = 0U }} }};
    mesh.lods[0].vertices[0].jointWeights = { 0.0F, 1.0F, 0.0F, 0.0F };
    details.SetDocument(mesh, skeleton, metadata);
    kb::editor::tests::Require(details.MorphTargets().size() == 1U && details.MorphTargets()[0].name == "Smile",
        "Skeletal Mesh editor Morph Targets panel should use the canonical mesh morph data");
    kb::editor::tests::Require(details.Build(10U, {}).sections[0].fields[5].value == "2",
        "Skeletal Mesh Details should rebuild cached bone influence statistics when the document changes");

    kb::assets::AssetMetadata skeletonMetadata{};
    skeletonMetadata.name = "Hero Skeleton";
    skeletonMetadata.virtualPath = "/Game/Hero.kbskeleton";
    details.SetSkeletonDocument(skeleton, skeletonMetadata, &metadata);
    const kb::editor::SkeletalMeshEditorDetailsModel skeletonAsset = details.Build(0U, {});
    kb::editor::tests::Require(
        skeletonAsset.title == "Skeleton" && skeletonAsset.sections.size() == 3U &&
            skeletonAsset.sections[0].fields[0].value == "Hero Skeleton" &&
            skeletonAsset.sections[0].fields[2].value == "2" &&
            skeletonAsset.sections[0].fields[5].value == "Hero.kbskeletalmesh" &&
            details.MorphTargets().empty(),
        "Skeleton document should expose rig data and identify mesh geometry as preview-only");
    kb::editor::tests::Require(details.Build(10U, {}).sections[0].fields.size() == 5U,
        "Skeleton document should not present preview-mesh skin weights as owned Skeleton data");
    details.SetSkeletonDocument(skeleton, skeletonMetadata, nullptr);
    kb::editor::tests::Require(details.Build(0U, {}).sections[0].fields[5].value == "None",
        "Skeleton document should remain valid when no compatible preview mesh exists");
}

void RunSkeletalMeshEditorDocumentStateTest() {
    kb::scene::SkeletalMeshAsset mesh{};
    mesh.skeletonAssetId = 10U;
    mesh.skeletonCompatibilitySignature = 20U;
    mesh.lods = {{ .vertices = std::vector<kb::scene::SkeletalMeshVertex>(3U), .indices = { 0U, 1U, 2U },
        .sections = {{ .firstIndex = 0U, .indexCount = 3U, .boneMap = { 1U } }}, .requiredBones = { 1U } }};
    kb::editor::SkeletalMeshEditorDocumentState document;
    document.Open(kb::assets::AssetId{ 42U }, mesh);
    kb::scene::SkeletalMeshAsset fixed = mesh;
    fixed.boundsMode = kb::scene::SkeletalMeshBoundsMode::Fixed;
    fixed.lods[0].minScreenCoverage = 0.65F;
    fixed.lods[0].sections[0].materialAssetId = 9001U;
    kb::editor::tests::Require(document.Apply(fixed) && document.Dirty() && document.CanUndo(),
        "Skeletal Mesh document should retain a dirty working-copy history");
    kb::editor::tests::Require(document.Undo() && !document.Dirty() && document.CanRedo() &&
            document.WorkingCopy() != nullptr &&
            document.WorkingCopy()->lods[0].minScreenCoverage == 0.0F &&
            document.WorkingCopy()->lods[0].sections[0].materialAssetId == 0U,
        "Skeletal Mesh document undo should atomically restore material, LOD and bounds edits");
    kb::editor::tests::Require(document.Redo() && document.MarkSaved() && !document.Dirty(),
        "Skeletal Mesh document save should establish a new clean history baseline");
    kb::editor::tests::Require(document.RevertToSaved() && !document.Dirty(),
        "Skeletal Mesh document revert should discard unsaved history");
    kb::scene::SkeletalMeshAsset reloaded = mesh;
    reloaded.boundsMode = kb::scene::SkeletalMeshBoundsMode::ImportedConservative;
    kb::editor::tests::Require(
        document.ReplaceFromReimport(reloaded) && !document.Dirty() && !document.CanUndo() && !document.CanRedo() &&
            document.WorkingCopy() != nullptr &&
            document.WorkingCopy()->boundsMode == kb::scene::SkeletalMeshBoundsMode::ImportedConservative,
        "Skeletal Mesh reload should replace the saved baseline and clear stale history");
}

void RunSkeletonEditorDocumentStateTest() {
    kb::scene::SkeletonAsset skeleton{};
    skeleton.bones = {{ .id = 10U, .parentIndex = -1, .name = "Root" }};
    kb::editor::SkeletonEditorDocumentState document;
    document.Open(kb::assets::AssetId{ 77U }, skeleton);
    kb::scene::SkeletonAsset withSocket = skeleton;
    withSocket.sockets.push_back({ .name = "Root Socket", .boneId = 10U });
    kb::editor::tests::Require(document.Apply(withSocket) && document.Dirty() && document.CanUndo() &&
            document.WorkingCopy() != nullptr && document.WorkingCopy()->sockets.size() == 1U,
        "Skeleton document should retain validated socket edits in its working-copy history");
    kb::editor::tests::Require(document.Undo() && !document.Dirty() && document.CanRedo() &&
            document.WorkingCopy() != nullptr && document.WorkingCopy()->sockets.empty(),
        "Skeleton document undo should restore the saved rig");
    kb::editor::tests::Require(document.Redo() && document.MarkSaved() && !document.Dirty(),
        "Skeleton document save should establish a clean history baseline");
    kb::scene::SkeletonAsset reloaded = skeleton;
    reloaded.sockets.push_back({ .name = "Reloaded Socket", .boneId = 10U });
    kb::editor::tests::Require(
        document.ReplaceFromReload(reloaded) && !document.Dirty() && !document.CanUndo() && !document.CanRedo() &&
            document.WorkingCopy() != nullptr && document.WorkingCopy()->sockets.size() == 1U &&
            document.WorkingCopy()->sockets.front().name == "Reloaded Socket",
        "Skeleton document reload should atomically replace the saved baseline and clear stale history");
}

void RunAnimationClipTimelineStateTest() {
    kb::scene::AnimationClip clip{};
    clip.durationSeconds = 2.0F;
    clip.skeletalTracks = {
        { .boneId = 20U, .keyframes = {{ .timeSeconds = 1.5F }, { .timeSeconds = 0.5F }} },
        { .boneId = 10U, .keyframes = {{ .timeSeconds = 1.0F }} },
    };
    clip.morphTracks = {{ .morphTarget = "Smile", .keyframes = {{ .timeSeconds = 0.25F }} }};
    clip.curves = {{ .name = "FootPlant", .keyframes = {{ .timeSeconds = 1.25F }} }};
    clip.events = {{ .timeSeconds = 1.75F, .id = 4U }, { .timeSeconds = 0.75F, .id = 2U }};
    clip.rootMotionMode = kb::scene::AnimationRootMotionMode::ExtractFromBone;
    clip.rootMotionBoneId = 20U;
    kb::editor::AnimationClipTimelineState timeline;
    timeline.SetClip(clip);
    const std::vector<kb::editor::AnimationClipTimelineTrack>& tracks = timeline.Tracks();
    const auto find = [&tracks](std::string_view label) {
        return std::find_if(tracks.begin(), tracks.end(), [label](const kb::editor::AnimationClipTimelineTrack& track) {
            return track.label == label;
        });
    };
    const auto bone = find("Bone 20");
    const auto events = find("Events");
    const auto rootMotion = find("Root Motion (Bone 20)");
    kb::editor::tests::Require(timeline.DurationSeconds() == 2.0F && bone != tracks.end() &&
            bone->keys.size() == 2U && bone->keys[0].timeSeconds == 0.5F &&
            find("Morph Smile") != tracks.end() && find("Curve FootPlant") != tracks.end(),
        "Animation Clip timeline should expose canonical bone, morph and curve tracks");
    kb::editor::tests::Require(events != tracks.end() && events->keys[0].timeSeconds == 0.75F &&
            rootMotion != tracks.end() && rootMotion->keys.size() == bone->keys.size(),
        "Animation Clip timeline should deterministically expose events and root motion keys");
    kb::editor::tests::Require(timeline.SelectBoneTrack(20U) && timeline.SelectedTrackData() != nullptr &&
            timeline.SelectedTrackData()->boneId == 20U,
        "Animation Clip timeline should retain the selected skeletal bone track");
    kb::editor::tests::Require(timeline.SetZoom(4.0F) && timeline.Pan(0.25F) &&
            std::fabs(timeline.VisibleDurationSeconds() - 0.5F) < 0.0001F &&
            std::fabs(timeline.SnapTime(1.12F, 20.0F) - 1.1F) < 0.0001F &&
            timeline.SetSnappingEnabled(false) && std::fabs(timeline.SnapTime(1.12F, 20.0F) - 1.12F) < 0.0001F,
        "Animation Clip timeline should retain zoom, pan and frame snapping state");
}

void RunAnimationClipEditorDocumentStateTest() {
    kb::scene::AnimationClip clip{};
    clip.durationSeconds = 2.0F;
    clip.looping = true;
    clip.targetSkeletonAssetId = 1U;
    clip.targetSkeletonCompatibilitySignature = 2U;
    clip.skeletalTracks = {{ .boneId = 7U, .keyframes = {{ .timeSeconds = 0.0F }} }};
    kb::editor::AnimationClipEditorDocumentState document;
    document.Open(kb::assets::AssetId{ 42U }, clip);
    document.BeginGroup();
    kb::editor::tests::Require(document.UpsertBoneKey(7U, 1.0F, {}) && document.UpsertEvent(9U, 0.5F),
        "Animation Clip document should accept valid grouped key and event edits");
    document.EndGroup();
    const kb::scene::AnimationClip* edited = document.WorkingCopy();
    kb::editor::tests::Require(edited != nullptr && document.Dirty() && document.CanUndo() &&
            edited->skeletalTracks[0].keyframes.size() == 2U && edited->events.size() == 1U,
        "Animation Clip document should retain grouped working-copy edits");
    kb::editor::tests::Require(document.Undo() && !document.Dirty() && document.Redo() &&
            !document.UpsertEvent(0U, 0.5F) && !document.UpsertEvent(10U, 2.0F),
        "Animation Clip document should undo grouped edits and reject invalid event times");
    kb::editor::tests::Require(document.MarkSaved() && !document.Dirty() && !document.CanUndo() && !document.CanRedo(),
        "Animation Clip document save should establish a clean history baseline");
}

void RunAutosaveStateTest() {
    kb::editor::EditorAutosaveState autosave;
    kb::editor::tests::Require(
        !autosave.Tick(599.0, true, true).saveRequested,
        "Autosave must not run before the ten-minute interval");
    kb::editor::tests::Require(
        autosave.Tick(1.0, true, true).saveRequested &&
            autosave.ElapsedSinceSave() == 0.0,
        "Autosave must request a dirty document save at ten minutes");

    static_cast<void>(autosave.Tick(600.0, false, true));
    kb::editor::tests::Require(
        autosave.ElapsedSinceSave() == kb::editor::EditorAutosaveState::IntervalSeconds &&
            autosave.Tick(0.1, true, true).saveRequested,
        "Autosave must defer an elapsed save while editing is temporarily ineligible");

    autosave.Complete(true, "Main.21kbscene");
    kb::editor::tests::Require(
        autosave.NotificationVisible() && autosave.NotificationSucceeded() &&
            autosave.NotificationText().find("Main.21kbscene") != std::string::npos,
        "A successful autosave must expose a named notification");
    const kb::editor::EditorAutosaveTickResult expired =
        autosave.Tick(kb::editor::EditorAutosaveState::NotificationSeconds + 0.1, true, false);
    kb::editor::tests::Require(
        expired.visualChanged && !autosave.NotificationVisible(),
        "Autosave notification must expire and request one repaint");

    autosave.Complete(false, {});
    kb::editor::tests::Require(
        autosave.NotificationVisible() && !autosave.NotificationSucceeded() &&
            autosave.NotificationText() == "Autosave failed",
        "A failed autosave must expose an explicit failure notification");
}

void RunSharedEditorRowComponentLayoutTest() {
    const RECT bounds{10, 20, 410, 44};
    const kb::editor::CategoryHeaderLayout category =
        kb::editor::CategoryHeader::Resolve(bounds, true, true, true);
    kb::editor::tests::Require(
        category.disclosure.left >= bounds.left && category.icon.left > category.disclosure.left &&
            category.title.left > category.icon.right && category.title.right < category.trailingText.left &&
            category.trailingText.right < category.trailingAction.left && category.trailingAction.right <= bounds.right,
        "CategoryHeader layout overlapped disclosure, title, count, or trailing action");

    const kb::editor::DenseListRowLayout dense =
        kb::editor::DenseListRow::Resolve(bounds, 28, 72, true);
    kb::editor::tests::Require(
        dense.icon.left == bounds.left + 28 && dense.text.left == dense.icon.right + kb::editor::DenseListRow::IconGap &&
            dense.text.right == bounds.right - 72,
        "DenseListRow layout did not preserve leading content and trailing action reservations");

    const kb::editor::PropertyRowLayout property = kb::editor::PropertyRow::Resolve(bounds);
    const int expectedSplit = bounds.left + (bounds.right - bounds.left) *
        kb::editor::PropertyRow::LabelWidthPercent / 100;
    kb::editor::tests::Require(
        property.label.left == bounds.left + kb::editor::PropertyRow::HorizontalPadding &&
            property.label.right == expectedSplit && property.value.left == expectedSplit &&
            property.value.right == bounds.right - kb::editor::PropertyRow::HorizontalPadding &&
            property.value.bottom - property.value.top == kb::editor::PropertyRow::ValueHeight,
        "PropertyRow layout did not preserve the shared 36/64 field geometry");

    const RECT narrowBounds{0, 0, 32, 12};
    const kb::editor::CategoryHeaderLayout narrowCategory =
        kb::editor::CategoryHeader::Resolve(narrowBounds, true, true, true);
    const kb::editor::DenseListRowLayout narrowDense =
        kb::editor::DenseListRow::Resolve(narrowBounds, 40, 40, true);
    const kb::editor::PropertyRowLayout narrowProperty =
        kb::editor::PropertyRow::Resolve(narrowBounds);
    const auto valid = [](const RECT& rect) noexcept {
        return rect.right >= rect.left && rect.bottom >= rect.top;
    };
    kb::editor::tests::Require(
        valid(narrowCategory.title) && valid(narrowCategory.trailingText) &&
            valid(narrowCategory.trailingAction) && valid(narrowDense.icon) &&
            valid(narrowDense.text) && valid(narrowProperty.label) && valid(narrowProperty.value),
        "Shared row layouts produced inverted rectangles in a narrow panel");
}

// A click on any tab — the active one or an inactive sibling — must hit-test as a
// dock Tab. The pointer router relies on this: a dock hit means the click is a
// layout action (switch tabs), so it must NOT clear the scene selection and blank
// the Inspector. Guards the "switching tabs deselects the object" fix.
void RunTabClickIsDockInteractionTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::DockLayout initialLayout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* sceneLayout = FindPanelLayout(initialLayout, 2U);
    kb::editor::tests::Require(sceneLayout != nullptr, "Scene panel should exist in default layout");

    // Dock the Inspector onto the Scene leaf so the leaf carries two tabs.
    model.Commands().UndockPanel(4U, kb::editor::DockRect{ 80, 90, 380, 300 });
    model.Commands().DockPanelTo(4U, kb::editor::DockDropPreview{
                                      .zone = kb::editor::DockDropZone::Center,
                                      .kind = kb::editor::DockDropPreviewKind::Glow,
                                      .leafId = sceneLayout->leafId,
                                      .rect = sceneLayout->content,
                                  });
    model.Commands().ActivatePanel(2U);

    const kb::editor::DockLayout layout = BuildDefaultLayout(model);
    const kb::editor::DockPanelLayout* scene = FindPanelLayout(layout, 2U);
    const kb::editor::DockPanelLayout* inspector = FindPanelLayout(layout, 4U);
    kb::editor::tests::Require(scene != nullptr && inspector != nullptr && scene->leafId == inspector->leafId,
        "Scene/Inspector should share a leaf with two tabs");
    kb::editor::tests::Require(scene->active && !inspector->active, "Scene tab should be active, Inspector inactive");

    const auto tabCenterHit = [&](const kb::editor::DockPanelLayout& panel) {
        return model.Queries().HitTest(layout, panel.tab.x + panel.tab.width / 2, panel.tab.y + panel.tab.height / 2);
    };
    const kb::editor::DockHit activeHit = tabCenterHit(*scene);
    const kb::editor::DockHit inactiveHit = tabCenterHit(*inspector);
    kb::editor::tests::Require(activeHit.kind == kb::editor::DockHitKind::Tab && activeHit.panelId == 2U,
        "Clicking the active tab must register as a dock Tab hit");
    kb::editor::tests::Require(inactiveHit.kind == kb::editor::DockHitKind::Tab && inactiveHit.panelId == 4U,
        "Clicking an inactive tab must register as a dock Tab hit");
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
    RunClosedUtilityPanelsReopenInRightDockTest();
    RunSkeletalMeshEditorWorkspaceActivationTest();
    RunAnimationClipEditorWorkspaceActivationTest();
    RunAnimatorEditorWorkspaceActivationTest();
    RunParticleEditorWorkspaceAndSessionPersistenceTest();
    RunAnimatorEditorDefaultLayoutTest();
    RunAnimatorEditorGraphDocumentStateTest();
    RunSkeletalMeshEditorDefaultLayoutTest();
    RunSkeletalMeshEditorTreeStateTest();
    RunSkeletalMeshEditorDetailsStateTest();
    RunSkeletalMeshEditorDocumentStateTest();
    RunSkeletonEditorDocumentStateTest();
    RunAnimationClipTimelineStateTest();
    RunAnimationClipEditorDocumentStateTest();
    RunAutosaveStateTest();
    RunSharedEditorRowComponentLayoutTest();
    RunTabClickIsDockInteractionTest();
}

} // namespace kb::editor::tests
