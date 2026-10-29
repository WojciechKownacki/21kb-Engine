#include "app/EditorSceneLifecycleGuard.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "platform/win32/EditorSceneDirtyPrompt.hpp"
#include "platform/win32/EditorMap2DWindow.hpp"
#include "rendering/TimelineEditorPanel.hpp"

#include <string>

namespace kb::editor {

std::optional<EditorDirtySceneResolution> EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(
    HWND owner,
    EditorSceneContext& sceneContext,
    EditorSceneTransition transition) {
    if (!EditorMap2DWindow::CloseAll(owner)) return std::nullopt;
    if (!TimelineEditorPanel::ResolveClose(owner, sceneContext)) return std::nullopt;
    sceneContext.ClearTimelineEditorPreview();
    if (!sceneContext.SceneDocumentDirty()) {
        return EditorDirtySceneResolution::Save;
    }

    switch (EditorSceneDirtyPrompt::Confirm(owner, sceneContext.CurrentScenePath(), transition)) {
    case EditorSceneDirtyPromptResult::Save:
        return EditorDirtySceneResolution::Save;
    case EditorSceneDirtyPromptResult::DontSave:
        return EditorDirtySceneResolution::Discard;
    case EditorSceneDirtyPromptResult::Cancel:
    default:
        return std::nullopt;
    }
}

bool EditorSceneLifecycleGuard::LeavePrefabEditMode(
    HWND owner,
    EditorSceneContext& sceneContext,
    EditorPrefabLeaveReason reason) {
    if (!sceneContext.InPrefabEditMode()) {
        return true;
    }
    if (sceneContext.HasUnsavedPrefabEdit()) {
        const std::string prefab = sceneContext.PrefabEditModeName();
        switch (EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
            .title = std::string{ EditorText("prefabDirty.title", "Unsaved Prefab") },
            .message = reason == EditorPrefabLeaveReason::CloseEditor
                ? EditorLocalization::Format("prefabDirty.beforeCloseEditor",
                      "Save changes to the prefab before closing the editor? ({prefab})", { { "prefab", prefab } })
                : EditorLocalization::Format("prefabDirty.beforeClosePrefab",
                      "Save changes to the prefab before closing it? ({prefab})", { { "prefab", prefab } }),
            .supportingText = std::string{ EditorText("prefabDirty.supporting", "Choose whether to preserve the current prefab changes.") },
            .primaryLabel = std::string{ EditorText("common.save", "Save") },
            .secondaryLabel = std::string{ EditorText("common.discard", "Discard") },
            .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
            .icon = HeroIconKind::Cube,
        })) {
        case EditorChoiceDialogResult::Primary:
            if (!sceneContext.SavePrefabEditMode()) {
                return false;
            }
            break;
        case EditorChoiceDialogResult::Secondary:
            break;
        case EditorChoiceDialogResult::Cancel:
        default:
            return false;
        }
    }
    return sceneContext.ClosePrefabEditMode();
}

} // namespace kb::editor

#endif
