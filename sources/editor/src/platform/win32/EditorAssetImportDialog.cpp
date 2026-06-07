#include "platform/win32/EditorAssetImportDialog.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetImportCatalog.hpp"

#include <CommDlg.h>

#include <array>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::wstring WidenAsciiFilter(const std::string& filter) {
    std::wstring output;
    output.reserve(filter.size());
    for (const char character : filter) {
        output.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    return output;
}

[[nodiscard]] std::vector<std::filesystem::path> ParseOpenFileNameBuffer(const std::array<wchar_t, 65536>& buffer) {
    std::vector<std::filesystem::path> paths;
    const wchar_t* cursor = buffer.data();
    if (*cursor == L'\0') {
        return paths;
    }

    const std::wstring first{ cursor };
    cursor += first.size() + 1U;
    if (*cursor == L'\0') {
        paths.emplace_back(first);
        return paths;
    }

    const std::filesystem::path folder{ first };
    while (*cursor != L'\0') {
        const std::wstring filename{ cursor };
        paths.push_back(folder / filename);
        cursor += filename.size() + 1U;
    }
    return paths;
}

} // namespace

std::vector<std::filesystem::path> EditorAssetImportDialog::Open(HWND owner) {
    std::array<wchar_t, 65536> fileBuffer{};
    const std::wstring filter = WidenAsciiFilter(kb::assets::AssetImportCatalog::WindowsFileDialogFilter());

    OPENFILENAMEW openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = owner;
    openFileName.lpstrFilter = filter.c_str();
    openFileName.lpstrFile = fileBuffer.data();
    openFileName.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    openFileName.lpstrTitle = L"Import assets";
    openFileName.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&openFileName) == FALSE) {
        return {};
    }
    return ParseOpenFileNameBuffer(fileBuffer);
}

} // namespace kb::editor

#endif
