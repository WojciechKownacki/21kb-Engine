#include "platform/win32/EditorBuildGameFileDialog.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorFileDialogFilter.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <CommDlg.h>
#include <ShlObj.h>

#include <array>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::optional<std::filesystem::path> SelectFile(
    HWND owner, std::string_view description, std::string_view pattern, std::string_view titleText) {
    const std::wstring filter = EditorFileDialogFilter({
        { description, pattern },
        { EditorText("fileDialog.allFiles", "All files (*.*)"), "*.*" },
    });
    const std::wstring title = ScriptEditorTextEncoding::Widen(titleText);
    std::array<wchar_t, 32768> buffer{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = title.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path{ buffer.data() };
}

} // namespace

std::optional<std::filesystem::path> EditorBuildGameFileDialog::SelectFolder(
    HWND owner, const std::filesystem::path& initialDirectory, std::string_view title) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog))) || dialog == nullptr) return std::nullopt;
    DWORD options = 0U;
    static_cast<void>(dialog->GetOptions(&options));
    static_cast<void>(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST));
    const std::wstring titleText = ScriptEditorTextEncoding::Widen(title);
    static_cast<void>(dialog->SetTitle(titleText.c_str()));
    IShellItem* initial = nullptr;
    if (!initialDirectory.empty() && SUCCEEDED(SHCreateItemFromParsingName(initialDirectory.c_str(), nullptr,
            IID_PPV_ARGS(&initial))) && initial != nullptr) {
        static_cast<void>(dialog->SetFolder(initial));
        initial->Release();
    }
    if (FAILED(dialog->Show(owner))) {
        dialog->Release();
        return std::nullopt;
    }
    IShellItem* selected = nullptr;
    if (FAILED(dialog->GetResult(&selected)) || selected == nullptr) {
        dialog->Release();
        return std::nullopt;
    }
    PWSTR path = nullptr;
    const HRESULT result = selected->GetDisplayName(SIGDN_FILESYSPATH, &path);
    selected->Release();
    dialog->Release();
    if (FAILED(result) || path == nullptr) return std::nullopt;
    std::filesystem::path selectedPath{ path };
    CoTaskMemFree(path);
    return selectedPath;
}

std::optional<std::filesystem::path> EditorBuildGameFileDialog::SelectPng(HWND owner) {
    return SelectFile(owner, EditorText("fileDialog.pngFilter", "PNG image (*.png)"), "*.png",
        EditorText("fileDialog.selectAppIconTitle", "Select application icon"));
}

std::optional<std::filesystem::path> EditorBuildGameFileDialog::SelectKeystore(HWND owner) {
    return SelectFile(owner, EditorText("fileDialog.keystoreFilter", "Java keystore (*.jks;*.keystore)"), "*.jks;*.keystore",
        EditorText("fileDialog.selectKeystoreTitle", "Select Android keystore"));
}

std::optional<std::filesystem::path> EditorBuildGameFileDialog::SelectCertificate(HWND owner) {
    return SelectFile(owner, EditorText("fileDialog.certificateFilter", "Code signing certificate (*.pfx;*.p12)"), "*.pfx;*.p12",
        EditorText("fileDialog.selectCertificateTitle", "Select code signing certificate"));
}

std::optional<std::filesystem::path> EditorBuildGameFileDialog::SelectIdentity(HWND owner) {
    return SelectFile(owner, EditorText("fileDialog.sshKeyFilter", "SSH private key (id_*)"), "id_*",
        EditorText("fileDialog.selectSshIdentityTitle", "Select SSH identity file"));
}

} // namespace kb::editor
#endif
