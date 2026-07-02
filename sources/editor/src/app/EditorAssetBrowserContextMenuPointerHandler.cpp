#include "app/EditorAssetBrowserContextMenuPointerHandler.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserContextCommandExecutor.hpp"
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {
namespace {

constexpr UINT_PTR kAssetMenuImport = 2001;
constexpr UINT_PTR kAssetMenuNewFolder = 2002;
constexpr UINT_PTR kAssetMenuNewLuaScript = 2003;
constexpr UINT_PTR kAssetMenuNewInputAction = 2004;
constexpr UINT_PTR kAssetMenuNewInputAxis = 2006;
constexpr UINT_PTR kAssetMenuNewInputMappingContext = 2005;
constexpr UINT_PTR kAssetMenuNewMaterial = 2007;
constexpr UINT_PTR kAssetMenuCreateMaterialInstance = 2008;
constexpr UINT_PTR kAssetMenuNewMaterialGraph = 2009;
constexpr UINT_PTR kAssetMenuNewMaterialType = 2010;
constexpr UINT_PTR kAssetMenuCreateMaterialFromGraph = 2011;
constexpr UINT_PTR kAssetMenuCreateMaterialFromMaterialType = 2012;
constexpr UINT_PTR kAssetMenuNewMaterialFunction = 2013;
constexpr UINT_PTR kAssetMenuDirectionalLight = 2101;
constexpr UINT_PTR kAssetMenuPointLight = 2102;
constexpr UINT_PTR kAssetMenuSpotLight = 2103;
constexpr UINT_PTR kAssetMenuRename = 2201;
constexpr UINT_PTR kAssetMenuDelete = 2202;
constexpr UINT_PTR kAssetMenuRefresh = 2203;
constexpr UINT_PTR kAssetMenuExtractMaterials = 2301;

[[nodiscard]] UINT_PTR CommandId(EditorAssetContextCommand command) noexcept {
    switch (command) {
    case EditorAssetContextCommand::Import:
        return kAssetMenuImport;
    case EditorAssetContextCommand::NewFolder:
        return kAssetMenuNewFolder;
    case EditorAssetContextCommand::NewLuaScript:
        return kAssetMenuNewLuaScript;
    case EditorAssetContextCommand::NewMaterial:
        return kAssetMenuNewMaterial;
    case EditorAssetContextCommand::NewMaterialFunction:
        return kAssetMenuNewMaterialFunction;
    case EditorAssetContextCommand::NewMaterialGraph:
        return kAssetMenuNewMaterialGraph;
    case EditorAssetContextCommand::NewMaterialType:
        return kAssetMenuNewMaterialType;
    case EditorAssetContextCommand::CreateMaterialInstance:
        return kAssetMenuCreateMaterialInstance;
    case EditorAssetContextCommand::CreateMaterialFromGraph:
        return kAssetMenuCreateMaterialFromGraph;
    case EditorAssetContextCommand::CreateMaterialFromMaterialType:
        return kAssetMenuCreateMaterialFromMaterialType;
    case EditorAssetContextCommand::NewInputAction:
        return kAssetMenuNewInputAction;
    case EditorAssetContextCommand::NewInputAxis:
        return kAssetMenuNewInputAxis;
    case EditorAssetContextCommand::NewInputMappingContext:
        return kAssetMenuNewInputMappingContext;
    case EditorAssetContextCommand::ExtractMaterials:
        return kAssetMenuExtractMaterials;
    case EditorAssetContextCommand::AddDirectionalLight:
        return kAssetMenuDirectionalLight;
    case EditorAssetContextCommand::AddPointLight:
        return kAssetMenuPointLight;
    case EditorAssetContextCommand::AddSpotLight:
        return kAssetMenuSpotLight;
    case EditorAssetContextCommand::Rename:
        return kAssetMenuRename;
    case EditorAssetContextCommand::Delete:
        return kAssetMenuDelete;
    case EditorAssetContextCommand::Refresh:
        return kAssetMenuRefresh;
    case EditorAssetContextCommand::AddLighting:
    case EditorAssetContextCommand::None:
    default:
        return 0;
    }
}

[[nodiscard]] EditorAssetContextCommand CommandFromId(UINT command) noexcept {
    switch (command) {
    case kAssetMenuImport:
        return EditorAssetContextCommand::Import;
    case kAssetMenuNewFolder:
        return EditorAssetContextCommand::NewFolder;
    case kAssetMenuNewLuaScript:
        return EditorAssetContextCommand::NewLuaScript;
    case kAssetMenuNewMaterial:
        return EditorAssetContextCommand::NewMaterial;
    case kAssetMenuNewMaterialFunction:
        return EditorAssetContextCommand::NewMaterialFunction;
    case kAssetMenuNewMaterialGraph:
        return EditorAssetContextCommand::NewMaterialGraph;
    case kAssetMenuNewMaterialType:
        return EditorAssetContextCommand::NewMaterialType;
    case kAssetMenuCreateMaterialInstance:
        return EditorAssetContextCommand::CreateMaterialInstance;
    case kAssetMenuCreateMaterialFromGraph:
        return EditorAssetContextCommand::CreateMaterialFromGraph;
    case kAssetMenuCreateMaterialFromMaterialType:
        return EditorAssetContextCommand::CreateMaterialFromMaterialType;
    case kAssetMenuNewInputAction:
        return EditorAssetContextCommand::NewInputAction;
    case kAssetMenuNewInputAxis:
        return EditorAssetContextCommand::NewInputAxis;
    case kAssetMenuNewInputMappingContext:
        return EditorAssetContextCommand::NewInputMappingContext;
    case kAssetMenuExtractMaterials:
        return EditorAssetContextCommand::ExtractMaterials;
    case kAssetMenuDirectionalLight:
        return EditorAssetContextCommand::AddDirectionalLight;
    case kAssetMenuPointLight:
        return EditorAssetContextCommand::AddPointLight;
    case kAssetMenuSpotLight:
        return EditorAssetContextCommand::AddSpotLight;
    case kAssetMenuRename:
        return EditorAssetContextCommand::Rename;
    case kAssetMenuDelete:
        return EditorAssetContextCommand::Delete;
    case kAssetMenuRefresh:
        return EditorAssetContextCommand::Refresh;
    default:
        return EditorAssetContextCommand::None;
    }
}

[[nodiscard]] HMENU CreateLightingSubmenu() {
    HMENU lighting = CreatePopupMenu();
    AppendMenuA(lighting, MF_STRING, kAssetMenuDirectionalLight, "Directional Light");
    AppendMenuA(lighting, MF_STRING, kAssetMenuPointLight, "Point Light");
    AppendMenuA(lighting, MF_STRING, kAssetMenuSpotLight, "Spot Light");
    return lighting;
}

void AppendAddSubmenu(HMENU menu) {
    HMENU add = CreatePopupMenu();
    AppendMenuA(add, MF_POPUP, reinterpret_cast<UINT_PTR>(CreateLightingSubmenu()), "Lighting");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(add), "Add");
}

[[nodiscard]] UINT ShowProjectFilesSystemMenu(HWND window, int x, int y, const std::vector<EditorAssetContextMenuItem>& items) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return 0;
    }

    for (const EditorAssetContextMenuItem& item : items) {
        if (item.command == EditorAssetContextCommand::AddLighting) {
            continue;
        } else if (const UINT_PTR id = CommandId(item.command); id != 0) {
            AppendMenuA(menu, MF_STRING, id, item.label);
        }
        if (item.separatorAfter) {
            AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
        }
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

[[nodiscard]] bool ExecuteNativeProjectFilesMenu(HWND window, int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    const std::vector<EditorAssetContextMenuItem> items = state.ContextMenuItems(sceneContext.Scene().Assets().Manager());
    state.CloseContextMenu();
    InvalidateRect(window, nullptr, FALSE);
    UpdateWindow(window);
    const EditorAssetContextCommand command = CommandFromId(ShowProjectFilesSystemMenu(window, x, y, items));
    if (command == EditorAssetContextCommand::None) {
        return true;
    }
    return EditorAssetBrowserContextCommandExecutor::Execute(command, sceneContext);
}

} // namespace

std::optional<bool> EditorAssetBrowserContextMenuPointerHandler::HandleOpenMenuPointerDown(const EditorAssetBrowserHit& hit, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (!state.IsContextMenuOpen()) {
        return std::nullopt;
    }

    if (hit.kind == EditorAssetBrowserHitKind::ContextMenuCommand) {
        if (hit.command == EditorAssetContextCommand::AddLighting) {
            static_cast<void>(state.SetContextMenuHoveredCommand(EditorAssetContextCommand::AddLighting));
            return true;
        }
        static_cast<void>(EditorAssetBrowserContextCommandExecutor::Execute(hit.command, sceneContext));
        state.CloseContextMenu();
        return true;
    }
    if (hit.kind == EditorAssetBrowserHitKind::ContextMenuBody) {
        return true;
    }

    state.CloseContextMenu();
    return std::nullopt;
}

bool EditorAssetBrowserContextMenuPointerHandler::HandleRightButtonDown(HWND window, const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (state.IsTextEditing()) {
        static_cast<void>(sceneContext.CommitAssetTextEdit());
    }

    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    state.CloseContextMenu();
    const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(content, x, y, state, manager);

    switch (hit.kind) {
    case EditorAssetBrowserHitKind::Asset: {
        const std::optional<kb::assets::AssetId> id = EditorAssetBrowserHitPayloadResolver::AssetIdAt(hit, state, manager);
        if (!id.has_value()) {
            return false;
        }
        static_cast<void>(state.SelectAsset(*id, manager));
        return state.OpenContextMenuForAsset(x, y, *id, manager) && ExecuteNativeProjectFilesMenu(window, x, y, sceneContext);
    }
    case EditorAssetBrowserHitKind::ContentFolder: {
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderAt(hit, state, manager);
        if (!folder.has_value()) {
            return false;
        }
        static_cast<void>(state.SelectContentFolder(*folder, manager));
        return state.OpenContextMenuForFolder(x, y, *folder, manager) && ExecuteNativeProjectFilesMenu(window, x, y, sceneContext);
    }
    case EditorAssetBrowserHitKind::FolderDisclosure:
    case EditorAssetBrowserHitKind::Folder: {
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderDropTargetAt(hit, state, manager);
        if (!folder.has_value()) {
            return false;
        }
        static_cast<void>(state.SelectFolder(*folder, manager));
        return state.OpenContextMenuForFolder(x, y, *folder, manager) && ExecuteNativeProjectFilesMenu(window, x, y, sceneContext);
    }
    case EditorAssetBrowserHitKind::DropTarget:
        state.OpenContextMenuForBackground(x, y);
        return ExecuteNativeProjectFilesMenu(window, x, y, sceneContext);
    case EditorAssetBrowserHitKind::DeleteConfirmBody:
    case EditorAssetBrowserHitKind::DeleteConfirmListBody:
    case EditorAssetBrowserHitKind::DeleteConfirmCheckbox:
    case EditorAssetBrowserHitKind::DeleteConfirmScrollbarThumb:
    case EditorAssetBrowserHitKind::DeleteConfirmScrollbarTrack:
    case EditorAssetBrowserHitKind::DeleteConfirmAccept:
    case EditorAssetBrowserHitKind::DeleteConfirmCancel:
        return true;
    default:
        return false;
    }
}

bool EditorAssetBrowserContextMenuPointerHandler::HandlePointerMove(const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (!state.IsContextMenuOpen()) {
        return false;
    }

    const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(content, x, y, state, sceneContext.Scene().Assets().Manager());
    const EditorAssetContextCommand hovered = hit.kind == EditorAssetBrowserHitKind::ContextMenuCommand ? hit.command : EditorAssetContextCommand::None;
    return state.SetContextMenuHoveredCommand(hovered);
}

} // namespace kb::editor

#endif
