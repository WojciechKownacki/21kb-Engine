#include "platform/win32/EditorSceneDirtyPrompt.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorChoiceDialog.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::wstring SceneLabel(const std::filesystem::path& scenePath) {
    const std::filesystem::path filename = scenePath.filename();
    if (!filename.empty()) {
        return filename.wstring();
    }
    return L"Untitled.21kbscene";
}

[[nodiscard]] std::wstring PromptText(const std::filesystem::path& scenePath, std::wstring_view action) {
    std::wstring text = L"Save changes to ";
    text += SceneLabel(scenePath);
    text += L" before ";
    text += action;
    text += L"?";
    return text;
}

} // namespace

EditorSceneDirtyPromptResult EditorSceneDirtyPrompt::Confirm(
    HWND owner,
    const std::filesystem::path& scenePath,
    std::wstring_view action) {
    const std::wstring text = PromptText(scenePath, action);
    switch (EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
        .title = "Unsaved Scene",
        .message = ScriptEditorTextEncoding::Narrow(text),
        .supportingText = "Choose whether to preserve the current scene changes.",
        .primaryLabel = "Save",
        .secondaryLabel = "Don't Save",
        .cancelLabel = "Cancel",
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
