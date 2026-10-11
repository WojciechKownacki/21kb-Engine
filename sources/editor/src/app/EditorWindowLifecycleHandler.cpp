#include "app/EditorWindowLifecycleHandler.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "app/EditorParticleDocumentLifecycle.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/editor/docking/DockTypes.hpp"
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>
#include <string>

namespace kb::editor {
namespace {

// The question asked before the Material Editor window (or the whole editor) closes over unsaved work.
[[nodiscard]] std::string DirtyMaterialPromptText(const EditorSceneContext& sceneContext, bool closingEditor) {
    const kb::assets::AssetId materialId = sceneContext.MaterialEditor().OpenAssetId();
    if (const kb::assets::AssetMetadata* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(materialId);
        metadata != nullptr) {
        const std::string name = metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
        return closingEditor
            ? EditorLocalization::Format("materialDirty.namedBeforeCloseEditor", "Save changes to {material} before closing the editor?",
                  { { "material", name } })
            : EditorLocalization::Format("materialDirty.namedBeforeCloseMaterialEditor",
                  "Save changes to {material} before closing the Material Editor?", { { "material", name } });
    }
    return std::string{ closingEditor
        ? EditorText("materialDirty.beforeCloseEditor", "Save changes to the open material before closing the editor?")
        : EditorText("materialDirty.beforeCloseMaterialEditor", "Save changes to the open material before closing the Material Editor?") };
}

[[nodiscard]] bool ResolveDirtyMaterialEditorClose(HWND owner, EditorSceneContext& sceneContext, bool closingEditor) {
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

    const EditorChoiceDialogResult result = EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
        .title = std::string{ EditorText("materialDirty.title", "Unsaved Material") },
        .message = DirtyMaterialPromptText(sceneContext, closingEditor),
        .supportingText = std::string{ EditorText("materialDirty.supporting", "Choose whether to preserve the current material changes.") },
        .primaryLabel = std::string{ EditorText("common.save", "Save") },
        .secondaryLabel = std::string{ EditorText("common.discard", "Discard") },
        .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
        .icon = HeroIconKind::RectangleGroup,
    });

    const kb::assets::AssetId materialId = sceneContext.MaterialEditor().OpenAssetId();
    switch (result) {
    case EditorChoiceDialogResult::Primary:
        return sceneContext.SaveMaterialEditorAsset(materialId);
    case EditorChoiceDialogResult::Secondary:
        return sceneContext.RevertMaterialEditorAsset(materialId);
    case EditorChoiceDialogResult::Cancel:
    default:
        return false;
    }
}

[[nodiscard]] bool ResolveDirtySkeletalMeshEditorClose(HWND owner, EditorSceneContext& sceneContext, bool closingEditor) {
    if (!sceneContext.HasDirtySkeletalMeshEditorAssetEdit()) return true;
    switch (EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
        .title = std::string{ EditorText("skeletalDirty.title", "Unsaved Skeletal Asset") },
        .message = std::string{ closingEditor
            ? EditorText("skeletalDirty.beforeCloseTab", "Save changes to the open skeletal assets before closing the editor?")
            : EditorText("skeletalDirty.beforeCloseWindow", "Save changes to the open skeletal assets before closing the Skeletal Mesh Editor?") },
        .supportingText = std::string{ EditorText("skeletalDirty.supporting", "Choose whether to preserve the current asset changes.") },
        .primaryLabel = std::string{ EditorText("common.save", "Save") },
        .secondaryLabel = std::string{ EditorText("common.discard", "Discard") },
        .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
        .icon = HeroIconKind::Skeleton,
    })) {
    case EditorChoiceDialogResult::Primary:
        return sceneContext.SaveSkeletalMeshEditorAsset();
    case EditorChoiceDialogResult::Secondary:
        return sceneContext.RevertSkeletalMeshEditorAsset();
    case EditorChoiceDialogResult::Cancel:
    default:
        return false;
    }
}

[[nodiscard]] bool ResolveDirtyAnimatorEditorClose(HWND owner, EditorSceneContext& sceneContext, bool closingEditor) {
    if (!sceneContext.HasDirtyAnimatorEditorAssetEdit()) return true;
    switch (EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
        .title = std::string{ EditorText("animatorDirty.title", "Unsaved Animator Controller") },
        .message = std::string{ closingEditor
            ? EditorText("animatorDirty.beforeCloseTab", "Save changes to the open Animator Controller before closing the editor?")
            : EditorText("animatorDirty.beforeCloseWindow", "Save changes to the open Animator Controller before closing the Animator Editor?") },
        .supportingText = std::string{ EditorText("animatorDirty.supporting", "Choose whether to preserve the current controller changes.") },
        .primaryLabel = std::string{ EditorText("common.save", "Save") },
        .secondaryLabel = std::string{ EditorText("common.discard", "Discard") },
        .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
        .icon = HeroIconKind::Gamepad2,
    })) {
    case EditorChoiceDialogResult::Primary:
        return sceneContext.SaveAnimatorEditorAsset();
    case EditorChoiceDialogResult::Secondary:
        return sceneContext.RevertAnimatorEditorAsset();
    case EditorChoiceDialogResult::Cancel:
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
            !ResolveDirtyMaterialEditorClose(messageWindow, sceneContext_, false)) {
            return 0;
        }
        if (panel != nullptr && panel->kind == DockPanelKind::SkeletalMeshEditor &&
            !ResolveDirtySkeletalMeshEditorClose(messageWindow, sceneContext_, false)) {
            return 0;
        }
        if (panel != nullptr && panel->kind == DockPanelKind::AnimatorEditor &&
            !ResolveDirtyAnimatorEditorClose(messageWindow, sceneContext_, false)) {
            return 0;
        }
        if (panel != nullptr && panel->kind == DockPanelKind::ParticleEditor &&
            !EditorParticleDocumentLifecycle::Resolve(
                messageWindow, sceneContext_,
                kb::particle_editor::ParticleDocumentTransition::CloseWindow)) {
            return 0;
        }
        floatingWindows_.Commands().Destroy(panelId);
        if (panel != nullptr && panel->kind == DockPanelKind::ParticleEditor) {
            dockModel_.Commands().DockPanelTo(panelId, DockDropPreview{ .zone = DockDropZone::Bottom });
            static_cast<void>(dockModel_.Commands().ClosePanel(panelId));
            sceneContext_.CloseParticleEditorAsset();
            InvalidateRect(mainWindow_, nullptr, FALSE);
            return 0;
        }
        dockModel_.Commands().DockPanelTo(panelId, DockDropPreview{ .zone = DockDropZone::Bottom });
        InvalidateRect(mainWindow_, nullptr, FALSE);
        return 0;
    }

    static_cast<void>(sceneContext_.RestorePlayModeSceneSession());
    if (!ResolveDirtyMaterialEditorClose(messageWindow, sceneContext_, true)) {
        return 0;
    }
    if (!ResolveDirtySkeletalMeshEditorClose(messageWindow, sceneContext_, true)) {
        return 0;
    }
    if (!ResolveDirtyAnimatorEditorClose(messageWindow, sceneContext_, true)) {
        return 0;
    }
    if (!EditorParticleDocumentLifecycle::Resolve(
            messageWindow, sceneContext_,
            kb::particle_editor::ParticleDocumentTransition::ExitApplication)) {
        return 0;
    }
    if (!EditorSceneLifecycleGuard::LeavePrefabEditMode(mainWindow_, sceneContext_, EditorPrefabLeaveReason::CloseEditor)) {
        return 0;
    }
    const std::optional<EditorDirtySceneResolution> resolution =
        EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(mainWindow_, sceneContext_, EditorSceneTransition::CloseEditor);
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
