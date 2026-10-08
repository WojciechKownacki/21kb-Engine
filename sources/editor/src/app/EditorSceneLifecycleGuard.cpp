#include "app/EditorSceneLifecycleGuard.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorChoiceDialog.hpp"
#include "platform/win32/EditorSceneDirtyPrompt.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <string>

namespace kb::editor {

std::optional<EditorDirtySceneResolution> EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(
    HWND owner,
    EditorSceneContext& sceneContext,
    std::wstring_view action) {
    if (!sceneContext.SceneDocumentDirty()) {
        return EditorDirtySceneResolution::Save;
    }

    switch (EditorSceneDirtyPrompt::Confirm(owner, sceneContext.CurrentScenePath(), action)) {
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
    std::wstring_view action) {
    if (!sceneContext.InPrefabEditMode()) {
        return true;
    }
    if (sceneContext.HasUnsavedPrefabEdit()) {
        std::wstring text = L"Save changes to the prefab before ";
        text += action;
        text += L"?";
        switch (EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
            .title = "Unsaved Prefab",
            .message = ScriptEditorTextEncoding::Narrow(text) + " (" + sceneContext.PrefabEditModeName() + ")",
            .supportingText = "Choose whether to preserve the current prefab changes.",
            .primaryLabel = "Save",
            .secondaryLabel = "Discard",
            .cancelLabel = "Cancel",
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
