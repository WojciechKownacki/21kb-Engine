#include "platform/win32/EditorSceneFileDialog.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorFileDialogFilter.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <CommDlg.h>

#include <array>
#include <cwchar>
#include <string>

namespace kb::editor {
namespace {

constexpr wchar_t kSceneDefaultExtension[] = L"21kbscene";

[[nodiscard]] std::wstring SceneDialogFilter() {
    return EditorFileDialogFilter({
        { EditorText("fileDialog.sceneFilter", "21KB Scene (*.21kbscene)"), "*.21kbscene" },
        { EditorText("fileDialog.allFiles", "All files (*.*)"), "*.*" },
    });
}

// Opening also accepts partitioned worlds, which the editor edits in the same scene view.
[[nodiscard]] std::wstring OpenDialogFilter() {
    return EditorFileDialogFilter({
        { EditorText("fileDialog.sceneOrWorldFilter", "21KB Scene or World (*.21kbscene;*.21kbworld)"), "*.21kbscene;*.21kbworld" },
        { EditorText("fileDialog.allFiles", "All files (*.*)"), "*.*" },
    });
}

[[nodiscard]] std::filesystem::path WithSceneExtension(std::filesystem::path path) {
    if (path.extension() != ".21kbscene") {
        path.replace_extension(".21kbscene");
    }
    return path;
}

void CopyPathToBuffer(const std::filesystem::path& path, std::array<wchar_t, 32768>& buffer) noexcept {
    const std::wstring text = path.wstring();
    if (text.empty()) {
        return;
    }
    static_cast<void>(wcsncpy_s(buffer.data(), buffer.size(), text.c_str(), _TRUNCATE));
}

} // namespace

std::optional<std::filesystem::path> EditorSceneFileDialog::Open(HWND owner, const std::filesystem::path& initialDirectory) {
    std::array<wchar_t, 32768> fileBuffer{};
    const std::wstring initialDir = initialDirectory.wstring();
    const std::wstring filter = OpenDialogFilter();
    const std::wstring title = ScriptEditorTextEncoding::Widen(EditorText("fileDialog.openSceneTitle", "Open scene"));

    OPENFILENAMEW openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = owner;
    openFileName.lpstrFilter = filter.c_str();
    openFileName.lpstrFile = fileBuffer.data();
    openFileName.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    openFileName.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    openFileName.lpstrTitle = title.c_str();
    openFileName.lpstrDefExt = kSceneDefaultExtension;
    openFileName.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&openFileName) == FALSE) {
        return std::nullopt;
    }
    return std::filesystem::path{ fileBuffer.data() };
}

std::optional<std::filesystem::path> EditorSceneFileDialog::SaveAs(HWND owner, const std::filesystem::path& initialPath, const std::filesystem::path& fallbackDirectory) {
    std::array<wchar_t, 32768> fileBuffer{};
    CopyPathToBuffer(initialPath.filename().empty() ? fallbackDirectory / "Untitled.21kbscene" : initialPath, fileBuffer);
    const std::wstring initialDir = (initialPath.parent_path().empty() ? fallbackDirectory : initialPath.parent_path()).wstring();
    const std::wstring filter = SceneDialogFilter();
    const std::wstring title = ScriptEditorTextEncoding::Widen(EditorText("fileDialog.saveSceneAsTitle", "Save scene as"));

    OPENFILENAMEW saveFileName{};
    saveFileName.lStructSize = sizeof(saveFileName);
    saveFileName.hwndOwner = owner;
    saveFileName.lpstrFilter = filter.c_str();
    saveFileName.lpstrFile = fileBuffer.data();
    saveFileName.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    saveFileName.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    saveFileName.lpstrTitle = title.c_str();
    saveFileName.lpstrDefExt = kSceneDefaultExtension;
    saveFileName.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&saveFileName) == FALSE) {
        return std::nullopt;
    }
    return WithSceneExtension(std::filesystem::path{ fileBuffer.data() });
}

} // namespace kb::editor

#endif
