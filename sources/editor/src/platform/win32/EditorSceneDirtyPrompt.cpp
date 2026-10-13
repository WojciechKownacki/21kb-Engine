#include "platform/win32/EditorSceneDirtyPrompt.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::string SceneLabel(const std::filesystem::path& scenePath) {
    const std::filesystem::path filename = scenePath.filename();
    if (!filename.empty()) {
        return ScriptEditorTextEncoding::Narrow(filename.wstring());
    }
    return std::string{ EditorText("toolbar.untitledScene", "Untitled.21kbscene") };
}

[[nodiscard]] std::string PromptText(const std::filesystem::path& scenePath, EditorSceneTransition transition) {
    const std::string scene = SceneLabel(scenePath);
    switch (transition) {
    case EditorSceneTransition::NewScene:
        return EditorLocalization::Format("sceneDirty.beforeNewScene",
            "Save changes to {scene} before creating a new scene?", { { "scene", scene } });
    case EditorSceneTransition::OpenScene:
        return EditorLocalization::Format("sceneDirty.beforeOpenScene",
            "Save changes to {scene} before opening another scene?", { { "scene", scene } });
    case EditorSceneTransition::CloseEditor:
        return EditorLocalization::Format("sceneDirty.beforeCloseEditor",
            "Save changes to {scene} before closing the editor?", { { "scene", scene } });
    case EditorSceneTransition::ConvertSceneToWorld:
        return EditorLocalization::Format("sceneDirty.beforeConvertToWorld",
            "Save changes to {scene} before converting the scene to a world?", { { "scene", scene } });
    case EditorSceneTransition::BuildGame:
    default:
        return EditorLocalization::Format("sceneDirty.beforeBuildGame",
            "Save changes to {scene} before building the game?", { { "scene", scene } });
    }
}

} // namespace

EditorSceneDirtyPromptResult EditorSceneDirtyPrompt::Confirm(
    HWND owner,
    const std::filesystem::path& scenePath,
    EditorSceneTransition transition) {
    switch (EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
        .title = std::string{ EditorText("sceneDirty.title", "Unsaved Scene") },
        .message = PromptText(scenePath, transition),
        .supportingText = std::string{ EditorText("sceneDirty.supporting", "Choose whether to preserve the current scene changes.") },
        .primaryLabel = std::string{ EditorText("common.save", "Save") },
        .secondaryLabel = std::string{ EditorText("common.dontSave", "Don't Save") },
        .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
        .icon = HeroIconKind::DocumentText,
    })) {
    case EditorChoiceDialogResult::Primary:
        return EditorSceneDirtyPromptResult::Save;
    case EditorChoiceDialogResult::Secondary:
        return EditorSceneDirtyPromptResult::DontSave;
    case EditorChoiceDialogResult::Cancel:
    default:
        return EditorSceneDirtyPromptResult::Cancel;
    }
}

} // namespace kb::editor

#endif
