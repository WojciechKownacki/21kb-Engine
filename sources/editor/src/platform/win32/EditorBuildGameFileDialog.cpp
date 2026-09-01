#include "platform/win32/EditorBuildGameFileDialog.hpp"

#if defined(_WIN32)
#include <CommDlg.h>
#include <ShlObj.h>

#include <array>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::optional<std::filesystem::path> SelectFile(
    HWND owner, const wchar_t* filter, const wchar_t* title) {
    std::array<wchar_t, 32768> buffer{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path{ buffer.data() };
}

} // namespace

std::optional<std::filesystem::path> EditorBuildGameFileDialog::SelectFolder(
    HWND owner, const std::filesystem::path& initialDirectory, std::wstring_view title) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog))) || dialog == nullptr) return std::nullopt;
    DWORD options = 0U;
    static_cast<void>(dialog->GetOptions(&options));
    static_cast<void>(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST));
    const std::wstring titleText{ title };
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
    constexpr wchar_t filter[] = L"PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0";
    return SelectFile(owner, filter, L"Select application icon");
}

std::optional<std::filesystem::path> EditorBuildGameFileDialog::SelectKeystore(HWND owner) {
    constexpr wchar_t filter[] = L"Java keystore (*.jks;*.keystore)\0*.jks;*.keystore\0All files (*.*)\0*.*\0";
    return SelectFile(owner, filter, L"Select Android keystore");
}

std::optional<std::filesystem::path> EditorBuildGameFileDialog::SelectIdentity(HWND owner) {
    constexpr wchar_t filter[] = L"SSH private key (id_*)\0id_*\0All files (*.*)\0*.*\0";
    return SelectFile(owner, filter, L"Select SSH identity file");
}

} // namespace kb::editor
#endif
