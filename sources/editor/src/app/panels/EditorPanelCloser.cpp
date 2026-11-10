#include "app/panels/EditorPanelCloser.hpp"

#if defined(_WIN32)
#include "app/EditorParticleDocumentLifecycle.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "rendering/BlendSpace2DEditorPanel.hpp"
#include "rendering/GameplayGraphEditorPanel.hpp"
#include "rendering/TimelineEditorPanel.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/GameplayGraphEditorState.hpp"

#include <string>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

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

[[nodiscard]] EditorChoiceDialogResult AskSaveDiscardCancel(HWND owner, std::string title, std::string message, std::string supportingText, HeroIconKind icon) {
    return EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
        .title = std::move(title),
        .message = std::move(message),
        .supportingText = std::move(supportingText),
        .primaryLabel = std::string{ EditorText("common.save", "Save") },
        .secondaryLabel = std::string{ EditorText("common.discard", "Discard") },
        .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
        .icon = icon,
    });
}

[[nodiscard]] bool ResolveDirtyMaterial(HWND owner, EditorSceneContext& sceneContext, bool closingEditor) {
    // A close can arrive without a mouse-up (Alt+F4, or a wire dropped on empty canvas whose node
    // menu is still open), so a graph gesture may still own the working copy. Settle it before
    // anything reads the document: the prompt then describes, and Save writes, a finished document.
    static_cast<void>(sceneContext.SettleMaterialGraphGesture());
    if (!sceneContext.HasDirtyMaterialAssetEdit()) {
        return true;
    }
    const EditorChoiceDialogResult result = AskSaveDiscardCancel(owner,
        std::string{ EditorText("materialDirty.title", "Unsaved Material") },
        DirtyMaterialPromptText(sceneContext, closingEditor),
        std::string{ EditorText("materialDirty.supporting", "Choose whether to preserve the current material changes.") },
        HeroIconKind::RectangleGroup);
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

[[nodiscard]] bool ResolveDirtySkeletalMesh(HWND owner, EditorSceneContext& sceneContext, bool closingEditor) {
    if (!sceneContext.HasDirtySkeletalMeshEditorAssetEdit()) return true;
    switch (AskSaveDiscardCancel(owner,
        std::string{ EditorText("skeletalDirty.title", "Unsaved Skeletal Asset") },
        std::string{ closingEditor
            ? EditorText("skeletalDirty.beforeCloseTab", "Save changes to the open skeletal assets before closing the editor?")
            : EditorText("skeletalDirty.beforeCloseWindow", "Save changes to the open skeletal assets before closing the Skeletal Mesh Editor?") },
        std::string{ EditorText("skeletalDirty.supporting", "Choose whether to preserve the current asset changes.") },
        HeroIconKind::Skeleton)) {
    case EditorChoiceDialogResult::Primary:
        return sceneContext.SaveSkeletalMeshEditorAsset();
    case EditorChoiceDialogResult::Secondary:
        return sceneContext.RevertSkeletalMeshEditorAsset();
    case EditorChoiceDialogResult::Cancel:
    default:
        return false;
    }
}

[[nodiscard]] bool ResolveDirtyAnimator(HWND owner, EditorSceneContext& sceneContext, bool closingEditor) {
    if (!sceneContext.HasDirtyAnimatorEditorAssetEdit()) return true;
    switch (AskSaveDiscardCancel(owner,
        std::string{ EditorText("animatorDirty.title", "Unsaved Animator Controller") },
        std::string{ closingEditor
            ? EditorText("animatorDirty.beforeCloseTab", "Save changes to the open Animator Controller before closing the editor?")
            : EditorText("animatorDirty.beforeCloseWindow", "Save changes to the open Animator Controller before closing the Animator Editor?") },
        std::string{ EditorText("animatorDirty.supporting", "Choose whether to preserve the current controller changes.") },
        HeroIconKind::Gamepad2)) {
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

bool EditorPanelCloser::ResolveUnsavedDocument(HWND owner, DockPanelKind kind, EditorSceneContext& sceneContext, bool closingEditor, bool floating) {
    switch (kind) {
    case DockPanelKind::TimelineEditor:
        return TimelineEditorPanel::ResolveClose(owner, sceneContext);
    case DockPanelKind::GameplayGraphEditor:
        return GameplayGraphEditorPanel::ResolveClose(owner, sceneContext);
    case DockPanelKind::BlendSpace2DEditor:
        return BlendSpace2DEditorPanel::ResolveClose(owner, sceneContext);
    case DockPanelKind::MaterialEditor:
        return ResolveDirtyMaterial(owner, sceneContext, closingEditor);
    case DockPanelKind::SkeletalMeshEditor:
        return ResolveDirtySkeletalMesh(owner, sceneContext, closingEditor);
    case DockPanelKind::AnimatorEditor:
        return ResolveDirtyAnimator(owner, sceneContext, closingEditor);
    case DockPanelKind::ParticleEditor:
        return EditorParticleDocumentLifecycle::Resolve(owner, sceneContext,
            closingEditor ? kb::particle_editor::ParticleDocumentTransition::ExitApplication
                : floating ? kb::particle_editor::ParticleDocumentTransition::CloseWindow
                           : kb::particle_editor::ParticleDocumentTransition::CloseTab);
    default:
        return true;
    }
}

bool EditorPanelCloser::Close(HWND owner, std::uint32_t panelId, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext) {
    const DockPanel* panel = dockModel.Queries().FindPanel(panelId);
    if (panel == nullptr || !panel->visible) {
        return false;
    }
    const DockPanelKind kind = panel->kind;
    if (!ResolveUnsavedDocument(owner, kind, sceneContext, false, panel->area == DockArea::Floating)) {
        return false;
    }
    if (!dockModel.Commands().ClosePanel(panelId)) {
        return false;
    }
    floatingWindows.Commands().Destroy(panelId);
    switch (kind) {
    case DockPanelKind::MaterialEditor:
        sceneContext.CloseMaterialEditorAsset();
        break;
    case DockPanelKind::SkeletalMeshEditor:
        sceneContext.CloseSkeletalMeshEditorAsset();
        break;
    case DockPanelKind::ParticleEditor:
        sceneContext.CloseParticleEditorAsset();
        break;
    case DockPanelKind::TimelineEditor:
        sceneContext.PauseTimelineEditorPreview();
        break;
    default:
        break;
    }
    return true;
}

void EditorPanelCloser::CloseOthersInGroup(HWND owner, std::uint32_t panelId, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext) {
    const std::vector<std::uint32_t> group = dockModel.Queries().PanelsSharingLeaf(panelId);
    for (const std::uint32_t other : group) {
        if (other != panelId && !Close(owner, other, dockModel, floatingWindows, sceneContext)) {
            return;
        }
    }
    dockModel.Commands().ActivatePanel(panelId);
}

} // namespace kb::editor

#endif
