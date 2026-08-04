#include "app/EditorWindowLifecycleHandler.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/editor/docking/DockTypes.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::wstring DirtyMaterialPromptText(const EditorSceneContext& sceneContext, std::wstring_view action) {
    std::wstring materialName = L"the open material";
    const kb::assets::AssetId materialId = sceneContext.MaterialEditor().OpenAssetId();
    if (const kb::assets::AssetMetadata* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(materialId);
        metadata != nullptr) {
        const std::string name = metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
        materialName.assign(name.begin(), name.end());
    }

    std::wstring text = L"Save changes to ";
    text += materialName;
    text += L" before ";
    text += action;
    text += L"?\n\nYes = Save\nNo = Discard changes\nCancel = keep editing";
    return text;
}

[[nodiscard]] bool ResolveDirtyMaterialEditorClose(HWND owner, EditorSceneContext& sceneContext, std::wstring_view action) {
    // Alt+F4 reaches here without any mouse-up, so a graph gesture can still own the working copy (the docked
    // tab's close button carries the same guard, in EditorLeftButtonDownRouter). Settle it before anything
    // reads the document: a drag is finished the way a mouse-up would finish it - so the move is recorded,
    // counts as unsaved work and stays undoable instead of being silently dropped - and a wire still in
    // mid-air was never dropped on a pin, so it is cancelled and the link comes back. Without this the prompt
    // describes, and Save writes, a half-finished document.
    static_cast<void>(sceneContext.SettleMaterialGraphGesture());

    if (!sceneContext.HasDirtyMaterialAssetEdit()) {
        return true;
    }

    const int result = MessageBoxW(
        owner,
        DirtyMaterialPromptText(sceneContext, action).c_str(),
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

[[nodiscard]] bool ResolveDirtySkeletalMeshEditorClose(HWND owner, EditorSceneContext& sceneContext, std::wstring_view action) {
    if (!sceneContext.HasDirtySkeletalMeshEditorAssetEdit()) return true;
    std::wstring text = L"Save changes to the open Skeletal Mesh before ";
    text += action;
    text += L"?\n\nYes = Save\nNo = Discard changes\nCancel = keep editing";
    switch (MessageBoxW(owner, text.c_str(), L"Unsaved Skeletal Mesh", MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON1 | MB_APPLMODAL)) {
    case IDYES:
        return sceneContext.SaveSkeletalMeshEditorAsset();
    case IDNO:
        return sceneContext.RevertSkeletalMeshEditorAsset();
    case IDCANCEL:
    default:
        return false;
    }
}

} // namespace

EditorWindowLifecycleHandler::EditorWindowLifecycleHandler(HWND& mainWindow, bool& running, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext) noexcept
    : mainWindow_(mainWindow)
    , running_(running)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , sceneContext_(sceneContext) {}

LRESULT EditorWindowLifecycleHandler::HandleClose(HWND messageWindow) {
    if (const std::uint32_t panelId = floatingWindows_.Queries().PanelId(messageWindow); panelId != 0) {
        const DockPanel* panel = dockModel_.Queries().FindPanel(panelId);
        if (panel != nullptr && panel->kind == DockPanelKind::MaterialEditor &&
            !ResolveDirtyMaterialEditorClose(messageWindow, sceneContext_, L"closing the Material Editor")) {
            return 0;
        }
        if (panel != nullptr && panel->kind == DockPanelKind::SkeletalMeshEditor &&
            !ResolveDirtySkeletalMeshEditorClose(messageWindow, sceneContext_, L"closing the Skeletal Mesh Editor")) {
            return 0;
        }
        floatingWindows_.Commands().Destroy(panelId);
        dockModel_.Commands().DockPanelTo(panelId, DockDropPreview{ .zone = DockDropZone::Bottom });
        InvalidateRect(mainWindow_, nullptr, FALSE);
        return 0;
    }

    static_cast<void>(sceneContext_.RestorePlayModeSceneSession());
    if (!ResolveDirtyMaterialEditorClose(messageWindow, sceneContext_, L"closing the editor")) {
        return 0;
    }
    if (!ResolveDirtySkeletalMeshEditorClose(messageWindow, sceneContext_, L"closing the editor")) {
        return 0;
    }
    const std::optional<EditorDirtySceneResolution> resolution =
        EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(mainWindow_, sceneContext_, L"closing the editor");
    if (!resolution.has_value()) {
        return 0;
    }
    if (*resolution == EditorDirtySceneResolution::Save) {
        if (!sceneContext_.PrepareDirtySceneTransition("application close", *resolution)) {
            return 0;
        }
    } else {
        sceneContext_.DiscardDirtySceneDocument("application close");
    }

    running_ = false;
    DestroyWindow(mainWindow_);
    mainWindow_ = nullptr;
    PostQuitMessage(0);
    return 0;
}

LRESULT EditorWindowLifecycleHandler::HandleDestroy(HWND messageWindow) {
    if (floatingWindows_.Queries().IsFloatingWindow(messageWindow)) {
        floatingWindows_.Lifecycle().OnDestroyed(messageWindow);
        return 0;
    }

    if (mainWindow_ != nullptr && messageWindow == mainWindow_) {
        mainWindow_ = nullptr;
        running_ = false;
        PostQuitMessage(0);
    }

    return 0;
}

} // namespace kb::editor

#endif
