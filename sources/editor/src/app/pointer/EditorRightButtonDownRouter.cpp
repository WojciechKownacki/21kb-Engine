#include "app/pointer/EditorRightButtonDownRouter.hpp"
#include "localization/EditorLocalization.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPendingTextEditCommitter.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/project_files/EditorProjectFilesDeleteConfirmOverlayController.hpp"
#include "app/project_files/EditorProjectFilesTransientUiController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "engine/scene/SceneCreateMenu.hpp"
#include "inspection/ui/InspectorUIComponentModel.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/SkeletalMeshEditorPanelRenderer.hpp"
#include "scene/EditorHierarchyRowPicker.hpp"
#include "platform/win32/Utf8Gdi.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

constexpr UINT_PTR kHierarchyMenuCreateUI = 2000;
// + index into kb::scene::SceneCreateMenu::Items().
constexpr UINT_PTR kHierarchyMenuCreateItem = 3000;
constexpr UINT_PTR kHierarchyMenuDelete = 1002;
constexpr UINT_PTR kHierarchyMenuDuplicate = 1003;
constexpr UINT_PTR kHierarchyMenuRename = 1004;
constexpr UINT_PTR kSkeletonTreeMenuAddSocket = 1201;
constexpr UINT_PTR kSkeletonTreeMenuRenameSocket = 1202;
constexpr UINT_PTR kSkeletonTreeMenuDeleteSocket = 1203;

void AppendDisabled(HMENU menu, const char* label) {
    kb::editor::AppendMenuUtf8(menu, MF_STRING | MF_GRAYED, 0, label);
}

void AppendSeparator(HMENU menu) {
    kb::editor::AppendMenuUtf8(menu, MF_SEPARATOR, 0, nullptr);
}

[[nodiscard]] UINT ShowHierarchySystemMenu(HWND window, int x, int y) {
    HMENU menu = EditorRightButtonDownRouter::CreateHierarchyMenu();
    if (menu == nullptr) {
        return 0;
    }

    POINT screenPoint{ x, y };
    ClientToScreen(window, &screenPoint);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x,
        screenPoint.y,
        0,
        window,
        nullptr);
    DestroyMenu(menu);
    return command;
}

// The Create menu in the editor language. The engine's SceneCreateMenu keeps the English rows kb_cli shows;
// a row the engine adds later keeps its English label until it gets an entry here.
[[nodiscard]] std::string_view CreateItemLabel(const kb::scene::SceneCreateItem& item) {
    if (item.id == "Map Top Down") return EditorText("map2d.createTopDown", "Map - Top Down");
    if (item.id == "Map Platformer") return EditorText("map2d.createPlatformer", "Map - Platformer");
    if (item.id == "Sprite 2D") return EditorText("hierarchy.create.sprite2D", "Sprite 2D");
    if (item.id == "Animated Sprite") return EditorText("hierarchy.create.animatedSprite", "Animated Sprite");
    if (item.id == "Empty") return EditorText("hierarchy.create.empty", "Create Empty");
    if (item.id == "Cube") return EditorText("hierarchy.create.cube", "Cube");
    if (item.id == "Sphere") return EditorText("hierarchy.create.sphere", "Sphere");
    if (item.id == "Capsule") return EditorText("hierarchy.create.capsule", "Capsule");
    if (item.id == "Cylinder") return EditorText("hierarchy.create.cylinder", "Cylinder");
    if (item.id == "Cone") return EditorText("hierarchy.create.cone", "Cone");
    if (item.id == "Plane") return EditorText("hierarchy.create.plane", "Plane");
    if (item.id == "Quad") return EditorText("hierarchy.create.quad", "Quad");
    if (item.id == "Directional Light") return EditorText("hierarchy.create.directionalLight", "Directional");
    if (item.id == "Point Light") return EditorText("hierarchy.create.pointLight", "Point");
    if (item.id == "Spot Light") return EditorText("hierarchy.create.spotLight", "Spot");
    if (item.id == "Area Light") return EditorText("hierarchy.create.areaLight", "Area");
    if (item.id == "Camera") return EditorText("hierarchy.create.camera", "Camera");
    if (item.id == "Environment") return EditorText("hierarchy.create.environment", "Environment");
    if (item.id == "Particle System") return EditorText("hierarchy.create.particleSystem", "Particle System");
    if (item.id == "Audio Source") return EditorText("hierarchy.create.audioSource", "Audio Source");
    if (item.id == "Audio Listener") return EditorText("hierarchy.create.audioListener", "Audio Listener");
    return item.label;
}

[[nodiscard]] std::string_view CreateSubmenuLabel(std::string_view submenu) {
    if (submenu == "2D Object") return EditorText("map2d.createSubmenu", "2D Object");
    if (submenu == "3D Object") return EditorText("hierarchy.create.submenu.objects3D", "3D Object");
    if (submenu == "Light") return EditorText("hierarchy.create.submenu.light", "Light");
    if (submenu == "Effects") return EditorText("hierarchy.create.submenu.effects", "Effects");
    if (submenu == "Audio") return EditorText("hierarchy.create.submenu.audio", "Audio");
    return submenu;
}

[[nodiscard]] std::string_view CreateItemHint(const kb::scene::SceneCreateItem& item) {
    if (item.id == "Environment") return EditorText("hierarchy.create.environmentHint", "Sky + ambient");
    return item.hint;
}

[[nodiscard]] HMENU CreateSkeletonTreeSystemMenu(bool canAddSocket) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return nullptr;
    kb::editor::AppendMenuUtf8(
        menu,
        MF_STRING | (canAddSocket ? MF_ENABLED : MF_GRAYED),
        kSkeletonTreeMenuAddSocket,
        std::string{ EditorText("skeletalMeshEditor.menu.addSocket", "Add Socket") }.c_str());
    kb::editor::AppendMenuUtf8(menu, MF_STRING | MF_GRAYED, kSkeletonTreeMenuRenameSocket,
        std::string{ EditorText("skeletalMeshEditor.menu.renameSocket", "Rename Socket") }.c_str());
    kb::editor::AppendMenuUtf8(menu, MF_STRING | MF_GRAYED, kSkeletonTreeMenuDeleteSocket,
        std::string{ EditorText("skeletalMeshEditor.menu.deleteSocket", "Delete Socket") }.c_str());
    return menu;
}

[[nodiscard]] UINT ShowSkeletonTreeSystemMenu(HWND window, int x, int y, bool canAddSocket) {
    HMENU menu = CreateSkeletonTreeSystemMenu(canAddSocket);
    if (menu == nullptr) return 0;

    POINT screenPoint{ x, y };
    ClientToScreen(window, &screenPoint);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x,
        screenPoint.y,
        0,
        window,
        nullptr);
    DestroyMenu(menu);
    return command;
}

} // namespace

HMENU EditorRightButtonDownRouter::CreateHierarchyMenu() {
    HMENU menu = CreatePopupMenu();
    HMENU create = CreatePopupMenu();
    HMENU widgets = CreatePopupMenu();
    HMENU layouts = CreatePopupMenu();
    HMENU effects = CreatePopupMenu();
    HMENU advanced = CreatePopupMenu();
    if (!menu || !create || !widgets || !layouts || !effects || !advanced) {
        for (const auto handle : {menu, create, widgets, layouts, effects, advanced})
            if (handle) DestroyMenu(handle);
        return nullptr;
    }
    // The scene objects, in the order and grouping of kb::scene::SceneCreateMenu.
    std::vector<std::pair<std::string_view, HMENU>> submenus;
    const auto items = kb::scene::SceneCreateMenu::Items();
    for (std::size_t index = 0U; index < items.size(); ++index) {
        const kb::scene::SceneCreateItem& item = items[index];
        std::string label{ CreateItemLabel(item) };
        if (!item.hint.empty()) label += "\t" + std::string{ CreateItemHint(item) };
        HMENU destination = create;
        if (!item.submenu.empty()) {
            auto found = std::ranges::find_if(submenus, [&item](const auto& entry) { return entry.first == item.submenu; });
            if (found == submenus.end()) {
                HMENU submenu = CreatePopupMenu();
                if (!submenu) {
                    // Destroying a menu destroys the submenus already appended to it.
                    for (const auto handle : {menu, create, widgets, layouts, effects, advanced}) DestroyMenu(handle);
                    return nullptr;
                }
                kb::editor::AppendMenuUtf8(create, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), std::string{ CreateSubmenuLabel(item.submenu) }.c_str());
                found = submenus.insert(submenus.end(), { item.submenu, submenu });
            }
            destination = found->second;
        }
        kb::editor::AppendMenuUtf8(destination, MF_STRING, kHierarchyMenuCreateItem + index, label.c_str());
    }
    for (const auto& descriptor : kb::scene::UIComponentCatalog()) {
        using enum kb::scene::UIComponentType;
        HMENU destination = widgets;
        switch (descriptor.type) {
        case HorizontalLayout: case VerticalLayout: case GridLayout: case WrapLayout: case OverlayLayout:
        case LayoutElement: case ContentSizeFitter: case AspectRatioFitter: destination = layouts; break;
        case Mask: case Shadow: case Outline: case BackgroundBlur: destination = effects; break;
        case CanvasScaler: case CanvasGroup: case Selectable: destination = advanced; break;
        default: break;
        }
        kb::editor::AppendMenuUtf8(destination, MF_STRING, kHierarchyMenuCreateUI + static_cast<UINT>(descriptor.type),
            std::string{ InspectorUIComponentModel::DisplayName(descriptor.type) }.c_str());
    }
    AppendSeparator(widgets);
    const auto append = [](HMENU target, UINT flags, UINT_PTR id, std::string_view text, std::string_view shortcut = {}) {
        std::string row{ text };
        if (!shortcut.empty()) row += "\t" + std::string{ shortcut };
        kb::editor::AppendMenuUtf8(target, flags, id, row.c_str());
    };
    append(widgets, MF_POPUP, reinterpret_cast<UINT_PTR>(layouts), EditorText("hierarchy.create.layout", "Layout"));
    append(widgets, MF_POPUP, reinterpret_cast<UINT_PTR>(effects), EditorText("hierarchy.create.uiEffects", "Effects"));
    append(widgets, MF_POPUP, reinterpret_cast<UINT_PTR>(advanced), EditorText("hierarchy.create.advanced", "Advanced"));
    append(create, MF_POPUP, reinterpret_cast<UINT_PTR>(widgets), EditorText("hierarchy.create.userInterface", "User Interface"));
    append(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(create), EditorText("hierarchy.create", "Create"));
    AppendSeparator(menu);
    append(menu, MF_STRING, kHierarchyMenuDelete, EditorText("common.delete", "Delete"), "Del");
    append(menu, MF_STRING, kHierarchyMenuDuplicate, EditorText("hierarchy.menu.duplicate", "Duplicate"), "Ctrl+D");
    append(menu, MF_STRING, kHierarchyMenuRename, EditorText("common.rename", "Rename"), "F2");
    AppendDisabled(menu, std::string{ EditorText("hierarchy.menu.pasteSpecial", "Paste Special") }.c_str());
    AppendDisabled(menu, (std::string{ EditorText("hierarchy.menu.paste", "Paste") } + "\tCtrl+V").c_str());
    AppendDisabled(menu, (std::string{ EditorText("hierarchy.menu.copy", "Copy") } + "\tCtrl+C").c_str());
    AppendDisabled(menu, (std::string{ EditorText("hierarchy.menu.cut", "Cut") } + "\tCtrl+X").c_str());
    return menu;
}

bool EditorRightButtonDownRouter::ExecuteHierarchyMenuCommand(UINT command, EditorSceneContext& sceneContext, kb::scene::SceneEntity parent) {
    for (const auto& descriptor : kb::scene::UIComponentCatalog()) {
        if (command == kHierarchyMenuCreateUI + static_cast<UINT>(descriptor.type))
            return sceneContext.CreateUIObject(descriptor.type, parent).IsValid();
    }
    const auto items = kb::scene::SceneCreateMenu::Items();
    if (command >= kHierarchyMenuCreateItem && command < kHierarchyMenuCreateItem + items.size())
        return sceneContext.CreateMenuObject(items[command - kHierarchyMenuCreateItem].id, parent).IsValid();
    switch (command) {
    case kHierarchyMenuDelete:
        return sceneContext.DeleteSelectedHierarchyEntity();
    case kHierarchyMenuDuplicate:
        return sceneContext.DuplicateSelectedHierarchyEntities();
    case kHierarchyMenuRename:
        return sceneContext.BeginHierarchyRename();
    default:
        return false;
    }
}

EditorRightButtonDownRouter::EditorRightButtonDownRouter(
    HWND mainWindow,
    EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

void EditorRightButtonDownRouter::Handle(HWND messageWindow, int x, int y) {
    const EditorProjectFilesDeleteConfirmOverlayController deleteConfirm(messageWindow, sceneContext_);
    if (deleteConfirm.HandlePointerDown(x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    EditorPendingTextEditCommitter pendingTextEdits(sceneContext_);
    if (pendingTextEdits.CommitPendingEdits()) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }

    pointerDrag_.Clear();

    const std::optional<RECT> materialEditorContent = EditorPanelContentResolver::Resolve(DockPanelKind::MaterialEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (materialEditorContent.has_value() && x >= materialEditorContent->left && x < materialEditorContent->right && y >= materialEditorContent->top && y < materialEditorContent->bottom) {
        // A right-click while the node palette / wire-drop menu is open dismisses it instead of
        // beginning a canvas pan. Panning under the open, screen-anchored palette slid the whole graph
        // around beneath it, which reads as "the dropdown can be dragged". Drop any parked wire too, so a
        // dismissed wire-drop menu leaves the pin unplugged rather than half-connected.
        if (sceneContext_.IsMaterialGraphContextMenuOpen()) {
            static_cast<void>(sceneContext_.CloseMaterialGraphContextMenu());
            static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
        if (sceneContext_.MaterialEditor().InfoPanelVisible() &&
            MaterialEditorPanelRectWidth(layout.detailsPanel) >= 220 &&
            MaterialEditorPanelRectHeight(layout.detailsPanel) >= 140 &&
            MaterialEditorPanelPointInRect(layout.detailsPanel, x, y)) {
            static_cast<void>(sceneContext_.CloseMaterialGraphContextMenu());
            static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        if (MaterialEditorPanelPointInRect(layout.graphCanvas, x, y)) {
            sceneContext_.AssetBrowser().ClearSelection();
            sceneContext_.ClearHierarchySelection();
            sceneContext_.FocusMaterialGraph(true);
            EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
            const kb::assets::AssetId materialId = sceneContext_.MaterialEditor().OpenAssetId();
            const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext_.MaterialEditor().WorkingCopy().has_value()
                ? sceneContext_.MaterialEditor().WorkingCopy()
                : sceneContext_.ReadMaterialDocumentAsset(materialId);
            if (material.has_value()) {
                if (const std::optional<std::uint32_t> nodeId = MaterialEditorPanelRenderer::GraphNodeAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphContextTarget(*nodeId, 0U));
                } else if (const std::optional<std::uint32_t> commentId = MaterialEditorPanelRenderer::GraphCommentAt(
                               *materialEditorContent,
                               material->graph,
                               sceneContext_,
                               materialId,
                               x,
                               y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphContextTarget(0U, *commentId));
                }
            }
            static_cast<void>(sceneContext_.CloseMaterialGraphContextMenu());
            static_cast<void>(sceneContext_.BeginMaterialGraphPan(x, y));
            SetCapture(messageWindow);
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
    }

    EditorSceneViewportCameraController sceneCamera(mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, sceneViewport_);
    if (sceneCamera.HandleRightButtonDown(messageWindow, x, y)) {
        return;
    }

    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (consoleContent.has_value() && consolePointer.HandleContextMenu(*consoleContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    const std::optional<RECT> skeletalMeshEditorContent = EditorPanelContentResolver::Resolve(
        DockPanelKind::SkeletalMeshEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (skeletalMeshEditorContent.has_value() && sceneContext_.HasSkeletalMeshEditorAsset()) {
        const RECT treeList = SkeletalMeshEditorPanelRenderer::TreeListRect(
            *skeletalMeshEditorContent, sceneContext_);
        const RECT scrollbar = SkeletalMeshEditorPanelRenderer::TreeScrollbarTrack(
            *skeletalMeshEditorContent, sceneContext_);
        const bool insideTree = x >= treeList.left && x < treeList.right &&
            y >= treeList.top && y < treeList.bottom;
        const bool insideScrollbar = SkeletalMeshEditorPanelRenderer::TreeMaxScroll(
            *skeletalMeshEditorContent, sceneContext_) > 0 &&
            x >= scrollbar.left && x < scrollbar.right && y >= scrollbar.top && y < scrollbar.bottom;
        const std::optional<SkeletalMeshEditorTreeRow> row =
            SkeletalMeshEditorPanelRenderer::TreeRowAt(*skeletalMeshEditorContent, sceneContext_, x, y);
        if (insideTree && !insideScrollbar) {
            bool changed = row.has_value() && (row->kind == SkeletalMeshEditorTreeItemKind::Bone
                ? sceneContext_.SelectSkeletalMeshEditorBone(row->boneId)
                : sceneContext_.SelectSkeletalMeshEditorSocket(row->socketName));
            const UINT command = ShowSkeletonTreeSystemMenu(
                messageWindow, x, y, sceneContext_.CanAddSkeletonEditorSocket());
            if (command == kSkeletonTreeMenuAddSocket) {
                changed = sceneContext_.AddSkeletonEditorSocket() || changed;
                if (changed) {
                    static_cast<void>(sceneContext_.SetSkeletalMeshEditorTreeScrollOffset(
                        SkeletalMeshEditorPanelRenderer::TreeScrollOffsetToRevealSelection(
                            *skeletalMeshEditorContent, sceneContext_),
                        SkeletalMeshEditorPanelRenderer::TreeMaxScroll(
                            *skeletalMeshEditorContent, sceneContext_)));
                }
            }
            if (changed) {
                sceneViewport_.RequestPresent();
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
    }

    const std::optional<RECT> hierarchyContent = EditorPanelContentResolver::Resolve(DockPanelKind::Hierarchy, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (hierarchyContent.has_value() && x >= hierarchyContent->left && x < hierarchyContent->right && y >= hierarchyContent->top && y < hierarchyContent->bottom) {
        const kb::scene::SceneEntity entity = EditorHierarchyRowPicker::EntityAtContentPoint(*hierarchyContent, x, y, sceneContext_);
        if (entity.IsValid() && !sceneContext_.IsHierarchyEntitySelected(entity)) {
            sceneContext_.SelectEntity(entity);
        }
        const UINT command = ShowHierarchySystemMenu(messageWindow, x, y);
        if (ExecuteHierarchyMenuCommand(command, sceneContext_, entity)) {
            const auto items = kb::scene::SceneCreateMenu::Items();
            if (command >= kHierarchyMenuCreateItem && command < kHierarchyMenuCreateItem + items.size()) {
                const auto id = items[command - kHierarchyMenuCreateItem].id;
                if (id == "Map Top Down" || id == "Map Platformer") static_cast<void>(dockModel_.Commands().ActivatePanelKind(DockPanelKind::TilemapEditor, DockArea::Center));
            }
            sceneViewport_.RequestPresent();
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandleRightButtonDown(
            messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
}

} // namespace kb::editor

#endif
