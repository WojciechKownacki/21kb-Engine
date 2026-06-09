#include "app/EditorSceneLifecycleGuard.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorSceneDirtyPrompt.hpp"

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

} // namespace kb::editor

#endif
