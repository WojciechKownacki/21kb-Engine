#include "app/pointer/EditorLeftButtonDownRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPendingTextEditCommitter.hpp"
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorPointerDragSourceResolver.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/EditorWindowToolbarPointerHandler.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/docking/EditorMainDockSplitterPointerController.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/panels/EditorPanelPointerHitContext.hpp"
#include "app/plugins/EditorPluginsPointerController.hpp"
#include "app/project_files/EditorProjectFilesDeleteConfirmOverlayController.hpp"
#include "app/project_files/EditorProjectFilesTransientUiController.hpp"
#include "app/project_settings/EditorProjectSettingsPointerController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "app/scene_viewport/EditorSceneViewportToolbarPointerController.hpp"
#include "docking/DockMainLayoutResolver.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/DockTabControlGeometry.hpp"
#include "platform/win32/EditorMaterialAssetPickerDialog.hpp"
#include "platform/win32/EditorMaterialParameterValueDialog.hpp"
#include "platform/win32/EditorMeshAssetPickerDialog.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace kb::editor {
namespace {

constexpr int kHierarchyScrollbarWidth = 12;
constexpr int kHierarchyScrollbarInset = 3;
constexpr int kHierarchyScrollbarMinThumb = 24;

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] bool LeftAltDown() noexcept {
    return (GetKeyState(VK_LMENU) & 0x8000) != 0;
}

[[nodiscard]] const DockPanelLayout* TabCloseHit(const DockLayout& layout, int x, int y) noexcept {
    for (const DockPanelLayout& panel : layout.panels) {
        if (DockTabControlGeometry::ContainsClose(panel.tab, x, y)) {
            return &panel;
        }
    }
    return nullptr;
}

[[nodiscard]] bool ResolveDirtyMaterialEditorTabClose(HWND owner, EditorSceneContext& sceneContext) {
    if (!sceneContext.HasDirtyMaterialAssetEdit()) {
        return true;
    }
    const int result = MessageBoxW(
        owner,
        L"Save changes to the open material before closing the Material Editor?\n\nYes = Save\nNo = Discard changes\nCancel = keep editing",
        L"Unsaved Material",
        MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON1 | MB_APPLMODAL);
    const kb::assets::AssetId materialId = sceneContext.MaterialEditor().OpenAssetId();
    switch (result) {
    case IDYES:
        return sceneContext.SaveMaterialEditorAsset(materialId);
    case IDNO:
        return sceneContext.RevertMaterialEditorAsset(materialId);
    case IDCANCEL:
    default:
        return false;
    }
}

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] RECT HierarchyScrollbarTrack(const RECT& hierarchyContent) noexcept {
    const RECT list = HierarchyToolbarLayout::Resolve(hierarchyContent).listContent;
    return RECT{
        .left = list.right - kHierarchyScrollbarWidth,
        .top = list.top + kHierarchyScrollbarInset,
        .right = list.right - kHierarchyScrollbarInset,
        .bottom = list.bottom - kHierarchyScrollbarInset,
    };
}

[[nodiscard]] int HierarchyContentHeight(const EditorSceneContext& sceneContext) {
    return static_cast<int>(sceneContext.HierarchyRowCount()) * kHierarchyRowHeight;
}

[[nodiscard]] int HierarchyMaxScroll(const RECT& hierarchyContent, const EditorSceneContext& sceneContext) {
    const RECT list = HierarchyToolbarLayout::Resolve(hierarchyContent).listContent;
    return std::max(0, HierarchyContentHeight(sceneContext) - RectHeight(list));
}

[[nodiscard]] RECT HierarchyScrollbarThumb(const RECT& hierarchyContent, const EditorSceneContext& sceneContext) {
    const RECT list = HierarchyToolbarLayout::Resolve(hierarchyContent).listContent;
    const RECT track = HierarchyScrollbarTrack(hierarchyContent);
    const int viewportHeight = RectHeight(list);
    const int contentHeight = HierarchyContentHeight(sceneContext);
    const int trackHeight = RectHeight(track);
    if (trackHeight <= 0 || contentHeight <= viewportHeight) {
        return {};
    }

    const int thumbHeight = std::clamp((trackHeight * viewportHeight) / std::max(1, contentHeight), kHierarchyScrollbarMinThumb, trackHeight);
    const int maxOffset = std::max(1, contentHeight - viewportHeight);
    const int travel = std::max(0, trackHeight - thumbHeight);
    const int thumbTop = track.top + (travel * std::clamp(sceneContext.HierarchyScrollOffset(), 0, maxOffset)) / maxOffset;
    return RECT{ track.left + 2, thumbTop, track.right - 2, thumbTop + thumbHeight };
}

[[nodiscard]] std::optional<std::uint32_t> MeshRendererMaterialSlotPickerForProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::MeshRendererMaterialOverridePicker:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker0:
        return 0U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker1:
        return 1U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker2:
        return 2U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker3:
        return 3U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker4:
        return 4U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker5:
        return 5U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker6:
        return 6U;
    case InspectorPropertyId::MeshRendererMaterialSlotPicker7:
        return 7U;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] kb::assets::AssetId TextureSampleAssetId(const kb::render::RenderMaterialAssetData& material, std::uint32_t nodeId) {
    const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(material.graph, nodeId);
    if (node == nullptr || node->kind != kb::render::RenderMaterialGraphNodeKind::TextureSample) {
        return {};
    }
    const std::string stableId = node->parameter.stableId.empty()
        ? "textureSample" + std::to_string(node->id)
        : node->parameter.stableId;
    for (const kb::render::RenderMaterialGraphParameterValue& value : material.graphParameterValues) {
        if (value.stableId == stableId && value.type == kb::render::RenderMaterialParameterType::Texture) {
            return kb::assets::AssetId{ value.assetId };
        }
    }
    return {};
}

} // namespace

EditorLeftButtonDownRouter::EditorLeftButtonDownRouter(
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

void EditorLeftButtonDownRouter::Handle(HWND messageWindow, int x, int y) {
    const EditorProjectFilesDeleteConfirmOverlayController deleteConfirm(messageWindow, sceneContext_);
    if (deleteConfirm.HandlePointerDown(x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    EditorPendingTextEditCommitter pendingTextEdits(sceneContext_);
    if (pendingTextEdits.CommitPendingEdits()) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (EditorWindowToolbarPointerHandler::HandleLeftButtonDown(mainWindow_, messageWindow, x, y, dockModel_, sceneContext_, sceneViewport_, playMode_, shellInteraction_, metrics_)) {
        return;
    }
    if (messageWindow == mainWindow_) {
        const DockLayout layout = DockMainLayoutResolver::Resolve(mainWindow_, dockModel_, metrics_);
        if (const DockPanelLayout* closeTab = TabCloseHit(layout, x, y); closeTab != nullptr) {
            const DockPanel* panel = dockModel_.Queries().FindPanel(closeTab->panelId);
            if (panel != nullptr && panel->kind == DockPanelKind::MaterialEditor && !ResolveDirtyMaterialEditorTabClose(messageWindow, sceneContext_)) {
                return;
            }
            if (dockModel_.Commands().ClosePanel(closeTab->panelId)) {
                if (panel != nullptr && panel->kind == DockPanelKind::MaterialEditor) {
                    sceneContext_.CloseMaterialEditorAsset();
                }
                sceneViewport_.RequestPresent();
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            }
            return;
        }
    }

    EditorMainDockSplitterPointerController mainSplitter(mainWindow_, dockModel_, dockController_, sceneContext_, sceneViewport_, metrics_);
    if (mainSplitter.HandlePointerDown(messageWindow, x, y)) {
        return;
    }

    const EditorPanelPointerHitContext panelHit =
        EditorPanelPointerHitContextResolver::Resolve(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, x, y);

    if (const std::optional<RECT> materialEditorContent = EditorPanelContentResolver::Resolve(DockPanelKind::MaterialEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        materialEditorContent.has_value() && PointInRect(*materialEditorContent, x, y)) {
        const kb::assets::AssetId materialId = sceneContext_.MaterialEditor().OpenAssetId();
        if (sceneContext_.IsMaterialGraphContextMenuOpen()) {
            const MaterialEditorGraphContextMenuHit menuHit = MaterialEditorPanelRenderer::GraphContextMenuHit(sceneContext_, x, y);
            if (menuHit.kind == MaterialEditorGraphContextMenuHitKind::Category) {
                static_cast<void>(sceneContext_.ToggleMaterialGraphContextMenuCategory(menuHit.categoryIndex));
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
            if (menuHit.kind == MaterialEditorGraphContextMenuHitKind::Command) {
                if (MaterialEditorGraphContextMenuCommandEnabled(menuHit.command, sceneContext_.SelectedMaterialGraphNodeId() != 0U)) {
                    static_cast<void>(sceneContext_.ExecuteMaterialGraphContextMenuCommand(menuHit.command));
                }
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
            static_cast<void>(sceneContext_.CloseMaterialGraphContextMenu());
        }
        const MaterialEditorPanelCommand command = MaterialEditorPanelRenderer::CommandAt(*materialEditorContent, x, y);
        if (command != MaterialEditorPanelCommand::None) {
            static_cast<void>(ExecuteMaterialEditorPanelCommand(sceneContext_, materialId, command));
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }

        const MaterialEditorPanelLayout materialLayout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
        if (MaterialEditorPanelPointInRect(materialLayout.graphCanvas, x, y)) {
            sceneContext_.AssetBrowser().ClearSelection();
            sceneContext_.ClearHierarchySelection();
            sceneContext_.FocusMaterialGraph(true);
            EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
            const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext_.MaterialEditor().WorkingCopy().has_value()
                ? sceneContext_.MaterialEditor().WorkingCopy()
                : sceneContext_.ReadMaterialDocumentAsset(materialId);
            if (material.has_value()) {
                if (sceneContext_.MaterialEditor().InfoPanelVisible()) {
                    const std::size_t debugRowCount = MaterialAssetFormatter::DebugChannelRows(material->desc, materialId.value).size();
                    if (const std::optional<MaterialEditorPanelParameterHit> parameter =
                            MaterialEditorPanelRenderer::ParameterAt(
                                *materialEditorContent,
                                sceneContext_.MaterialEditor().Parameters(),
                                debugRowCount,
                                x,
                                y)) {
                        const std::optional<std::string> value = EditorMaterialParameterValueDialog::Show(
                            mainWindow_,
                            parameter->displayName,
                            MaterialEditorPanelParameterValueText(parameter->value));
                        if (value.has_value()) {
                            static_cast<void>(sceneContext_.SetMaterialEditorGraphParameterValue(
                                materialId,
                                parameter->stableId,
                                parameter->type,
                                *value));
                        }
                        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                        return;
                    }
                }
                if (LeftAltDown()) {
                    if (const std::optional<MaterialEditorGraphLinkHit> link = MaterialEditorPanelRenderer::GraphLinkAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                        static_cast<void>(sceneContext_.DisconnectMaterialGraphLink(
                            materialId,
                            link->fromNodeId,
                            link->fromPin,
                            link->toNodeId,
                            link->toPin));
                        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                        return;
                    }
                }
                if (const std::optional<MaterialEditorGraphPinHit> pin = MaterialEditorPanelRenderer::GraphPinAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphNode(pin->nodeId));
                    const bool connectionStarted = pin->direction == MaterialEditorGraphPinDirection::Output
                        ? (sceneContext_.DetachMaterialGraphOutputPinConnection(materialId, pin->nodeId, pin->pin, x, y) ||
                            sceneContext_.BeginMaterialGraphPinConnection(materialId, pin->nodeId, pin->pin, true, x, y))
                        : sceneContext_.BeginMaterialGraphPinConnection(materialId, pin->nodeId, pin->pin, false, x, y);
                    if (connectionStarted) {
                        SetCapture(messageWindow);
                    }
                    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                    return;
                }
                if (const std::optional<std::uint32_t> textureSampleNodeId = MaterialEditorPanelRenderer::GraphTextureSampleAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphNode(*textureSampleNodeId));
                    const EditorTextureAssetPickerDialog::Result result = EditorTextureAssetPickerDialog::Show(
                        mainWindow_,
                        MakeEditorDarkTheme(),
                        sceneContext_,
                        TextureSampleAssetId(*material, *textureSampleNodeId));
                    if (result.accepted) {
                        static_cast<void>(sceneContext_.SetMaterialGraphTextureSampleAsset(materialId, *textureSampleNodeId, result.assetId));
                    }
                    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                    return;
                }
                sceneContext_.CancelMaterialGraphPinConnection();
                if (const std::optional<std::uint32_t> nodeId = MaterialEditorPanelRenderer::GraphNodeAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphNode(*nodeId));
                    if (sceneContext_.BeginMaterialGraphNodeDrag(materialId, *nodeId, x, y)) {
                        SetCapture(messageWindow);
                    }
                } else {
                    static_cast<void>(sceneContext_.ClearMaterialGraphNodeSelection());
                }
            } else {
                static_cast<void>(sceneContext_.ClearMaterialGraphNodeSelection());
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
    }

    // A click outside the Project Settings panel dismisses its open dropdown,
    // then proceeds with normal handling of wherever the click landed.
    if (!panelHit.inProjectSettingsPanel && sceneContext_.CloseProjectSettingsDropdowns()) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }

    if (!panelHit.sceneContent.has_value() && sceneContext_.CloseViewportToolbarDropdowns()) {
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (panelHit.sceneContent.has_value()) {
        EditorSceneViewportToolbarPointerController sceneToolbar(sceneContext_, sceneViewport_);
        if (sceneToolbar.HandlePointerDown(*panelHit.sceneContent, x, y)) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }

        if (EditorSceneViewportObjectInteraction::BeginGizmoDrag(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
            SetCapture(messageWindow);
            sceneContext_.AssetBrowser().FocusSelection(false);
            EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
            sceneViewport_.RequestPresent();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }

        const bool boxSelectionReady = EditorSceneViewportObjectInteraction::BeginBoxSelection(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_);
        if (EditorSceneViewportObjectInteraction::SelectAt(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
            if (boxSelectionReady) {
                SetCapture(messageWindow);
            }
            sceneContext_.AssetBrowser().FocusSelection(false);
            EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
            sceneViewport_.RequestPresent();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
    }

    EditorPointerDragSourceResolver::Resolve(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
    EditorPointerDragInteraction::CaptureIfActive(messageWindow, pointerDrag_);

    if (panelHit.hierarchyContent.has_value()) {
        const int maxOffset = HierarchyMaxScroll(*panelHit.hierarchyContent, sceneContext_);
        if (maxOffset > 0) {
            const RECT track = HierarchyScrollbarTrack(*panelHit.hierarchyContent);
            const RECT thumb = HierarchyScrollbarThumb(*panelHit.hierarchyContent, sceneContext_);
            if (PointInRect(thumb, x, y)) {
                sceneContext_.BeginHierarchyScrollbarDrag(y);
                SetCapture(messageWindow);
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
            if (PointInRect(track, x, y)) {
                const int page = std::max(kHierarchyRowHeight, RectHeight(HierarchyToolbarLayout::Resolve(*panelHit.hierarchyContent).listContent) - kHierarchyRowHeight);
                static_cast<void>(sceneContext_.SetHierarchyScrollOffset(
                    sceneContext_.HierarchyScrollOffset() + (y < thumb.top ? -page : page),
                    maxOffset));
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
        }
    }

    if (hierarchySelection_.HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        // Project Files has its own selection/focus. Clicking it must not clear
        // the current scene entity selection or hide the scene gizmo.
        if (EditorAssetBrowserPointerHandler::RequiresMouseCapture(sceneContext_)) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (panelHit.inConsolePanel && consolePointer.HandlePointerDown(*panelHit.consoleContent, x, y)) {
        sceneContext_.ClearHierarchySelection();
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
        if (sceneContext_.Console().IsDetailResizeDragging() || sceneContext_.Console().IsDetailScrollbarDragging() || sceneContext_.Console().IsListScrollbarDragging()) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (panelHit.inInspectorPanel) {
        const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(*panelHit.inspectorContent, sceneContext_, x, y);
        if (hit.section == InspectorSectionId::MeshRenderer && hit.property == InspectorPropertyId::MeshRendererMeshPicker) {
            const kb::scene::SceneEntity entity = sceneContext_.SelectedEntity();
            const kb::scene::MeshRendererComponent* renderer = sceneContext_.Scene().Components().MeshRenderers().TryGet(entity);
            if (renderer != nullptr) {
                const EditorMeshAssetPickerDialog::Result result = EditorMeshAssetPickerDialog::Show(
                    mainWindow_,
                    MakeEditorDarkTheme(),
                    sceneContext_,
                    kb::assets::AssetId{ renderer->meshAssetId });
                if (result.accepted) {
                    static_cast<void>(sceneContext_.SetMeshRendererMeshAsset(entity, result.assetId));
                    sceneViewport_.RequestPresent();
                }
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        const std::optional<std::uint32_t> materialSlotPicker = MeshRendererMaterialSlotPickerForProperty(hit.property);
        if (hit.section == InspectorSectionId::MeshRenderer
            && (hit.property == InspectorPropertyId::MeshRendererMaterialPicker || materialSlotPicker.has_value())) {
            const kb::scene::SceneEntity entity = sceneContext_.SelectedEntity();
            const kb::scene::MeshRendererComponent* renderer = sceneContext_.Scene().Components().MeshRenderers().TryGet(entity);
            if (renderer != nullptr) {
                const bool overridePicker = materialSlotPicker.has_value();
                const std::uint32_t slotIndex = materialSlotPicker.value_or(0U);
                const std::uint64_t current = overridePicker
                    ? (slotIndex < renderer->materialSlotOverrideCount ? renderer->materialSlotAssetIds[slotIndex] : 0U)
                    : renderer->materialAssetId;
                const EditorMaterialAssetPickerDialog::Result result = EditorMaterialAssetPickerDialog::Show(
                    mainWindow_,
                    MakeEditorDarkTheme(),
                    sceneContext_,
                    kb::assets::AssetId{ current });
                if (result.accepted) {
                    const bool assigned = overridePicker
                        ? sceneContext_.SetMeshRendererMaterialSlotAsset(entity, slotIndex, result.assetId)
                        : sceneContext_.SetMeshRendererMaterialAsset(entity, result.assetId);
                    if (assigned) {
                        sceneViewport_.RequestPresent();
                    }
                }
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        EditorInspectorPointerController inspectorPointer(sceneContext_);
        static_cast<void>(inspectorPointer.HandlePointerDown(*panelHit.inspectorContent, x, y));
        if (inspectorPointer.ShouldCaptureMouse()) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (panelHit.inProjectSettingsPanel) {
        EditorProjectSettingsPointerController projectSettingsPointer(sceneContext_);
        if (projectSettingsPointer.HandlePointerDown(*panelHit.projectSettingsContent, x, y, renderBackendSettings_)) {
            sceneViewport_.RequestPresent();
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (panelHit.inPluginsPanel) {
        EditorPluginsPointerController pluginsPointer(sceneContext_);
        static_cast<void>(pluginsPointer.HandlePointerDown(*panelHit.pluginsContent, x, y));
        if (sceneContext_.Plugins().IsScrollbarDragging()) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (!panelHit.inAssetPanel) {
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
    }
    if (!panelHit.inHierarchyPanel && !panelHit.inConsolePanel) {
        sceneContext_.ClearHierarchySelection();
    }
    if (dockController_.HandlePointerDown(messageWindow, x, y)) {
        sceneViewport_.RequestPresent();
    }
}

} // namespace kb::editor

#endif
