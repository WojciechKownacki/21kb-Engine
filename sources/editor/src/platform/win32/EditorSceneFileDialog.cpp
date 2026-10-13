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

// "Level.v2" becomes "Level.v2.21kbscene" (like Unity's "Level.v2.unity"): the typed name is kept,
// so it never turns into an other file the user did not name.
[[nodiscard]] std::filesystem::path WithSceneExtension(std::filesystem::path path) {
    if (path.extension() != ".21kbscene") {
        path += ".21kbscene";
    }
    return path;
}

[[nodiscard]] bool InsideFolder(const std::filesystem::path& path, const std::filesystem::path& folder) {
    std::error_code error;
    const std::filesystem::path relative =
        std::filesystem::weakly_canonical(path, error).lexically_relative(std::filesystem::weakly_canonical(folder, error));
    return !error && !relative.empty() && *relative.begin() != "..";
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

std::optional<std::filesystem::path> EditorSceneFileDialog::SaveAs(HWND owner, const std::filesystem::path& initialPath,
    const std::filesystem::path& fallbackDirectory, const std::filesystem::path& assetsFolder) {
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

    // Ask again until the scene goes inside the project's Assets folder (the editor's content is
    // there only) or the user cancels.
    while (true) {
        if (GetSaveFileNameW(&saveFileName) == FALSE) {
            return std::nullopt;
        }
        const std::filesystem::path typed{ fileBuffer.data() };
        const std::filesystem::path chosen = WithSceneExtension(typed);
        if (!InsideFolder(chosen, assetsFolder)) {
            const std::wstring message = ScriptEditorTextEncoding::Widen(EditorLocalization::Format("fileDialog.sceneOutsideAssets",
                "Scenes are saved inside this project's Assets folder:\n{folder}\n\nChoose a place there.",
                { { "folder", assetsFolder.generic_string() } }));
            MessageBoxW(owner, message.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
            continue;
        }
        // The dialog asked about the name as typed; the extension may name an other, existing file.
        std::error_code exists;
        if (chosen != typed && std::filesystem::exists(chosen, exists)) {
            const std::wstring question = ScriptEditorTextEncoding::Widen(EditorLocalization::Format("fileDialog.sceneReplace",
                "{file} already exists.\nDo you want to replace it?", { { "file", chosen.filename().generic_string() } }));
            if (MessageBoxW(owner, question.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                continue;
            }
        }
        return chosen;
    }
}

} // namespace kb::editor

#endif
