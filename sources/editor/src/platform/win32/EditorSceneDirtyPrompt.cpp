#include "platform/win32/EditorSceneDirtyPrompt.hpp"

#if defined(_WIN32)
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
    text += L"?\n\nYes = Save\nNo = Don't Save\nCancel = keep editing";
    return text;
}

} // namespace

EditorSceneDirtyPromptResult EditorSceneDirtyPrompt::Confirm(
    HWND owner,
    const std::filesystem::path& scenePath,
    std::wstring_view action) {
    const std::wstring text = PromptText(scenePath, action);
    const int result = MessageBoxW(
        owner,
        text.c_str(),
        L"Unsaved Scene",
        MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON1 | MB_APPLMODAL);

    switch (result) {
    case IDYES:
        return EditorSceneDirtyPromptResult::Save;
    case IDNO:
        return EditorSceneDirtyPromptResult::DontSave;
    case IDCANCEL:
    default:
        return EditorSceneDirtyPromptResult::Cancel;
    }
}

} // namespace kb::editor

#endif
