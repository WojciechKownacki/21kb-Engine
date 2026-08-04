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
#include "app/scene_viewport/EditorTerrainViewportInteraction.hpp"
#include "docking/DockMainLayoutResolver.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "inspection/TerrainMaterialLayerMenuState.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/SkeletalMeshEditorPanelRenderer.hpp"
#include "rendering/DockTabControlGeometry.hpp"
#include "platform/win32/EditorMaterialAssetPickerDialog.hpp"
#include "platform/win32/EditorAnimatorControllerAssetPickerDialog.hpp"
#include "platform/win32/EditorSkeletonAssetPickerDialog.hpp"
#include "platform/win32/EditorSkeletalMeshAssetPickerDialog.hpp"
#include "platform/win32/EditorMaterialColorPickerDialog.hpp"
#include "platform/win32/EditorMaterialParameterValueDialog.hpp"
#include "platform/win32/EditorMeshAssetPickerDialog.hpp"
#include "scene/EditorHierarchyMetrics.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <array>
#include <commdlg.h>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace kb::editor {
namespace {

constexpr int kHierarchyScrollbarWidth = 12;
constexpr int kHierarchyScrollbarInset = 3;
constexpr int kHierarchyScrollbarMinThumb = 24;

[[nodiscard]] std::optional<std::uint32_t> DeformedGeometryMaterialSlotForProperty(InspectorPropertyId property) noexcept {
    constexpr std::array<InspectorPropertyId, kb::scene::kMaxDeformedGeometryMaterialSlotOverrides> fields{ {
        InspectorPropertyId::DeformedGeometryMaterialSlot0, InspectorPropertyId::DeformedGeometryMaterialSlot1,
        InspectorPropertyId::DeformedGeometryMaterialSlot2, InspectorPropertyId::DeformedGeometryMaterialSlot3,
        InspectorPropertyId::DeformedGeometryMaterialSlot4, InspectorPropertyId::DeformedGeometryMaterialSlot5,
        InspectorPropertyId::DeformedGeometryMaterialSlot6, InspectorPropertyId::DeformedGeometryMaterialSlot7,
    } };
    constexpr std::array<InspectorPropertyId, kb::scene::kMaxDeformedGeometryMaterialSlotOverrides> pickers{ {
        InspectorPropertyId::DeformedGeometryMaterialSlotPicker0, InspectorPropertyId::DeformedGeometryMaterialSlotPicker1,
        InspectorPropertyId::DeformedGeometryMaterialSlotPicker2, InspectorPropertyId::DeformedGeometryMaterialSlotPicker3,
        InspectorPropertyId::DeformedGeometryMaterialSlotPicker4, InspectorPropertyId::DeformedGeometryMaterialSlotPicker5,
        InspectorPropertyId::DeformedGeometryMaterialSlotPicker6, InspectorPropertyId::DeformedGeometryMaterialSlotPicker7,
    } };
    for (std::uint32_t slot = 0U; slot < fields.size(); ++slot) {
        if (property == fields[slot] || property == pickers[slot]) return slot;
    }
    return std::nullopt;
}

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] bool LeftAltDown() noexcept {
    return (GetKeyState(VK_LMENU) & 0x8000) != 0;
}

[[nodiscard]] bool KeyDown(int virtualKey) noexcept {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] std::optional<kb::render::RenderMaterialGraphNodeKind> MaterialGraphShortcutNodeKind() noexcept {
    if (KeyDown(VK_CONTROL) || KeyDown(VK_MENU)) {
        return std::nullopt;
    }
    if (KeyDown('1')) {
        return kb::render::RenderMaterialGraphNodeKind::ConstantScalar;
    }
    if (KeyDown('2')) {
        return kb::render::RenderMaterialGraphNodeKind::ConstantVector2;
    }
    if (KeyDown('3')) {
        return kb::render::RenderMaterialGraphNodeKind::ConstantVector;
    }
    if (KeyDown('4')) {
        return kb::render::RenderMaterialGraphNodeKind::ConstantColor;
    }
    if (KeyDown('S')) {
        return kb::render::RenderMaterialGraphNodeKind::ParameterScalar;
    }
    if (KeyDown('V')) {
        return kb::render::RenderMaterialGraphNodeKind::ParameterVector;
    }
    if (KeyDown('T')) {
        return kb::render::RenderMaterialGraphNodeKind::TextureSample;
    }
    if (KeyDown('U')) {
        return kb::render::RenderMaterialGraphNodeKind::Uv;
    }
    return std::nullopt;
}

[[nodiscard]] POINT MaterialGraphDocumentPointFromWindow(const MaterialEditorPanelLayout& layout, const EditorSceneContext& sceneContext, int x, int y) noexcept {
    const float zoom = std::max(0.1F, sceneContext.MaterialGraphZoom());
    return POINT{
        static_cast<LONG>(static_cast<float>(x - layout.graphCanvas.left - sceneContext.MaterialGraphPanX()) / zoom),
        static_cast<LONG>(static_cast<float>(y - layout.graphCanvas.top - sceneContext.MaterialGraphPanY()) / zoom),
    };
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
    // Same contract as the window-close twin in EditorWindowLifecycleHandler: settle any graph gesture before
    // the dirty check, so the prompt describes - and a Save writes - a document the user could have confirmed.
    // The click that reaches this row normally ends a gesture on its own, but not every gesture dies at the
    // mouse-up (a wire dropped on empty canvas stays armed while its node menu is open), so do not rely on it.
    static_cast<void>(sceneContext.SettleMaterialGraphGesture());

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

[[nodiscard]] bool ResolveDirtySkeletalMeshEditorTabClose(HWND owner, EditorSceneContext& sceneContext) {
    if (!sceneContext.HasDirtySkeletalMeshEditorAssetEdit()) return true;
    const int result = MessageBoxW(
        owner,
        L"Save changes to the open Skeletal Mesh before closing the editor?\n\nYes = Save\nNo = Discard changes\nCancel = keep editing",
        L"Unsaved Skeletal Mesh",
        MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON1 | MB_APPLMODAL);
    if (result == IDYES) return sceneContext.SaveSkeletalMeshEditorAsset();
    if (result == IDNO) return sceneContext.RevertSkeletalMeshEditorAsset();
    return false;
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

[[nodiscard]] const kb::render::RenderMaterialGraphNode* TextureValueNode(
    const kb::render::RenderMaterialAssetData& material,
    const kb::render::RenderMaterialGraphNode& node) noexcept {
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture) {
        return &node;
    }
    if (node.kind != kb::render::RenderMaterialGraphNodeKind::TextureSample) {
        return nullptr;
    }
    for (const kb::render::RenderMaterialGraphLink& link : material.graph.links) {
        if (link.toNodeId != node.id || link.toPin != "texture") {
            continue;
        }
        const kb::render::RenderMaterialGraphNode* source = kb::render::FindRenderMaterialGraphNode(material.graph, link.fromNodeId);
        if (source != nullptr && source->kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture && link.fromPin == "texture") {
            return source;
        }
    }
    return &node;
}

[[nodiscard]] kb::assets::AssetId TextureGraphAssetId(const kb::render::RenderMaterialAssetData& material, std::uint32_t nodeId) {
    const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(material.graph, nodeId);
    if (node == nullptr) {
        return {};
    }
    const kb::render::RenderMaterialGraphNode* textureNode = TextureValueNode(material, *node);
    if (textureNode == nullptr) {
        return {};
    }
    const std::string stableId = !textureNode->parameter.stableId.empty()
        ? textureNode->parameter.stableId
        : (textureNode->kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture
                ? "texture" + std::to_string(textureNode->id)
                : "textureSample" + std::to_string(textureNode->id));
    for (const kb::render::RenderMaterialGraphParameterValue& value : material.graphParameterValues) {
        if (value.stableId == stableId && value.type == kb::render::RenderMaterialParameterType::Texture) {
            return kb::assets::AssetId{ value.assetId };
        }
    }
    return {};
}

[[nodiscard]] POINT ScreenPointFor(HWND window, int x, int y) noexcept {
    POINT point{ x, y };
    if (window != nullptr) {
        ClientToScreen(window, &point);
    }
    return point;
}

[[nodiscard]] std::optional<std::filesystem::path> ShowTerrainHeightmapDialog(HWND owner) {
    std::array<wchar_t, 32768U> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Heightmaps (*.png;*.raw;*.r16)\0*.png;*.raw;*.r16\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = L"Import Terrain Heightmap";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) != FALSE ? std::optional<std::filesystem::path>{ path.data() } : std::nullopt;
}

[[nodiscard]] std::optional<std::array<float, 4U>> ShowGraphColorPicker(
    HWND owner,
    const MaterialEditorParameterValue& value,
    POINT anchorScreenPoint) {
    return EditorMaterialColorPickerDialog::Show(owner, "Material Graph Color", value.numbers, &anchorScreenPoint);
}

[[nodiscard]] std::string GraphColorValueText(const MaterialEditorParameterValue& value, bool alpha) {
    std::string text = MaterialEditorPanelFloat(value.numbers[0]) + " " +
        MaterialEditorPanelFloat(value.numbers[1]) + " " +
        MaterialEditorPanelFloat(value.numbers[2]);
    if (alpha) {
        text += " " + MaterialEditorPanelFloat(value.numbers[3]);
    }
    return text;
}

[[nodiscard]] bool ApplyGraphColorWatcherHit(
    HWND owner,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId materialId,
    const MaterialEditorGraphColorWatcherHit& hit,
    POINT anchorScreenPoint) {
    MaterialEditorParameterValue value = hit.value;
    if (!hit.applyImmediately) {
        const std::optional<std::array<float, 4U>> picked = ShowGraphColorPicker(owner, hit.value, anchorScreenPoint);
        if (!picked.has_value()) {
            return false;
        }
        value.numbers = *picked;
    }

    switch (hit.target) {
    case MaterialEditorGraphColorWatcherTarget::ConstantRgb:
        return sceneContext.SetMaterialGraphConstantValue(materialId, hit.nodeId, GraphColorValueText(value, false));
    case MaterialEditorGraphColorWatcherTarget::ConstantColor:
        return sceneContext.SetMaterialGraphNodeColorPropertyValue(materialId, hit.nodeId, "constant.color", value.numbers);
    case MaterialEditorGraphColorWatcherTarget::ParameterColor:
        return sceneContext.SetMaterialEditorGraphParameterValue(
            materialId,
            hit.stableId,
            kb::render::RenderMaterialParameterType::Color,
            GraphColorValueText(value, true));
    case MaterialEditorGraphColorWatcherTarget::ColorRampStop:
        return sceneContext.SetMaterialGraphNodeColorPropertyValue(materialId, hit.nodeId, hit.propertyId, value.numbers);
    case MaterialEditorGraphColorWatcherTarget::None:
        break;
    }
    return false;
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
            if (panel != nullptr && panel->kind == DockPanelKind::SkeletalMeshEditor && !ResolveDirtySkeletalMeshEditorTabClose(messageWindow, sceneContext_)) {
                return;
            }
            if (dockModel_.Commands().ClosePanel(closeTab->panelId)) {
                if (panel != nullptr && panel->kind == DockPanelKind::MaterialEditor) {
                    sceneContext_.CloseMaterialEditorAsset();
                }
                if (panel != nullptr && panel->kind == DockPanelKind::SkeletalMeshEditor) {
                    sceneContext_.CloseSkeletalMeshEditorAsset();
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
        const kb::assets::AssetMetadata* materialMetadata = materialId.IsValid()
            ? sceneContext_.Scene().Assets().Manager().Registry().Find(materialId)
            : nullptr;
        const bool editingMaterialInstance = materialMetadata != nullptr && materialMetadata->type == "RenderMaterialInstance";
        if (sceneContext_.IsMaterialGraphTexturePickerOpen()) {
            const MaterialEditorGraphTexturePickerHit pickerHit =
                MaterialEditorPanelRenderer::GraphTexturePickerHit(*materialEditorContent, sceneContext_, x, y);
            const auto acceptTexturePickerSelection = [&]() {
                const kb::assets::AssetId textureId = sceneContext_.MaterialGraphTexturePickerSelectedAssetId();
                static_cast<void>(sceneContext_.SetMaterialGraphTextureSampleAsset(
                    sceneContext_.MaterialGraphTexturePickerAssetId(),
                    sceneContext_.MaterialGraphTexturePickerNodeId(),
                    textureId));
                static_cast<void>(sceneContext_.CloseMaterialGraphTexturePicker());
            };
            switch (pickerHit.kind) {
            case MaterialEditorGraphTexturePickerHitKind::Search:
                break;
            case MaterialEditorGraphTexturePickerHitKind::Texture:
                if (sceneContext_.MaterialGraphTexturePickerSelectedAssetId() == pickerHit.assetId) {
                    acceptTexturePickerSelection();
                } else {
                    static_cast<void>(sceneContext_.SetMaterialGraphTexturePickerSelected(pickerHit.assetId));
                }
                break;
            case MaterialEditorGraphTexturePickerHitKind::Clear:
                static_cast<void>(sceneContext_.SetMaterialGraphTexturePickerSelected({}));
                break;
            case MaterialEditorGraphTexturePickerHitKind::Accept:
                acceptTexturePickerSelection();
                break;
            case MaterialEditorGraphTexturePickerHitKind::Cancel:
            case MaterialEditorGraphTexturePickerHitKind::Backdrop:
                static_cast<void>(sceneContext_.CloseMaterialGraphTexturePicker());
                break;
            case MaterialEditorGraphTexturePickerHitKind::None:
                break;
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (sceneContext_.IsMaterialGraphContextMenuOpen()) {
            const MaterialEditorGraphContextMenuHit menuHit = MaterialEditorPanelRenderer::GraphContextMenuHit(sceneContext_, x, y);
            if (menuHit.kind == MaterialEditorGraphContextMenuHitKind::Search) {
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
            if (menuHit.kind == MaterialEditorGraphContextMenuHitKind::Category) {
                static_cast<void>(sceneContext_.ToggleMaterialGraphContextMenuCategory(menuHit.categoryIndex));
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
            if (menuHit.kind == MaterialEditorGraphContextMenuHitKind::FavoriteToggle) {
                static_cast<void>(sceneContext_.ToggleMaterialGraphPaletteFavorite(menuHit.command));
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
            if (menuHit.kind == MaterialEditorGraphContextMenuHitKind::Command) {
                if (editingMaterialInstance) {
                    static_cast<void>(sceneContext_.CloseMaterialGraphContextMenu());
                    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                    return;
                }
                const MaterialEditorPanelLayout menuLayout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
                sceneContext_.SetMaterialGraphCanvasViewport(
                    menuLayout.graphCanvas.left,
                    menuLayout.graphCanvas.top,
                    MaterialEditorPanelRectWidth(menuLayout.graphCanvas),
                    MaterialEditorPanelRectHeight(menuLayout.graphCanvas));
                const bool commandEnabled = MaterialEditorGraphContextMenuCommandEnabled(
                    menuHit.command,
                    sceneContext_.SelectedMaterialGraphNodeIds().size(),
                    sceneContext_.SelectedMaterialGraphCommentId() != 0U);
                // The deferred pointer-drag path exists only for the "drag a palette command onto the canvas
                // to drop it there" gesture. When the menu was opened by dropping a wire (pin-filtered), there
                // is no drop-placement to choose - the node goes at the wire's drop point and auto-connects -
                // so execute it immediately on press. Deferring to mouse-up left the pick at the mercy of the
                // pin-connection release branch (which reopened the menu) and of any micro-movement flipping
                // the drag to "active" (which skipped the mouse-up executor): both made "pick a node -> nothing
                // appears". Immediate execution here removes that whole fragile path for the wire-drop case.
                const bool takesDeferredPath = commandEnabled &&
                    MaterialEditorGraphMenuCommandCreatesCanvasObject(menuHit.command) &&
                    !sceneContext_.IsMaterialGraphContextMenuPinFiltered();
                if (takesDeferredPath) {
                    pointerDrag_.Clear();
                    pointerDrag_.kind = EditorPointerDragKind::MaterialGraphPaletteCommand;
                    pointerDrag_.materialGraphAssetId = materialId;
                    pointerDrag_.materialGraphCommand = menuHit.command;
                    pointerDrag_.assetLabel = std::string{ MaterialEditorGraphContextMenuCommandName(menuHit.command) };
                    pointerDrag_.startX = x;
                    pointerDrag_.startY = y;
                    pointerDrag_.x = x;
                    pointerDrag_.y = y;
                    SetCapture(messageWindow);
                } else if (commandEnabled) {
                    static_cast<void>(sceneContext_.ExecuteMaterialGraphContextMenuCommand(menuHit.command));
                }
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
            const bool hadPendingPinConnection = sceneContext_.HasMaterialGraphPinConnection();
            static_cast<void>(sceneContext_.CloseMaterialGraphContextMenu());
            if (hadPendingPinConnection) {
                // Dismissing the "what do you want to connect?" menu leaves the wire unplugged rather than
                // silently restoring the link the drag pulled off.
                static_cast<void>(sceneContext_.AbandonMaterialGraphPinConnection());
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        const MaterialEditorPanelCommand command = MaterialEditorPanelRenderer::CommandAt(*materialEditorContent, x, y);
        if (command != MaterialEditorPanelCommand::None) {
            static_cast<void>(ExecuteMaterialEditorPanelCommand(sceneContext_, materialId, command));
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }

        const MaterialEditorPanelLayout materialLayout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
        const MaterialEditorOpaqueOverlayHit opaqueOverlay =
            MaterialEditorPanelRenderer::OpaqueOverlayAt(*materialEditorContent, sceneContext_, x, y);
        if (opaqueOverlay.kind == MaterialEditorOpaqueOverlayKind::Diagnostics) {
            // A diagnostic tied to a node jumps to it; otherwise the click is swallowed by the overlay.
            if (const std::uint32_t nodeId =
                    MaterialEditorPanelRenderer::DiagnosticsRowNodeAt(*materialEditorContent, sceneContext_, x, y);
                nodeId != 0U) {
                static_cast<void>(sceneContext_.FocusMaterialGraphNode(nodeId));
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (opaqueOverlay.kind == MaterialEditorOpaqueOverlayKind::Preview) {
            // Drag the preview to orbit the object around it, Unreal-style. Capture so the drag continues
            // even as the cursor leaves the small overlay.
            static_cast<void>(sceneContext_.BeginMaterialPreviewOrbit(x, y));
            SetCapture(messageWindow);
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (opaqueOverlay.kind != MaterialEditorOpaqueOverlayKind::None &&
            opaqueOverlay.kind != MaterialEditorOpaqueOverlayKind::Details) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (MaterialEditorPanelPointInRect(materialLayout.graphCanvas, x, y)) {
            sceneContext_.AssetBrowser().ClearSelection();
            sceneContext_.FocusMaterialGraph(true);
            EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
            const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext_.MaterialEditor().WorkingCopy().has_value()
                ? sceneContext_.MaterialEditor().WorkingCopy()
                : sceneContext_.ReadMaterialDocumentAsset(materialId);
            if (material.has_value()) {
                if (sceneContext_.MaterialEditor().InfoPanelVisible()) {
                    const MaterialEditorPanelDetailsRows detailsRows =
                        MaterialEditorPanelRenderer::DetailsRowsForDocument(
                            sceneContext_,
                            *material,
                            editingMaterialInstance);
                    const MaterialEditorDetailsLayout detailsLayout =
                        MaterialEditorPanelRenderer::ResolveDetailsLayout(
                            *materialEditorContent,
                            detailsRows,
                            sceneContext_.MaterialEditorDetailsScrollOffset());
                    const MaterialEditorDetailsHit detailsHit =
                        MaterialEditorPanelRenderer::DetailsHitAt(detailsLayout, detailsRows, x, y);
                    if (detailsHit.kind == MaterialEditorDetailsHitKind::Search) {
                        sceneContext_.FocusMaterialEditorFind(true);
                        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                        return;
                    }
                    if (detailsHit.kind == MaterialEditorDetailsHitKind::FindResult) {
                        static_cast<void>(sceneContext_.FocusMaterialEditorFindResult(
                            detailsHit.resultIndex,
                            MaterialEditorPanelRectWidth(materialLayout.graphCanvas),
                            MaterialEditorPanelRectHeight(materialLayout.graphCanvas)));
                        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                        return;
                    }
                    if (detailsHit.kind == MaterialEditorDetailsHitKind::NodeProperty) {
                        const MaterialEditorGraphNodePropertyHit& property = detailsHit.nodeProperty;
                        // Node id 0 means the material-settings rows, which are not a node; only sync the
                        // selection for a real node.
                        if (property.nodeId != 0U) {
                            static_cast<void>(sceneContext_.SelectMaterialGraphNode(property.nodeId));
                        }
                        switch (property.kind) {
                        case MaterialEditorGraphNodePropertyHitKind::TextField:
                            sceneContext_.CloseMaterialGraphNodeEnumDropdown();
                            if (property.stableId == "node.name") {
                                static_cast<void>(sceneContext_.BeginMaterialGraphNodeRenameEdit(materialId, property.nodeId));
                            } else {
                                const std::optional<std::string> value = EditorMaterialParameterValueDialog::Show(
                                    messageWindow,
                                    property.displayName,
                                    MaterialEditorPanelParameterValueText(property.value));
                                if (value.has_value()) {
                                    static_cast<void>(sceneContext_.SetMaterialGraphNodeTextProperty(
                                        materialId,
                                        property.nodeId,
                                        property.stableId,
                                        *value));
                                }
                            }
                            break;
                        case MaterialEditorGraphNodePropertyHitKind::ColorPicker:
                            if (const std::optional<std::array<float, 4U>> color = ShowGraphColorPicker(messageWindow, property.value, ScreenPointFor(messageWindow, x, y))) {
                                static_cast<void>(sceneContext_.SetMaterialGraphNodeColorPropertyValue(
                                    materialId,
                                    property.nodeId,
                                    property.stableId,
                                    *color));
                            }
                            break;
                        case MaterialEditorGraphNodePropertyHitKind::Slider:
                            sceneContext_.CloseMaterialGraphNodeEnumDropdown();
                            static_cast<void>(sceneContext_.BeginMaterialGraphConstantInlineEdit(materialId, property.nodeId));
                            break;
                        case MaterialEditorGraphNodePropertyHitKind::EnumField:
                            // Node id 0 is the material itself (domain / shading model / blend mode), which
                            // uses its own dropdown-state slot; a real node uses the node-keyed one.
                            if (property.nodeId == 0U) {
                                sceneContext_.ToggleMaterialGraphSettingDropdown(property.stableId);
                            } else {
                                sceneContext_.ToggleMaterialGraphNodeEnumDropdown(property.nodeId, property.stableId);
                            }
                            break;
                        case MaterialEditorGraphNodePropertyHitKind::EnumOption:
                            if (property.nodeId == 0U) {
                                static_cast<void>(sceneContext_.SetMaterialGraphSetting(materialId, property.stableId, property.optionValue));
                            } else {
                                static_cast<void>(sceneContext_.SetMaterialGraphNodeEnumValue(materialId, property.nodeId, property.stableId, property.optionValue));
                            }
                            break;
                        case MaterialEditorGraphNodePropertyHitKind::TexturePicker:
                            sceneContext_.CloseMaterialGraphNodeEnumDropdown();
                            static_cast<void>(sceneContext_.OpenMaterialGraphTexturePicker(
                                materialId,
                                property.nodeId,
                                TextureGraphAssetId(*material, property.nodeId)));
                            break;
                        case MaterialEditorGraphNodePropertyHitKind::None:
                            break;
                        }
                        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                        return;
                    }
                    if (detailsHit.kind == MaterialEditorDetailsHitKind::Parameter) {
                        const MaterialEditorPanelParameterHit& parameter = detailsHit.parameter;
                        const std::optional<std::string> value = EditorMaterialParameterValueDialog::Show(
                            messageWindow,
                            parameter.displayName,
                            MaterialEditorPanelParameterValueText(parameter.value));
                        if (value.has_value()) {
                            if (editingMaterialInstance) {
                                static_cast<void>(sceneContext_.SetMaterialInstanceEditorGraphParameterValue(
                                    materialId,
                                    parameter.stableId,
                                    parameter.type,
                                    *value));
                            } else {
                                static_cast<void>(sceneContext_.SetMaterialEditorGraphParameterValue(
                                    materialId,
                                    parameter.stableId,
                                    parameter.type,
                                    *value));
                            }
                        }
                        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                        return;
                    }
                    if (detailsHit.kind == MaterialEditorDetailsHitKind::TextureParameter) {
                        const MaterialEditorPanelParameterHit& parameter = detailsHit.parameter;
                        const EditorTextureAssetPickerDialog::Result result = EditorTextureAssetPickerDialog::Show(
                            messageWindow,
                            MakeEditorDarkTheme(),
                            sceneContext_,
                            kb::assets::AssetId{ parameter.value.assetId },
                            EditorTextureAssetPickerFilter::Texture2D);
                        if (result.accepted) {
                            if (editingMaterialInstance) {
                                static_cast<void>(sceneContext_.SetMaterialInstanceEditorTextureParameterValue(
                                    materialId,
                                    parameter.stableId,
                                    result.assetId));
                            } else {
                                const auto materialSlot = [&]() -> std::optional<EditorMaterialTextureSlot> {
                                    if (parameter.stableId == "albedoTextureAssetId") return EditorMaterialTextureSlot::Albedo;
                                    if (parameter.stableId == "normalTextureAssetId") return EditorMaterialTextureSlot::Normal;
                                    if (parameter.stableId == "metallicRoughnessTextureAssetId") return EditorMaterialTextureSlot::MetallicRoughness;
                                    if (parameter.stableId == "occlusionTextureAssetId") return EditorMaterialTextureSlot::Occlusion;
                                    if (parameter.stableId == "emissiveTextureAssetId") return EditorMaterialTextureSlot::Emissive;
                                    return std::nullopt;
                                }();
                                if (materialSlot.has_value()) {
                                    static_cast<void>(sceneContext_.SetMaterialTextureAsset(materialId, *materialSlot, result.assetId));
                                }
                            }
                        }
                        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                        return;
                    }
                    if (detailsHit.kind == MaterialEditorDetailsHitKind::Backdrop) {
                        sceneContext_.FocusMaterialEditorFind(false);
                        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                        return;
                    }
                }
                if (editingMaterialInstance) {
                    static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
                    if (const std::optional<std::uint32_t> nodeId = MaterialEditorPanelRenderer::GraphNodeAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                        static_cast<void>(sceneContext_.SelectMaterialGraphNode(*nodeId));
                    } else if (const std::optional<std::uint32_t> commentId = MaterialEditorPanelRenderer::GraphCommentAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                        static_cast<void>(sceneContext_.SelectMaterialGraphComment(*commentId));
                    } else {
                        static_cast<void>(sceneContext_.ClearMaterialGraphNodeSelection());
                        static_cast<void>(sceneContext_.ClearMaterialGraphCommentSelection());
                    }
                    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                    return;
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
                if (const std::optional<std::uint32_t> textureSampleNodeId = MaterialEditorPanelRenderer::GraphTextureSampleAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphNode(*textureSampleNodeId));
                    static_cast<void>(sceneContext_.OpenMaterialGraphTexturePicker(
                        materialId,
                        *textureSampleNodeId,
                        TextureGraphAssetId(*material, *textureSampleNodeId)));
                    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                    return;
                }
                if (const std::optional<MaterialEditorGraphColorWatcherHit> colorWatcher =
                        MaterialEditorPanelRenderer::GraphColorWatcherAt(*materialEditorContent, *material, sceneContext_, materialId, x, y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphNode(colorWatcher->nodeId));
                    static_cast<void>(ApplyGraphColorWatcherHit(messageWindow, sceneContext_, materialId, *colorWatcher, ScreenPointFor(messageWindow, x, y)));
                    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                    return;
                }
                if (const std::optional<MaterialEditorGraphConstantValueHit> constant = MaterialEditorPanelRenderer::GraphConstantValueAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphNode(constant->nodeId));
                    static_cast<void>(sceneContext_.BeginMaterialGraphConstantInlineEdit(materialId, constant->nodeId));
                    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                    return;
                }
                if (const std::optional<MaterialEditorGraphPinHit> pin = MaterialEditorPanelRenderer::GraphPinAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    // Unplugging a wire is not a selection gesture: selecting the node that owns the input
                    // pin would leave e.g. Material Output selected just because a link was pulled off it.
                    const bool detached = pin->direction == MaterialEditorGraphPinDirection::Input &&
                        sceneContext_.DetachMaterialGraphInputPinConnection(materialId, pin->nodeId, pin->pin, x, y);
                    if (detached) {
                        static_cast<void>(sceneContext_.ClearMaterialGraphNodeSelection());
                    } else {
                        static_cast<void>(sceneContext_.SelectMaterialGraphNode(pin->nodeId));
                    }
                    const bool connectionStarted = detached ||
                        (pin->direction == MaterialEditorGraphPinDirection::Output
                            ? sceneContext_.BeginMaterialGraphPinConnection(materialId, pin->nodeId, pin->pin, true, x, y)
                            : sceneContext_.BeginMaterialGraphPinConnection(materialId, pin->nodeId, pin->pin, false, x, y));
                    if (connectionStarted) {
                        SetCapture(messageWindow);
                    }
                    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                    return;
                }
                static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
                if (const std::optional<std::uint32_t> nodeId = MaterialEditorPanelRenderer::GraphNodeAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    const bool ctrl = KeyDown(VK_CONTROL);
                    const bool shift = KeyDown(VK_SHIFT);
                    if (ctrl) {
                        static_cast<void>(sceneContext_.SelectMaterialGraphNode(*nodeId, false, true));
                    } else if (shift) {
                        static_cast<void>(sceneContext_.SelectMaterialGraphNode(*nodeId, true, false));
                    } else if (!sceneContext_.IsMaterialGraphNodeSelected(*nodeId)) {
                        static_cast<void>(sceneContext_.SelectMaterialGraphNode(*nodeId));
                    }
                    if (sceneContext_.IsMaterialGraphNodeSelected(*nodeId) && sceneContext_.BeginMaterialGraphNodeDrag(materialId, *nodeId, x, y)) {
                        SetCapture(messageWindow);
                    }
                } else if (const std::optional<std::uint32_t> commentId = MaterialEditorPanelRenderer::GraphCommentAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    if (!sceneContext_.IsMaterialGraphCommentSelected(*commentId)) {
                        static_cast<void>(sceneContext_.SelectMaterialGraphComment(*commentId));
                    }
                    if (sceneContext_.BeginMaterialGraphCommentDrag(materialId, *commentId, x, y)) {
                        SetCapture(messageWindow);
                    }
                } else if (const std::optional<kb::render::RenderMaterialGraphNodeKind> shortcutKind = MaterialGraphShortcutNodeKind()) {
                    const POINT graphPoint = MaterialGraphDocumentPointFromWindow(materialLayout, sceneContext_, x, y);
                    static_cast<void>(sceneContext_.AddMaterialGraphNode(materialId, *shortcutKind, graphPoint.x, graphPoint.y));
                } else {
                    static_cast<void>(sceneContext_.ClearMaterialGraphCommentSelection());
                    const MaterialGraphSelectionOperation selectionOperation =
                        ResolveMaterialGraphSelectionOperation(
                            KeyDown(VK_MENU),
                            KeyDown(VK_CONTROL),
                            KeyDown(VK_SHIFT));
                    // Clicking empty canvas drops the selection right away instead of waiting for the box
                    // selection to end; the modifier operations keep it, because they extend it.
                    if (selectionOperation == MaterialGraphSelectionOperation::Replace) {
                        static_cast<void>(sceneContext_.ClearMaterialGraphNodeSelection());
                    }
                    if (sceneContext_.BeginMaterialGraphBoxSelection(materialId, x, y, selectionOperation)) {
                        SetCapture(messageWindow);
                    }
                }
            } else {
                static_cast<void>(sceneContext_.ClearMaterialGraphNodeSelection());
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
    }

    if (const std::optional<RECT> skeletalMeshEditorContent = EditorPanelContentResolver::Resolve(
            DockPanelKind::SkeletalMeshEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        skeletalMeshEditorContent.has_value() && PointInRect(*skeletalMeshEditorContent, x, y) &&
        sceneContext_.HasSkeletalMeshEditorAsset()) {
        if (const std::optional<std::uint8_t> overlay = SkeletalMeshEditorPanelRenderer::AdvancedPreviewOverlayAt(
                *skeletalMeshEditorContent, x, y);
            overlay.has_value()) {
            AnimationPreviewOverlayState& overlays = sceneContext_.AnimationPreview().Overlays();
            switch (*overlay) {
            case 0U: static_cast<void>(overlays.SetBonesVisible(!overlays.BonesVisible())); break;
            case 1U: static_cast<void>(overlays.SetBoneNamesVisible(!overlays.BoneNamesVisible())); break;
            case 2U: static_cast<void>(overlays.SetSocketsVisible(!overlays.SocketsVisible())); break;
            case 3U: static_cast<void>(overlays.SetBoundsVisible(!overlays.BoundsVisible())); break;
            case 4U: static_cast<void>(overlays.SetLodVisible(!overlays.LodVisible())); break;
            case 5U: static_cast<void>(overlays.SetNormalsVisible(!overlays.NormalsVisible())); break;
            default: break;
            }
        } else if (SkeletalMeshEditorPanelRenderer::IsTreeSearchAt(*skeletalMeshEditorContent, x, y)) {
            sceneContext_.FocusSkeletalMeshEditorTreeSearch(true);
        } else if (const std::optional<SkeletalMeshEditorTreeRow> row =
                SkeletalMeshEditorPanelRenderer::TreeRowAt(*skeletalMeshEditorContent, sceneContext_, x, y);
            row.has_value()) {
            sceneContext_.FocusSkeletalMeshEditorTreeSearch(false);
            if (row->kind == SkeletalMeshEditorTreeItemKind::Bone) {
                static_cast<void>(sceneContext_.SelectSkeletalMeshEditorBone(row->boneId));
            } else {
                static_cast<void>(sceneContext_.SelectSkeletalMeshEditorSocket(row->socketName));
            }
        } else if (const std::optional<kb::scene::SkeletonBoneId> bone =
                       SkeletalMeshEditorPanelRenderer::BoneAt(*skeletalMeshEditorContent, sceneContext_, x, y);
                   bone.has_value()) {
            sceneContext_.FocusSkeletalMeshEditorTreeSearch(false);
            static_cast<void>(sceneContext_.SelectSkeletalMeshEditorBone(*bone));
        } else {
            sceneContext_.FocusSkeletalMeshEditorTreeSearch(false);
            static_cast<void>(sceneContext_.ClearSkeletalMeshEditorTreeSelection());
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
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

        if (EditorTerrainViewportInteraction::SelectAt(
                messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
            sceneContext_.AssetBrowser().FocusSelection(false);
            EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
            sceneViewport_.RequestPresent();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }

        if (EditorTerrainViewportInteraction::Stamp(
                messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, true)) {
            SetCapture(messageWindow);
            sceneViewport_.RequestPresent();
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
        // The Console is an output/inspection panel, not a scene-selection
        // surface: clicking it (Clear, Copy Line, Save Log, filter toggles, a log
        // row) must NOT clear the current scene entity selection, so the Inspector
        // keeps showing the selected object. This mirrors the Project Files panel
        // above, and the "clicked nowhere" fallback below already excludes it.
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
        if (sceneContext_.Console().IsDetailResizeDragging() || sceneContext_.Console().IsDetailScrollbarDragging() || sceneContext_.Console().IsListScrollbarDragging()) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (!panelHit.inInspectorPanel) {
        terrain_material_layer_menu::Close();
    }
    if (panelHit.inInspectorPanel) {
        const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(*panelHit.inspectorContent, sceneContext_, x, y);
        if (hit.section == InspectorSectionId::Terrain && hit.property == InspectorPropertyId::TerrainImportHeightmap) {
            if (const std::optional<std::filesystem::path> path = ShowTerrainHeightmapDialog(mainWindow_)) {
                std::string error;
                if (sceneContext_.ImportTerrainHeightmap(
                        sceneContext_.SelectedEntity(), *path,
                        EditorTerrainService::ToolState().heightmapImport,
                        &error)) {
                    sceneContext_.Console().Info("Terrain", "Imported heightmap: " + path->filename().string());
                    sceneViewport_.RequestPresent();
                } else {
                    sceneContext_.Console().Warning("Terrain", error.empty() ? "Heightmap import failed." : error);
                }
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (hit.section == InspectorSectionId::Animator &&
            (hit.property == InspectorPropertyId::AnimatorController ||
             hit.property == InspectorPropertyId::AnimatorControllerPicker)) {
            const kb::scene::SceneEntity entity = sceneContext_.SelectedEntity();
            const kb::scene::Animator* animator = sceneContext_.Scene().Components().Animators().TryGet(entity);
            if (animator != nullptr) {
                const EditorAnimatorControllerAssetPickerDialog::Result result =
                    EditorAnimatorControllerAssetPickerDialog::Show(
                        mainWindow_,
                        MakeEditorDarkTheme(),
                        sceneContext_,
                        kb::assets::AssetId{ animator->controllerAssetId });
                if (result.accepted && sceneContext_.SetAnimatorControllerAsset(entity, result.assetId)) {
                    sceneViewport_.RequestPresent();
                }
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (hit.section == InspectorSectionId::SkeletonBinding &&
            (hit.property == InspectorPropertyId::SkeletonBindingAsset ||
             hit.property == InspectorPropertyId::SkeletonBindingAssetPicker)) {
            const kb::scene::SceneEntity entity = sceneContext_.SelectedEntity();
            const kb::scene::SkeletonBindingComponent* binding = sceneContext_.Scene().Components().SkeletonBindings().TryGet(entity);
            if (binding != nullptr) {
                const EditorSkeletonAssetPickerDialog::Result result = EditorSkeletonAssetPickerDialog::Show(
                    mainWindow_, MakeEditorDarkTheme(), sceneContext_, kb::assets::AssetId{ binding->skeletonAssetId });
                if (result.accepted && sceneContext_.SetSkeletonBindingAsset(entity, result.assetId)) {
                    sceneViewport_.RequestPresent();
                }
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (hit.section == InspectorSectionId::DeformedGeometry &&
            (hit.property == InspectorPropertyId::DeformedGeometryMesh ||
             hit.property == InspectorPropertyId::DeformedGeometryMeshPicker)) {
            const kb::scene::SceneEntity entity = sceneContext_.SelectedEntity();
            const kb::scene::DrawD3DeformedGeometryComponent* geometry = sceneContext_.Scene().Components().DeformedGeometries().TryGet(entity);
            if (geometry != nullptr) {
                const EditorSkeletalMeshAssetPickerDialog::Result result = EditorSkeletalMeshAssetPickerDialog::Show(
                    mainWindow_, MakeEditorDarkTheme(), sceneContext_, kb::assets::AssetId{ geometry->skeletalMeshAssetId });
                if (result.accepted && sceneContext_.SetDeformedGeometryMeshAsset(entity, result.assetId)) {
                    sceneViewport_.RequestPresent();
                }
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (hit.section == InspectorSectionId::DeformedGeometry) {
            if (const std::optional<std::uint32_t> slot = DeformedGeometryMaterialSlotForProperty(hit.property)) {
                const kb::scene::SceneEntity entity = sceneContext_.SelectedEntity();
                const kb::scene::DrawD3DeformedGeometryComponent* geometry = sceneContext_.Scene().Components().DeformedGeometries().TryGet(entity);
                if (geometry != nullptr) {
                    const std::uint64_t current = *slot < geometry->materialSlotOverrideCount
                        ? geometry->materialSlotAssetIds[*slot]
                        : 0U;
                    const EditorMaterialAssetPickerDialog::Result result = EditorMaterialAssetPickerDialog::Show(
                        mainWindow_, MakeEditorDarkTheme(), sceneContext_, sceneViewport_, kb::assets::AssetId{ current }, true);
                    if (result.accepted && sceneContext_.SetDeformedGeometryMaterialSlotAsset(entity, *slot, result.assetId)) {
                        sceneViewport_.RequestPresent();
                    }
                }
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
        }
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
        if (hit.section == InspectorSectionId::Terrain &&
            hit.property == InspectorPropertyId::TerrainMaterialLayerCreate) {
            terrain_material_layer_menu::Close();
            const kb::scene::SceneEntity entity = sceneContext_.SelectedEntity();
            const kb::assets::TerrainAsset* terrain = sceneContext_.TerrainForEditing(entity);
            if (terrain == nullptr) {
                sceneContext_.Console().Warning("Terrain", "Terrain asset is unavailable.");
            } else if (terrain->materialLayers.size() >= kb::assets::TerrainAsset::MaximumMaterialLayers) {
                sceneContext_.Console().Warning("Terrain", "Terrain supports at most four material layers.");
            } else {
                const std::filesystem::path destinationFolder = sceneContext_.AssetBrowser().SelectedFolder();
                if (sceneContext_.CreateMaterialAsset(destinationFolder)) {
                    const kb::assets::AssetId materialAssetId = sceneContext_.AssetBrowser().SelectedAsset();
                    sceneContext_.SelectEntity(entity);
                    std::string error;
                    if (!materialAssetId.IsValid() ||
                        !sceneContext_.AddTerrainMaterialLayer(entity, materialAssetId, &error)) {
                        sceneContext_.Console().Warning(
                            "Terrain",
                            error.empty() ? "The material was created, but its terrain layer could not be added." : error);
                    } else {
                        sceneViewport_.RequestPresent();
                    }
                }
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (hit.section == InspectorSectionId::Terrain &&
            hit.property == InspectorPropertyId::TerrainMaterialLayerAdd) {
            terrain_material_layer_menu::Close();
            const kb::scene::SceneEntity entity = sceneContext_.SelectedEntity();
            const EditorMaterialAssetPickerDialog::Result result = EditorMaterialAssetPickerDialog::Show(
                mainWindow_, MakeEditorDarkTheme(), sceneContext_, sceneViewport_, {}, false);
            if (result.accepted) {
                std::string error;
                const bool assigned = sceneContext_.AddTerrainMaterialLayer(entity, result.assetId, &error);
                if (!assigned) {
                    sceneContext_.Console().Warning("Terrain", error.empty() ? "Material layer could not be added." : error);
                } else {
                    sceneViewport_.RequestPresent();
                }
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if ((hit.section == InspectorSectionId::MeshRenderer || hit.section == InspectorSectionId::Terrain)
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
                    sceneViewport_,
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
    // Let the dock system consume the click first (switch/re-click a tab, drag a
    // splitter). Switching tabs — e.g. Scene View ↔ Script Editor — is a layout
    // action, not a scene action, so it must NOT clear the scene selection (the
    // Inspector keeps showing the selected object). Only a click that hit no dock
    // element and landed outside the Hierarchy/Console falls through to deselect.
    const bool dockConsumed = dockController_.HandlePointerDown(messageWindow, x, y);
    if (dockConsumed) {
        sceneViewport_.RequestPresent();
    }
    if (!dockConsumed && !panelHit.inHierarchyPanel && !panelHit.inConsolePanel) {
        sceneContext_.ClearHierarchySelection();
    }
}

} // namespace kb::editor

#endif
